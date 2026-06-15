import crypto from 'crypto'

// 获取旧的盐值（支持从环境变量配置，或使用原有的硬编码盐值进行老用户密码校验）
const OLD_SALT = process.env.SALT || 'pet-garden-secret-salt-2024'

/**
 * 使用 scrypt 加密密码
 * 返回格式：salt:hash (salt是16字节hex，hash是64字节hex)
 */
export function hashPassword(password) {
  const salt = crypto.randomBytes(16).toString('hex')
  const hash = crypto.scryptSync(password, salt, 64).toString('hex')
  return `${salt}:${hash}`
}

/**
 * 校验密码，支持新版 scrypt 和旧版 HMAC-SHA256
 */
export function verifyPassword(password, storedHash) {
  if (!storedHash) return false

  // 如果哈希值中不带 ':'，说明是旧版哈希（HMAC-SHA256）
  if (!storedHash.includes(':')) {
    const oldHash = crypto.createHmac('sha256', OLD_SALT).update(password).digest('hex')
    return oldHash === storedHash
  }

  // 新版 scrypt 校验
  try {
    const [salt, hash] = storedHash.split(':')
    if (!salt || !hash) return false
    const checkHash = crypto.scryptSync(password, salt, 64).toString('hex')
    return checkHash === hash
  } catch (error) {
    console.error('密码校验异常:', error)
    return false
  }
}

