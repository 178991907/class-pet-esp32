// TTS (文本转语音) 服务层
// 策略: Google Translate TTS (免费) > 百度语音合成 (需配置) > 有道翻译 (保底)
// Google TTS 免费、无需 API Key、中文音质好，是首选方案

import fetch from 'node-fetch'
import { getAsync } from '../db.js'

/**
 * 从数据库读取 Baidu TTS 相关配置
 */
async function getTtsConfig() {
  const baiduKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'baidu_api_key'")
  const baiduKey = baiduKeyRow ? JSON.parse(baiduKeyRow.value) : ''

  const baiduSecretRow = await getAsync("SELECT value FROM settings WHERE key = 'baidu_secret_key'")
  const baiduSecret = baiduSecretRow ? JSON.parse(baiduSecretRow.value) : ''

  return { baiduKey, baiduSecret }
}

/**
 * 组装出能让单片机直接拉取音频流的 URL
 *
 * 策略说明:
 * 1. 返回服务器代理 URL (/tts-stream)，由服务器从 Google TTS 拉取音频后流式转发给 ESP32
 *    - Google Translate TTS 在中国被墙，ESP32 无法直接访问 translate.google.com
 *    - 服务器有外网访问能力，可以代理 TTS 请求
 *    - ESP32 已能通过 HTTPS 访问服务器 (所有 API 调用都走 HTTPS)
 *
 * 2. 服务器代理 handleTtsStream 会依次尝试: Google TTS > 百度 TTS > 有道 TTS
 *
 * @param {string} text 要合成的文本
 * @param {object} req Express 的 request 对象 (用于提取 host 和 protocol)
 * @returns {string} 完整的拉流 URL
 */
export function getTtsAudioUrl(text, req) {
  if (!text) return null

  const encodeText = encodeURIComponent(text)
  // 使用服务器代理: 服务器从 Google TTS 获取音频后流式转发给 ESP32
  // 这样 ESP32 只需要连接它已经能访问的服务器，不需要直连被墙的 Google
  const protocol = req.protocol
  const host = req.get('host')
  return `${protocol}://${host}/pet-garden/api/device/tts-stream?text=${encodeText}`
}

/**
 * Express 路由处理函数：处理来自单片机的 /tts-stream 流请求
 * 作为 ESP32 直接访问 Google TTS 失败时的服务器代理备用方案
 *
 * 优先级: Google TTS > 百度语音合成 (需配置) > 有道翻译 (保底)
 */
export async function handleTtsStream(req, res) {
  const text = req.query.text
  if (!text) {
    return res.status(400).send('No text provided')
  }

  const encodeText = encodeURIComponent(text)

  try {
    // ====== 1. 首选: Google Translate TTS (免费) ======
    const googleUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=zh-CN&client=tw-ob&q=${encodeText}`
    const googleResp = await fetch(googleUrl, {
      headers: { 'User-Agent': 'Mozilla/5.0' }
    })

    if (googleResp.ok && googleResp.headers.get('content-type')?.includes('audio')) {
      console.log(`🗣️ [TTS] Google Translate TTS 合成成功: ${text.substring(0, 30)}...`)
      res.setHeader('Content-Type', 'audio/mpeg')
      res.setHeader('Content-Length', googleResp.headers.get('content-length') || '')
      return googleResp.body.pipe(res)
    }
    console.warn(`⚠️ [TTS] Google TTS 失败 (${googleResp.status}), 尝试下一个 provider...`)
  } catch (e) {
    console.warn(`⚠️ [TTS] Google TTS 异常: ${e.message}, 尝试下一个 provider...`)
  }

  try {
    const config = await getTtsConfig()

    // ====== 2. 百度语音合成 (需配置 API Key) ======
    if (config.baiduKey && config.baiduSecret) {
      // 获取百度 Token
      const tokenResp = await fetch(
        `https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=${config.baiduKey}&client_secret=${config.baiduSecret}`
      )
      const tokenData = await tokenResp.json()

      if (tokenData.access_token) {
        const form = new URLSearchParams()
        form.append('tex', text)
        form.append('tok', tokenData.access_token)
        form.append('cuid', 'classpet-device')
        form.append('ctp', '1')
        form.append('lan', 'zh')
        form.append('spd', '5')
        form.append('pit', '5')
        form.append('vol', '10')
        form.append('per', '4') // 度丫丫童声
        form.append('aue', '3') // MP3

        const ttsResp = await fetch('https://tsn.baidu.com/text2audio', {
          method: 'POST',
          body: form,
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        })

        const ct = ttsResp.headers.get('content-type') || ''
        if (ct.includes('audio') || ct.includes('mpeg')) {
          console.log(`🗣️ [TTS] 百度语音合成成功: ${text.substring(0, 30)}...`)
          res.setHeader('Content-Type', 'audio/mpeg')
          return ttsResp.body.pipe(res)
        } else {
          console.warn(`⚠️ [TTS] 百度 TTS 失败: ${await ttsResp.text()}`)
        }
      }
    }
  } catch (e) {
    console.warn(`⚠️ [TTS] 百度 TTS 异常: ${e.message}`)
  }

  try {
    // ====== 3. 保底: 有道翻译公开语音接口 ======
    console.log(`🗣️ [TTS] 使用有道保底接口合成: ${text.substring(0, 30)}...`)
    const fallbackUrl = `https://tts.youdao.com/fanyivoice?word=${encodeText}&le=zh&keyfrom=speaker-target`
    const fbResp = await fetch(fallbackUrl)

    if (fbResp.ok) {
      res.setHeader('Content-Type', 'audio/mpeg')
      return fbResp.body.pipe(res)
    } else {
      throw new Error(`有道 TTS 失败: ${fbResp.status}`)
    }
  } catch (e) {
    console.error('❌ [TTS] 所有 TTS provider 均失败:', e.message)
    res.status(500).send('TTS Engine Error')
  }
}
