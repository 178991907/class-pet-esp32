// ASR (自动语音识别) 服务层
// 将多个 provider 的调用逻辑从路由中抽出，配置与实现分离
// provider 可选: 'baidu' | 'openai' | 'openrouter' | 'groq'

import fetch from 'node-fetch'
import FormData from 'form-data'
import { getAsync } from '../db.js'

// ================= 配置读取 =================

/**
 * 从数据库 settings 表读取 ASR 相关配置
 * @returns {{ provider: string, apiKey: string, baiduKey: string, baiduSecret: string }}
 */
export async function getAsrConfig() {
  const providerRow = await getAsync("SELECT value FROM settings WHERE key = 'asr_provider'")
  const provider = providerRow ? JSON.parse(providerRow.value) : 'openrouter'

  const apiKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
  const apiKey = apiKeyRow ? JSON.parse(apiKeyRow.value) : ''

  const baiduKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'baidu_api_key'")
  const baiduKey = baiduKeyRow ? JSON.parse(baiduKeyRow.value) : ''

  const baiduSecretRow = await getAsync("SELECT value FROM settings WHERE key = 'baidu_secret_key'")
  const baiduSecret = baiduSecretRow ? JSON.parse(baiduSecretRow.value) : ''

  const groqKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'groq_api_key'")
  const groqKey = groqKeyRow ? JSON.parse(groqKeyRow.value) : ''

  return { provider, apiKey, baiduKey, baiduSecret, groqKey }
}

// ================= WAV → PCM 转换 =================

/**
 * 从 WAV 格式的 Buffer 中提取原始 PCM 数据
 * 百度 ASR 需要纯 PCM (16kHz, 16bit, mono)
 */
export function extractPcmFromWav(buffer) {
  if (
    buffer.length > 44 &&
    buffer.slice(0, 4).toString() === 'RIFF' &&
    buffer.slice(8, 12).toString() === 'WAVE'
  ) {
    const dataIdx = buffer.indexOf('data')
    if (dataIdx >= 0 && dataIdx + 8 < buffer.length) {
      return buffer.slice(dataIdx + 8)
    }
  }
  return buffer
}

// ================= Provider 实现 =================

/**
 * 百度短语音识别
 * 免费额度：每月 5 万次，专攻中文
 */
async function recognizeBaidu(audioBuffer, { baiduKey, baiduSecret }) {
  if (!baiduKey || !baiduSecret) {
    throw new Error('未配置百度 ASR 凭证 (baidu_api_key / baidu_secret_key)')
  }

  // 1) 换取 access_token
  const tokenResp = await fetch(
    `https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=${baiduKey}&client_secret=${baiduSecret}`
  )
  const tokenData = await tokenResp.json()
  if (!tokenData.access_token) {
    throw new Error('百度 ASR 获取 access_token 失败: ' + JSON.stringify(tokenData))
  }

  // 2) 调用短语音识别接口 (需原始 PCM 16k 16bit mono)
  const pcm = extractPcmFromWav(audioBuffer)
  const b64Audio = pcm.toString('base64')

  const asrResp = await fetch('https://vop.baidu.com/server_api', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      format: 'pcm',
      rate: 16000,
      channel: 1,
      cuid: 'classpet-device',
      token: tokenData.access_token,
      speech: b64Audio,
      len: pcm.length
    })
  })

  const asrData = await asrResp.json()
  if (asrData.err_no !== 0) {
    throw new Error(`百度 ASR 错误 ${asrData.err_no}: ${asrData.err_msg}`)
  }

  return (asrData.result || []).map(s => s[0] || '').join('')
}

/**
 * OpenAI 官方 Whisper API
 */
async function recognizeOpenAI(audioBuffer, { apiKey }) {
  if (!apiKey) {
    throw new Error('未配置 ASR API Key')
  }

  const form = new FormData()
  form.append('file', audioBuffer, { filename: 'record.wav', contentType: 'audio/wav' })
  form.append('model', 'whisper-1')

  const response = await fetch('https://api.openai.com/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${apiKey}`, ...form.getHeaders() },
    body: form
  })

  if (!response.ok) {
    const err = await response.text()
    throw new Error(`OpenAI Whisper ${response.status}: ${err}`)
  }

  const data = await response.json()
  return data.text
}

/**
 * OpenRouter ASR (使用 openai/whisper-large-v3 模型)
 */
async function recognizeOpenRouter(audioBuffer, { apiKey }) {
  if (!apiKey) {
    throw new Error('未配置 ASR API Key')
  }

  const form = new FormData()
  form.append('file', audioBuffer, { filename: 'record.wav', contentType: 'audio/wav' })
  form.append('model', 'openai/whisper-large-v3')

  const response = await fetch('https://openrouter.ai/api/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${apiKey}`, ...form.getHeaders() },
    body: form
  })

  if (!response.ok) {
    const err = await response.text()
    throw new Error(`OpenRouter ASR ${response.status}: ${err}`)
  }

  const data = await response.json()
  return data.text
}

/**
 * Groq ASR (免费 Whisper API，速度极快)
 * 模型: whisper-large-v3 / whisper-large-v3-turbo
 * API 兼容 OpenAI 接口格式
 */
async function recognizeGroq(audioBuffer, { groqKey }) {
  if (!groqKey) {
    throw new Error('未配置 Groq API Key (groq_api_key)')
  }

  const form = new FormData()
  form.append('file', audioBuffer, { filename: 'record.wav', contentType: 'audio/wav' })
  form.append('model', 'whisper-large-v3')
  form.append('language', 'zh')
  form.append('response_format', 'json')

  const response = await fetch('https://api.groq.com/openai/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${groqKey}`, ...form.getHeaders() },
    body: form
  })

  if (!response.ok) {
    const err = await response.text()
    throw new Error(`Groq ASR ${response.status}: ${err}`)
  }

  const data = await response.json()
  return data.text
}

// ================= 统一入口 =================

const PROVIDERS = {
  baidu: recognizeBaidu,
  openai: recognizeOpenAI,
  openrouter: recognizeOpenRouter,
  groq: recognizeGroq
}

/**
 * 语音识别统一入口
 * @param {Buffer} audioBuffer - 音频数据 (WAV 或 PCM)
 * @param {string} deviceId - 设备 ID (用于日志)
 * @returns {Promise<string>} 识别出的文本
 */
export async function recognizeSpeech(audioBuffer, deviceId = 'unknown') {
  const config = await getAsrConfig()
  const recognizer = PROVIDERS[config.provider]

  if (!recognizer) {
    throw new Error(`不支持的 ASR provider: ${config.provider}`)
  }

  const text = await recognizer(audioBuffer, config)
  console.log(`🎙️ [ASR] provider=${config.provider} 设备 ${deviceId} 语音识别结果: ${text}`)
  return text
}

/**
 * 判断错误是否为配置类错误 (非网络/上游问题)
 */
export function isAsrConfigError(message) {
  return /401|402|api[_ ]?key|credentials|access_token/i.test(message)
}
