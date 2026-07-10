// TTS (文本转语音) 服务层
// 优先使用配置的百度语音合成，如果未配置，则回退到免费的公开接口

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
 * @param {string} text 要合成的文本
 * @param {object} req Express 的 request 对象 (用于提取 host 和 protocol)
 * @returns {string} 完整的拉流 URL
 */
export function getTtsAudioUrl(text, req) {
  if (!text) return null

  // 绕过所有后端代理，直接向设备返回纯 HTTP 直链
  // 1. 避免 Vercel 强制 HTTPS 导致 ESP32 内存不足 (SSL handshake OOM)
  // 2. 避免 Node pipe() 产生的 Transfer-Encoding: chunked 导致 ESP32 音频库卡死
  const encodeText = encodeURIComponent(text)
  return `http://tts.youdao.com/fanyivoice?word=${encodeText}&le=zh&keyfrom=speaker-target`
}

/**
 * Express 路由处理函数：处理来自单片机的 /tts-stream 流请求
 */
export async function handleTtsStream(req, res) {
  const text = req.query.text
  if (!text) {
    return res.status(400).send('No text provided')
  }

  try {
    const config = await getTtsConfig()

    // 如果配置了百度 Key，尝试使用百度童声 (度丫丫)
    if (config.baiduKey && config.baiduSecret) {
      // 1. 获取百度 Token
      const tokenResp = await fetch(
        `https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=${config.baiduKey}&client_secret=${config.baiduSecret}`
      )
      const tokenData = await tokenResp.json()
      
      if (tokenData.access_token) {
        // 2. 向百度请求音频流 (通过表单格式)
        const form = new URLSearchParams()
        form.append('tex', text)
        form.append('tok', tokenData.access_token)
        form.append('cuid', 'classpet-device')
        form.append('ctp', '1')
        form.append('lan', 'zh')
        form.append('spd', '5') // 速度 0-15
        form.append('pit', '5') // 音调 0-15
        form.append('vol', '10') // 音量调大点
        form.append('per', '4') // 角色 4:度丫丫童声
        form.append('aue', '3') // 3: mp3 格式

        const ttsResp = await fetch('https://tsn.baidu.com/text2audio', {
          method: 'POST',
          body: form,
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        })

        // 检查百度是否成功返回了音频 (如果失败会返回 application/json 错误信息)
        if (ttsResp.headers.get('content-type').includes('audio') || ttsResp.headers.get('content-type').includes('mpeg')) {
          res.setHeader('Content-Type', 'audio/mpeg')
          return ttsResp.body.pipe(res)
        } else {
          console.error('Baidu TTS Failed:', await ttsResp.text())
          // 失败则继续往下执行回退逻辑
        }
      }
    }

    // ====== 保底回退方案：使用免费且稳定的有道翻译公开语音接口 ======
    console.log(`🎙️ [TTS] 使用保底的有道语音接口合成: ${text}`)
    const fallbackUrl = `https://tts.youdao.com/fanyivoice?word=${encodeURIComponent(text)}&le=zh&keyfrom=speaker-target`
    const fbResp = await fetch(fallbackUrl)
    
    if (fbResp.ok) {
      res.setHeader('Content-Type', 'audio/mpeg')
      return fbResp.body.pipe(res)
    } else {
      throw new Error(`Fallback API failed with status: ${fbResp.status}`)
    }

  } catch (e) {
    console.error('TTS Stream Exception:', e)
    res.status(500).send('TTS Engine Error')
  }
}
