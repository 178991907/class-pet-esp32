// 班级宠物园 · Cloudflare Worker 主入口
// 承担: REST API + /ws/voice(转交 Durable Object) + SSE 实时事件 + /device/* 设备端点
// 数据库: D1 (SQLite 兼容) · AI: Workers AI (keyless) · 资产: R2(可选)

import { bindDb, q, getAsync, allAsync, runAsync, getSetting, setSetting } from './db.js'
import { setSecrets, hashPassword, verifyPassword, generateToken, verifyToken, getUserIdFromAuth } from './auth.js'
import { fetchTtsMp3, petChat } from './ai.js'
import { VoiceDO } from './voice-do.js'

export { VoiceDO }

// ============ 等级计算 ============
const LEVEL_CONFIG = [40, 60, 80, 100, 120, 140, 160]
function calculateLevel(exp) {
  let level = 1
  let total = 0
  for (const required of LEVEL_CONFIG) {
    total += required
    if (exp >= total) level++
    else break
  }
  return Math.min(level, 8)
}
function getLevelProgress(exp) {
  const level = calculateLevel(exp)
  let prevTotal = 0
  for (let i = 0; i < level - 1; i++) prevTotal += LEVEL_CONFIG[i]
  const current = exp - prevTotal
  const required = LEVEL_CONFIG[level - 1] || 0
  const isMax = level >= 8
  return { level, current, required: isMax ? current : required, isMaxLevel: isMax, percentage: isMax ? 100 : Math.min(100, (current / required) * 100) }
}

// ============ 工具 ============
function uuid() {
  return crypto.randomUUID()
}
function normalize(pathname) {
  let p = pathname
  if (p.startsWith('/pet-garden')) p = p.slice('/pet-garden'.length)
  if (p.startsWith('/api')) p = p.slice(4)
  if (!p.startsWith('/')) p = '/' + p
  return p
}
function json(data, status = 200, headers = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', 'Access-Control-Allow-Origin': '*', ...headers }
  })
}
function getDeviceId(request) {
  const url = new URL(request.url)
  return request.headers.get('x-device-id') || url.searchParams.get('device_id') || ''
}
async function requireUser(request, env) {
  const token = getUserIdFromAuth(request)
  const payload = await verifyToken(token)
  if (!payload) throw Object.assign(new Error('unauthorized'), { status: 401 })
  return payload.userId
}
async function readBody(request) {
  try {
    return await request.json()
  } catch {
    return {}
  }
}

// 首次运行确保管理员存在(迁移 SQL 已注入 settings, 这里只补 admin)
async function ensureSeed(env) {
  const admin = await q.get(env.DB, "SELECT id FROM users WHERE username='admin'")
  if (!admin) {
    await q.run(
      env.DB,
      'INSERT INTO users (id, username, password_hash, is_guest, role, created_at) VALUES (?,?,?,?,?,?)',
      'admin-default-id', 'admin', await hashPassword('admin'), 0, 'admin', Date.now()
    )
  }
}

// ============ SSE 实时事件 ============
async function handleEvents(request, env) {
  const encoder = new TextEncoder()
  let lastId = 0
  let timer
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue(encoder.encode('event: connected\ndata: {"ok":true}\n\n'))
      timer = setInterval(async () => {
        try {
          const rows = await q.all(
            env.DB,
            'SELECT id, type, payload, timestamp FROM device_events WHERE id > ? ORDER BY id ASC',
            lastId
          )
          for (const r of rows) {
            lastId = r.id
            controller.enqueue(encoder.encode(`event: ${r.type}\ndata: ${r.payload}\n\n`))
          }
        } catch {}
      }, 1500)
    },
    cancel() {
      clearInterval(timer)
    }
  })
  return new Response(stream, {
    headers: {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      Connection: 'keep-alive',
      'Access-Control-Allow-Origin': '*'
    }
  })
}

// ============ 语音 HTTP 回退 (/device/voice) ============
async function handleDeviceVoice(request, env) {
  const deviceId = getDeviceId(request)
  const body = await request.arrayBuffer().catch(() => null)
  if (!body || body.byteLength === 0) {
    // 可能是 JSON 带 text
    const b = await readBody(request)
    return await runVoicePipeline(env, deviceId, null, b.text || '')
  }
  return await runVoicePipeline(env, deviceId, [body], body.byteLength)
}

