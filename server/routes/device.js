import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import multer from 'multer'
import express from 'express'

import { getAsync, allAsync, runAsync } from '../db.js'
import { calculateLevel } from '../utils/level.js'
import { authMiddleware } from '../middleware/auth.js'
import { deviceAuthMiddleware } from '../middleware/deviceAuth.js'
import { recognizeSpeech, isAsrConfigError } from '../services/asrService.js'
import { classifyIntent } from '../services/nlpService.js'
import { autoConfirmLazyLoad } from '../services/taskService.js'
import { getSetting, setSetting, getSettings, setSettings } from '../utils/settings.js'
import { getTtsAudioUrl, handleTtsStream } from '../services/ttsService.js'

const router = Router()
const upload = multer({ storage: multer.memoryStorage() })
const lastVoiceRequestTimes = new Map()
const unboundDevices = new Map()

// ================= 辅助函数 =================

/**
 * 等级经验计算 (与 src/data/pets.ts 计算逻辑一致)
 */
function getLevelProgress(exp) {
  const thresholds = [40, 60, 80, 100, 120, 140, 160]
  let currentExp = Math.max(0, exp)
  let level = 1

  for (let i = 0; i < thresholds.length; i++) {
    if (currentExp >= thresholds[i]) {
      currentExp -= thresholds[i]
      level++
    } else {
      break
    }
  }

  const isMaxLevel = level >= 8
  const required = isMaxLevel ? 0 : thresholds[level - 1]
  const current = isMaxLevel ? 0 : currentExp

  return { level, current, required, isMaxLevel }
}

// ================= 1. 设备状态 =================

router.get('/status', deviceAuthMiddleware, async (req, res) => {
  const deviceId = req.deviceId

  try {
    const student = await getAsync(
      `
      SELECT s.*, c.name as class_name, c.user_id
      FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE UPPER(s.device_id) = UPPER(?)
    `,
      deviceId
    )

    if (!student) {
      return res.json({
        status: 'unbound',
        message: '该硬件设备尚未与任何学生绑定，请在教师管理后台输入该设备 ID 绑定学生。',
        device_id: deviceId
      })
    }

    await autoConfirmLazyLoad(student.user_id)

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

// ================= 2. 语音 ASR + NLP 意图分类 =================

router.post(
  '/voice',
  deviceAuthMiddleware,
  express.raw({ type: '*/*', limit: '10mb' }),
  upload.single('audio'),
  async (req, res) => {
    let { text } = req.body || {}
    const deviceId = req.deviceId

    let audioBuffer = null
    if (req.file) {
      audioBuffer = req.file.buffer
    } else if (Buffer.isBuffer(req.body)) {
      audioBuffer = req.body
    }

    try {
      // 1. 验证设备是否绑定
      const student = await getAsync(
        `
        SELECT s.*, c.user_id
        FROM students s
        JOIN classes c ON s.class_id = c.id
        WHERE UPPER(s.device_id) = UPPER(?)
      `,
        deviceId
      )

      if (student) {
        unboundDevices.delete(deviceId)
      } else {
        return res.json({
          action: 'none',
          text: '未绑定',
          reply_text: '设备尚未绑定学生，请先在后台完成绑定。'
        })
      }

      // 2. 限频检查：5秒防刷
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

      // 3. 如果上传了音频文件，执行 ASR (调用服务层)
      if (audioBuffer) {
        try {
          text = await recognizeSpeech(audioBuffer, deviceId)
        } catch (asrErr) {
          console.error('ASR 识别失败:', asrErr.message)
          const configErr = isAsrConfigError(asrErr.message)
          return res.json({
            action: 'none',
            text: '听不清楚',
            reply_text: configErr
              ? '语音识别服务配置异常，请联系管理员检查后台 ASR 设置。'
              : '抱歉，刚才没听清，请再说一遍。',
            asr_error: asrErr.message
          })
        }
      }

      if (!text) {
        return res.status(400).json({ error: '未提供音频文件或文本' })
      }

      // 4. NLP 意图分类 (大模型优先，自动降级正则)
      const nlpResult = await classifyIntent(text)

      // 5. 重新触发自动审核核销
      await autoConfirmLazyLoad(student.user_id)

      // 6. 根据分类意图执行不同的云端逻辑
      let responseData = {
        action: nlpResult.action,
        text: text,
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
          `
          INSERT INTO student_task_applications (id, student_id, task_name, points, status, auto_confirm_at, created_at)
          VALUES (?, ?, ?, ?, 'pending', ?, ?)
        `,
          taskId,
          student.id,
          taskName,
          points,
          autoConfirmAt,
          now
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
              scheduleId,
              student.id,
              day,
              info.time,
              info.task_desc,
              now
            )
          }
          responseData.reply_text =
            nlpResult.reply_text || `已为您安排在周${info.days.join('、')}的 ${info.time} 提醒：${info.task_desc}。`
        } else {
          responseData.reply_text = '抱歉，我未能听清您要设定的日程时间和任务，请重试。'
        }
      }

      // 7. 记入 chat_logs
      const logId = uuidv4()
      await runAsync(
        'INSERT INTO chat_logs (id, student_id, user_message, ai_response, timestamp) VALUES (?, ?, ?, ?, ?)',
        logId,
        student.id,
        text,
        responseData.reply_text,
        now
      )

      // 异步清理 30 天前的聊天记录
      const thirtyDaysAgo = now - 2592000000
      runAsync('DELETE FROM chat_logs WHERE timestamp < ?', thirtyDaysAgo).catch(err => {
        console.error('自动清理历史对话记录失败:', err)
      })

      // === 新增：为回复生成 TTS 语音 URL ===
      const audioUrl = getTtsAudioUrl(responseData.reply_text, req)
      if (audioUrl) {
        responseData.audio_url = audioUrl
      }

      res.json(responseData)
    } catch (error) {
      console.error('语音路由执行失败:', error.stack || error)
      res.status(500).json({ error: '语音处理失败', details: error.message })
    }
  }
)

