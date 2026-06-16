import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import crypto from 'crypto'
import vm from 'vm'
import { getAsync, allAsync, runAsync } from '../db.js'
import { calculateLevel } from '../utils/level.js'

// 全局拦截异步未捕获的 Rejection 与 Exception，防止在 Vercel Serverless 环境下由于沙箱 Promise 失败导致 502 崩溃进程
process.on('unhandledRejection', (reason, promise) => {
  console.error('⚠️ [系统防崩卫士] 拦截到未捕获的 Promise Rejection:', reason)
})
process.on('uncaughtException', (err) => {
  console.error('⚠️ [系统防崩卫士] 拦截到未捕获的 Exception:', err.message || err)
})

const router = Router()

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
    const requestBody = req.method === 'POST' ? JSON.stringify(req.body) : ''
    const data = `${deviceId}${timestamp}${requestBody}`
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
      WHERE s.device_id = ?
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


// ================= 2. 隔离安全沙箱与多音源 Failover =================

// 带有超时保护的安全 fetch 封装，防止在 Serverless 平台由于网络挂起导致云函数被平台强杀
async function fetchWithTimeout(url, options = {}, timeout = 1500) {
  const controller = new AbortController()
  const id = setTimeout(() => controller.abort(), timeout)
  try {
    const response = await fetch(url, {
      ...options,
      signal: controller.signal
    })
    clearTimeout(id)
    return response
  } catch (err) {
    clearTimeout(id)
    throw err
  }
}

// 纯原生内置安全沙箱执行器 (防 native addons 编译及加载风险，完美兼容 Vercel)
async function safeSandboxExecute(code, contextVars = {}) {
  // 注入带 1.5 秒超时限制的网络请求及处理工具
  const context = vm.createContext({
    console,
    fetch: (url, opts) => fetchWithTimeout(url, opts, 1500), // 限制沙箱内部网络请求最多 1.5 秒
    URLSearchParams: typeof URLSearchParams !== 'undefined' ? URLSearchParams : undefined,
    Buffer: typeof Buffer !== 'undefined' ? Buffer : undefined,
    process: {
      env: {
        NODE_ENV: process.env.NODE_ENV
      }
    },
    ...contextVars
  })
  const script = new vm.Script(code)
  // 5000ms 执行超时限制
  const result = script.runInContext(context, { timeout: 5000 })

  // 如果沙箱内返回的是一个 Promise，我们在外部进行 await 等待其决议
  if (result && typeof result.then === 'function') {
    return await result
  }
  return result
}



// ================= 音源管理 CRUD API（Web 管理后台使用） =================

// 获取所有音源列表
router.get('/music-sources', async (req, res) => {
  try {
    const sources = await allAsync('SELECT * FROM music_sources ORDER BY priority DESC, created_at DESC')
    res.json({ sources })
  } catch (error) {
    console.error('获取音源列表失败:', error)
    res.status(500).json({ error: '获取音源列表失败', details: error.message })
  }
})

// 新增音源
router.post('/music-sources', async (req, res) => {
  const { id, name, script_code, priority } = req.body
  if (!name || !script_code) {
    return res.status(400).json({ error: '缺少音源名称或脚本代码' })
  }
  try {
    const sourceId = id || `src-${Date.now()}-${Math.random().toString(36).substr(2, 6)}`
    await runAsync(
      'INSERT INTO music_sources (id, name, script_code, priority, is_enabled, failure_count, created_at) VALUES (?, ?, ?, ?, 1, 0, ?)',
      sourceId, name, script_code, priority || 0, Date.now()
    )
    res.status(201).json({ success: true, id: sourceId })
  } catch (error) {
    console.error('新增音源失败:', error)
    res.status(500).json({ error: '新增音源失败', details: error.message })
  }
})

// 删除音源
router.delete('/music-sources/:id', async (req, res) => {
  try {
    await runAsync('DELETE FROM music_sources WHERE id = ?', req.params.id)
    res.json({ success: true })
  } catch (error) {
    console.error('删除音源失败:', error)
    res.status(500).json({ error: '删除音源失败' })
  }
})

// 重置音源熔断计数
router.post('/music-sources/:id/reset', async (req, res) => {
  try {
    await runAsync('UPDATE music_sources SET failure_count = 0, last_failure_at = NULL WHERE id = ?', req.params.id)
    res.json({ success: true })
  } catch (error) {
    console.error('重置音源失败:', error)
    res.status(500).json({ error: '重置音源失败' })
  }
})



