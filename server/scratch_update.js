import multer from 'multer'
import FormData from 'form-data'
import fetch from 'node-fetch'

const upload = multer({ storage: multer.memoryStorage() })

async function transcribeAudio(buffer, fileName) {
  const apiKeyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
  const apiKey = apiKeyRow ? JSON.parse(apiKeyRow.value) : ''
  if (!apiKey) throw new Error('ASR_KEY_MISSING')
  
  // 默认尝试使用 OpenAI Whisper 兼容接口
  // 如果用户用的是 OneAPI/NewAPI 代理池，同一个 BaseURL 和 Key 也能处理 Whisper
  const form = new FormData()
  form.append('file', buffer, { filename: fileName, contentType: 'audio/wav' })
  form.append('model', 'whisper-1')

  const response = await fetch('https://api.openai.com/v1/audio/transcriptions', {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${apiKey}`,
      ...form.getHeaders()
    },
    body: form
  })

  if (!response.ok) {
    const err = await response.text()
    throw new Error(`ASR Failed: ${err}`)
  }

  const data = await response.json()
  return data.text
}
