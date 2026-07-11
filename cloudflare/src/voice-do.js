// 语音 Durable Object —— 承接设备 WebSocket(/ws/voice)
// 协议(与服务端 voiceWs.js 一致):
//   设备->DO: hello{device_id} / binary PCM(16k/16bit/mono) / end / abort
//   DO->设备: hello / stt / llm / tts{url}
// 链路: Workers AI Whisper 识别 -> Workers AI Llama 意图 -> 落地意图 -> Google TTS 代理下发 MP3 URL。

import { q } from './db.js'
import { transcribe, classifyIntent, regexClassifyIntent, fetchTtsMp3 } from './ai.js'

function uuid() {
  return crypto.randomUUID()
}

async function publishEvent(env, type, payload) {
  try {
    await q.run(
      env.DB,
      'INSERT INTO device_events (type, payload, timestamp) VALUES (?, ?, ?)',
      type,
      JSON.stringify(payload),
      Date.now()
    )
  } catch (e) {
    console.warn('⚠️ [DO] 事件写入失败:', e.message)
  }
}

async function getRecentHistory(db, studentId, limit = 6) {
  try {
    const rows = await q.all(
      db,
      'SELECT user_message, ai_response FROM chat_logs WHERE student_id = ? ORDER BY timestamp DESC LIMIT ?',
      studentId,
      limit
    )
    if (!rows.length) return ''
    return rows
      .reverse()
      .map((r) => `用户: ${r.user_message || ''}\n宠物: ${r.ai_response || ''}`)
      .join('\n')
  } catch {
    return ''
  }
}

async function applyIntent(env, student, nlpResult, now) {
  const db = env.DB
  const responseData = { action: nlpResult.action, text: '', reply_text: nlpResult.reply_text }

  if (nlpResult.action === 'apply_task') {
    const taskName = nlpResult.task_name || '日常表现优秀'
    const rule = await q.get(db, 'SELECT points FROM evaluation_rules WHERE name LIKE ? LIMIT 1', `%${taskName}%`)
    const points = rule ? rule.points : 1
    const taskId = uuid()
    const delayRow = await q.get(db, "SELECT value FROM settings WHERE key = 'task_confirm_delay'")
    const delayMinutes = delayRow ? Number(JSON.parse(delayRow.value)) : 30
    const autoConfirmAt = now + delayMinutes * 60 * 1000
    await q.run(
      db,
      'INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
      taskId, student.id, taskName, points, 'pending', autoConfirmAt, now
    )
    responseData.reply_text = nlpResult.reply_text || `已为您申报了"${taskName}"，价值${points}分。请等待老师审核确认。`
    responseData.task_id = taskId
    responseData.points = points
  } else if (nlpResult.action === 'query_status') {
    const latest = await q.get(db, 'SELECT * FROM students WHERE id = ?', student.id)
    responseData.reply_text =
      nlpResult.reply_text || `你当前的积分总计 ${latest.total_points} 分，宠物当前处于等级 ${latest.pet_level}。加油！`
  } else if (nlpResult.action === 'create_schedule') {
    const info = nlpResult.schedule_info
    if (info && Array.isArray(info.days) && info.time && info.task_desc) {
      for (const day of info.days) {
        await q.run(
          db,
          'INSERT INTO schedules (id, student_id, day_of_week, time_str, task_desc, is_active, created_at) VALUES (?, ?, ?, ?, ?, 1, ?)',
          uuid(), student.id, day, info.time, info.task_desc, now
        )
      }
      responseData.reply_text = nlpResult.reply_text || `已为您安排在周${info.days.join('、')}的 ${info.time} 提醒：${info.task_desc}。`
    } else {
      responseData.reply_text = '抱歉，我未能听清您要设定的日程时间和任务，请重试。'
    }
  }
  return responseData
}

export class VoiceDO {
  constructor(state, env) {
    this.state = state
    this.env = env
    this.lastVoice = new Map()
  }

