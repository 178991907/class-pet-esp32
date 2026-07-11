// 班级宠物园 · 流式语音 WebSocket 端点 (Route B Phase 1)
// 抄小智 AI 架构: 设备录音即分帧上行 -> 服务端 ASR -> 意图 -> TTS 解码为 16k PCM -> 同一连接流式下行, 设备边收边播
// 协议 (简化自 xiaozhi websocket_protocol):
//   设备 -> 服务端:
//     text  {"type":"hello","device_id":"...","audio_params":{"format":"pcm","sample_rate":16000,"channels":1,"frame_duration":20}}
//     binary  PCM 帧 (16k/16bit/单声道/小端), 录音期间持续发送
//     text  {"type":"end"}              // 本段录音结束, 服务端开始处理
//     text  {"type":"abort"}            // 用户打断, 取消当前回复
//   服务端 -> 设备:
//     text  {"type":"hello","session_id":"...","audio_params":{"format":"pcm","sample_rate":16000,"frame_duration":20}}
//     text  {"type":"stt","text":"..."} // 识别出的文本 (用于 LCD 显示)
//     text  {"type":"llm","text":"..."} // 最终回复文本 (用于 LCD 显示)
//     text  {"type":"tts","state":"start"|"stop"}  // TTS 播放起止
//     text  {"type":"tts","url":"..."}  // 解码失败时的降级: 设备用原 MP3 拉流播放
//     binary PCM 帧 (TTS 音频, 16k/16bit/mono)

import { WebSocketServer } from 'ws'
import fetch from 'node-fetch'
import { v4 as uuidv4 } from 'uuid'

import { getAsync, runAsync } from '../db.js'
import { autoConfirmLazyLoad } from '../services/taskService.js'
import { getSetting } from '../utils/settings.js'
import { recognizeSpeech, isAsrConfigError } from '../services/asrService.js'
import { classifyIntent } from '../services/nlpService.js'

const SAMPLE_RATE = 16000
const CHUNK = 640 // 20ms @ 16k mono (16k * 2byte * 0.02 = 640)

// ================= 音频工具 =================

// 原始 PCM -> WAV (16k/16bit/mono)
function pcmToWav(pcm, sampleRate = SAMPLE_RATE, numChannels = 1, bitsPerSample = 16) {
  const byteRate = (sampleRate * numChannels * bitsPerSample) / 8
  const blockAlign = (numChannels * bitsPerSample) / 8
  const dataSize = pcm.length
  const buffer = Buffer.alloc(44 + dataSize)
  buffer.write('RIFF', 0)
  buffer.writeUInt32LE(36 + dataSize, 4)
  buffer.write('WAVE', 8)
  buffer.write('fmt ', 12)
  buffer.writeUInt32LE(16, 16)
  buffer.writeUInt16LE(1, 20) // PCM
  buffer.writeUInt16LE(numChannels, 22)
  buffer.writeUInt32LE(sampleRate, 24)
  buffer.writeUInt32LE(byteRate, 28)
  buffer.writeUInt16LE(blockAlign, 32)
  buffer.writeUInt16LE(bitsPerSample, 34)
  buffer.write('data', 36)
  buffer.writeUInt32LE(dataSize, 40)
  pcm.copy(buffer, 44)
  return buffer
}

// 注: 原 MP3->PCM 解码依赖 (@breezystack/lamejs / lamejs 均无 Mp3Decoder) 已移除。
// TTS 改为由设备经服务端代理端点 /api/device/tts-stream 拉取 MP3 直接播放, 见 handleEnd()。

// ================= TTS (优先返回可解码的 MP3; 失败返回 null) =================

async function getTtsConfig() {
  const baiduKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'baidu_api_key'")
  const baiduKey = baiduKeyRow ? JSON.parse(baiduKeyRow.value) : ''
  const baiduSecretRow = await getAsync("SELECT value FROM settings WHERE key = 'baidu_secret_key'")
  const baiduSecret = baiduSecretRow ? JSON.parse(baiduSecretRow.value) : ''
  return { baiduKey, baiduSecret }
}

