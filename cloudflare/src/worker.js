// 班级宠物园 · Cloudflare Worker 主入口
// 承担: REST API + /ws/voice(转交 Durable Object) + SSE 实时事件 + /device/* 设备端点
// 数据库: D1 (SQLite 兼容) · AI: Workers AI (keyless) · 资产: R2(可选)

import { bindDb, q, getAsync, allAsync, runAsync, getSetting, setSetting, getDeviceSettings, setDeviceSetting, readAsrKeys, getOwnerProfile, ownerProfileToContext } from './db.js'
import { setSecrets, hashPassword, verifyPassword, generateToken, verifyToken, getUserIdFromAuth } from './auth.js'
import { fetchTtsMp3, petChat } from './ai.js'
import { VoiceDO, applyIntent } from './voice-do.js'
import { matchCorpus } from './intents.js'

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

// 方案 B: 通过 devices 表做"设备 -> 学生"解析 (学生与设备解耦)
async function deviceToStudent(deviceId) {
  if (!deviceId) return null
  const dev = await getAsync('SELECT student_id FROM devices WHERE UPPER(device_id) = UPPER(?)', deviceId)
  return dev ? dev.student_id : null
}

// 绑定/解绑设备 (维护 devices 表与 students.device_id 镜像)
async function bindDevice(deviceId, studentId, classId) {
  if (!deviceId) return
  await runAsync(
    `INSERT INTO devices (device_id, student_id, class_id, created_at) VALUES (?, ?, ?, ?)
       ON CONFLICT(device_id) DO UPDATE SET student_id = excluded.student_id, class_id = COALESCE(excluded.class_id, class_id)`,
    deviceId, studentId, classId, Date.now()
  )
  await runAsync('UPDATE students SET device_id = ? WHERE id = ?', deviceId, studentId)
}
async function unbindDeviceByStudent(studentId) {
  const stu = await getAsync('SELECT device_id FROM students WHERE id = ?', studentId)
  if (stu && stu.device_id) {
    await runAsync('UPDATE devices SET student_id = NULL WHERE UPPER(device_id) = UPPER(?)', stu.device_id)
  }
  await runAsync('UPDATE students SET device_id = NULL WHERE id = ?', studentId)
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
      // 按设备配置选择 ASR provider (device 覆盖 + 全局回退)
      const ds = await getDeviceSettings(deviceId, ['asr_provider'])
      const provider = ds.asr_provider || 'workers-ai'
      const keys = await readAsrKeys(env.DB, provider)
      text = await transcribe(env, pcmChunks, pcmBytes, { provider, keys })
    } catch (e) {
      console.error('❌ [ASR] transcribe 失败:', e && (e.stack || e.message) || e)
      return { action: 'none', text: '听不清楚', reply_text: '抱歉，刚才没听清，请再说一遍。' }
    }
  }
  if (!text) return { action: 'none', reply_text: '未提供音频或文本。' }
  // 本地语料优先: 命中即执行动作(快 + 省 token), 否则走大模型闲聊
  const corpus = matchCorpus(text)
  let nlp
  if (corpus && corpus.action !== 'chat') {
    nlp = { action: corpus.action, ...corpus.params, reply_text: corpus.reply || '' }
    console.log('🧠 [API] 语料命中:', corpus.action, JSON.stringify(corpus.params || {}))
  } else {
    try {
      let ownerCtx = ''
      try {
        const profile = await getOwnerProfile(db, student.id)
        ownerCtx = ownerProfileToContext(profile)
      } catch {}
      nlp = await classifyIntent(env, text, '', ownerCtx)
    } catch {
      nlp = regexClassifyIntent(text)
    }
  }
  if (nlp.reply_text) nlp.reply_text = nlp.reply_text.slice(0, 200) // 防超长 TTS 链接
  const responseData = await applyIntent(env, student, nlp, now, text)
  responseData.text = text
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
      if (b.deviceId) await bindDevice(b.deviceId, id, b.classId)
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
      const newDev = b.deviceId || null
      if ((stu.device_id || null) !== newDev) {
        if (stu.device_id) await runAsync('UPDATE devices SET student_id=NULL WHERE UPPER(device_id)=UPPER(?)', stu.device_id)
        if (newDev) await bindDevice(newDev, m[1], stu.class_id)
      }
      await runAsync('UPDATE students SET name=?, student_no=?, device_id=? WHERE id=?', b.name, b.studentNo || null, newDev, m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/students\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.* FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '学生不存在' }, 404)
      await unbindDeviceByStudent(m[1])
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
      const dev = deviceId ? await getAsync('SELECT * FROM devices WHERE UPPER(device_id)=UPPER(?)', deviceId) : null
      if (!dev) return json({ status: 'unbound', message: '该硬件设备尚未注册', device_id: deviceId })
      const deviceInfo = {
        device_id: dev.device_id,
        name: dev.name,
        battery_level: dev.battery_level,
        is_charging: dev.is_charging,
        last_seen: dev.last_seen,
        firmware_version: dev.firmware_version
      }
      if (!dev.student_id) {
        return json({ status: 'unbound', message: '该设备尚未绑定学生', device_id: deviceId, device: deviceInfo })
      }
      const stu = await getAsync('SELECT s.*, c.name as class_name FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=?', dev.student_id)
      if (!stu) return json({ status: 'unbound', message: '绑定的学生不存在', device_id: deviceId, device: deviceInfo })
      const lp = getLevelProgress(stu.pet_exp)
      return json({
        status: 'ok',
        student: { id: stu.id, name: stu.name, class_name: stu.class_name, total_points: stu.total_points, pet_type: stu.pet_type, pet_level: stu.pet_level, pet_exp: stu.pet_exp, exp_progress: lp.current, exp_required: lp.required, is_max_level: lp.isMaxLevel },
        device: deviceInfo
      })
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
      if (!deviceId) return json({ status: 'unbound', error: '缺少设备标识' })
      let dev = await getAsync('SELECT * FROM devices WHERE UPPER(device_id)=UPPER(?)', deviceId)
      if (!dev) {
        // 自动登记未注册设备 (处于未绑定状态)
        await runAsync('INSERT INTO devices (device_id, name, battery_level, is_charging, last_seen, created_at) VALUES (?,?,?,?,?,?)', deviceId, deviceId, b.battery_level ?? 100, b.is_charging ? 1 : 0, Date.now(), Date.now())
        dev = { device_id: deviceId, student_id: null }
      }
      await runAsync('UPDATE devices SET battery_level=?, is_charging=?, last_seen=? WHERE UPPER(device_id)=UPPER(?)', b.battery_level ?? 100, b.is_charging ? 1 : 0, Date.now(), deviceId)
      // 镜像电量到绑定的学生(供学生卡片展示)
      if (dev.student_id) {
        await runAsync('UPDATE students SET battery_level=?, is_charging=?, last_seen=? WHERE id=?', b.battery_level ?? 100, b.is_charging ? 1 : 0, Date.now(), dev.student_id)
      }
      return json({ success: true, timestamp: Date.now() })
    }
    if (path === '/device/schedules' && method === 'GET') {
      const deviceId = getDeviceId(request)
      const sid = await deviceToStudent(deviceId)
      if (!sid) return json({ status: 'unbound', schedules: [] })
      await runAsync('UPDATE students SET last_seen=? WHERE id=?', Date.now(), sid)
      const schedules = await allAsync('SELECT id, day_of_week, time_str, task_desc, is_active FROM schedules WHERE student_id=? AND is_active=1', sid)
      return json({ status: 'ok', schedules })
    }
    if (path === '/device/settings' && method === 'GET') {
      const deviceId = getDeviceId(request) || params.get('device_id') || ''
      const keys = ['screen_brightness', 'screen_sleep_seconds', 'asr_provider']
      const out = await getDeviceSettings(deviceId, keys)
      return json(out)
    }
    if (path === '/device/settings' && method === 'POST') {
      const deviceId = getDeviceId(request) || params.get('device_id') || ''
      const b = await readBody(request)
      for (const [k, v] of Object.entries(b)) {
        if (['screen_brightness', 'screen_sleep_seconds', 'asr_provider'].includes(k)) {
          await setDeviceSetting(deviceId, k, v)
        }
      }
      return json({ success: true })
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
    if ((m = path.match(/^\/device\/schedules\/([^/]+)$/)) && method === 'PUT') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      const exists = await getAsync('SELECT id FROM schedules WHERE id=? AND student_id=?', m[1], sid)
      if (!exists) return json({ error: '日程不存在' }, 404)
      const b = await readBody(request)
      const updates = []
      const vals = []
      if (b.day_of_week !== undefined) { updates.push('day_of_week=?'); vals.push(Number(b.day_of_week)) }
      if (b.time_str !== undefined) { updates.push('time_str=?'); vals.push(String(b.time_str).slice(0, 5)) }
      if (b.task_desc !== undefined) { updates.push('task_desc=?'); vals.push(String(b.task_desc).slice(0, 80)) }
      if (!updates.length) return json({ error: '无有效字段' }, 400)
      vals.push(m[1])
      await runAsync(`UPDATE schedules SET ${updates.join(', ')} WHERE id=?`, ...vals)
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

    // ===== DEVICES (设备独立管理, 方案 B) =====
    if (path === '/devices' && method === 'GET') {
      const uid = await requireUser(request, env)
      const rows = await allAsync(`SELECT d.*, s.name as student_name, c.name as class_name
        FROM devices d LEFT JOIN students s ON d.student_id=s.id LEFT JOIN classes c ON d.class_id=c.id
        WHERE (c.user_id=? OR d.class_id IS NULL OR d.student_id IS NULL)
        ORDER BY d.last_seen DESC`, uid)
      return json({ success: true, devices: rows })
    }
    if (path === '/devices/register' && method === 'POST') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const deviceId = (b.device_id || '').trim()
      if (!deviceId) return json({ error: '缺少 device_id' }, 400)
      let classId = b.class_id || null
      if (classId) {
        const cls = await getAsync('SELECT user_id FROM classes WHERE id=?', classId)
        if (!cls || cls.user_id !== uid) return json({ error: '无权绑定该班级' }, 403)
      }
      await runAsync(`INSERT INTO devices (device_id, name, class_id, created_at) VALUES (?,?,?,?)
        ON CONFLICT(device_id) DO UPDATE SET name=COALESCE(excluded.name, name), class_id=COALESCE(excluded.class_id, class_id)`,
        deviceId, b.name || deviceId, classId, Date.now())
      return json({ success: true, device_id: deviceId })
    }
    if (path === '/devices/unbound' && method === 'GET') {
      const uid = await requireUser(request, env)
      const rows = await allAsync(`SELECT d.device_id, d.name, d.last_seen, d.battery_level, d.is_charging, d.firmware_version
        FROM devices d LEFT JOIN classes c ON d.class_id=c.id
        WHERE d.student_id IS NULL AND (c.user_id=? OR d.class_id IS NULL)
        ORDER BY d.last_seen DESC`, uid)
      return json({ success: true, devices: rows })
    }
    if ((m = path.match(/^\/devices\/([^/]+)$/)) && method === 'GET') {
      const uid = await requireUser(request, env)
      const dev = await getAsync('SELECT d.*, s.name as student_name, c.name as class_name FROM devices d LEFT JOIN students s ON d.student_id=s.id LEFT JOIN classes c ON d.class_id=c.id WHERE UPPER(d.device_id)=UPPER(?)', m[1])
      if (!dev) return json({ error: '设备不存在' }, 404)
      const settings = await getDeviceSettings(m[1], ['screen_brightness', 'screen_sleep_seconds', 'asr_provider'])
      return json({ success: true, device: dev, settings })
    }
    if ((m = path.match(/^\/devices\/([^/]+)$/)) && method === 'PUT') {
      const uid = await requireUser(request, env)
      const b = await readBody(request)
      const dev = await getAsync('SELECT * FROM devices WHERE UPPER(device_id)=UPPER(?)', m[1])
      if (!dev) return json({ error: '设备不存在' }, 404)
      if (dev.class_id) {
        const cls = await getAsync('SELECT user_id FROM classes WHERE id=?', dev.class_id)
        if (cls && cls.user_id !== uid) return json({ error: '无权操作该设备' }, 403)
      }
      let studentId = b.student_id === undefined ? dev.student_id : (b.student_id || null)
      if (studentId) {
        const stu = await getAsync('SELECT s.* FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', studentId, uid)
        if (!stu) return json({ error: '学生不存在或无权' }, 404)
        await runAsync('UPDATE devices SET student_id=NULL WHERE student_id=?', studentId) // 该学生原有设备解绑
      }
      await runAsync('UPDATE devices SET student_id=?, name=COALESCE(?, name), class_id=COALESCE(?, class_id) WHERE UPPER(device_id)=UPPER(?)',
        studentId, b.name || null, b.class_id || null, m[1])
      if (studentId) await runAsync('UPDATE students SET device_id=? WHERE id=?', m[1], studentId)
      else await runAsync('UPDATE students SET device_id=NULL WHERE device_id=?', m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/devices\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const dev = await getAsync('SELECT * FROM devices WHERE UPPER(device_id)=UPPER(?)', m[1])
      if (!dev) return json({ error: '设备不存在' }, 404)
      if (dev.class_id) {
        const cls = await getAsync('SELECT user_id FROM classes WHERE id=?', dev.class_id)
        if (cls && cls.user_id !== uid) return json({ error: '无权操作该设备' }, 403)
      }
      await runAsync('UPDATE students SET device_id=NULL WHERE device_id=?', m[1])
      await runAsync('DELETE FROM devices WHERE UPPER(device_id)=UPPER(?)', m[1])
      return json({ success: true })
    }

    // ===== SYSTEM SETTINGS (全局平台配置) =====
    if (path === '/system/settings' && method === 'GET') {
      const keys = ['task_confirm_mode', 'task_confirm_delay', 'openrouter_api_key', 'openrouter_model', 'groq_api_key', 'openai_api_key', 'baidu_api_key', 'baidu_secret_key', 'asr_provider', 'firmware_latest_version', 'firmware_download_url', 'firmware_checksum']
      const out = {}
      for (const k of keys) out[k] = await getSetting(k)
      return json(out)
    }
    if (path === '/system/settings' && method === 'POST') {
      const b = await readBody(request)
      const allowed = ['task_confirm_mode', 'task_confirm_delay', 'openrouter_api_key', 'openrouter_model', 'groq_api_key', 'openai_api_key', 'baidu_api_key', 'baidu_secret_key', 'asr_provider', 'firmware_latest_version', 'firmware_download_url', 'firmware_checksum']
      for (const [k, v] of Object.entries(b)) {
        if (allowed.includes(k)) await setSetting(k, v)
      }
      return json({ success: true })
    }

    // ===== BACKUP / RESTORE =====
    if (path === '/backup' && method === 'GET') {
      const [classes, students, evaluations, settings, devices, deviceSettings] = await Promise.all([
        allAsync('SELECT * FROM classes'),
        allAsync('SELECT * FROM students'),
        allAsync('SELECT * FROM evaluation_records'),
        allAsync('SELECT * FROM settings'),
        allAsync('SELECT * FROM devices'),
        allAsync('SELECT * FROM device_settings')
      ])
      return json({ classes, students, evaluations, settings, devices, device_settings: deviceSettings })
    }
    if (path === '/restore' && method === 'POST') {
      const b = await readBody(request)
      if (!b || !b.classes) return json({ error: '无效备份' }, 400)
      for (const c of b.classes) await runAsync('INSERT OR REPLACE INTO classes (id, user_id, name, created_at, updated_at) VALUES (?,?,?,?,?)', c.id, c.user_id, c.name, c.created_at, c.updated_at)
      for (const s of b.students) await runAsync('INSERT OR REPLACE INTO students (id, class_id, name, student_no, total_points, pet_type, pet_level, pet_exp, device_id, created_at) VALUES (?,?,?,?,?,?,?,?,?,?)', s.id, s.class_id, s.name, s.student_no, s.total_points, s.pet_type, s.pet_level, s.pet_exp, s.device_id, s.created_at)
      if (Array.isArray(b.devices)) {
        for (const d of b.devices) await runAsync('INSERT OR REPLACE INTO devices (device_id, name, student_id, class_id, firmware_version, pet_type, pet_level, battery_level, is_charging, last_seen, created_at) VALUES (?,?,?,?,?,?,?,?,?,?,?)', d.device_id, d.name, d.student_id, d.class_id, d.firmware_version, d.pet_type, d.pet_level, d.battery_level, d.is_charging, d.last_seen, d.created_at)
      }
      if (Array.isArray(b.device_settings)) {
        for (const ds of b.device_settings) await runAsync('INSERT OR REPLACE INTO device_settings (device_id, key, value, updated_at) VALUES (?,?,?,?)', ds.device_id, ds.key, ds.value, ds.updated_at)
      }
      return json({ success: true })
    }

    // ===== 宠物主人记忆(设备 + 网页通用) =====
    // 通过 device_id 解析 student_id 的辅助 (方案 B: 经 devices 表)
    async function studentIdByDevice(deviceId) {
      return await deviceToStudent(deviceId)
    }

    // ---- 设备端: 日历 ----
    if (path === '/device/calendar' && method === 'GET') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', events: [] })
      await runAsync('UPDATE students SET last_seen=? WHERE id=?', Date.now(), sid)
      const events = await allAsync('SELECT id, title, event_date, time_str, description, created_at FROM calendar_events WHERE student_id=? ORDER BY event_date ASC, time_str ASC', sid)
      return json({ status: 'ok', events })
    }
    if (path === '/device/calendar' && method === 'POST') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      const b = await readBody(request)
      if (!b.title || !b.event_date) return json({ error: '缺少 title 或 event_date' }, 400)
      const id = uuid()
      await runAsync('INSERT INTO calendar_events (id, student_id, title, event_date, time_str, description, created_at) VALUES (?,?,?,?,?,?,?)', id, sid, String(b.title).slice(0,80), String(b.event_date).slice(0,10), b.time_str ? String(b.time_str).slice(0,5) : null, b.description ? String(b.description).slice(0,200) : '', Date.now())
      return json({ success: true, id })
    }
    if ((m = path.match(/^\/device\/calendar\/([^/]+)$/)) && method === 'DELETE') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      await runAsync('DELETE FROM calendar_events WHERE id=? AND student_id=?', m[1], sid)
      return json({ success: true })
    }
    if ((m = path.match(/^\/device\/calendar\/([^/]+)$/)) && method === 'PUT') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      const b = await readBody(request)
      const updates = []
      const vals = []
      if (b.title !== undefined) { updates.push('title=?'); vals.push(String(b.title).slice(0, 80)) }
      if (b.event_date !== undefined) { updates.push('event_date=?'); vals.push(String(b.event_date).slice(0, 10)) }
      if (b.time_str !== undefined) { updates.push('time_str=?'); vals.push(b.time_str ? String(b.time_str).slice(0, 5) : null) }
      if (b.description !== undefined) { updates.push('description=?'); vals.push(String(b.description).slice(0, 200)) }
      if (!updates.length) return json({ error: '无有效字段' }, 400)
      vals.push(m[1], sid)
      await runAsync(`UPDATE calendar_events SET ${updates.join(', ')} WHERE id=? AND student_id=?`, ...vals)
      return json({ success: true })
    }

    // ---- 设备端: 清单 ----
    if (path === '/device/checklist' && method === 'GET') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', items: [] })
      await runAsync('UPDATE students SET last_seen=? WHERE id=?', Date.now(), sid)
      const items = await allAsync('SELECT id, content, is_done, created_at FROM checklist_items WHERE student_id=? ORDER BY is_done ASC, created_at ASC', sid)
      return json({ status: 'ok', items })
    }
    if (path === '/device/checklist' && method === 'POST') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      const b = await readBody(request)
      if (!b.content) return json({ error: '缺少 content' }, 400)
      const id = uuid()
      await runAsync('INSERT INTO checklist_items (id, student_id, content, is_done, created_at) VALUES (?,?,?,0,?)', id, sid, String(b.content).slice(0,120), Date.now())
      return json({ success: true, id })
    }
    if ((m = path.match(/^\/device\/checklist\/([^/]+)$/)) && method === 'PUT') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      const b = await readBody(request)
      const updates = []
      const vals = []
      if (b.content !== undefined) { updates.push('content=?'); vals.push(String(b.content).slice(0, 120)) }
      if (b.is_done !== undefined) { updates.push('is_done=?'); vals.push(b.is_done ? 1 : 0) }
      if (!updates.length) return json({ error: '无有效字段' }, 400)
      vals.push(m[1], sid)
      await runAsync(`UPDATE checklist_items SET ${updates.join(', ')} WHERE id=? AND student_id=?`, ...vals)
      return json({ success: true })
    }
    if ((m = path.match(/^\/device\/checklist\/([^/]+)$/)) && method === 'DELETE') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      await runAsync('DELETE FROM checklist_items WHERE id=? AND student_id=?', m[1], sid)
      return json({ success: true })
    }

    // ---- 设备端: 宠物主人记忆 ----
    if (path === '/device/owner-profile' && method === 'GET') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', profile: null })
      await runAsync('UPDATE students SET last_seen=? WHERE id=?', Date.now(), sid)
      const p = await getOwnerProfile(env.DB, sid)
      if (!p) return json({ status: 'ok', profile: null, emotion_recent: [], learning_recent: [] })
      let profile = null, emotion_recent = [], learning_recent = []
      try { profile = p.profile_json ? JSON.parse(p.profile_json) : null } catch {}
      try { emotion_recent = p.emotion_log ? JSON.parse(p.emotion_log).slice(-8) : [] } catch {}
      try { learning_recent = p.learning_log ? JSON.parse(p.learning_log).slice(-8) : [] } catch {}
      return json({ status: 'ok', profile, emotion_recent, learning_recent })
    }
    if (path === '/device/owner-profile' && method === 'POST') {
      const deviceId = getDeviceId(request)
      const sid = await studentIdByDevice(deviceId)
      if (!sid) return json({ status: 'unbound', error: '未绑定' }, 400)
      const b = await readBody(request)
      const now = Date.now()
      let p = await getOwnerProfile(env.DB, sid)
      if (!p) {
        p = { profile_json: null, emotion_log: '[]', learning_log: '[]' }
        await runAsync('INSERT INTO owner_profiles (student_id, profile_json, emotion_log, learning_log, updated_at) VALUES (?,?,?,?,?)', sid, null, '[]', '[]', now)
      }
      if (b.type === 'profile' && b.data) {
        await runAsync('UPDATE owner_profiles SET profile_json=?, updated_at=? WHERE student_id=?', JSON.stringify(b.data).slice(0,2000), now, sid)
      } else if (b.type === 'emotion' && b.data) {
        let arr = []
        try { arr = p.emotion_log ? JSON.parse(p.emotion_log) : [] } catch {}
        arr.push({ ts: now, ...b.data })
        if (arr.length > 100) arr = arr.slice(-100)
        await runAsync('UPDATE owner_profiles SET emotion_log=?, updated_at=? WHERE student_id=?', JSON.stringify(arr), now, sid)
      } else if (b.type === 'learning' && b.data) {
        let arr = []
        try { arr = p.learning_log ? JSON.parse(p.learning_log) : [] } catch {}
        arr.push({ ts: now, ...b.data })
        if (arr.length > 100) arr = arr.slice(-100)
        await runAsync('UPDATE owner_profiles SET learning_log=?, updated_at=? WHERE student_id=?', JSON.stringify(arr), now, sid)
      } else {
        return json({ error: '未知 type' }, 400)
      }
      return json({ success: true })
    }

    // ---- 网页端(老师后台): 日历 ----
    if ((m = path.match(/^\/students\/([^/]+)\/calendar$/)) && method === 'GET') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const events = await allAsync('SELECT * FROM calendar_events WHERE student_id=? ORDER BY event_date ASC, time_str ASC', m[1])
      return json({ success: true, events })
    }
    if ((m = path.match(/^\/students\/([^/]+)\/calendar$/)) && method === 'POST') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const b = await readBody(request)
      if (!b.title || !b.event_date) return json({ error: '缺少 title 或 event_date' }, 400)
      const id = uuid()
      await runAsync('INSERT INTO calendar_events (id, student_id, title, event_date, time_str, description, created_at) VALUES (?,?,?,?,?,?,?)', id, m[1], String(b.title).slice(0,80), String(b.event_date).slice(0,10), b.time_str ? String(b.time_str).slice(0,5) : null, b.description ? String(b.description).slice(0,200) : '', Date.now())
      return json({ success: true, id })
    }
    if ((m = path.match(/^\/students\/([^/]+)\/calendar\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      await runAsync('DELETE FROM calendar_events WHERE id=? AND student_id=?', m[2], m[1])
      return json({ success: true })
    }

    // ---- 网页端(老师后台): 清单 ----
    if ((m = path.match(/^\/students\/([^/]+)\/checklist$/)) && method === 'GET') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const items = await allAsync('SELECT * FROM checklist_items WHERE student_id=? ORDER BY is_done ASC, created_at ASC', m[1])
      return json({ success: true, items })
    }
    if ((m = path.match(/^\/students\/([^/]+)\/checklist$/)) && method === 'POST') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const b = await readBody(request)
      if (!b.content) return json({ error: '缺少 content' }, 400)
      const id = uuid()
      await runAsync('INSERT INTO checklist_items (id, student_id, content, is_done, created_at) VALUES (?,?,?,0,?)', id, m[1], String(b.content).slice(0,120), Date.now())
      return json({ success: true, id })
    }
    if ((m = path.match(/^\/students\/([^/]+)\/checklist\/([^/]+)$/)) && method === 'PUT') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const b = await readBody(request)
      await runAsync('UPDATE checklist_items SET is_done=? WHERE id=? AND student_id=?', b.is_done ? 1 : 0, m[2], m[1])
      return json({ success: true })
    }
    if ((m = path.match(/^\/students\/([^/]+)\/checklist\/([^/]+)$/)) && method === 'DELETE') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      await runAsync('DELETE FROM checklist_items WHERE id=? AND student_id=?', m[2], m[1])
      return json({ success: true })
    }

    // ---- 网页端(老师后台): 宠物主人记忆 ----
    if ((m = path.match(/^\/students\/([^/]+)\/owner-profile$/)) && method === 'GET') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const p = await getOwnerProfile(env.DB, m[1])
      let profile = null, emotion_log = [], learning_log = []
      if (p) {
        try { profile = p.profile_json ? JSON.parse(p.profile_json) : null } catch {}
        try { emotion_log = p.emotion_log ? JSON.parse(p.emotion_log) : [] } catch {}
        try { learning_log = p.learning_log ? JSON.parse(p.learning_log) : [] } catch {}
      }
      return json({ success: true, profile, emotion_log, learning_log })
    }
    if ((m = path.match(/^\/students\/([^/]+)\/owner-profile$/)) && method === 'POST') {
      const uid = await requireUser(request, env)
      const stu = await getAsync('SELECT s.id FROM students s JOIN classes c ON s.class_id=c.id WHERE s.id=? AND c.user_id=?', m[1], uid)
      if (!stu) return json({ error: '无权限' }, 403)
      const b = await readBody(request)
      const now = Date.now()
      let p = await getOwnerProfile(env.DB, m[1])
      if (!p) {
        await runAsync('INSERT INTO owner_profiles (student_id, profile_json, emotion_log, learning_log, updated_at) VALUES (?,?,?,?,?)', m[1], null, '[]', '[]', now)
        p = { profile_json: null, emotion_log: '[]', learning_log: '[]' }
      }
      if (b.type === 'profile' && b.data) {
        await runAsync('UPDATE owner_profiles SET profile_json=?, updated_at=? WHERE student_id=?', JSON.stringify(b.data).slice(0,2000), now, m[1])
      } else if (b.type === 'emotion' && b.data) {
        let arr = []
        try { arr = p.emotion_log ? JSON.parse(p.emotion_log) : [] } catch {}
        arr.push({ ts: now, ...b.data })
        if (arr.length > 100) arr = arr.slice(-100)
        await runAsync('UPDATE owner_profiles SET emotion_log=?, updated_at=? WHERE student_id=?', JSON.stringify(arr), now, m[1])
      } else if (b.type === 'learning' && b.data) {
        let arr = []
        try { arr = p.learning_log ? JSON.parse(p.learning_log) : [] } catch {}
        arr.push({ ts: now, ...b.data })
        if (arr.length > 100) arr = arr.slice(-100)
        await runAsync('UPDATE owner_profiles SET learning_log=?, updated_at=? WHERE student_id=?', JSON.stringify(arr), now, m[1])
      } else {
        return json({ error: '未知 type' }, 400)
      }
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
