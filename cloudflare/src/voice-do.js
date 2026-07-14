// 语音 Durable Object —— 承接设备 WebSocket(/ws/voice)
// 协议(与服务端 voiceWs.js 一致):
//   设备->DO: hello{device_id} / binary PCM(16k/16bit/mono) / end / abort
//   DO->设备: hello / stt / llm / tts{url}
// 链路: Workers AI Whisper 识别 -> Workers AI Llama 意图 -> 落地意图 -> Google TTS 代理下发 MP3 URL。

import { q } from './db.js'
import { getOwnerProfile, ownerProfileToContext } from './db.js'
import { transcribe, classifyIntent, regexClassifyIntent, fetchTtsMp3 } from './ai.js'
import { matchCorpus } from './intents.js'

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

// 等级计算 (与 worker.js 保持一致)
function calculateLevel(exp) {
  const LEVEL_CONFIG = [40, 60, 80, 100, 120, 140, 160]
  let level = 1, total = 0
  for (const r of LEVEL_CONFIG) { total += r; if (exp >= total) level++; else break }
  return Math.min(level, 8)
}

// 互动/喂食等直接加分 (复用 evaluation 写入 + 宠物经验等级更新)
async function awardPoints(env, student, points, reason, category) {
  const db = env.DB
  const now = Date.now()
  const id = uuid()
  await q.run(db, 'INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?,?,?,?,?,?,?)', id, student.class_id, student.id, points, reason, category, now)
  await q.run(db, 'UPDATE students SET total_points = total_points + ? WHERE id = ?', points, student.id)
  const stu = await q.get(db, 'SELECT * FROM students WHERE id = ?', student.id)
  if (stu && stu.pet_type) {
    const newExp = Math.max(0, stu.total_points)
    const newLevel = calculateLevel(newExp)
    if (newLevel === 8 && stu.pet_level < 8) await q.run(db, 'INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?,?,?,?)', uuid(), student.id, stu.pet_type, now)
    await q.run(db, 'UPDATE students SET pet_exp=?, pet_level=? WHERE id=?', newExp, newLevel, student.id)
  }
  return stu ? calculateLevel(Math.max(0, stu.total_points)) : 1
}

