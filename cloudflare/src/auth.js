// 鉴权助手 —— 用 Web Crypto 重写 server/utils/password.js 与 token.js
// 因为 Cloudflare Workers 没有 Node 的 crypto 模块，且 D1 是全新库，只需内部自洽即可。

let SECRET = 'classpet-garden-default-secret'
let PEPPER = 'classpet-garden-v1'

export function setSecrets(env) {
  if (env && env.SECRET) SECRET = env.SECRET
  if (env && env.SALT) PEPPER = env.SALT
}

function bufToHex(buf) {
  return [...new Uint8Array(buf)].map((x) => x.toString(16).padStart(2, '0')).join('')
}

export async function hashPassword(password) {
  const data = new TextEncoder().encode(PEPPER + ':' + password)
  const digest = await crypto.subtle.digest('SHA-256', data)
  return bufToHex(digest)
}

export async function verifyPassword(password, hash) {
  const h = await hashPassword(password)
  return h === hash
}

async function hmac(data, secret) {
  const key = await crypto.subtle.importKey(
    'raw',
    new TextEncoder().encode(secret),
    { name: 'HMAC', hash: 'SHA-256' },
    false,
    ['sign']
  )
  const sig = await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(data))
  return bufToHex(sig)
}

export async function generateToken(userId) {
  const exp = Date.now() + 1000 * 60 * 60 * 24 * 30 // 30 天
  const payload = `${userId}.${exp}`
  const sig = await hmac(payload, SECRET)
  return `${payload}.${sig}`
}

export async function verifyToken(token) {
  if (!token || typeof token !== 'string') return null
  const parts = token.split('.')
  if (parts.length !== 3) return null
  const [userId, exp, sig] = parts
  if (!userId || !exp || !sig) return null
  if (Date.now() > Number(exp)) return null
  const expected = await hmac(`${userId}.${exp}`, SECRET)
  if (expected !== sig) return null
  return { userId }
}

export function getUserIdFromAuth(request) {
  const auth = request.headers.get('Authorization') || ''
  const token = auth.startsWith('Bearer ') ? auth.slice(7) : ''
  return token
}
