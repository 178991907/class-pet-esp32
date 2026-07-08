import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import crypto from 'crypto'
import multer from 'multer'
import fetch from 'node-fetch'
import FormData from 'form-data'
import { getAsync, allAsync, runAsync } from '../db.js'
import { calculateLevel } from '../utils/level.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()
const upload = multer({ storage: multer.memoryStorage() })
const lastVoiceRequestTimes = new Map()
const unboundDevices = new Map()

// ================= 安全防重放中间件 =================

// 通用设备通信签名与防重放中间件
async function deviceAuthMiddleware(req, res, next) {
  const deviceId = req.headers['x-device-id'] || req.query.device_id
  const timestamp = req.headers['x-device-timestamp']
  const signature = req.headers['x-device-signature']

  if (!deviceId) {
    return res.status(401).json({ error: 'Missing X-Device-ID' })
  }

  const SECRET = process.env.DEVICE_SECRET

  // 仅在显式配置了 DEVICE_SECRET 环境变量时，才强制进行防重放的 HMAC 签名校验
  // 若未配置该变量，则视为调试/无硬件模式，直接放行未签名的设备请求，方便开发测试
  if (SECRET) {
    if (!timestamp || !signature) {
      return res.status(403).json({ error: 'Missing signature or timestamp headers (DEVICE_SECRET protection is active)' })
    }

    // 允许 120 秒的时钟漂移差值，防止重放攻击
    const diff = Math.abs(Date.now() - Number(timestamp))
    if (diff > 120 * 1000) {
      return res.status(403).json({ error: 'Request expired (anti-replay check failed)' })
    }

    // 计算签名 (deviceId + timestamp + requestBody)
    // 针对 /voice 路由，由于是纯二进制流，设备端是用空字符串计算的签名
    // 而此时中间件由于 req.body 还未经过 express.raw 解析，默认为 {}，所以需要特判处理
    let bodyStr = ''
    if (req.method === 'POST') {
      if (req.path === '/voice') {
        bodyStr = ''
      } else {
        bodyStr = JSON.stringify(req.body)
      }
    }
    const data = `${deviceId}${timestamp}${bodyStr}`
    const expectedSignature = crypto.createHmac('sha256', SECRET).update(data).digest('hex')

    if (signature !== expectedSignature) {
      return res.status(403).json({ error: 'Signature mismatch' })
    }
  } else {
    // 调试模式下，若收到带有签名的请求，打出友好提示
    if (signature) {
      console.warn(`⚠️ 调试提示: 收到设备 ${deviceId} 的签名请求，但后端未配置 DEVICE_SECRET 环境变量，已跳过校验直接放行。`)
    }
  }

  // 挂载设备信息
  req.deviceId = deviceId
  next()
}


// ================= 自动审批懒加载机制 =================

// 自动确认已过期的任务申报申请（在查询状态或语音申报前静默执行）
async function autoConfirmLazyLoad(userId) {
  try {
    const now = Date.now()
    // 1. 读取该教师的自动审批配置
    const modeRow = await getAsync("SELECT value FROM settings WHERE key = 'task_confirm_mode'")
    const taskConfirmMode = modeRow ? JSON.parse(modeRow.value) : 'auto'

    if (taskConfirmMode !== 'auto') return

    // 2. 查询该用户所属班级中所有已超时但仍处于 pending 状态的任务申报
    const expiredApplications = await allAsync(`
      SELECT ta.*, s.class_id, s.name as student_name
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE c.user_id = ? AND ta.status = 'pending' AND ta.auto_confirm_at <= ?
    `, userId, now)

    if (expiredApplications.length === 0) return

    console.log(`⏱️ 自动审核懒加载: 检测到有 ${expiredApplications.length} 个过期申请，开始自动核销...`)

    for (const task of expiredApplications) {
      // a. 将状态置为 approved
      await runAsync('UPDATE student_task_applications SET status = ? WHERE id = ?', 'approved', task.id)

      // b. 创建评价记录
      const evalId = uuidv4()
      await runAsync(
        'INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)',
        evalId,
        task.class_id,
        task.student_id,
        task.points,
        `自动核销: ${task.task_name}`,
        '学习',
        now
      )

      // c. 累加学生积分并重新核算宠物等级
      await runAsync('UPDATE students SET total_points = total_points + ? WHERE id = ?', task.points, task.student_id)
      const student = await getAsync('SELECT * FROM students WHERE id = ?', task.student_id)

      if (student && student.pet_type) {
        const newExp = Math.max(0, student.total_points)
        const newLevel = calculateLevel(newExp)

        if (newLevel === 8 && student.pet_level < 8) {
          const badgeId = uuidv4()
          await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?, ?, ?, ?)', badgeId, task.student_id, student.pet_type, now)
        }
        await runAsync('UPDATE students SET pet_exp = ?, pet_level = ? WHERE id = ?', newExp, newLevel, task.student_id)
      }
    }
    console.log(`⏱️ 自动审核懒加载: 核销完成。`)
  } catch (error) {
    console.error('❌ 自动审核懒加载异常:', error)
  }
}