// ================= 3. 语音合成 TTS 中继 =================

router.get('/tts-stream', handleTtsStream)

// 兼容遗留的 /tts (若有旧代码调用)
router.get('/tts', async (req, res) => {
  const { text } = req.query

  if (!text) {
    return res.status(400).json({ error: 'Missing text parameter' })
  }

  try {
    const ttsUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=zh-CN&client=tw-ob&q=${encodeURIComponent(text)}`
    res.json({ text, audio_url: ttsUrl })
  } catch (error) {
    console.error('语音合成失败:', error)
    res.status(500).json({ error: 'TTS 合成服务不可用' })
  }
})

// ================= 4. 固件版本检查 =================

router.get('/firmware-version', deviceAuthMiddleware, async (req, res) => {
  try {
    const latestVersion = await getSetting('firmware_latest_version', '2.0.0')
    let downloadUrl = await getSetting('firmware_download_url', '/firmware/latest.bin')
    const checksum = await getSetting('firmware_checksum', 'dummy_checksum_sha256')

    if (downloadUrl.startsWith('/')) {
      downloadUrl = `${req.protocol}://${req.get('host')}${downloadUrl}`
    }

    res.json({ latest_version: latestVersion, download_url: downloadUrl, checksum })
  } catch (error) {
    console.error('获取固件版本失败:', error)
    res.status(500).json({ error: '获取固件版本接口异常' })
  }
})

// ================= 5. 设备心跳上报 =================

router.post('/heartbeat', deviceAuthMiddleware, async (req, res) => {
  const { battery_level, is_charging } = req.body
  const deviceId = req.deviceId
  try {
    const student = await getAsync('SELECT id FROM students WHERE UPPER(device_id) = UPPER(?)', deviceId)
    if (!student) {
      unboundDevices.set(deviceId, Date.now())
      return res.json({ status: 'unbound', error: '未找到绑定该设备的学生' })
    }
    unboundDevices.delete(deviceId)

    await runAsync(
      `UPDATE students SET battery_level = ?, is_charging = ?, last_seen = ? WHERE id = ?`,
      battery_level ?? 100,
      is_charging ? 1 : 0,
      Date.now(),
      student.id
    )

    res.json({ success: true, timestamp: Date.now() })
  } catch (error) {
    console.error('设备心跳上报失败:', error)
    res.status(500).json({ error: '服务器内部错误' })
  }
})

// ================= 6. 设备端获取日程表 =================

