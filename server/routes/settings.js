import { Router } from 'express'
import { getAsync, allAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()

// 获取设置（公开接口）
router.get('/', async (req, res) => {
  try {
    const settings = await allAsync('SELECT * FROM settings')
    const result = {}
    for (const s of settings) {
      result[s.key] = JSON.parse(s.value)
    }
    res.json(result)
  } catch (error) {
    res.status(500).json({ error: '获取系统设置失败' })
  }
})

// 修复经验值（将 pet_exp 与 total_points 同步）
router.post('/fix-exp', authMiddleware, async (req, res) => {
  try {
    const result = await runAsync(`
      UPDATE students SET pet_exp = MAX(0, total_points)
      WHERE pet_type IS NOT NULL
      AND class_id IN (SELECT id FROM classes WHERE user_id = ?)
    `, req.userId)
    res.json({ success: true, updated: result.changes })
  } catch (error) {
    res.status(500).json({ error: '修复经验值失败' })
  }
})

// 获取排行榜
router.get('/ranking/:classId', authMiddleware, async (req, res) => {
  try {
    // 验证班级归属
    const cls = await getAsync('SELECT * FROM classes WHERE id = ?', req.params.classId)
    if (!cls || cls.user_id !== req.userId) {
      return res.status(403).json({ error: '无权访问此班级' })
    }

    const ranking = await allAsync(`
      SELECT s.*,
             (SELECT COUNT(*) FROM badges WHERE student_id = s.id) as badge_count
      FROM students s
      WHERE s.class_id = ?
      ORDER BY s.total_points DESC, s.pet_level DESC
    `, req.params.classId)
    res.json({ ranking })
  } catch (error) {
    res.status(500).json({ error: '获取排行榜失败' })
  }
})

export default router