// ================= 1. 获取设备绑定状态 API =================

// 设备请求获取绑定学生及其宠物的数据 (需要防重放中间件)
router.get('/status', deviceAuthMiddleware, async (req, res) => {
  const deviceId = req.deviceId

  try {
    // 根据设备 ID 寻找对应的学生及其绑定的班级
    const student = await getAsync(`
      SELECT s.*, c.name as class_name, c.user_id
      FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE UPPER(s.device_id) = UPPER(?)
    `, deviceId)

    if (!student) {
      return res.json({ 
        status: 'unbound', 
        message: '该硬件设备尚未与任何学生绑定，请在教师管理后台输入该设备 ID 绑定学生。',
        device_id: deviceId
      })
    }

    // 触发自动加分的懒加载核销
    await autoConfirmLazyLoad(student.user_id)

    // 重新获取最新的学生数据
    const latestStudent = await getAsync('SELECT * FROM students WHERE id = ?', student.id)
    const levelInfo = getLevelProgress(latestStudent.pet_exp)

    res.json({
      status: 'ok',
      student: {
        id: latestStudent.id,
        name: latestStudent.name,
        class_name: student.class_name,
        total_points: latestStudent.total_points,
        pet_type: latestStudent.pet_type,
        pet_level: latestStudent.pet_level,
        pet_exp: latestStudent.pet_exp,
        exp_progress: levelInfo.current,
        exp_required: levelInfo.required,
        is_max_level: levelInfo.isMaxLevel
      }
    })
  } catch (error) {
    console.error('设备状态拉取失败:', error)
    res.status(500).json({ error: '服务器内部错误' })
  }
})

// 等级经验计算辅助函数 (与 src/data/pets.ts 计算逻辑一致)
function getLevelProgress(exp) {
  const thresholds = [40, 60, 80, 100, 120, 140, 160]
  let currentExp = Math.max(0, exp)
  let level = 1
  let accum = 0

  for (let i = 0; i < thresholds.length; i++) {
    if (currentExp >= thresholds[i]) {
      currentExp -= thresholds[i]
      level++
      accum += thresholds[i]
    } else {
      break
    }
  }

  const isMaxLevel = level >= 8
  const required = isMaxLevel ? 0 : thresholds[level - 1]
  const current = isMaxLevel ? 0 : currentExp

  return {
    level,
    current,
    required,
    isMaxLevel
  }
}



// ================= 3. 语音 ASR 与大模型 NLP 意图分类 API =================

import express from 'express'