// 语料命中时直接执行动作(不调大模型); chat 时 nlpResult 由 LLM 提供
export async function applyIntent(env, student, nlpResult, now, rawText = '') {
  const db = env.DB
  const responseData = { action: nlpResult.action, text: '', reply_text: nlpResult.reply_text || '', params: {} }

  if (nlpResult.action === 'help') {
    responseData.reply_text = nlpResult.reply_text || '你可以申报任务加分、查询积分、设定提醒、添加日历和待办、喂食宠物、开始番茄钟、更换宠物。'

  } else if (nlpResult.action === 'apply_task') {
    let taskName = (nlpResult.task_name || '日常表现优秀').toString().slice(0, 64)
    let points = 1
    try {
      const likePat = `%${taskName.replace(/[%_\\]/g, '\\$&')}%`
      const rule = await q.get(db, "SELECT points FROM evaluation_rules WHERE name LIKE ? ESCAPE '\\' LIMIT 1", likePat)
      if (rule) points = rule.points
    } catch (e) {
      console.warn('⚠️ [DO] 规则匹配失败, 用默认1分:', e && e.message)
    }
    const taskId = uuid()
    const delayRow = await q.get(db, "SELECT value FROM settings WHERE key = 'task_confirm_delay'")
    const delayMinutes = delayRow ? Number(JSON.parse(delayRow.value)) : 30
    const autoConfirmAt = now + delayMinutes * 60 * 1000
    await q.run(db, 'INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)', taskId, student.id, taskName, points, 'pending', autoConfirmAt, now)
    responseData.reply_text = nlpResult.reply_text || `已为您申报了"${taskName}"，价值${points}分。请等待老师审核确认。`
    responseData.task_id = taskId
    responseData.points = points

  } else if (nlpResult.action === 'query_status') {
    const latest = await q.get(db, 'SELECT * FROM students WHERE id = ?', student.id)
    responseData.reply_text = nlpResult.reply_text || `你当前的积分总计 ${latest.total_points} 分，宠物当前处于等级 ${latest.pet_level}。加油！`

  } else if (nlpResult.action === 'create_schedule') {
    const info = nlpResult.schedule_info || { days: nlpResult.days, time: nlpResult.time, task_desc: nlpResult.task_desc }
    if (info && Array.isArray(info.days) && info.time && info.task_desc) {
      for (const day of info.days) {
        await q.run(db, 'INSERT INTO schedules (id, student_id, day_of_week, time_str, task_desc, is_active, created_at) VALUES (?, ?, ?, ?, ?, 1, ?)', uuid(), student.id, day, info.time, info.task_desc, now)
      }
      responseData.reply_text = nlpResult.reply_text || `已为您安排在周${info.days.join('、')}的 ${info.time} 提醒：${info.task_desc}。`
    } else {
      responseData.reply_text = '抱歉，我未能听清您要设定的日程时间和任务，请重试。'
    }

  } else if (nlpResult.action === 'add_calendar') {
    const title = (nlpResult.title || '事项').toString().slice(0, 80)
    const eventDate = (nlpResult.event_date || '').toString().slice(0, 10)
    const timeStr = nlpResult.time_str ? String(nlpResult.time_str).slice(0, 5) : null
    if (!eventDate) {
      responseData.reply_text = '抱歉，我没听清日期，请再说一遍，比如"周六去春游"。'
    } else {
      await q.run(db, 'INSERT INTO calendar_events (id, student_id, title, event_date, time_str, description, created_at) VALUES (?,?,?,?,?,?,?)', uuid(), student.id, title, eventDate, timeStr, '', now)
      responseData.reply_text = nlpResult.reply_text || `好的，已添加到日历：${title}。`
    }

  } else if (nlpResult.action === 'add_checklist') {
    const content = (nlpResult.content || '').toString().trim()
    if (!content) {
      responseData.reply_text = '请告诉我待办内容哦。'
    } else {
      await q.run(db, 'INSERT INTO checklist_items (id, student_id, content, is_done, created_at) VALUES (?,?,?,0,?)', uuid(), student.id, content.slice(0, 120), now)
      responseData.reply_text = nlpResult.reply_text || `好的，已添加待办：${content}。`
    }

  } else if (nlpResult.action === 'check_checklist') {
    const content = (nlpResult.content || '').toString().trim()
    if (!content) {
      responseData.reply_text = '请告诉我完成哪一项待办。'
    } else {
      const items = await q.all(db, 'SELECT id, content FROM checklist_items WHERE student_id=? AND is_done=0', student.id)
      const match = items.find(it => (it.content || '').includes(content) || content.includes(it.content || ''))
      if (match) {
        await q.run(db, 'UPDATE checklist_items SET is_done=1 WHERE id=?', match.id)
        responseData.reply_text = nlpResult.reply_text || `好的，已勾掉待办：${match.content}。`
      } else {
        responseData.reply_text = '没找到对应的待办项，可能已经完成了。'
      }
    }

  } else if (nlpResult.action === 'feed_pet') {
    const lvl = await awardPoints(env, student, 1, '语音喂食互动', '互动')
    responseData.reply_text = nlpResult.reply_text || '好呀，小宠物吃得好开心，亲密度+1～'
    responseData.params = { new_level: lvl }

  } else if (nlpResult.action === 'change_pet') {
    const petType = nlpResult.pet_type || null
    if (!petType) {
      responseData.reply_text = nlpResult.reply_text || '请告诉我想换成哪种宠物，比如边牧、金毛、橘猫。'
    } else {
      await q.run(db, 'UPDATE students SET pet_type=?, pet_level=1, pet_exp=0 WHERE id=?', petType, student.id)
      responseData.reply_text = nlpResult.reply_text || '好的，已为你更换宠物。'
      responseData.params = { pet_type: petType }
    }

  } else if (nlpResult.action === 'start_pomodoro') {
    const minutes = Math.min(60, Math.max(1, parseInt(nlpResult.minutes) || 25))
    responseData.reply_text = nlpResult.reply_text || `好的，开始${minutes}分钟番茄钟。`
    responseData.params = { minutes }

  } else if (nlpResult.action === 'query_list') {
    const which = nlpResult.which || 'checklist'
    if (which === 'calendar') {
      const rows = await q.all(db, 'SELECT id FROM calendar_events WHERE student_id=?', student.id)
      responseData.reply_text = `你近期共有 ${rows.length} 个日历安排。`
    } else {
      const rows = await q.all(db, 'SELECT id, is_done FROM checklist_items WHERE student_id=?', student.id)
      const done = rows.filter(r => r.is_done).length
      responseData.reply_text = `你共有 ${rows.length} 项待办，已完成 ${done} 项。`
    }

  } else {
    responseData.reply_text = nlpResult.reply_text || '好的'
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
      `SELECT s.*, c.user_id FROM students s JOIN classes c ON s.class_id = c.id JOIN devices d ON d.student_id = s.id WHERE UPPER(d.device_id) = UPPER(?)`,
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

    // 本地语料优先: 命中即执行动作, 不调用大模型(快 + 省 token); 否则走 LLM 闲聊
    const corpus = matchCorpus(text)
    let nlpResult
    if (corpus && corpus.action !== 'chat') {
      nlpResult = { action: corpus.action, ...corpus.params, reply_text: corpus.reply || '' }
      console.log('🧠 [DO] 语料命中:', corpus.action, JSON.stringify(corpus.params || {}))
    } else {
      // 意图分类(含多轮记忆 + 宠物主人记忆)
      const history = await getRecentHistory(db, student.id, 6)
      let ownerCtx = ''
      try {
        const profile = await getOwnerProfile(db, student.id)
        ownerCtx = ownerProfileToContext(profile)
      } catch {}
      try {
        nlpResult = await classifyIntent(env, text, history, ownerCtx)
      } catch {
        nlpResult = regexClassifyIntent(text)
      }
    }
    if (nlpResult.reply_text) nlpResult.reply_text = nlpResult.reply_text.slice(0, 200) // 防超长 TTS 链接

    const responseData = await applyIntent(env, student, nlpResult, now, text)
    const replyText = responseData.reply_text || '好的'
    server.send(JSON.stringify({ type: 'llm', text: replyText, action: responseData.action, params: responseData.params || {} }))

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