async function fetchTtsMp3(text) {
  const enc = encodeURIComponent(text)
  // 1. Google Translate TTS (免费, 中文音质好)
  const gUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=zh-CN&client=tw-ob&q=${enc}`
  try {
    const r = await fetch(gUrl, { headers: { 'User-Agent': 'Mozilla/5.0' } })
    if (r.ok && r.headers.get('content-type')?.includes('audio')) {
      const buf = Buffer.from(await r.arrayBuffer())
      if (buf.length) return { mp3: buf, url: gUrl, provider: 'google' }
    }
  } catch (e) {
    console.warn('⚠️ [WS] Google TTS 失败:', e.message)
  }
  // 2. 百度语音合成
  const config = await getTtsConfig()
  if (config.baiduKey && config.baiduSecret) {
    try {
      const tokenResp = await fetch(
        `https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=${config.baiduKey}&client_secret=${config.baiduSecret}`
      )
      const tokenData = await tokenResp.json()
      if (tokenData.access_token) {
        const form = new URLSearchParams()
        form.append('tex', text)
        form.append('tok', tokenData.access_token)
        form.append('cuid', 'classpet-device')
        form.append('ctp', '1')
        form.append('lan', 'zh')
        form.append('spd', '5')
        form.append('pit', '5')
        form.append('vol', '10')
        form.append('per', '4')
        form.append('aue', '3')
        const ttsResp = await fetch('https://tsn.baidu.com/text2audio', {
          method: 'POST',
          body: form,
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        })
        const ct = ttsResp.headers.get('content-type') || ''
        if (ct.includes('audio') || ct.includes('mpeg')) {
          const buf = Buffer.from(await ttsResp.arrayBuffer())
          if (buf.length) return { mp3: buf, url: 'baidu', provider: 'baidu' }
        }
      }
    } catch (e) {
      console.warn('⚠️ [WS] 百度 TTS 失败:', e.message)
    }
  }
  // 3. 有道保底
  try {
    const fUrl = `https://tts.youdao.com/fanyivoice?word=${enc}&le=zh&keyfrom=speaker-target`
    const fb = await fetch(fUrl)
    if (fb.ok) {
      const buf = Buffer.from(await fb.arrayBuffer())
      if (buf.length) return { mp3: buf, url: fUrl, provider: 'youdao' }
    }
  } catch (e) {
    console.warn('⚠️ [WS] 有道 TTS 失败:', e.message)
  }
  return null
}

// ================= 意图落库 (复制自 routes/device.js, 保持一致行为) =================

async function applyIntent(student, nlpResult, now) {
  const responseData = {
    action: nlpResult.action,
    text: '',
    reply_text: nlpResult.reply_text
  }

  if (nlpResult.action === 'apply_task') {
    const taskName = nlpResult.task_name || '日常表现优秀'
    const rule = await getAsync(
      'SELECT points FROM evaluation_rules WHERE name LIKE ? LIMIT 1',
      `%${taskName}%`
    )
    const points = rule ? rule.points : 1
    const taskId = uuidv4()
    const delayMinutes = await getSetting('task_confirm_delay', 30)
    const autoConfirmAt = now + delayMinutes * 60 * 1000
    await runAsync(
      `INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at)
       VALUES (?, ?, ?, ?, 'pending', ?, ?)`,
      taskId, student.id, taskName, points, autoConfirmAt, now
    )
    responseData.reply_text =
      nlpResult.reply_text || `已为您申报了"${taskName}"，价值${points}分。请等待老师审核确认。`
    responseData.task_id = taskId
    responseData.points = points
  } else if (nlpResult.action === 'query_status') {
    const latestStudent = await getAsync('SELECT * FROM students WHERE id = ?', student.id)
    responseData.reply_text =
      nlpResult.reply_text ||
      `你当前的积分总计 ${latestStudent.total_points} 分，宠物当前处于等级 ${latestStudent.pet_level}。加油！`
  } else if (nlpResult.action === 'create_schedule') {
    const info = nlpResult.schedule_info
    if (info && Array.isArray(info.days) && info.time && info.task_desc) {
      for (const day of info.days) {
        const scheduleId = uuidv4()
        await runAsync(
          'INSERT INTO schedules (id, student_id, day_of_week, time_str, task_desc, is_active, created_at) VALUES (?, ?, ?, ?, ?, 1, ?)',
          scheduleId, student.id, day, info.time, info.task_desc, now
        )
      }
      responseData.reply_text =
        nlpResult.reply_text || `已为您安排在周${info.days.join('、')}的 ${info.time} 提醒：${info.task_desc}。`
    } else {
      responseData.reply_text = '抱歉，我未能听清您要设定的日程时间和任务，请重试。'
    }
  }
  return responseData
}

// ================= 连接处理 =================

const lastVoiceRequestTimes = new Map()

