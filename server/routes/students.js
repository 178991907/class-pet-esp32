import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import { getAsync, allAsync, runAsync } from '../db.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()

// 验证班级归属的中间件
async function verifyClassOwnership(req, res, next) {
  const classId = req.params.classId || req.body.classId
  if (!classId) {
    return res.status(400).json({ error: '缺少班级ID' })
  }
  try {
    const cls = await getAsync('SELECT * FROM classes WHERE id = ?', classId)
    if (!cls) {
      return res.status(404).json({ error: '班级不存在' })
    }
    if (cls.user_id !== req.userId) {
      return res.status(403).json({ error: '无权访问此班级' })
    }
    req.classId = classId
    next()
  } catch (error) {
    res.status(500).json({ error: '服务器错误' })
  }
}

// 获取班级学生列表
router.get('/classes/:classId/students', authMiddleware, verifyClassOwnership, async (req, res) => {
  try {
    const students = await allAsync('SELECT * FROM students WHERE class_id = ? ORDER BY name', req.params.classId)
    res.json({ students })
  } catch (error) {
    res.status(500).json({ error: '获取学生列表失败' })
  }
})

// 添加学生
router.post('/', authMiddleware, async (req, res) => {
  const { classId, name, studentNo, deviceId } = req.body

  try {
    // 验证班级归属
    const cls = await getAsync('SELECT * FROM classes WHERE id = ?', classId)
    if (!cls) {
      return res.status(404).json({ error: '班级不存在' })
    }
    if (cls.user_id !== req.userId) {
      return res.status(403).json({ error: '无权访问此班级' })
    }

    const id = uuidv4()
    const now = Date.now()
    await runAsync('INSERT INTO students (id, class_id, name, student_no, device_id, total_points, pet_level, pet_exp, created_at) VALUES (?, ?, ?, ?, ?, 0, 1, 0, ?)', id, classId, name, studentNo || null, deviceId || null, now)
    res.json({ id, class_id: classId, name, student_no: studentNo || null, device_id: deviceId || null, total_points: 0, pet_level: 1, pet_exp: 0, created_at: now })
  } catch (error) {
    res.status(500).json({ error: '添加学生失败: ' + error.message })
  }
})

// 更新学生
router.put('/:id', authMiddleware, async (req, res) => {
  const { name, studentNo, deviceId } = req.body

  try {
    // 验证学生归属
    const student = await getAsync(`
      SELECT s.* FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE s.id = ? AND c.user_id = ?
    `, req.params.id, req.userId)

    if (!student) {
      return res.status(404).json({ error: '学生不存在或无权访问' })
    }

    await runAsync('UPDATE students SET name = ?, student_no = ?, device_id = ? WHERE id = ?', name, studentNo || null, deviceId || null, req.params.id)
    res.json({ success: true })
  } catch (error) {
    res.status(500).json({ error: '更新学生失败' })
  }
})

// 删除学生
router.delete('/:id', authMiddleware, async (req, res) => {
  try {
    // 验证学生归属
    const student = await getAsync(`
      SELECT s.* FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE s.id = ? AND c.user_id = ?
    `, req.params.id, req.userId)

    if (!student) {
      return res.status(404).json({ error: '学生不存在或无权访问' })
    }

    // 先删除相关的评价记录
    await runAsync('DELETE FROM evaluation_records WHERE student_id = ?', req.params.id)
    // 再删除学生
    await runAsync('DELETE FROM students WHERE id = ?', req.params.id)
    res.json({ success: true })
  } catch (error) {
    res.status(500).json({ error: '删除学生失败' })
  }
})

// 批量导入学生
router.post('/import', authMiddleware, async (req, res) => {
  const { classId, students } = req.body
  if (!classId || !students || !Array.isArray(students)) {
    return res.status(400).json({ error: 'Invalid input' })
  }

  try {
    // 验证班级归属
    const cls = await getAsync('SELECT * FROM classes WHERE id = ?', classId)
    if (!cls) {
      return res.status(404).json({ error: '班级不存在' })
    }
    if (cls.user_id !== req.userId) {
      return res.status(403).json({ error: '无权访问此班级' })
    }

    const now = Date.now()
    let imported = 0
    for (const student of students) {
      if (student.name && student.name.trim()) {
        const id = uuidv4()
        await runAsync('INSERT INTO students (id, class_id, name, student_no, device_id, total_points, pet_level, pet_exp, created_at) VALUES (?, ?, ?, ?, NULL, 0, 1, 0, ?)', id, classId, student.name.trim(), student.studentNo?.trim() || null, now)
        imported++
      }
    }

    res.json({ success: true, imported })
  } catch (error) {
    res.status(500).json({ error: '导入学生失败' })
  }
})

// 更新学生宠物
router.put('/:id/pet', authMiddleware, async (req, res) => {
  const { petType } = req.body
  const now = Date.now()

  try {
    // 验证学生归属
    const student = await getAsync(`
      SELECT s.* FROM students s
      JOIN classes c ON s.class_id = c.id
      WHERE s.id = ? AND c.user_id = ?
    `, req.params.id, req.userId)

    if (!student) {
      return res.status(404).json({ error: '学生不存在或无权访问' })
    }

    // If student already has a pet, create a badge for it if level is 8
    if (student.pet_type && student.pet_level >= 8) {
      const badgeId = uuidv4()
      await runAsync('INSERT INTO badges (id, student_id, pet_type, earned_at) VALUES (?, ?, ?, ?)', badgeId, req.params.id, student.pet_type, now)
    }

    // Update pet type and reset level/exp
    await runAsync('UPDATE students SET pet_type = ?, pet_level = 1, pet_exp = 0 WHERE id = ?', petType, req.params.id)

    res.json({ success: true })
  } catch (error) {
    res.status(500).json({ error: '更新宠物失败' })
  }
})

export default router