// 通用语音接收与智能核销路由
router.post('/voice', deviceAuthMiddleware, express.raw({ type: 'audio/wav', limit: '10mb' }), upload.single('audio'), async (req, res) => {
  let { text } = req.body || {}
  const deviceId = req.deviceId
  
  let audioBuffer = null
  if (req.file) {
    audioBuffer = req.file.buffer
  } else if (Buffer.isBuffer(req.body)) {
    audioBuffer = req.body
  }

  try {
    // 限频检查：5秒防刷
    const now = Date.now()
    const lastTime = lastVoiceRequestTimes.get(deviceId) || 0
    if (now - lastTime < 5000) {
      return res.json({
        action: 'none',
        text: text || '语音',
        reply_text: '歇一会儿再聊哦，不要太频繁啦。'
      })
    }
    lastVoiceRequestTimes.set(deviceId, now)

    // 如果上传了音频文件，执行 ASR
    if (audioBuffer) {
      try {
        const apiKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
        const apiKey = apiKeyRow ? JSON.parse(apiKeyRow.value) : ''
        if (!apiKey) {
          throw new Error('未配置 ASR API Key')
        }
        
        const form = new FormData()
        form.append('file', audioBuffer, { filename: 'record.wav', contentType: 'audio/wav' })
        form.append('model', 'whisper-1')

        const response = await fetch('https://api.openai.com/v1/audio/transcriptions', {
          method: 'POST',
          headers: {
            'Authorization': `Bearer ${apiKey}`,
            ...form.getHeaders()
          },
          body: form
        })

        if (!response.ok) {
          const err = await response.text()
          throw new Error(err)
        }
        const data = await response.json()
        text = data.text
        console.log(`🎙️ [ASR] 设备 ${deviceId} 语音识别结果: ${text}`)
      } catch (asrErr) {
        console.error('ASR 识别失败:', asrErr.message)
        return res.json({
          action: 'none',
          text: '听不清楚',
          reply_text: '抱歉，刚才没听清，请再说一遍。'
        })
      }
    }

    // 获取绑定设备的学生
    const student = await getAsync(`
      SELECT s.*, c.user_id
      FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE UPPER(s.device_id) = UPPER(?)
    `, deviceId)

    if (!student) {
      unboundDevices.set(deviceId, now)
      return res.status(403).json({ error: '设备尚未绑定学生，无法执行操作', status: 'unbound' })
    } else {
      unboundDevices.delete(deviceId)
    }

    if (!text) {
      return res.status(400).json({ error: '未提供音频文件或文本' })
    }

    let nlpResult = null
    // 优先调用大模型，失败后自动退化为内置正则提取器
    try {
      nlpResult = await askLLMIntent(text)
    } catch (err) {
      console.warn('⚠️ 大模型 API 调用失败，自动降级为内置正则分类器:', err.message)
      nlpResult = regexClassifyIntent(text)
    }

    // 重新触发自动审核核销
    await autoConfirmLazyLoad(student.user_id)

    // 根据分类意图执行不同的云端逻辑
    let responseData = {
      action: nlpResult.action,
      text: text,
      reply_text: nlpResult.reply_text
    }

    if (nlpResult.action === 'apply_task') {
      const taskName = nlpResult.task_name || '日常表现优秀'
      
      // 检索该班级的评价规则确定分值
      const rule = await getAsync(
        'SELECT points FROM evaluation_rules WHERE name LIKE ? LIMIT 1',
        `%${taskName}%`
      )
      const points = rule ? rule.points : 1 // 未检索到匹配分值则默认加 1 分

      // 生成任务申报记录存库
      const taskId = uuidv4()

      // 从 settings 读取当前延时时长
      const delayRow = await getAsync("SELECT value FROM settings WHERE key = 'task_confirm_delay'")
      const delayMinutes = delayRow ? JSON.parse(delayRow.value) : 30
      const autoConfirmAt = now + delayMinutes * 60 * 1000

      await runAsync(`
        INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at)
        VALUES (?, ?, ?, ?, 'pending', ?, ?)
      `, taskId, student.id, taskName, points, autoConfirmAt, now)

      responseData.reply_text = nlpResult.reply_text || `已为您申报了“${taskName}”，价值${points}分。请等待老师审核确认。`
      responseData.task_id = taskId
      responseData.points = points
    } else if (nlpResult.action === 'query_status') {
      const latestStudent = await getAsync('SELECT * FROM students WHERE id = ?', student.id)
      responseData.reply_text = nlpResult.reply_text || `你当前的积分总计 ${latestStudent.total_points} 分，宠物当前处于等级 ${latestStudent.pet_level}。加油！`
    } else if (nlpResult.action === 'create_schedule') {
      const info = nlpResult.schedule_info
      if (info && Array.isArray(info.days) && info.time && info.task_desc) {
        for (const day of info.days) {
          const scheduleId = uuidv4()
          await runAsync(
            'INSERT INTO schedules (id, student_id, day_of_week, time_str, task_desc, is_active, created_at) VALUES (?, ?, ?, ?, ?, 1, ?)',
            scheduleId,
            student.id,
            day,
            info.time,
            info.task_desc,
            now
          )
        }
        responseData.reply_text = nlpResult.reply_text || `已为您安排在周${info.days.join('、')}的 ${info.time} 提醒：${info.task_desc}。`
      } else {
        responseData.reply_text = '抱歉，我未能听清您要设定的日程时间和任务，请重试。'
      }
    }

    // 自动将本次有效交互记入 chat_logs 数据库
    const logId = uuidv4()
    await runAsync(
      'INSERT INTO chat_logs (id, student_id, user_message, ai_response, timestamp) VALUES (?, ?, ?, ?, ?)',
      logId,
      student.id,
      text,
      responseData.reply_text,
      now
    )

    // 异步自动清理 30 天前的聊天审计记录 (30 天 = 30 * 24 * 3600 * 1000 = 2592000000 毫秒)
    const thirtyDaysAgo = now - 2592000000
    runAsync('DELETE FROM chat_logs WHERE timestamp < ?', thirtyDaysAgo).catch(err => {
      console.error('自动清理历史对话记录失败:', err)
    })

    res.json(responseData)
  } catch (error) {
    console.error('语音路由执行失败:', error)
    res.status(500).json({ error: '语音处理失败' })
  }
})

