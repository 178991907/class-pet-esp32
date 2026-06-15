import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import { allAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()

// 获取规则列表（公开接口，无需认证）
router.get('/', async (req, res) => {
  try {
    const rules = await allAsync('SELECT * FROM evaluation_rules ORDER BY category, points DESC')
    res.json({ rules })
  } catch (error) {
    res.status(500).json({ error: '获取规则列表失败' })
  }
})

// 添加自定义规则（需要认证）
router.post('/', authMiddleware, async (req, res) => {
  const { name, points, category } = req.body
  const id = uuidv4()
  const now = Date.now()
  try {
    await runAsync('INSERT INTO evaluation_rules (id, name, points, category, is_custom, created_at) VALUES (?, ?, ?, ?, 1, ?)', id, name, points, category, now)
    res.json({ id, name, points, category, is_custom: 1, created_at: now })
  } catch (error) {
    res.status(500).json({ error: '添加规则失败' })
  }
})

// 删除自定义规则（需要认证）
router.delete('/:id', authMiddleware, async (req, res) => {
  try {
    await runAsync('DELETE FROM evaluation_rules WHERE id = ? AND is_custom = 1', req.params.id)
    res.json({ success: true })
  } catch (error) {
    res.status(500).json({ error: '删除规则失败' })
  }
})

export default router

