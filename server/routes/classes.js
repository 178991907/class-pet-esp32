import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import { getAsync, allAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()

// 获取班级列表（只返回当前用户的班级）
router.get('/', authMiddleware, async (req, res) => {
  try {
    const classes = await allAsync('SELECT * FROM classes WHERE user_id = ? ORDER BY created_at DESC', req.userId)
    res.json({ classes })
  } catch (error) {
    res.status(500).json({ error: '获取班级列表失败' })
  }
})

// 创建班级（关联当前用户）
router.post('/', authMiddleware, async (req, res) => {
  const { name } = req.body
  const id = uuidv4()
  const now = Date.now()

  try {
    await runAsync('INSERT INTO classes (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)', id, req.userId, name, now, now)
    res.json({ id, user_id: req.userId, name, created_at: now, updated_at: now })
  } catch (error) {
    res.status(500).json({ error: '创建班级失败' })
  }
})

// 更新班级
router.put('/:id', authMiddleware, async (req, res) => {
  const { name } = req.body
  try {
    const classInfo = await getAsync('SELECT user_id FROM classes WHERE id = ?', req.params.id)

    if (!classInfo) {
      return res.status(404).json({ error: '班级不存在' })
    }
    if (classInfo.user_id !== req.userId) {
      return res.status(403).json({ error: '无权修改' })
    }

    const now = Date.now()
    await runAsync('UPDATE classes SET name = ?, updated_at = ? WHERE id = ?', name, now, req.params.id)
    res.json({ success: true })
  } catch (error) {
    res.status(500).json({ error: '更新班级失败' })
  }
})

// 删除班级
router.delete('/:id', authMiddleware, async (req, res) => {
  try {
    const classInfo = await getAsync('SELECT user_id FROM classes WHERE id = ?', req.params.id)

    if (!classInfo) {
      return res.status(404).json({ error: '班级不存在' })
    }
    if (classInfo.user_id !== req.userId) {
      return res.status(403).json({ error: '无权删除' })
    }

    // Delete students in this class first
    await runAsync('DELETE FROM students WHERE class_id = ?', req.params.id)
    await runAsync('DELETE FROM classes WHERE id = ?', req.params.id)
    res.json({ success: true })
  } catch (error) {
    res.status(500).json({ error: '删除班级失败' })
  }
})

export default router