// 正则表达式降级意图提取引擎
function regexClassifyIntent(text) {
  const t = text.trim()

  // 1. 音乐搜索判定 (已停用)
  if (/(听|播放|唱|来一首|放一首|点歌|搜歌|音乐)/.test(t)) {
    return {
      action: 'none',
      reply_text: '抱歉，当前系统的音乐播放功能已关闭。'
    }
  }

  // 2. 状态查询判定
  if (/(状态|几分|等级|多少级|经验|多少分|徽章|宠物)/.test(t)) {
    return {
      action: 'query_status',
      reply_text: '正在为您检索您和宠物的最新积分数据。'
    }
  }

  // 3. 默认判定为加分任务申报
  // 提取申报任务名，如“我完成了认真打扫卫生” -> “认真打扫卫生”
  const taskName = t.replace(/(我完成了|帮我申请|我要申请|帮我申报|申报|申请|完成了|做完了|我做了)/g, '').trim() || '日常任务'
  return {
    action: 'apply_task',
    task_name: taskName,
    reply_text: `已提交申报任务：${taskName}`
  }
}

// 真实大模型 API 交互
async function askLLMIntent(text) {
  const keyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
  const modelRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_model'")

  let apiKey = keyRow ? JSON.parse(keyRow.value) : ''
  let model = modelRow ? JSON.parse(modelRow.value) : 'openrouter/free'

  if (!apiKey) {
    apiKey = process.env.OPENROUTER_API_KEY || ''
  }

  if (!apiKey) {
    throw new Error('OpenRouter API Key not provided in settings or env')
  }

  const systemPrompt = `你是一个智能班级助手意图分类器。你需要将用户的语音输入分类，并以标准的 JSON 格式输出，不要包含任何 markdown 标记（如 \`\`\`json）或多余解释。
JSON 输出格式：
{
  "action": "apply_task" | "query_status" | "create_schedule" | "none",
  "task_name": "申报任务名称，如'认真打扫卫生'（仅在 action 为 apply_task 时需要，去除'我完成了'等废话前缀）",
  "schedule_info": {
    "days": [1, 2, 3, 4, 5, 6, 7], // 仅在 action 为 create_schedule 时需要。用数字数组表示周几，1为周一，7为周日，如果是每天则为 [1,2,3,4,5,6,7]
    "time": "HH:MM", // 仅在 action 为 create_schedule 时需要。24小时制，如 '08:30'，如果语音没提具体时间，默认为当前时间 '08:00'
    "task_desc": "日程提醒的内容，如 '背英语单词'"
  },
  "reply_text": "给学生的口语化鼓励或确认回复，字数控制在 25 字以内（适合设备语音播放，如：已为您安排好了日程）"
}

意图分类规则：
1. 申请加分/申报完成了某件事，如“我扫地了”、“完成作业了”，action 为 apply_task。
2. 查询宠物等级/几分/经验，如“我多少分了”、“我几级了”，action 为 query_status。
3. 设定日程提醒，如“周三下午两点半提醒我背单词”、“每天早上八点叫我起床”，action 为 create_schedule。
4. 其它无法识别或无意义的内容，action 为 none，reply_text 可作简短闲聊回复。`;

  try {
    const response = await fetch('https://openrouter.ai/api/v1/chat/completions', {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${apiKey}`,
        'Content-Type': 'application/json',
        'HTTP-Referer': 'https://github.com/class-pet-garden',
        'X-Title': 'ClassPetGarden'
      },
      body: JSON.stringify({
        model: model,
        messages: [
          { role: 'system', content: systemPrompt },
          { role: 'user', content: text }
        ],
        temperature: 0.1
      })
    })

    if (!response.ok) {
      const errorText = await response.text()
      throw new Error(`OpenRouter API response error: ${response.status} - ${errorText}`)
    }

    const data = await response.json()
    const content = data.choices?.[0]?.message?.content?.trim()
    if (!content) {
      throw new Error('Empty response from OpenRouter')
    }

    let cleanJson = content
    if (cleanJson.startsWith('```')) {
      cleanJson = cleanJson.replace(/^```json\s*/i, '').replace(/```$/, '').trim()
    }

    const parsed = JSON.parse(cleanJson)
    return parsed
  } catch (err) {
    console.error('askLLMIntent 异常:', err)
    throw err
  }
}


// ================= 4. 高可用语音合成 TTS API =================

// 通用语音合成中继服务
router.get('/tts', async (req, res) => {
  const { text } = req.query

  if (!text) {
    return res.status(400).json({ error: 'Missing text parameter' })
  }

  try {
    // 免费 Edge-TTS 微软在线朗读中继接口，如果失效可在这里捕获并 Fallback 降级到其它音源
    // 这里使用公共的 edge-tts 服务或返回文字提示设备进行本地离线降级
    const ttsUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=zh-CN&client=tw-ob&q=${encodeURIComponent(text)}`
    
    // 返回带合成地址的 JSON，由通用开发板直接发起拉取播放，避免了单次请求超过 10s 的超时风险
    res.json({
      text,
      audio_url: ttsUrl
    })
  } catch (error) {
    console.error('语音合成失败:', error)
    res.status(500).json({ error: 'TTS 合成服务不可用' })
  }
})

// 通用固件 OTA 版本检查 API (支持防重放校验)
router.get('/firmware-version', deviceAuthMiddleware, async (req, res) => {
  try {
    const versionRow = await getAsync("SELECT value FROM settings WHERE key = 'firmware_latest_version'")
    const urlRow = await getAsync("SELECT value FROM settings WHERE key = 'firmware_download_url'")
    const checksumRow = await getAsync("SELECT value FROM settings WHERE key = 'firmware_checksum'")

    const latestVersion = versionRow ? JSON.parse(versionRow.value) : '2.0.0'
    let downloadUrl = urlRow ? JSON.parse(urlRow.value) : '/firmware/latest.bin'
    const checksum = checksumRow ? JSON.parse(checksumRow.value) : 'dummy_checksum_sha256'

    if (downloadUrl.startsWith('/')) {
      downloadUrl = `${req.protocol}://${req.get('host')}${downloadUrl}`
    }

    res.json({
      latest_version: latestVersion,
      download_url: downloadUrl,
      checksum: checksum
    })
  } catch (error) {
    console.error('获取固件版本失败:', error)
    res.status(500).json({ error: '获取固件版本接口异常' })
  }
})

