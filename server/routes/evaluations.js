import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import { getAsync, allAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'
import { calculateLevel } from '../utils/level.js'
import { publishEvent } from '../services/eventBus.js'

const router = Router()

// 添加评价
router.post('/', authMiddleware, async (req, res) => {
  const { classId, studentId, points, reason, category } = req.body

  try {
    // 验证班级归属
    const cls = await getAsync('SELECT * FROM classes WHERE id = ?', classId)
    if (!cls || cls.user_id !== req.userId) {
      return res.status(403).json({ error: '无权访问此班级' })
    }

    const id = uuidv4()
    const now = Date.now()

    // Insert record
    await runAsync('INSERT INTO evaluation_records (id, class_id, student_id, points, reason, category, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)', id, classId, studentId, points, reason, category, now)

    // Update student points
    await runAsync('UPDATE students SET total_points = total_points + ? WHERE id = ?', points, studentId)

    // Get student info (after update)
    const student = await getAsync('SELECT * FROM students WHERE id = ?', studentId)

    // Update pet exp if student has a pet
    if (student && student.pet_type) {
      // pet_exp should always equal total_points (with minimum 0)
      const newExp = Math.max(0, student.total_points)

      // Calculate new level based on exp
      const newLevel = calculateLevel(newExp)

      // Check if pet graduated (reached level 8)
      let graduated = false
      if (newLevel === 8 && student.pet_level < 8) {
        const badgeId = uuidv4()
        await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?, ?, ?, ?)', badgeId, studentId, student.pet_type, now)
        graduated = true
      }

      await runAsync('UPDATE students SET pet_exp = ?, pet_level = ? WHERE id = ?', newExp, newLevel, studentId)

      const result = {
        id,
        timestamp: now,
        petLevel: newLevel,
        petExp: newExp,
        levelUp: newLevel > student.pet_level,
        levelDown: newLevel < student.pet_level,
        graduated
      }
      // P3: 实时广播评价与升级事件
      try {
        publishEvent('evaluation', {
          studentId,
          studentName: student?.name,
          points, reason, category,
          petLevel: newLevel,
          levelUp: result.levelUp,
          graduated
        })
        if (result.levelUp) {
          publishEvent('levelup', { studentId, studentName: student?.name, petLevel: newLevel })
        }
      } catch { /* 事件广播失败不影响主流程 */ }
      return res.json(result)
    }

    try {
      publishEvent('evaluation', {
        studentId,
        studentName: student?.name,
        points, reason, category
      })
    } catch { /* ignore */ }
    res.json({ id, timestamp: now })
  } catch (error) {
    res.status(500).json({ error: '添加评价失败' })
  }
})

// 获取评价记录列表
router.get('/', authMiddleware, async (req, res) => {
  const { classId, studentId, page = 1, pageSize = 20 } = req.query
  const offset = (Number(page) - 1) * Number(pageSize)

  let countQuery = 'SELECT COUNT(*) as total FROM evaluation_records er JOIN classes c ON er.class_id = c.id'
  let query = 'SELECT er.*, s.name as student_name FROM evaluation_records er JOIN students s ON er.student_id = s.id JOIN classes c ON er.class_id = c.id'
  const params = []
  const countParams = []

  // 始终添加用户过滤
  params.push(req.userId)
  countParams.push(req.userId)

  // 支持按班级或学生筛选
  const conditions = ['c.user_id = ?']
  if (classId) {
    conditions.push('er.class_id = ?')
    params.push(classId)
    countParams.push(classId)
  }
  if (studentId) {
    conditions.push('er.student_id = ?')
    params.push(studentId)
    countParams.push(studentId)
  }

  query += ' WHERE ' + conditions.join(' AND ')
  countQuery += ' WHERE ' + conditions.join(' AND ')

  try {
    // Get total count
    const totalResult = await getAsync(countQuery, ...countParams)
    const total = totalResult?.total || 0

    // Get paginated records
    query += ' ORDER BY er.timestamp DESC LIMIT ? OFFSET ?'
    params.push(Number(pageSize), offset)

    const records = await allAsync(query, ...params)
    res.json({
      records,
      total,
      page: Number(page),
      pageSize: Number(pageSize),
      totalPages: Math.ceil(total / Number(pageSize))
    })
  } catch (error) {
    res.status(500).json({ error: '获取评价记录失败' })
  }
})

// 撤回最新评价
router.delete('/latest', authMiddleware, async (req, res) => {
  const { classId } = req.query
  if (!classId) {
    return res.status(400).json({ error: 'classId required' })
  }

  try {
    // 验证班级归属
    const cls = await getAsync('SELECT * FROM classes WHERE id = ?', classId)
    if (!cls || cls.user_id !== req.userId) {
      return res.status(403).json({ error: '无权访问此班级' })
    }

    // Get latest record
    const record = await getAsync('SELECT * FROM evaluation_records WHERE class_id = ? ORDER BY timestamp DESC LIMIT 1', classId)
    if (!record) {
      return res.status(404).json({ error: 'No record found' })
    }

    // Get student current data
    const student = await getAsync('SELECT * FROM students WHERE id = ?', record.student_id)

    // Calculate new exp
    const expChange = Math.abs(record.points)
    const newExp = Math.max(0, student.pet_exp - expChange)

    // Recalculate level based on new exp
    const newLevel = calculateLevel(newExp)

    // Undo points, exp and update level
    await runAsync('UPDATE students SET total_points = total_points - ?, pet_exp = ?, pet_level = ? WHERE id = ?', record.points, newExp, newLevel, record.student_id)

    // Delete record
    await runAsync('DELETE FROM evaluation_records WHERE id = ?', record.id)

    res.json({ success: true, undone: record })
  } catch (error) {
    res.status(500).json({ error: '撤回评价失败' })
  }
})

// 删除指定评价记录
router.delete('/:id', authMiddleware, async (req, res) => {
  const { id } = req.params

  try {
    // Get record
    const record = await getAsync(`
      SELECT er.* FROM evaluation_records er
      JOIN classes c ON er.class_id = c.id
      WHERE er.id = ? AND c.user_id = ?
    `, id, req.userId)

    if (!record) {
      return res.status(404).json({ error: 'Record not found' })
    }

    // Get student current data
    const student = await getAsync('SELECT * FROM students WHERE id = ?', record.student_id)

    // Calculate new exp
    const expChange = Math.abs(record.points)
    const newExp = Math.max(0, student.pet_exp - expChange)

    // Recalculate level based on new exp
    const newLevel = calculateLevel(newExp)

    // Undo points, exp and update level
    await runAsync('UPDATE students SET total_points = total_points - ?, pet_exp = ?, pet_level = ? WHERE id = ?', record.points, newExp, newLevel, record.student_id)

    // Delete record
    await runAsync('DELETE FROM evaluation_records WHERE id = ?', id)

    res.json({ success: true, undone: record })
  } catch (error) {
    res.status(500).json({ error: '删除评价失败' })
  }
})

export default router

