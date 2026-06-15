import crypto from 'crypto'

// 优先读取环境变量，不存在则使用开发期默认密钥，并打印警告
const TOKEN_SECRET = process.env.TOKEN_SECRET || 'pet-garden-token-secret'
if (!process.env.TOKEN_SECRET) {
  console.warn('⚠️ 警告: TOKEN_SECRET 环境变量未配置，当前正在使用开发期默认密钥。请在生产部署时对其进行安全配置。')
}


export function generateToken(userId) {
  const timestamp = Date.now()
  const data = `${userId}:${timestamp}`
  const signature = crypto.createHmac('sha256', TOKEN_SECRET).update(data).digest('hex')
  return `${userId}:${timestamp}:${signature}`
}

export function verifyToken(token) {
  if (!token) return null

  const parts = token.split(':')
  if (parts.length !== 3) return null

  const [userId, timestamp, signature] = parts
  const data = `${userId}:${timestamp}`
  const expectedSignature = crypto.createHmac('sha256', TOKEN_SECRET).update(data).digest('hex')

  if (signature !== expectedSignature) return null

  // Token 有效期 30 天
  const tokenAge = Date.now() - parseInt(timestamp)
  if (tokenAge > 30 * 24 * 60 * 60 * 1000) return null

  return { userId }
}