// 1. 教师获取某个学生的全部日程
router.get('/schedules/student/:studentId', authMiddleware, async (req, res) => {
  try {
    const schedules = await allAsync(
      'SELECT * FROM schedules WHERE student_id = ? ORDER BY day_of_week ASC, time_str ASC',
      req.params.studentId
    )
    res.json({ success: true, schedules })
  } catch (error) {
    console.error('获取日程失败:', error)
    res.status(500).json({ error: '获取日程失败' })
  }
})

// 2. 教师为某个学生添加日程
router.post('/schedules/student/:studentId', authMiddleware, async (req, res) => {
  const { day_of_week, time_str, task_desc } = req.body
  if (!day_of_week || !time_str || !task_desc) {
    return res.status(400).json({ error: '参数缺失' })
  }
  try {
    const scheduleId = uuidv4()
    await runAsync(
      'INSERT INTO schedules (id, student_id, day_of_week, time_str, task_desc, is_active, created_at) VALUES (?, ?, ?, ?, ?, 1, ?)',
      scheduleId,
      req.params.studentId,
      day_of_week,
      time_str,
      task_desc,
      Date.now()
    )
    res.json({ success: true, id: scheduleId })
  } catch (error) {
    console.error('添加日程失败:', error)
    res.status(500).json({ error: '添加日程失败' })
  }
})