// 通用设备音乐搜索 API (仅执行自定义音源，具有降级和熔断机制)
router.get('/music/search', deviceAuthMiddleware, async (req, res) => {
  const { keyword, source_id } = req.query

  if (!keyword || !keyword.trim()) {
    return res.status(400).json({ error: '缺少搜索关键字 keyword' })
  }

  try {
    let sources = []
    if (source_id) {
      // 1. 用户显式指定了音源：只查询该特定的启用且未熔断的音源
      sources = await allAsync(
        'SELECT * FROM music_sources WHERE id = ? AND is_enabled = 1 AND failure_count < 3',
        source_id
      )
    } else {
      // 2. 硬件设备盲搜：获取全部启用且未熔断的音源按优先级降序
      sources = await allAsync(`
        SELECT * FROM music_sources
        WHERE is_enabled = 1 AND failure_count < 3
        ORDER BY priority DESC, created_at DESC
      `)
    }

    if (sources.length === 0) {
      return res.status(502).json({ error: '数据库中当前无可用的自定义音源配置或均已熔断' })
    }

    let playUrl = null
    let usedSourceId = null

    for (const src of sources) {
      try {
        console.log(`🎵 沙箱执行自定义源 [${src.name}] 搜索: "${keyword}"`)
        const result = await safeSandboxExecute(src.script_code, { keyword: keyword.trim() })

        if (result && typeof result === 'string' && result.startsWith('http')) {
          playUrl = result
          usedSourceId = src.id
          break // 成功获取，跳出重试环
        } else {
          throw new Error('未返回有效的 MP3 直链')
        }
      } catch (err) {
        console.error(`❌ 自定义音源 [${src.name}] 执行异常:`, err.message)
        // 失败计数 +1 并在达到阈值时记录失败时间进行自动熔断
        await runAsync(
          'UPDATE music_sources SET failure_count = failure_count + 1, last_failure_at = ? WHERE id = ?',
          Date.now(),
          src.id
        )
      }
    }

    if (!playUrl) {
      return res.status(502).json({ error: '所有启用的自定义音乐源解析均已失效或熔断' })
    }

    // 成功解析后，重置该源的失败计数
    if (usedSourceId) {
      await runAsync('UPDATE music_sources SET failure_count = 0, last_failure_at = NULL WHERE id = ?', usedSourceId)
    }

    res.json({
      keyword,
      url: playUrl
    })
  } catch (error) {
    console.error('音乐检索流程异常:', error)
    res.status(500).json({ error: '音乐搜索接口异常', details: error.message })
  }
})


// ================= 3. 语音 ASR 与大模型 NLP 意图分类 API =================

// 通用语音接收与智能核销路由
router.post('/voice', deviceAuthMiddleware, async (req, res) => {
  let { text } = req.body // 支持直接传递文字 text 调试降级

  try {
    // 获取绑定设备的学生
    const student = await getAsync(`
      SELECT s.*, c.user_id
      FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE s.device_id = ?
    `, req.deviceId)

    if (!student) {
      return res.status(403).json({ error: '设备尚未绑定学生，无法执行操作' })
    }

    // ASR 语音合成在未配置 ASR Key 时的测试降级：必须包含文本
    if (!text) {
      // 在此可接入 Whisper 或腾讯/阿里 ASR 音频流转文字，无 Key 时直接拦截并提示
      return res.status(400).json({ error: 'ASR 未配置，请直接使用 text 属性进行调试输入。' })
    }

    // 规则匹配降级分类引擎 (未配置大模型 API Key 时的备选方案)
    const OPENROUTER_KEY = process.env.OPENROUTER_API_KEY
    let nlpResult = null

    if (OPENROUTER_KEY) {
      // 接入 OpenRouter 大模型分类意图
      try {
        nlpResult = await askLLMIntent(text)
      } catch (err) {
        console.warn('⚠️ 大模型 API 调用失败，自动降级为内置正则分类器:', err.message)
      }
    }

    if (!nlpResult) {
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
      const now = Date.now()

      // 从 settings 读取当前延时时长
      const delayRow = await getAsync("SELECT value FROM settings WHERE key = 'task_confirm_delay'")
      const delayMinutes = delayRow ? JSON.parse(delayRow.value) : 30
      const autoConfirmAt = now + delayMinutes * 60 * 1000

      await runAsync(`
        INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at)
        VALUES (?, ?, ?, ?, 'pending', ?, ?)
      `, taskId, student.id, taskName, points, autoConfirmAt, now)

      responseData.reply_text = `已为您申报了“${taskName}”，价值${points}分。请等待老师审核确认。`
      responseData.task_id = taskId
      responseData.points = points
    } else if (nlpResult.action === 'query_status') {
      const latestStudent = await getAsync('SELECT * FROM students WHERE id = ?', student.id)
      responseData.reply_text = `你当前的积分总计 ${latestStudent.total_points} 分，宠物当前处于等级 ${latestStudent.pet_level}。加油！`
    } else if (nlpResult.action === 'search_music') {
      responseData.music_keyword = nlpResult.music_keyword || '童话'
      responseData.reply_text = `正在为您寻找音乐《${responseData.music_keyword}》，请稍候...`
    }

    res.json(responseData)
  } catch (error) {
    console.error('语音路由执行失败:', error)
    res.status(500).json({ error: '语音处理失败' })
  }
})

// 正则表达式降级意图提取引擎
function regexClassifyIntent(text) {
  const t = text.trim()

  // 1. 音乐搜索判定
  if (/(听|播放|唱|来一首|放一首|点歌|搜歌|音乐)/.test(t)) {
    const musicKeyword = t.replace(/(请|帮我|我要|听|播放|唱|来一首|放一首|点歌|搜歌|音乐|的歌)/g, '').trim() || '童声音乐'
    return {
      action: 'search_music',
      music_keyword: musicKeyword,
      reply_text: `正在为您搜索音乐：${musicKeyword}`
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
  // 此处可以使用 axios 调用 OpenRouter 接口将文本翻译并返回标准 JSON
  // 由于没有配置真实 API，这里直接抛错以触发降级正则
  throw new Error('LLM API Key not provided')
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

export default router
export { deviceAuthMiddleware }
