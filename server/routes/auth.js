import { Router } from 'express'
import { v4 as uuidv4 } from 'uuid'
import { getAsync, runAsync, allAsync } from '../db.js'
import { hashPassword, verifyPassword } from '../utils/password.js'
import { generateToken } from '../utils/token.js'
import { authMiddleware } from '../middleware/auth.js'

const router = Router()

// 注册
router.post('/register', async (req, res) => {
  const { username, password } = req.body

  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' })
  }

  if (username.toLowerCase() === 'admin') {
    return res.status(400).json({ error: '该用户名已被系统保留，无法注册' })
  }

  if (username.length < 3 || username.length > 20) {
    return res.status(400).json({ error: '用户名长度3-20字符' })
  }

  if (password.length < 6) {
    return res.status(400).json({ error: '密码至少6位' })
  }

  try {
    // 检查用户名是否已存在
    const existingUser = await getAsync('SELECT id FROM users WHERE username = ?', username)
    if (existingUser) {
      return res.status(400).json({ error: '用户名已存在' })
    }

    // 读取数据库中的教师邀请码，默认为 TEACHER_INVITE
    const inviteSetting = await getAsync("SELECT value FROM settings WHERE key = 'teacher_invite_code'")
    const teacherInviteCode = inviteSetting ? JSON.parse(inviteSetting.value) : 'TEACHER_INVITE'

    // 根据邀请码判定分配角色，若匹配则为 teacher，否则为 student
    const inviteCodeInput = req.body.inviteCode || ''
    const assignedRole = inviteCodeInput === teacherInviteCode ? 'teacher' : 'student'

    const userId = uuidv4()
    const passwordHash = hashPassword(password)

    await runAsync('INSERT INTO users (id, username, password_hash, is_guest, role, created_at) VALUES (?, ?, ?, ?, ?, ?)', userId, username, passwordHash, 0, assignedRole, Date.now())

    const token = generateToken(userId)
    res.json({
      success: true,
      token,
      user: { id: userId, username, isGuest: false, role: assignedRole }
    })
  } catch (error) {
    res.status(500).json({ error: '注册失败' })
  }
})

// 登录
router.post('/login', async (req, res) => {
  const { username, password } = req.body

  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' })
  }

  try {
    const user = await getAsync('SELECT id, username, password_hash, is_guest, role FROM users WHERE username = ?', username)

    if (!user) {
      return res.status(401).json({ error: '用户名或密码错误' })
    }

    if (!verifyPassword(password, user.password_hash)) {
      return res.status(401).json({ error: '用户名或密码错误' })
    }

    const token = generateToken(user.id)
    res.json({
      success: true,
      token,
      user: { id: user.id, username: user.username, isGuest: !!user.is_guest, role: user.role || 'student' }
    })
  } catch (error) {
    res.status(500).json({ error: '登录失败' })
  }
})

// 获取当前用户信息
router.get('/me', authMiddleware, async (req, res) => {
  try {
    const user = await getAsync('SELECT id, username, is_guest, role FROM users WHERE id = ?', req.userId)
    if (!user) {
      return res.status(404).json({ error: '用户不存在' })
    }
    res.json({
      user: {
        id: user.id,
        username: user.username,
        isGuest: !!user.is_guest,
        role: user.role || 'student'
      }
    })
  } catch (error) {
    res.status(500).json({ error: '获取用户信息失败' })
  }
})

// 管理员权限验证中间件
async function adminAuthMiddleware(req, res, next) {
  try {
    const user = await getAsync("SELECT username FROM users WHERE id = ?", req.userId)
    if (user && user.username === 'admin') {
      next()
    } else {
      res.status(403).json({ error: '无权操作，仅限管理员使用' })
    }
  } catch (err) {
    res.status(500).json({ error: '权限校验失败' })
  }
}

// 获取所有注册用户列表（管理员专用）
router.get('/users', authMiddleware, adminAuthMiddleware, async (req, res) => {
  try {
    const users = await allAsync("SELECT id, username, role, created_at FROM users WHERE is_guest = 0 ORDER BY created_at DESC")
    res.json({ users })
  } catch (err) {
    res.status(500).json({ error: '获取用户列表失败' })
  }
})

// 修改指定用户的角色权限（管理员专用）
router.put('/users/:id/role', authMiddleware, adminAuthMiddleware, async (req, res) => {
  const { id } = req.params
  const { role } = req.body

  if (!['student', 'teacher', 'admin'].includes(role)) {
    return res.status(400).json({ error: '非法的角色类型' })
  }

  try {
    const user = await getAsync("SELECT username FROM users WHERE id = ?", id)
    if (!user) {
      return res.status(404).json({ error: '目标用户不存在' })
    }
    // 保护：不能修改 admin 的 role 
    if (user.username === 'admin' && role !== 'admin') {
      return res.status(400).json({ error: '系统保留管理员账号的角色不可修改' })
    }

    await runAsync("UPDATE users SET role = ? WHERE id = ?", role, id)
    res.json({ success: true })
  } catch (err) {
    res.status(500).json({ error: '更新用户角色失败' })
  }
})

// 修改管理员密码（管理员专用）
router.put('/admin/password', authMiddleware, adminAuthMiddleware, async (req, res) => {
  const { password } = req.body

  if (!password || password.length < 6) {
    return res.status(400).json({ error: '密码长度至少为 6 位' })
  }

  try {
    const passwordHash = hashPassword(password)
    await runAsync("UPDATE users SET password_hash = ? WHERE username = 'admin'", passwordHash)
    res.json({ success: true })
  } catch (err) {
    res.status(500).json({ error: '修改密码失败' })
  }
})

export default router