// 3. 教师删除单条日程
router.delete('/schedules/:id', authMiddleware, async (req, res) => {
  try {
    await runAsync('DELETE FROM schedules WHERE id = ?', req.params.id)
    res.json({ success: true })
  } catch (error) {
    console.error('删除日程失败:', error)
    res.status(500).json({ error: '删除日程失败' })
  }
})

// 4. 教师获取某个学生的聊天历史纪录
router.get('/chat-logs/student/:studentId', authMiddleware, async (req, res) => {
  try {
    const logs = await allAsync(
      'SELECT * FROM chat_logs WHERE student_id = ? ORDER BY timestamp DESC LIMIT 200',
      req.params.studentId
    )
    res.json({ success: true, logs })
  } catch (error) {
    console.error('获取对话记录失败:', error)
    res.status(500).json({ error: '获取对话记录失败' })
  }
})

// 5. 设备端获取自身绑定的学生的定时日程表
router.get('/schedules', deviceAuthMiddleware, async (req, res) => {
  try {
    const student = await getAsync('SELECT id FROM students WHERE UPPER(device_id) = UPPER(?)', req.deviceId)
    if (!student) {
      unboundDevices.set(req.deviceId, Date.now())
      return res.json({ status: 'unbound', schedules: [] })
    } else {
      unboundDevices.delete(req.deviceId)
    }
    // 自动更新 last_seen 心跳时间
    await runAsync('UPDATE students SET last_seen = ? WHERE id = ?', Date.now(), student.id)

    const schedules = await allAsync(
      'SELECT id, day_of_week, time_str, task_desc, is_active FROM schedules WHERE student_id = ? AND is_active = 1',
      student.id
    )
    res.json({ status: 'ok', schedules })
  } catch (error) {
    console.error('设备获取日程表失败:', error)
    res.status(500).json({ error: '服务器内部错误' })
  }
})

// 6. 教师获取系统设置
router.get('/settings', authMiddleware, async (req, res) => {
  try {
    const modeRow = await getAsync("SELECT value FROM settings WHERE key = 'task_confirm_mode'")
    const delayRow = await getAsync("SELECT value FROM settings WHERE key = 'task_confirm_delay'")
    const keyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
    const modelRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_model'")
    const brightnessRow = await getAsync("SELECT value FROM settings WHERE key = 'screen_brightness'")
    const sleepSecsRow = await getAsync("SELECT value FROM settings WHERE key = 'screen_sleep_seconds'")

    res.json({
      task_confirm_mode: modeRow ? JSON.parse(modeRow.value) : 'auto',
      task_confirm_delay: delayRow ? JSON.parse(delayRow.value) : 30,
      openrouter_api_key: keyRow ? JSON.parse(keyRow.value) : '',
      openrouter_model: modelRow ? JSON.parse(modelRow.value) : 'openrouter/free',
      screen_brightness: brightnessRow ? JSON.parse(brightnessRow.value) : 80,
      screen_sleep_seconds: sleepSecsRow ? JSON.parse(sleepSecsRow.value) : 15
    })
  } catch (error) {
    console.error('获取系统设置失败:', error)
    res.status(500).json({ error: '获取系统设置失败' })
  }
})