async function runVoicePipeline(env, deviceId, pcmChunks, pcmBytes, prefext) {
  const db = env.DB
  const now = Date.now()
  const student = await q.get(
    db,
    `SELECT s.*, c.user_id FROM students s JOIN classes c ON s.class_id = c.id WHERE UPPER(s.device_id) = UPPER(?)`,
    deviceId
  )
  if (!student) return { action: 'none', text: '未绑定', reply_text: '设备尚未绑定学生，请先在后台完成绑定。' }
  const { transcribe, classifyIntent, regexClassifyIntent } = await import('./ai.js')
  let text = prefext || ''
  if (pcmChunks) {
    try {
      text = await transcribe(env, pcmChunks, pcmBytes)
    } catch (e) {
      return { action: 'none', text: '听不清楚', reply_text: '抱歉，刚才没听清，请再说一遍。' }
    }
  }
  if (!text) return { action: 'none', reply_text: '未提供音频或文本。' }
  let nlp
  try {
    nlp = await classifyIntent(env, text, '')
  } catch {
    nlp = regexClassifyIntent(text)
  }
  // 复用 DO 的 applyIntent 逻辑(复制一份以保持独立)
  const responseData = { action: nlp.action, text, reply_text: nlp.reply_text }
  if (nlp.action === 'apply_task') {
    const taskName = nlp.task_name || '日常表现优秀'
    const rule = await q.get(db, 'SELECT points FROM evaluation_rules WHERE name LIKE ? LIMIT 1', `%${taskName}%`)
    const points = rule ? rule.points : 1
    await q.run(
      db,
      'INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)',
      uuid(), student.id, taskName, points, 'pending', now + 30 * 60 * 1000, now
    )
    responseData.reply_text = nlp.reply_text || `已为您申报了"${taskName}"，价值${points}分。`
  }
  await q.run(db, 'INSERT INTO chat_logs (id, student_id, user_message, ai_response, timestamp) VALUES (?, ?, ?, ?, ?)', uuid(), student.id, text, responseData.reply_text, now)
  const { fetchTtsMp3 } = await import('./ai.js')
  const tts = await fetchTtsMp3(responseData.reply_text)
  if (tts) responseData.audio_url = tts.url
  return responseData
}

