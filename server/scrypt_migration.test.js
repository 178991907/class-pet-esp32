import crypto from 'crypto'
import { hashPassword, verifyPassword } from './utils/password.js'

console.log('🧪 开始进行密码哈希迁移与兼容性单元测试...')

const password = 'mySecurePassword123'
const wrongPassword = 'wrongPassword'

// 1. 模拟旧版本 HMAC-SHA256 哈希值
const OLD_SALT = 'pet-garden-secret-salt-2024'
const oldHash = crypto.createHmac('sha256', OLD_SALT).update(password).digest('hex')

console.log('1. 旧版本 HMAC-SHA256 哈希值模拟完毕:', oldHash)

// 2. 测试使用新验证函数校验旧密码哈希
const verifyOldResult = verifyPassword(password, oldHash)
const verifyOldWrongResult = verifyPassword(wrongPassword, oldHash)

console.log(`- 校验旧哈希 + 正确密码: ${verifyOldResult ? '✅ 通过' : '❌ 失败'}`)
console.log(`- 校验旧哈希 + 错误密码: ${!verifyOldWrongResult ? '✅ 通过 (拦截成功)' : '❌ 失败 (未拦截)'}`)

if (!verifyOldResult || verifyOldWrongResult) {
  console.error('❌ 旧哈希兼容性校验失败！')
  process.exit(1)
}

// 3. 测试新版 scrypt 密码哈希生成与校验
const newHash = hashPassword(password)
console.log('3. 新版本 scrypt 哈希生成完毕:', newHash)

if (!newHash.includes(':')) {
  console.error('❌ 新哈希格式不正确，缺少随机盐前缀！')
  process.exit(1)
}

const verifyNewResult = verifyPassword(password, newHash)
const verifyNewWrongResult = verifyPassword(wrongPassword, newHash)

console.log(`- 校验新哈希 + 正确密码: ${verifyNewResult ? '✅ 通过' : '❌ 失败'}`)
console.log(`- 校验新哈希 + 错误密码: ${!verifyNewWrongResult ? '✅ 通过 (拦截成功)' : '❌ 失败 (未拦截)'}`)

if (!verifyNewResult || verifyNewWrongResult) {
  console.error('❌ 新哈希逻辑校验失败！')
  process.exit(1)
}

console.log('🎉 所有密码迁移与兼容性测试全部通过！')
process.exit(0)