// 7. 教师保存系统设置
router.post('/settings', authMiddleware, async (req, res) => {
  const { task_confirm_mode, task_confirm_delay, openrouter_api_key, openrouter_model, screen_brightness, screen_sleep_seconds } = req.body
  try {
    if (task_confirm_mode !== undefined) {
      const existing = await getAsync("SELECT key FROM settings WHERE key = 'task_confirm_mode'")
      if (existing) {
        await runAsync("UPDATE settings SET value = ? WHERE key = 'task_confirm_mode'", JSON.stringify(task_confirm_mode))
      } else {
        await runAsync("INSERT INTO settings (key, value) VALUES ('task_confirm_mode', ?)", JSON.stringify(task_confirm_mode))
      }
    }
    if (task_confirm_delay !== undefined) {
      const existing = await getAsync("SELECT key FROM settings WHERE key = 'task_confirm_delay'")
      if (existing) {
        await runAsync("UPDATE settings SET value = ? WHERE key = 'task_confirm_delay'", JSON.stringify(task_confirm_delay))
      } else {
        await runAsync("INSERT INTO settings (key, value) VALUES ('task_confirm_delay', ?)", JSON.stringify(task_confirm_delay))
      }
    }
    if (openrouter_api_key !== undefined) {
      const existing = await getAsync("SELECT key FROM settings WHERE key = 'openrouter_api_key'")
      if (existing) {
        await runAsync("UPDATE settings SET value = ? WHERE key = 'openrouter_api_key'", JSON.stringify(openrouter_api_key))
      } else {
        await runAsync("INSERT INTO settings (key, value) VALUES ('openrouter_api_key', ?)", JSON.stringify(openrouter_api_key))
      }
    }
    if (openrouter_model !== undefined) {
      const existing = await getAsync("SELECT key FROM settings WHERE key = 'openrouter_model'")
      if (existing) {
        await runAsync("UPDATE settings SET value = ? WHERE key = 'openrouter_model'", JSON.stringify(openrouter_model))
      } else {
        await runAsync("INSERT INTO settings (key, value) VALUES ('openrouter_model', ?)", JSON.stringify(openrouter_model))
      }
    }
    if (screen_brightness !== undefined) {
      const existing = await getAsync("SELECT key FROM settings WHERE key = 'screen_brightness'")
      if (existing) {
        await runAsync("UPDATE settings SET value = ? WHERE key = 'screen_brightness'", JSON.stringify(Number(screen_brightness)))
      } else {
        await runAsync("INSERT INTO settings (key, value) VALUES ('screen_brightness', ?)", JSON.stringify(Number(screen_brightness)))
      }
    }
    if (screen_sleep_seconds !== undefined) {
      const existing = await getAsync("SELECT key FROM settings WHERE key = 'screen_sleep_seconds'")
      if (existing) {
        await runAsync("UPDATE settings SET value = ? WHERE key = 'screen_sleep_seconds'", JSON.stringify(Number(screen_sleep_seconds)))
      } else {
        await runAsync("INSERT INTO settings (key, value) VALUES ('screen_sleep_seconds', ?)", JSON.stringify(Number(screen_sleep_seconds)))
      }
    }
    res.json({ success: true })
  } catch (error) {
    console.error('更新系统设置失败:', error)
    res.status(500).json({ error: '保存系统设置失败' })
  }
})

// 8. 设备端心跳与电量状态上报接口
router.post('/heartbeat', deviceAuthMiddleware, async (req, res) => {
  const { battery_level, is_charging } = req.body
  const deviceId = req.deviceId
  try {
    const student = await getAsync('SELECT id FROM students WHERE UPPER(device_id) = UPPER(?)', deviceId)
    if (!student) {
      unboundDevices.set(deviceId, Date.now())
      return res.json({ status: 'unbound', error: '未找到绑定该设备的学生' })
    } else {
      unboundDevices.delete(deviceId)
    }
    
    // 更新电量、充电状态和心跳活跃时间
    await runAsync(`
      UPDATE students 
      SET battery_level = ?, is_charging = ?, last_seen = ? 
      WHERE id = ?
    `, battery_level ?? 100, is_charging ? 1 : 0, Date.now(), student.id)
    
    res.json({ success: true, timestamp: Date.now() })
  } catch (error) {
    console.error('设备心跳上报失败:', error)
    res.status(500).json({ error: '服务器内部错误' })
  }
})