// ============ 主路由 ============
async function handleApi(request, env, ctx) {
  const method = request.method
  const url = new URL(request.url)
  const path = normalize(url.pathname)
  const params = url.searchParams

  // 预检
  if (method === 'OPTIONS') {
    return new Response(null, {
      headers: {
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type,Authorization,x-device-id'
      }
    })
  }

  // 健康检查
  if (path === '/' && method === 'GET') return json({ ok: true, service: 'classpet-api' })

  // 语音 WebSocket -> Durable Object
  if (path === '/ws/voice') {
    const id = env.VOICE_DO.idFromName(params.get('device_id') || 'anon')
    return env.VOICE_DO.get(id).fetch(request)
  }

  // 实时事件 SSE
  if (path === '/events' && method === 'GET') return handleEvents(request, env)

  try {
    // ===== AUTH =====
    if (path === '/auth/register' && method === 'POST') {
      const b = await readBody(request)
      if (!b.username || !b.password) return json({ error: '用户名和密码不能为空' }, 400)
      if (b.username.toLowerCase() === 'admin') return json({ error: '该用户名已被系统保留' }, 400)
      if (b.username.length < 3 || b.username.length > 20) return json({ error: '用户名长度3-20字符' }, 400)
      if (b.password.length < 6) return json({ error: '密码至少6位' }, 400)
      const existing = await getAsync('SELECT id FROM users WHERE username = ?', b.username)
      if (existing) return json({ error: '用户名已存在' }, 400)
      const invite = (await getSetting('teacher_invite_code', 'TEACHER_INVITE')) || 'TEACHER_INVITE'
      const role = b.inviteCode === invite ? 'teacher' : 'student'
      const id = uuid()
      await runAsync('INSERT INTO users (id, username, password_hash, is_guest, role, created_at) VALUES (?,?,?,?,?,?)', id, b.username, await hashPassword(b.password), 0, role, Date.now())
      return json({ success: true, token: await generateToken(id), user: { id, username: b.username, isGuest: false, role } }, 201)
    }
    if (path === '/auth/login' && method === 'POST') {
      const b = await readBody(request)
      if (!b.username || !b.password) return json({ error: '用户名和密码不能为空' }, 400)
      const user = await getAsync('SELECT id, username, password_hash, is_guest, role FROM users WHERE username = ?', b.username)
      if (!user || !(await verifyPassword(b.password, user.password_hash))) return json({ error: '用户名或密码错误' }, 401)
      return json({ success: true, token: await generateToken(user.id), user: { id: user.id, username: user.username, isGuest: !!user.is_guest, role: user.role || 'student' } })
    }
    if (path === '/auth/me' && method === 'GET') {
      const uid = await requireUser(request, env)
      const user = await getAsync('SELECT id, username, is_guest, role FROM users WHERE id = ?', uid)
      if (!user) return json({ error: '用户不存在' }, 404)
      return json({ user: { id: user.id, username: user.username, isGuest: !!user.is_guest, role: user.role || 'student' } })
    }
    if (path === '/auth/users' && method === 'GET') {
      const uid = await requireUser(request, env)
      const admin = await getAsync('SELECT username FROM users WHERE id = ?', uid)
      if (!admin || admin.username !== 'admin') return json({ error: '无权操作，仅限管理员' }, 403)
      const users = await allAsync("SELECT id, username, role, created_at FROM users WHERE is_guest = 0 ORDER BY created_at DESC")
      return json({ users })
    }
    if (/^\/auth\/users\/[^/]+\/role$/.test(path) && method === 'PUT') {
      const uid = await requireUser(request, env)
      const admin = await getAsync('SELECT username FROM users WHERE id = ?', uid)
      if (!admin || admin.username !== 'admin') return json({ error: '无权操作，仅限管理员' }, 403)
      const targetId = path.split('/')[3]
      const b = await readBody(request)
      if (!['student', 'teacher', 'admin'].includes(b.role)) return json({ error: '非法角色' }, 400)
      const t = await getAsync('SELECT username FROM users WHERE id = ?', targetId)
      if (!t) return json({ error: '目标用户不存在' }, 404)
      if (t.username === 'admin' && b.role !== 'admin') return json({ error: '系统保留管理员角色不可修改' }, 400)
      await runAsync('UPDATE users SET role = ? WHERE id = ?', b.role, targetId)
      return json({ success: true })
    }
    if (path === '/auth/admin/password' && method === 'PUT') {
      const uid = await requireUser(request, env)
      const admin = await getAsync('SELECT username FROM users WHERE id = ?', uid)
      if (!admin || admin.username !== 'admin') return json({ error: '无权操作，仅限管理员' }, 403)
      const b = await readBody(request)
      if (!b.password || b.password.length < 6) return json({ error: '密码至少6位' }, 400)
      await runAsync("UPDATE users SET password_hash = ? WHERE username = 'admin'", await hashPassword(b.password))
      return json({ success: true })
    }

    // ===== CLASSES =====
    if (path === '/classes' && method === 'GET') {
      const uid = await requireUser(request, env)
      const classes = await allAsync('SELECT * FROM classes WHERE user_id = ? ORDER BY created_at DESC', uid)
      return json({ classes })
    }
    if (path === '/classes' && method === 'POST') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const id = uuid()
      const now = Date.now()
      await runAsync('INSERT INTO classes (id, user_id, name, created_at, updated_at) VALUES (?,?,?,?,?)', id, uid, b.name, now, now)
      return json({ id, user_id: uid, name: b.name, created_at: now, updated_at: now }, 201)
    }
    let m
    if ((m = path.match(/^\/classes\/([^/]+)$/)) && method === 'PUT') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const cls = await getAsync('SELECT user_id FROM classes WHERE id = ?', m[1])
      if (!cls) return json({ error: '班级不存在' }, 404)
      if (cls.user_id !== uid) return json({ error: '无权修改' }, 403)
      await runAsync('UPDATE classes SET name = ?, updated_at = ? WHERE id = ?', b.name, Date.now(), m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/classes\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const cls = await getAsync('SELECT user_id FROM classes WHERE id = ?', m[1])
      if (!cls) return json({ error: '班级不存在' }, 404)
      if (cls.user_id !== uid) return json({ error: '无权删除' }, 403)
      await runAsync('DELETE FROM students WHERE class_id = ?', m[1])
      await runAsync('DELETE FROM classes WHERE id = ?', m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/classes\/([^/]+)\/students$/)) && method === 'GET') {
      const uid = await requireUser(request, env)
      const cls = await getAsync('SELECT user_id FROM classes WHERE id = ?', m[1])
      if (!cls) return json({ error: '班级不存在' }, 404)
      if (cls.user_id !== uid) return json({ error: '无权访问此班级' }, 403)
      const students = await allAsync('SELECT * FROM students WHERE class_id = ? ORDER BY name', m[1])
      return json({ students })
    }

    // ===== STUDENTS =====
    if (path === '/students' && method === 'POST') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const cls = await getAsync('SELECT user_id FROM classes WHERE id = ?', b.classId)
      if (!cls) return json({ error: '班级不存在' }, 404)
      if (cls.user_id !== uid) return json({ error: '无权访问此班级' }, 403)
      const id = uuid()
      const now = Date.now()
      await runAsync('INSERT INTO students (id, class_id, name, student_no, device_id, total_points, pet_level, pet_exp, created_at) VALUES (?,?,?,?,?,0,1,0,?)', id, b.classId, b.name, b.studentNo || null, b.deviceId || null, now)
      return json({ id, class_id: b.classId, name: b.name, student_no: b.studentNo || null, device_id: b.deviceId || null, total_points: 0, pet_level: 1, pet_exp: 0, created_at: now }, 201)
    }
    if ((m = path.match(/^\/students\/([^/]+)\/pet$/)) && method === 'PUT') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const stu = await getAsync('SELECT s.* FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '学生不存在' }, 404)
      if (stu.pet_type && stu.pet_level >= 8) await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?,?,?,?)', uuid(), m[1], stu.pet_type, Date.now())
      await runAsync('UPDATE students SET pet_type=?, pet_level=1, pet_exp=0 WHERE id=?', b.petType, m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/students\/([^/]+)$/)) && method === 'PUT') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const stu = await getAsync('SELECT s.* FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '学生不存在' }, 404)
      await runAsync('UPDATE students SET name=?, student_no=?, device_id=? WHERE id=?', b.name, b.studentNo || null, b.deviceId || null, m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/students\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.* FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '学生不存在' }, 404)
      await runAsync('DELETE FROM evaluation_records WHERE student_id=?', m[1])
      await runAsync('DELETE FROM students WHERE id=?', m[1])
      return json({ success: true })
    }
    if (path === '/students/import' && method === 'POST') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const cls = await getAsync('SELECT user_id FROM classes WHERE id = ?', b.classId)
      if (!cls || cls.user_id !== uid) return json({ error: '班级不存在或无权' }, 404)
      const now = Date.now()
      let imported = 0
      for (const s of b.students || []) {
        if (s.name && s.name.trim()) {
          await runAsync('INSERT INTO students (id, class_id, name, student_no, device_id, total_points, pet_level, pet_exp, created_at) VALUES (?,?,?,?,NULL,0,1,0,?)', uuid(), b.classId, s.name.trim(), s.studentNo?.trim() || null, now)
          imported++
        }
      }
      return json({ success: true, imported }, 201)
    }

    // ===== EVALUATIONS =====
    if (path === '/evaluations' && method === 'GET') {
      const uid = await requireUser(request, env)
      const page = Number(params.get('page') || 1)
      const pageSize = Number(params.get('pageSize') || 20)
      const offset = (page - 1) * pageSize
      const cond = ['c.user_id = ?']
      const p = [uid]
      if (params.get('classId')) { cond.push('er.class_id = ?'); p.push(params.get('classId')) }
      if (params.get('studentId')) { cond.push('er.student_id = ?'); p.push(params.get('studentId')) }
      const where = cond.join(' AND ')
      const totalR = await getAsync(`SELECT COUNT(*) as total FROM evaluation_records er JOIN classes c ON er.class_id=c.id WHERE ${where}`, ...p)
      const records = await allAsync(`SELECT er.*, s.name as student_name FROM evaluation_records er JOIN students s ON er.student_id=s.id JOIN classes c ON er.class_id=c.id WHERE ${where} ORDER BY er.timestamp DESC LIMIT ? OFFSET ?`, ...p, pageSize, offset)
      return json({ records, total: totalR?.total || 0, page, pageSize, totalPages: Math.ceil((totalR?.total || 0) / pageSize) })
    }
    if (path === '/evaluations' && method === 'POST') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const cls = await getAsync('SELECT * FROM classes WHERE id = ?', b.classId)
      if (!cls || cls.user_id !== uid) return json({ error: '无权访问此班级' }, 403)
      const studentIds = Array.isArray(b.studentId) ? b.studentId : b.studentIds ? b.studentIds : [b.studentId]
      const results = []
      for (const sid of studentIds) {
        const id = uuid()
        const now = Date.now()
        await runAsync('INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?,?,?,?,?,?,?)', id, b.classId, sid, b.points, b.reason, b.category, now)
        await runAsync('UPDATE students SET total_points = total_points + ? WHERE id = ?', b.points, sid)
        const stu = await getAsync('SELECT * FROM students WHERE id = ?', sid)
        if (stu && stu.pet_type) {
          const newExp = Math.max(0, stu.total_points)
          const newLevel = calculateLevel(newExp)
          let graduated = false
          if (newLevel === 8 && stu.pet_level < 8) {
            await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?,?,?,?)', uuid(), sid, stu.pet_type, now)
            graduated = true
          }
          await runAsync('UPDATE students SET pet_exp=?, pet_level=? WHERE id=?', newExp, newLevel, sid)
          const result = { id, timestamp: now, petLevel: newLevel, petExp: newExp, levelUp: newLevel > stu.pet_level, levelDown: newLevel < stu.pet_level, graduated }
          await publishEvent(env, 'evaluation', { studentId: sid, studentName: stu.name, points: b.points, reason: b.reason, category: b.category, petLevel: newLevel, levelUp: result.levelUp, graduated })
          if (result.levelUp) await publishEvent(env, 'levelup', { studentId: sid, studentName: stu.name, petLevel: newLevel })
          results.push(result)
        } else {
          results.push({ id, timestamp: now })
        }
      }
      return json(results.length === 1 ? results[0] : results)
    }
    if (path === '/evaluations/latest' && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const classId = params.get('classId')
      if (!classId) return json({ error: 'classId required' }, 400)
      const cls = await getAsync('SELECT * FROM classes WHERE id = ?', classId)
      if (!cls || cls.user_id !== uid) return json({ error: '无权访问此班级' }, 403)
      const rec = await getAsync('SELECT * FROM evaluation_records WHERE class_id = ? ORDER BY timestamp DESC LIMIT 1', classId)
      if (!rec) return json({ error: '未找到记录' }, 404)
      const stu = await getAsync('SELECT * FROM students WHERE id = ?', rec.student_id)
      const newExp = Math.max(0, stu.pet_exp - Math.abs(rec.points))
      const newLevel = calculateLevel(newExp)
      await runAsync('UPDATE students SET total_points = total_points - ?, pet_exp=?, pet_level=? WHERE id=?', rec.points, newExp, newLevel, rec.student_id)
      await runAsync('DELETE FROM evaluation_records WHERE id = ?', rec.id)
      return json({ success: true, undone: rec })
    }
    if ((m = path.match(/^\/evaluations\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const rec = await getAsync('SELECT er.* FROM evaluation_records er JOIN classes c ON er.class_id=c.id WHERE er.id=? AND c.user_id=?', m[1], uid)
      if (!rec) return json({ error: '记录不存在' }, 404)
      const stu = await getAsync('SELECT * FROM students WHERE id = ?', rec.student_id)
      const newExp = Math.max(0, stu.pet_exp - Math.abs(rec.points))
      const newLevel = calculateLevel(newExp)
      await runAsync('UPDATE students SET total_points = total_points - ?, pet_exp=?, pet_level=? WHERE id=?', rec.points, newExp, newLevel, rec.student_id)
      await runAsync('DELETE FROM evaluation_records WHERE id = ?', m[1])
      return json({ success: true, undone: rec })
    }

    // ===== RULES =====
    if (path === '/rules' && method === 'GET') {
      const rules = await allAsync('SELECT * FROM evaluation_rules ORDER BY created_at ASC')
      return json({ rules })
    }
    if (path === '/rules' && method === 'POST') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const id = uuid()
      await runAsync('INSERT INTO evaluation_rules (id, name, points, category, is_custom, created_at) VALUES (?,?,?,?,1,?)', id, b.name, b.points, b.category, Date.now())
      return json({ id, name: b.name, points: b.points, category: b.category, is_custom: 1 }, 201)
    }
    if ((m = path.match(/^\/rules\/([^/]+)$/)) && method === 'DELETE') {
      await runAsync('DELETE FROM evaluation_rules WHERE id = ?', m[1])
      return json({ success: true })
    }

    // ===== SETTINGS =====
    if (path === '/settings' && method === 'GET') {
      const keys = ['task_confirm_mode', 'task_confirm_delay', 'openrouter_api_key', 'openrouter_model', 'groq_api_key', 'screen_brightness', 'screen_sleep_seconds', 'asr_provider', 'baidu_api_key', 'baidu_secret_key']
      const defaults = { task_confirm_mode: 'auto', task_confirm_delay: 30, openrouter_api_key: '', openrouter_model: 'openrouter/free', groq_api_key: '', screen_brightness: 80, screen_sleep_seconds: 15, asr_provider: 'workers-ai', baidu_api_key: '', baidu_secret_key: '' }
      const out = {}
      for (const k of keys) out[k] = await getSetting(k, defaults[k])
      return json(out)
    }
    if (path === '/settings' && method === 'POST') {
      const b = await readBody(request)
      for (const [k, v] of Object.entries(b)) await setSetting(k, v)
      return json({ success: true })
    }
    if ((m = path.match(/^\/settings\/ranking\/([^/]+)$/)) && method === 'GET') {
      const students = await allAsync('SELECT id, name, total_points, pet_level, pet_exp FROM students WHERE class_id = ? ORDER BY total_points DESC', m[1])
      return json({ ranking: students })
    }

    // ===== CHAT (Web 语音伙伴) =====
    if (path === '/chat' && method === 'POST') {
      const b = await readBody(request)
      const msg = (b.message || '').trim()
      if (!msg) return json({ success: true, reply: '嗯？我好像没听清，再跟我说说好不好～' })
      const reply = await petChat(env, msg)
      if (b.studentId) {
        await runAsync('INSERT INTO chat_logs (id, student_id, user_message, ai_response, timestamp) VALUES (?,?,?,?,?)', uuid(), b.studentId, msg, reply, Date.now())
      }
      return json({ success: true, reply })
    }

    // ===== DEVICE =====
    if (path === '/device/status' && method === 'GET') {
      const deviceId = getDeviceId(request)
      const stu = await getAsync('SELECT s.*, c.name as class_name FROM students s JOIN classes c ON s.class_id=c.id WHERE UPPER(s.device_id)=UPPER(?)', deviceId)
      if (!stu) return json({ status: 'unbound', message: '该硬件设备尚未与任何学生绑定', device_id: deviceId })
      const lp = getLevelProgress(stu.pet_exp)
      return json({ status: 'ok', student: { id: stu.id, name: stu.name, class_name: stu.class_name, total_points: stu.total_points, pet_type: stu.pet_type, pet_level: stu.pet_level, pet_exp: stu.pet_exp, exp_progress: lp.current, exp_required: lp.required, is_max_level: lp.isMaxLevel } })
    }
    if (path === '/device/voice' && method === 'POST') {
      return json(await handleDeviceVoice(request, env))
    }
    if (path === '/device/tts-stream' && method === 'GET') {
      const text = params.get('text') || ''
      if (!text) return new Response('Missing text', { status: 400 })
      const tts = await fetchTtsMp3(text)
      if (!tts) return new Response('TTS unavailable', { status: 502 })
      return new Response(tts.mp3, { headers: { 'Content-Type': 'audio/mpeg', 'Cache-Control': 'public, max-age=3600', 'Access-Control-Allow-Origin': '*' } })
    }
    if (path === '/device/firmware-version' && method === 'GET') {
      const v = await getSetting('firmware_latest_version', '2.0.0')
      const dl = await getSetting('firmware_download_url', '/firmware/latest.bin')
      const cs = await getSetting('firmware_checksum', 'dummy')
      return json({ latest_version: v, download_url: dl, checksum: cs })
    }
    if (path === '/device/heartbeat' && method === 'POST') {
      const deviceId = getDeviceId(request)
      const b = await readBody(request)
      const stu = await getAsync('SELECT id FROM students WHERE UPPER(device_id)=UPPER(?)', deviceId)
      if (!stu) return json({ status: 'unbound', error: '未找到绑定学生' })
      await runAsync('UPDATE students SET battery_level=?, is_charging=?, last_seen=? WHERE id=?', b.battery_level ?? 100, b.is_charging ? 1 : 0, Date.now(), stu.id)
      return json({ success: true, timestamp: Date.now() })
    }
    if (path === '/device/schedules' && method === 'GET') {
      const deviceId = getDeviceId(request)
      const stu = await getAsync('SELECT id FROM students WHERE UPPER(device_id)=UPPER(?)', deviceId)
      if (!stu) return json({ status: 'unbound', schedules: [] })
      await runAsync('UPDATE students SET last_seen=? WHERE id=?', Date.now(), stu.id)
      const schedules = await allAsync('SELECT id, day_of_week, time_str, task_desc, is_active FROM schedules WHERE student_id=? AND is_active=1', stu.id)
      return json({ status: 'ok', schedules })
    }
    if (path === '/device/settings' && method === 'GET') {
      const keys = ['task_confirm_mode', 'task_confirm_delay', 'asr_provider', 'screen_brightness', 'screen_sleep_seconds']
      const out = {}
      for (const k of keys) out[k] = await getSetting(k)
      return json(out)
    }
    if (path === '/device/settings' && method === 'POST') {
      const b = await readBody(request)
      for (const [k, v] of Object.entries(b)) await setSetting(k, v)
      return json({ success: true })
    }
    if (path === '/device/unbound-devices' && method === 'GET') {
      return json({ success: true, devices: [] })
    }
    if (path === '/device/tasks' && method === 'GET') {
      const uid = await requireUser(request, env)
      const status = params.get('status') || ''
      const tasks = await allAsync(`SELECT ta.*, s.name as student_name, c.name as class_name FROM student_task_applications ta JOIN students s ON ta.student_id=s.id JOIN classes c ON s.class_id=c.id WHERE c.user_id=? AND (?='' OR ta.status=?) ORDER BY ta.created_at DESC`, uid, status, status)
      return json({ success: true, tasks })
    }
    if ((m = path.match(/^\/device\/tasks\/([^/]+)\/(approve|reject)$/)) && method === 'POST') {
      const uid = await requireUser(request, env)
      const task = await getAsync(`SELECT ta.*, s.class_id FROM student_task_applications ta JOIN students s ON ta.student_id=s.id JOIN classes c ON s.class_id=c.id WHERE ta.id=? AND c.user_id=?`, m[1], uid)
      if (!task) return json({ error: '任务不存在' }, 404)
      if (task.status !== 'pending') return json({ error: '已审核' }, 400)
      if (m[2] === 'approve') {
        const now = Date.now()
        await runAsync("UPDATE student_task_applications SET status='approved' WHERE id=?", m[1])
        const evalId = uuid()
        await runAsync('INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?,?,?,?,?,?,?)', evalId, task.class_id, task.student_id, task.points, `加分申请核销: ${task.task_name}`, '学习', now)
        await runAsync('UPDATE students SET total_points = total_points + ? WHERE id=?', task.points, task.student_id)
        const stu = await getAsync('SELECT * FROM students WHERE id=?', task.student_id)
        if (stu && stu.pet_type) {
          const newExp = Math.max(0, stu.total_points)
          const newLevel = calculateLevel(newExp)
          if (newLevel === 8 && stu.pet_level < 8) await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?,?,?,?)', uuid(), task.student_id, stu.pet_type, now)
          await runAsync('UPDATE students SET pet_exp=?, pet_level=? WHERE id=?', newExp, newLevel, task.student_id)
        }
      } else {
        await runAsync("UPDATE student_task_applications SET status='rejected' WHERE id=?", m[1])
      }
      return json({ success: true })
    }
    if ((m = path.match(/^\/device\/schedules\/student\/([^/]+)$/)) && method === 'GET') {
      const schedules = await allAsync('SELECT * FROM schedules WHERE student_id=? ORDER BY day_of_week ASC, time_str ASC', m[1])
      return json({ success: true, schedules })
    }
    if ((m = path.match(/^\/device\/schedules\/student\/([^/]+)$/)) && method === 'POST') {
      const b = await readBody(request)
      if (!b.day_of_week || !b.time_str || !b.task_desc) return json({ error: '参数缺失' }, 400)
      const id = uuid()
      await runAsync('INSERT INTO schedules (id, student_id, day_of_week, time_str, task_desc, is_active, created_at) VALUES (?,?,?,?,?,1,?)', id, m[1], b.day_of_week, b.time_str, b.task_desc, Date.now())
      return json({ success: true, id })
    }
    if ((m = path.match(/^\/device\/schedules\/([^/]+)$/)) && method === 'DELETE') {
      await runAsync('DELETE FROM schedules WHERE id=?', m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/device\/chat-logs\/student\/([^/]+)$/)) && method === 'GET') {
      const logs = await allAsync('SELECT * FROM chat_logs WHERE student_id=? ORDER BY timestamp DESC LIMIT 200', m[1])
      return json({ success: true, logs })
    }
    if (path === '/device/font/cjk16.bin' && method === 'GET') {
      if (env.BUCKET) {
        const obj = await env.BUCKET.get('cjk16.bin')
        if (obj) return new Response(obj.body, { headers: { 'Content-Type': 'application/octet-stream', 'Cache-Control': 'public, max-age=86400' } })
      }
      return new Response('font not found', { status: 404 })
    }

    // ===== BACKUP / RESTORE =====
    if (path === '/backup' && method === 'GET') {
      const [classes, students, evaluations, settings] = await Promise.all([
        allAsync('SELECT * FROM classes'),
        allAsync('SELECT * FROM students'),
        allAsync('SELECT * FROM evaluation_records'),
        allAsync('SELECT * FROM settings')
      ])
      return json({ classes, students, evaluations, settings })
    }
    if (path === '/restore' && method === 'POST') {
      const b = await readBody(request)
      if (!b || !b.classes) return json({ error: '无效备份' }, 400)
      for (const c of b.classes) await runAsync('INSERT OR REPLACE INTO classes (id, user_id, name, created_at, updated_at) VALUES (?,?,?,?,?)', c.id, c.user_id, c.name, c.created_at, c.updated_at)
      for (const s of b.students) await runAsync('INSERT OR REPLACE INTO students (id, class_id, name, student_no, total_points, pet_type, pet_level, pet_exp, device_id, created_at) VALUES (?,?,?,?,?,?,?,?,?,?)', s.id, s.class_id, s.name, s.student_no, s.total_points, s.pet_type, s.pet_level, s.pet_exp, s.device_id, s.created_at)
      return json({ success: true })
    }

    return json({ error: `未匹配路由: ${method} ${path}` }, 404)
  } catch (err) {
    if (err.status === 401) return json({ error: '未登录或登录已过期' }, 401)
    console.error('❌ [API]', err.stack || err)
    return json({ error: '服务器错误', detail: err.message }, 500)
  }
}

async function publishEvent(env, type, payload) {
  try {
    await q.run(env.DB, 'INSERT INTO device_events (type, payload, timestamp) VALUES (?, ?, ?)', type, JSON.stringify(payload), Date.now())
  } catch (e) {
    console.warn('⚠️ [API] 事件写入失败:', e.message)
  }
}

// ============ 入口 ============
export default {
  async fetch(request, env, ctx) {
    try {
      bindDb(env.DB)
      setSecrets(env)
      await ensureSeed(env)
      return handleApi(request, env, ctx)
    } catch (e) {
      const msg = (e && e.message) ? e.message : String(e)
      const stack = e && e.stack ? String(e.stack) : ''
      return new Response(JSON.stringify({ error: msg, stack }), {
        status: 500,
        headers: { 'Content-Type': 'application/json; charset=utf-8', 'Access-Control-Allow-Origin': '*' }
      })
    }
  }
}
