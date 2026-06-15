import { Router } from 'express'
import { getAsync, allAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()

// 导出备份
router.get('/', authMiddleware, async (req, res) => {
  try {
    const backup = {
      version: '1.0.0',
      exportedAt: new Date().toISOString(),
      classes: await allAsync('SELECT * FROM classes WHERE user_id = ?', req.userId),
      students: await allAsync(`
        SELECT s.* FROM students s
        JOIN classes c ON s.class_id = c.id
        WHERE c.user_id = ?
      `, req.userId),
      rules: await allAsync('SELECT * FROM evaluation_rules'),
      records: await allAsync(`
        SELECT er.* FROM evaluation_records er
        JOIN classes c ON er.class_id = c.id
        WHERE c.user_id = ?
      `, req.userId),
      badges: await allAsync(`
        SELECT b.* FROM badges b
        JOIN students s ON b.student_id = s.id
        JOIN classes c ON s.class_id = c.id
        WHERE c.user_id = ?
      `, req.userId),
      settings: await allAsync('SELECT * FROM settings')
    }
    res.setHeader('Content-Disposition', `attachment; filename="pet-garden-backup-${Date.now()}.json"`)
    res.json(backup)
  } catch (error) {
    res.status(500).json({ error: '导出备份失败' })
  }
})

// 导入备份
router.post('/', authMiddleware, async (req, res) => {
  const { classes, students, rules, records, badges } = req.body

  if (!classes || !students) {
    return res.status(400).json({ error: 'Invalid backup data' })
  }

  try {
    // Clear existing data for current user
    await runAsync('DELETE FROM evaluation_records WHERE class_id IN (SELECT id FROM classes WHERE user_id = ?)', req.userId)
    await runAsync('DELETE FROM badges WHERE student_id IN (SELECT s.id FROM students s JOIN classes c ON s.class_id = c.id WHERE c.user_id = ?)', req.userId)
    await runAsync('DELETE FROM students WHERE class_id IN (SELECT id FROM classes WHERE user_id = ?)', req.userId)
    await runAsync('DELETE FROM classes WHERE user_id = ?', req.userId)

    // Restore classes (关联当前用户)
    for (const c of classes) {
      await runAsync('INSERT INTO classes (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)', c.id, req.userId, c.name, c.created_at, c.updated_at)
    }

    // Restore students (兼容旧备份的 device_id，将其默认为 null)
    for (const s of students) {
      await runAsync('INSERT INTO students (id, class_id, name, student_no, device_id, total_points, pet_type, pet_level, pet_exp, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)', s.id, s.class_id, s.name, s.student_no, s.device_id || null, s.total_points, s.pet_type, s.pet_level, s.pet_exp, s.created_at)
    }

    // Restore custom rules
    if (rules) {
      for (const r of rules.filter(r => r.is_custom)) {
        await runAsync('INSERT OR IGNORE INTO evaluation_rules (id, name, points, category, is_custom, created_at) VALUES (?, ?, ?, ?, ?, ?)', r.id, r.name, r.points, r.category, r.is_custom, r.created_at)
      }
    }

    // Restore records
    if (records) {
      for (const r of records) {
        await runAsync('INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)', r.id, r.class_id, r.student_id, r.points, r.reason, r.category, r.timestamp)
      }
    }

    // Restore badges
    if (badges) {
      for (const b of badges) {
        await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?, ?, ?, ?)', b.id, b.student_id, b.pet_type, b.earned_at)
      }
    }

    res.json({ success: true, message: '数据恢复成功' })
  } catch (error) {
    console.error('Restore error:', error)
    res.status(500).json({ error: '恢复失败' })
  }
})

export default router

