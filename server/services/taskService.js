// 自动审批懒加载机制
// 在查询状态或语音申报前静默执行，自动核销已过期的任务申报

import { v4 as uuidv4 } from 'uuid'
import { getAsync, allAsync, runAsync } from '../db.js'
import { calculateLevel } from '../utils/level.js'
import { getSetting } from '../utils/settings.js'

/**
 * 自动确认已过期的任务申报申请
 * @param {string} userId - 教师/用户 ID
 */
export async function autoConfirmLazyLoad(userId) {
  try {
    const now = Date.now()
    const taskConfirmMode = await getSetting('task_confirm_mode', 'auto')

    if (taskConfirmMode !== 'auto') return

    // 查询该用户所属班级中所有已超时但仍处于 pending 状态的任务申报
    const expiredApplications = await allAsync(
      `
      SELECT ta.*, s.class_id, s.name as student_name
      FROM student_task_applications ta
      JOIN students s ON ta.student_id = s.id
      JOIN classes c ON s.class_id = c.id
      WHERE c.user_id = ? AND ta.status = 'pending' AND ta.auto_confirm_at <= ?
    `,
      userId,
      now
    )

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
    }
    console.log(`⏱️ 自动审核懒加载: 核销完成。`)
  } catch (error) {
    console.error('❌ 自动审核懒加载异常:', error)
  }
}