  async fetch(request) {
    const upgrade = request.headers.get('Upgrade')
    if (upgrade !== 'websocket') {
      return new Response('expected websocket', { status: 426 })
    }

    const url = new URL(request.url)
    const deviceId =
      request.headers.get('x-device-id') || url.searchParams.get('device_id') || ''

    const pair = new WebSocketPair()
    const [client, server] = pair
    server.accept()

    let audioChunks = []
    let audioBytes = 0
    let finished = false
    let aborted = false

    server.addEventListener('message', async (event) => {
      if (finished || aborted) return

      if (typeof event.data === 'string') {
        let msg
        try {
          msg = JSON.parse(event.data)
        } catch {
          return
        }
        if (msg.type === 'hello') {
          const hello = {
            type: 'hello',
            session_id: uuid(),
            transport: 'websocket',
            audio_params: { format: 'pcm', sample_rate: 16000, frame_duration: 20 }
          }
          server.send(JSON.stringify(hello))
          return
        }
        if (msg.type === 'abort') {
          aborted = true
          return
        }
        if (msg.type === 'end') {
          finished = true
          try {
            await this.handleEnd(server, deviceId, audioChunks, audioBytes, url)
          } catch (e) {
            console.error('❌ [DO] 处理失败:', e.stack || e)
          } finally {
            try { server.close() } catch {}
          }
        }
        return
      }

      // 二进制 PCM 上行
      if (audioBytes < 10 * 1024 * 1024) {
        audioChunks.push(event.data)
        audioBytes += event.data.byteLength
      }
    })

    server.addEventListener('close', () => {})
    server.addEventListener('error', () => {})

    return new Response(null, { status: 101, webSocket: client })
  }

  async handleEnd(server, deviceId, audioChunks, audioBytes, url) {
    const env = this.env
    const db = env.DB
    const now = Date.now()

    // 限频 5 秒
    const last = this.lastVoice.get(deviceId) || 0
    if (now - last < 5000) {
      server.send(JSON.stringify({ type: 'llm', text: '歇一会儿再聊哦，不要太频繁啦。', action: 'none' }))
      return
    }
    this.lastVoice.set(deviceId, now)

    if (!deviceId) {
      server.send(JSON.stringify({ type: 'llm', text: '设备未标识，请先完成绑定。', action: 'none' }))
      return
    }

    const student = await q.get(
      db,
      `SELECT s.*, c.user_id FROM students s JOIN classes c ON s.class_id = c.id WHERE UPPER(s.device_id) = UPPER(?)`,
      deviceId
    )
    if (!student) {
      server.send(JSON.stringify({ type: 'llm', text: '设备尚未绑定学生，请先在后台完成绑定。', action: 'none' }))
      return
    }

    // ASR
    let text = ''
    try {
      text = await transcribe(env, audioChunks, audioBytes)
    } catch (e) {
      console.error('❌ [DO] ASR 失败:', e.message)
      server.send(JSON.stringify({ type: 'llm', text: '抱歉，刚才没听清，请再说一遍。', action: 'none' }))
      return
    }
    if (!text) {
      server.send(JSON.stringify({ type: 'llm', text: '没听清哦，再试一次吧～', action: 'none' }))
      return
    }
    server.send(JSON.stringify({ type: 'stt', text }))

    // 意图分类(含多轮记忆)
    const history = await getRecentHistory(db, student.id, 6)
    let nlpResult
    try {
      nlpResult = await classifyIntent(env, text, history)
    } catch {
      nlpResult = regexClassifyIntent(text)
    }

    const responseData = await applyIntent(env, student, nlpResult, now)
    const replyText = responseData.reply_text || '好的'
    server.send(JSON.stringify({ type: 'llm', text: replyText, action: responseData.action }))

    // 广播事件(供网页 SSE 实时刷新)
    try {
      await publishEvent(env, 'voice_session', {
        studentId: student.id,
        studentName: student.name,
        text,
        reply: replyText,
        action: responseData.action
      })
    } catch {}

    // 写 chat_logs
    try {
      await q.run(
        db,
        'INSERT INTO chat_logs (id, student_id, user_message, ai_response, timestamp) VALUES (?, ?, ?, ?, ?)',
        uuid(), student.id, text, replyText, now
      )
    } catch {}

    // TTS: 设备经本 Worker 代理拉取 MP3
    const ttsUrl = `${url.origin}/device/tts-stream?text=${encodeURIComponent(replyText)}`
    server.send(JSON.stringify({ type: 'tts', url: ttsUrl }))
  }
}
