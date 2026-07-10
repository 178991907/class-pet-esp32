// 设备通信签名与防重放中间件
// 从 device.js 中抽出，供多个路由文件复用

import crypto from 'crypto'

/**
 * 通用设备通信签名与防重放中间件
 * - 仅在配置了 DEVICE_SECRET 环境变量时强制 HMAC 签名校验
 * - 未配置时视为调试模式，直接放行
 */
export async function deviceAuthMiddleware(req, res, next) {
  const deviceId = req.headers['x-device-id'] || req.query.device_id
  const timestamp = req.headers['x-device-timestamp']
  const signature = req.headers['x-device-signature']

  if (!deviceId) {
    return res.status(401).json({ error: 'Missing X-Device-ID' })
  }

  const SECRET = process.env.DEVICE_SECRET

  if (SECRET) {
    if (!timestamp || !signature) {
      return res.status(403).json({
        error: 'Missing signature or timestamp headers (DEVICE_SECRET protection is active)'
      })
    }

    // 允许 120 秒的时钟漂移差值，防止重放攻击
    const diff = Math.abs(Date.now() - Number(timestamp))
    if (diff > 120 * 1000) {
      return res.status(403).json({ error: 'Request expired (anti-replay check failed)' })
    }

    // 计算签名 (deviceId + timestamp + requestBody)
    // 针对 /voice 路由，由于是纯二进制流，设备端是用空字符串计算的签名
    let bodyStr = ''
    if (req.method === 'POST') {
      if (req.path === '/voice') {
        bodyStr = ''
      } else {
        bodyStr = JSON.stringify(req.body)
      }
    }
    const data = `${deviceId}${timestamp}${bodyStr}`
    const expectedSignature = crypto.createHmac('sha256', SECRET).update(data).digest('hex')

    if (signature !== expectedSignature) {
      return res.status(403).json({ error: 'Signature mismatch' })
    }
  } else {
    // 调试模式下，若收到带有签名的请求，打出友好提示
    if (signature) {
      console.warn(`⚠️ 调试提示: 收到设备 ${deviceId} 的签名请求，但后端未配置 DEVICE_SECRET 环境变量，已跳过校验直接放行。`)
    }
  }

  // 挂载设备信息
  req.deviceId = deviceId
  next()
}