// 9. 获取最近 5 分钟内活跃的未绑定设备列表 (教师权限)
router.get('/unbound-devices', authMiddleware, async (req, res) => {
  const now = Date.now()
  const list = []
  
  for (const [devId, timestamp] of unboundDevices.entries()) {
    if (now - timestamp > 300000) {
      unboundDevices.delete(devId)
    } else {
      list.push({ deviceId: devId, lastSeen: timestamp })
    }
  }
  
  res.json({ success: true, devices: list })
})

// 10. 获取待审核任务申报列表 (教师权限)
router.get('/tasks', authMiddleware, async (req, res) => {
  try {
    const status = req.query.status || ''
    const tasks = await allAsync(`
      SELECT ta.*, s.name as student_name, c.name as class_name
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE c.user_id = ? AND (? = '' OR ta.status = ?)
      ORDER BY ta.created_at DESC
    `, req.userId, status, status)
    res.json({ success: true, tasks })
  } catch (error) {
    console.error('获取申报任务列表失败:', error)
    res.status(500).json({ error: '获取申报任务列表失败' })
  }
})

// 11. 审核同意任务申报 (教师权限)
router.post('/tasks/:id/approve', authMiddleware, async (req, res) => {
  try {
    const task = await getAsync(`
      SELECT ta.*, s.class_id, s.name as student_name
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE ta.id = ? AND c.user_id = ?
    `, req.params.id, req.userId)

    if (!task) {
      return res.status(404).json({ error: '任务申报不存在或无权审核' })
    }

    if (task.status !== 'pending') {
      return res.status(400).json({ error: '该申请已被审核处理' })
    }

    const now = Date.now()
    // a. 更新申请状态为已通过
    await runAsync("UPDATE student_task_applications SET status = 'approved' WHERE id = ?", req.params.id)

    // b. 创建对应的评分记录
    const evalId = uuidv4()
    await runAsync(
      'INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)',
      evalId,
      task.class_id,
      task.student_id,
      task.points,
      `加分申请核销: ${task.task_name}`,
      '学习',
      now
    )

    // c. 累加学生积分并更新宠物等级
    await runAsync('UPDATE students SET total_points = total_points + ? WHERE id = ?', task.points, task.student_id)
    const student = await getAsync('SELECT * FROM students WHERE id = ?', task.student_id)

    if (student && student.pet_type) {
      const newExp = Math.max(0, student.total_points)
      const newLevel = calculateLevel(newExp)

      if (newLevel === 8 && student.pet_level < 8) {
        const badgeId = uuidv4()
        await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?, ?, ?, ?)', badgeId, task.student_id, student.pet_type, now)
      }
      await runAsync('UPDATE students SET pet_exp = ?, pet_level = ? WHERE id = ?', newExp, newLevel, task.student_id)
    }

    res.json({ success: true })
  } catch (error) {
    console.error('审核任务失败:', error)
    res.status(500).json({ error: '审核任务失败' })
  }
})

// 12. 审核拒绝任务申报 (教师权限)
router.post('/tasks/:id/reject', authMiddleware, async (req, res) => {
  try {
    const task = await getAsync(`
      SELECT ta.*
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE ta.id = ? AND c.user_id = ?
    `, req.params.id, req.userId)

    if (!task) {
      return res.status(404).json({ error: '任务申报不存在或无权审核' })
    }

    if (task.status !== 'pending') {
      return res.status(400).json({ error: '该申请已被审核处理' })
    }

    await runAsync("UPDATE student_task_applications SET status = 'rejected' WHERE id = ?", req.params.id)
    res.json({ success: true })
  } catch (error) {
    console.error('拒绝任务失败:', error)
    res.status(500).json({ error: '拒绝任务失败' })
  }
})

export default router
export { deviceAuthMiddleware }