router.get('/schedules', deviceAuthMiddleware, async (req, res) => {
  try {
    const student = await getAsync('SELECT id FROM students WHERE UPPER(device_id) = UPPER(?)', req.deviceId)
    if (!student) {
      unboundDevices.set(req.deviceId, Date.now())
      return res.json({ status: 'unbound', schedules: [] })
    }
    unboundDevices.delete(req.deviceId)

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

// ================= 7. 教师端：系统设置 =================

// 允许的 settings key 和默认值
const SETTINGS_KEYS = [
  'task_confirm_mode',
  'task_confirm_delay',
  'openrouter_api_key',
  'openrouter_model',
  'groq_api_key',
  'screen_brightness',
  'screen_sleep_seconds',
  'asr_provider',
  'baidu_api_key',
  'baidu_secret_key'
]

const SETTINGS_DEFAULTS = {
  task_confirm_mode: 'auto',
  task_confirm_delay: 30,
  openrouter_api_key: '',
  openrouter_model: 'openrouter/free',
  groq_api_key: '',
  screen_brightness: 80,
  screen_sleep_seconds: 15,
  asr_provider: 'groq',
  baidu_api_key: '',
  baidu_secret_key: ''
}

// 需要转 Number 的 key
const NUMERIC_KEYS = ['task_confirm_delay', 'screen_brightness', 'screen_sleep_seconds']
const ALLOWED_ASR_PROVIDERS = ['openai', 'openrouter', 'baidu', 'groq']

router.get('/settings', authMiddleware, async (req, res) => {
  try {
    const settings = await getSettings(SETTINGS_KEYS, SETTINGS_DEFAULTS)
    res.json(settings)
  } catch (error) {
    console.error('获取系统设置失败:', error)
    res.status(500).json({ error: '获取系统设置失败' })
  }
})

router.post('/settings', authMiddleware, async (req, res) => {
  try {
    const { asr_provider } = req.body

    // 验证 ASR provider
    if (asr_provider !== undefined && !ALLOWED_ASR_PROVIDERS.includes(asr_provider)) {
      return res.status(400).json({
        error: `不支持的 ASR provider: ${asr_provider}, 允许: ${ALLOWED_ASR_PROVIDERS.join(', ')}`
      })
    }

    // 构建值转换函数
    const transforms = {}
    for (const key of NUMERIC_KEYS) {
      transforms[key] = v => Number(v)
    }

    await setSettings(req.body, SETTINGS_KEYS, transforms)
    res.json({ success: true })
  } catch (error) {
    console.error('更新系统设置失败:', error)
    res.status(500).json({ error: '保存系统设置失败' })
  }
})

// ================= 8. 教师端：未绑定设备列表 =================

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

// ================= 9. 教师端：任务申报管理 =================

router.get('/tasks', authMiddleware, async (req, res) => {
  try {
    const status = req.query.status || ''
    const tasks = await allAsync(
      `
      SELECT ta.*, s.name as student_name, c.name as class_name
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE c.user_id = ? AND (? = '' OR ta.status = ?)
      ORDER BY ta.created_at DESC
    `,
      req.userId,
      status,
      status
    )
    res.json({ success: true, tasks })
  } catch (error) {
    console.error('获取申报任务列表失败:', error)
    res.status(500).json({ error: '获取申报任务列表失败' })
  }
})

router.post('/tasks/:id/approve', authMiddleware, async (req, res) => {
  try {
    const task = await getAsync(
      `
      SELECT ta.*, s.class_id, s.name as student_name
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE ta.id = ? AND c.user_id = ?
    `,
      req.params.id,
      req.userId
    )

    if (!task) {
      return res.status(404).json({ error: '任务申报不存在或无权审核' })
    }

    if (task.status !== 'pending') {
      return res.status(400).json({ error: '该申请已被审核处理' })
    }

    const now = Date.now()
    await runAsync("UPDATE student_task_applications SET status = 'approved' WHERE id = ?", req.params.id)

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

    await runAsync('UPDATE students SET total_points = total_points + ? WHERE id = ?', task.points, task.student_id)
    const student = await getAsync('SELECT * FROM students WHERE id = ?', task.student_id)

    if (student && student.pet_type) {
      const newExp = Math.max(0, student.total_points)
      const newLevel = calculateLevel(newExp)

      if (newLevel === 8 && student.pet_level < 8) {
        const badgeId = uuidv4()
        await runAsync(
          'INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?, ?, ?, ?)',
          badgeId,
          task.student_id,
          student.pet_type,
          now
        )
      }
      await runAsync('UPDATE students SET pet_exp = ?, pet_level = ? WHERE id = ?', newExp, newLevel, task.student_id)
    }

    res.json({ success: true })
  } catch (error) {
    console.error('审核任务失败:', error)
    res.status(500).json({ error: '审核任务失败' })
  }
})

router.post('/tasks/:id/reject', authMiddleware, async (req, res) => {
  try {
    const task = await getAsync(
      `
      SELECT ta.*
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE ta.id = ? AND c.user_id = ?
    `,
      req.params.id,
      req.userId
    )

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

// ================= 10. 教师端：日程管理 =================

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

router.delete('/schedules/:id', authMiddleware, async (req, res) => {
  try {
    await runAsync('DELETE FROM schedules WHERE id = ?', req.params.id)
    res.json({ success: true })
  } catch (error) {
    console.error('删除日程失败:', error)
    res.status(500).json({ error: '删除日程失败' })
  }
})

// ================= 11. 教师端：聊天记录 =================

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

export default router
export { deviceAuthMiddleware }