async function handleConnection(ws, req) {
  const deviceId = (req.headers['x-device-id'] || '').toString()
  if (!deviceId) {
    ws.close(4001, 'missing device id')
    return
  }

  const isTls = (req.headers['x-forwarded-proto'] === 'https') || req.url.startsWith('/pet-garden')
  const host = req.headers['host'] || `localhost:3003`
  const baseUrl = `${isTls ? 'https' : 'http'}://${host}`

  let audioChunks = []
  let audioBytes = 0
  let aborted = false
  let finished = false

  console.log(`🔌 [WS] 设备 ${deviceId} 已连接语音流`)

  ws.on('message', async (data, isBinary) => {
    if (finished || aborted) return

    if (isBinary) {
      // 累积 PCM 上行音频
      if (audioBytes < 10 * 1024 * 1024) {
        audioChunks.push(Buffer.from(data))
        audioBytes += data.length
      }
      return
    }

    // 文本控制消息
    let msg
    try {
      msg = JSON.parse(data.toString())
    } catch {
      return
    }

    if (msg.type === 'hello') {
      const hello = {
        type: 'hello',
        session_id: uuidv4(),
        transport: 'websocket',
        audio_params: { format: 'pcm', sample_rate: SAMPLE_RATE, frame_duration: 20 }
      }
      ws.send(JSON.stringify(hello))
      return
    }

    if (msg.type === 'abort') {
      aborted = true
      console.log(`🛑 [WS] 设备 ${deviceId} 打断`)
      return
    }

    if (msg.type === 'end') {
      finished = true
      await handleEnd(ws, deviceId, audioChunks, audioBytes, baseUrl, () => aborted)
        .catch((e) => console.error('❌ [WS] 处理失败:', e.stack || e))
        .finally(() => {
          try { ws.close() } catch {}
        })
    }
  })

  ws.on('close', () => console.log(`🔌 [WS] 设备 ${deviceId} 断开`))
  ws.on('error', (e) => console.warn('⚠️ [WS] 连接错误:', e.message))
}

async function handleEnd(ws, deviceId, audioChunks, audioBytes, baseUrl, isAborted) {
  const now = Date.now()

  // 限频 5 秒
  const last = lastVoiceRequestTimes.get(deviceId) || 0
  if (now - last < 5000) {
    ws.send(JSON.stringify({ type: 'llm', text: '歇一会儿再聊哦，不要太频繁啦。' }))
    return
  }
  lastVoiceRequestTimes.set(deviceId, now)

  // 设备绑定校验
  const student = await getAsync(
    `SELECT s.*, c.user_id
     FROM students s JOIN classes c ON s.class_id = c.id
     WHERE UPPER(s.device_id) = UPPER(?)`,
    deviceId
  )
  if (!student) {
    ws.send(JSON.stringify({ type: 'llm', text: '设备尚未绑定学生，请先在后台完成绑定。', action: 'none' }))
    return
  }

  // 拼装 PCM -> WAV -> ASR
  const pcm = Buffer.concat(audioChunks)
  audioChunks = null
  const wav = pcmToWav(pcm)
  console.log(`🎙️ [WS] 收到音频 ${audioBytes} 字节, 开始 ASR...`)

  let text = ''
  try {
    text = await recognizeSpeech(wav, deviceId)
  } catch (asrErr) {
    console.error('❌ [WS] ASR 失败:', asrErr.message)
    const reply = isAsrConfigError(asrErr.message)
      ? '语音识别服务配置异常，请联系管理员检查后台 ASR 设置。'
      : '抱歉，刚才没听清，请再说一遍。'
    ws.send(JSON.stringify({ type: 'llm', text: reply, action: 'none' }))
    return
  }

  ws.send(JSON.stringify({ type: 'stt', text }))
  console.log(`📝 [WS] ASR 文本: ${text}`)

  // 意图分类
  const nlpResult = await classifyIntent(text)
  await autoConfirmLazyLoad(student.user_id)
  const responseData = await applyIntent(student, nlpResult, now)

  const replyText = responseData.reply_text || '好的'
  ws.send(JSON.stringify({ type: 'llm', text: replyText, action: responseData.action }))

  // 记 chat_logs
  try {
    await runAsync(
      'INSERT INTO chat_logs (id, student_id, user_message, ai_response, timestamp) VALUES (?, ?, ?, ?, ?)',
      uuidv4(), student.id, text, replyText, now
    )
  } catch (e) {
    console.warn('⚠️ [WS] 写入 chat_logs 失败:', e.message)
  }

  // TTS: 直接让设备经服务端代理拉取 MP3 播放 (免去服务端 MP3 解码依赖, 中国网络下也更稳)
  console.log(`🗣️ [WS] 开始 TTS: ${replyText.substring(0, 30)}...`)
  const ttsUrl = `${baseUrl}/pet-garden/api/device/tts-stream?text=${encodeURIComponent(replyText)}`
  ws.send(JSON.stringify({ type: 'tts', url: ttsUrl }))
  console.log(`✅ [WS] TTS 播放网址已下发: ${ttsUrl.substring(0, 64)}...`)
}

// ================= 挂载 =================

export function attachVoiceWs(server) {
  const wss = new WebSocketServer({ noServer: true })

  server.on('upgrade', (req, socket, head) => {
    const url = req.url || ''
    if (!url.includes('/ws/voice')) return // 不是语音 WS, 忽略 (交给其它处理逻辑或关闭)
    wss.handleUpgrade(req, socket, head, (ws) => {
      wss.emit('connection', ws, req)
    })
  })

  wss.on('connection', (ws, req) => {
    handleConnection(ws, req).catch((e) => console.error('❌ [WS] 连接处理异常:', e))
  })

  console.log('🔊 [WS] 语音流式端点已挂载: /ws/voice')
}
