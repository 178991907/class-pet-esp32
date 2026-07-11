// 班级宠物园 · Web 端宠物语音/文字聊天端点 (P3 常驻麦克风按钮)
// 设备端语音走 /ws/voice（voiceWs.js），此处是网页管理端「宠物语音伙伴」的对话入口。
// 为降低对外部 LLM 的依赖与延迟，默认返回可爱宠物语气回复；
// 若 studentId + award=true 则顺带记一笔 +1 互动积分，并广播 voice_session 事件。
import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import { getAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'
import { publishEvent } from '../services/eventBus.js'

const router = Router()

// 生成宠物口吻的回复（无需外部 API，确保 Web 语音伙伴始终可用）
function petReply(message, studentName) {
  const name = studentName || '小朋友'
  const m = (message || '').trim()
  // 简单关键词友好回应
  if (/你好|您好|hi|hello|在吗/i.test(m)) {
    return `你好呀${name}！我是你的宠物小搭档，今天过得开心吗？😊`
  }
  if (/谢谢|感谢|多谢/i.test(m)) {
    return `不客气${name}～能陪你聊天我超开心的！🐾`
  }
  if (/陪我|无聊|孤单|不开心|难过|伤心/i.test(m)) {
    return `别难过${name}，我一直在你身边哦！我们一起聊聊天、给宠物加点成长值吧～💕`
  }
  if (/作业|学习|考试|背书|默写/i.test(m)) {
    return `学习辛苦啦${name}！认真完成任务的话，宠物会悄悄涨成长值的，加油加油！📚✨`
  }
  if (/\?|？|吗|怎么|为什么|什么/i.test(m)) {
    return `这个问题真有意思${name}！让我想想……我觉得你可以先试试看，遇到难题随时来找我呀～🤔`
  }
  if (m.length === 0) {
    return `嗯？我好像没听清${name}，再跟我说说好不好～👂`
  }
  return `我听到啦${name}：「${m.slice(0, 30)}」～你说得真棒，我记在心里啦！要不要给宠物加点成长值？🌟`
}

// Web 端发送一条对话
router.post('/', authMiddleware, async (req, res) => {
  const { message, studentId, studentName, award } = req.body || {}
  const reply = petReply(message, studentName)

  let awarded = null
  try {
    if (studentId && award) {
      // 互动奖励：记一笔 +1 分（其他类），并触发实时刷新事件
      const cls = await getAsync('SELECT class_id FROM students WHERE id = ?', studentId)
      if (cls) {
        const id = uuidv4()
        const now = Date.now()
        await runAsync(
          'INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)',
          id, cls.class_id, studentId, 1, '语音互动奖励', '其他', now
        )
        await runAsync('UPDATE students SET total_points = total_points + 1 WHERE id = ?', studentId)
        awarded = { id, studentId, points: 1 }
      }
    }
  } catch (err) {
    console.warn('⚠️ [chat] 互动奖励写入失败:', err.message)
  }

  // 广播实时事件，让网页管理端「实时动态」亮起来
  try {
    publishEvent('voice_session', {
      studentId: studentId || null,
      studentName: studentName || null,
      reply,
      awarded: !!awarded
    })
  } catch (err) {
    console.warn('⚠️ [chat] 事件广播失败:', err.message)
  }

  res.json({ success: true, reply, awarded })
})

export default router
