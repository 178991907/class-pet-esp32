// Workers AI 封装 + 语音合成代理
// ASR 默认 Workers AI Whisper(keyless), 可按设备切换 groq/openai/openrouter/baidu(见 transcribe 的 provider 路由);
// LLM 用 Workers AI Llama(中文)；TTS 复用 keyless 的 Google TTS 代理(设备直接拉 MP3)。

const WHISPER_MODEL = '@cf/openai/whisper'
// 全部走 Workers AI 免费额度(10k neurons/天)。
// 为安全占用免费额度、绝不触发付费：意图分类用最小的 3B 模型；宠物闲聊用 8B fp8-fast(比 17B 省 ~5x neuron)。
// 注意: 切勿换回 llama-4-scout-17b —— 它会快速吃爆每日免费 neuron 导致当天语音 429 失败。
const INTENT_MODEL = '@cf/meta/llama-3.2-3b-instruct'
const CHAT_MODEL = '@cf/meta/llama-3.1-8b-instruct-fp8-fast'

// 兼容 Workers AI 多种返回结构(legacy `response` / `text` / OpenAI `choices[].message.content`)
function extractText(result) {
  if (!result) return ''
  const r = result.response || result.text
  if (r) return r
  try {
    return result.choices?.[0]?.message?.content || ''
  } catch {
    return ''
  }
}

// ============ 工具 ============

function arrayBufferToBase64(buffer) {
  const bytes = new Uint8Array(buffer)
  let binary = ''
  const chunk = 0x8000
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk))
  }
  return btoa(binary)
}

// 原始 PCM(16k/16bit/mono) -> WAV(含 44 字节头)
export function pcmToWav(pcmChunks, totalBytes, sampleRate = 16000, numChannels = 1, bitsPerSample = 16) {
  const byteRate = (sampleRate * numChannels * bitsPerSample) / 8
  const blockAlign = (numChannels * bitsPerSample) / 8
  const buffer = new ArrayBuffer(44 + totalBytes)
  const view = new DataView(buffer)
  const writeStr = (off, s) => { for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i)) }
  writeStr(0, 'RIFF')
  view.setUint32(4, 36 + totalBytes, true)
  writeStr(8, 'WAVE')
  writeStr(12, 'fmt ')
  view.setUint32(16, 16, true)
  view.setUint16(20, 1, true)
  view.setUint16(22, numChannels, true)
  view.setUint32(24, sampleRate, true)
  view.setUint32(28, byteRate, true)
  view.setUint16(32, blockAlign, true)
  view.setUint16(34, bitsPerSample, true)
  writeStr(36, 'data')
  view.setUint32(40, totalBytes, true)
  let off = 44
  for (const chunk of pcmChunks) {
    const u = new Uint8Array(chunk)
    new Uint8Array(buffer, off, u.length).set(u)
    off += u.length
  }
  return buffer
}

// ============ ASR ============

export async function transcribe(env, pcmChunks, totalBytes, opts = {}) {
  const provider = (opts && opts.provider) || 'workers-ai'
  const keys = (opts && opts.keys) || {}
  // 兼容两种输入:
  //  - Route B(WS 流式): 设备上行裸 PCM 帧 -> 需 pcmToWav 封装
  //  - Route A(HTTP 回退): 设备直接上传 .wav 文件 -> 已是合法 WAV, 不可再封装(否则双重封装导致 Whisper 解析失败)
  let wav
  const first = pcmChunks && pcmChunks[0]
  const isWav =
    first &&
    first.byteLength >= 4 &&
    (() => {
      const sig = new Uint8Array(first.slice(0, 4))
      return sig[0] === 0x52 && sig[1] === 0x49 && sig[2] === 0x46 && sig[3] === 0x46 // "RIFF"
    })()
  if (isWav) {
    const merged = new Uint8Array(totalBytes)
    let off = 0
    for (const c of pcmChunks) {
      const u = new Uint8Array(c)
      merged.set(u, off)
      off += u.length
    }
    wav = merged.buffer
  } else {
    wav = pcmToWav(pcmChunks, totalBytes)
  }
  // 注意: @cf/openai/whisper 的输入 schema 为 oneOf:
  //   - 整体传 base64 二进制字符串, 或
  //   - { audio: number[] }  (每个元素是 0-255 的原始音频字节, 非 base64 字符串!)
  // 早期写成 { audio: base64String } 会被校验拒绝 (报 'array' not in 'string'), 导致 ASR 永远失败。
  const audioBytes = Array.from(new Uint8Array(wav))

  let text = ''
  try {
    switch (provider) {
      case 'groq':
        text = await groqTranscribe(keys, wav)
        break
      case 'openai':
        text = await openaiTranscribe(keys, wav)
        break
      case 'openrouter':
        text = await openrouterTranscribe(keys, wav)
        break
      case 'baidu':
        text = await baiduTranscribe(keys, wav)
        break
      case 'workers-ai':
      default: {
        const result = await env.AI.run(WHISPER_MODEL, { audio: audioBytes })
        text = (result && result.text) || ''
      }
    }
  } catch (e) {
    console.error(`❌ [ASR:${provider}] 失败:`, e && (e.stack || e.message) || e)
    // 非默认引擎失败时, 自动回退 Workers AI(免费兜底), 保证语音不中断
    if (provider !== 'workers-ai') {
      console.warn('↩️ [ASR] 回退 Workers AI')
      try {
        const r2 = await env.AI.run(WHISPER_MODEL, { audio: audioBytes })
        text = (r2 && r2.text) || ''
      } catch (e2) {
        console.error('❌ [ASR fallback] Workers AI 也失败:', e2 && (e2.stack || e2.message))
        throw e
      }
    } else {
      throw e
    }
  }
  return text
}

// ===== Groq Whisper (免费, 极速) =====
async function groqTranscribe(keys, wav) {
  const key = keys.groq_api_key
  if (!key) throw new Error('缺少 groq_api_key (请在系统后台配置)')
  const form = new FormData()
  form.append('file', new Blob([wav], { type: 'audio/wav' }), 'audio.wav')
  form.append('model', 'whisper-large-v3')
  const r = await fetch('https://api.groq.com/openai/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${key}` },
    body: form
  })
  if (!r.ok) {
    const t = await r.text().catch(() => '')
    throw new Error(`Groq ${r.status}: ${t.slice(0, 200)}`)
  }
  const j = await r.json().catch(() => ({}))
  return (j.text || '').trim()
}

// ===== OpenAI Whisper (付费, 需 key) =====
async function openaiTranscribe(keys, wav) {
  const key = keys.openai_api_key
  if (!key) throw new Error('缺少 openai_api_key (请在系统后台配置)')
  const form = new FormData()
  form.append('file', new Blob([wav], { type: 'audio/wav' }), 'audio.wav')
  form.append('model', 'whisper-1')
  const r = await fetch('https://api.openai.com/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${key}` },
    body: form
  })
  if (!r.ok) {
    const t = await r.text().catch(() => '')
    throw new Error(`OpenAI ${r.status}: ${t.slice(0, 200)}`)
  }
  const j = await r.json().catch(() => ({}))
  return (j.text || '').trim()
}

// ===== OpenRouter (兼容 OpenAI audio API, 需余额) =====
async function openrouterTranscribe(keys, wav) {
  const key = keys.openrouter_api_key
  if (!key) throw new Error('缺少 openrouter_api_key (请在系统后台配置)')
  const model = keys.openrouter_model || 'openai/whisper-1'
  const form = new FormData()
  form.append('file', new Blob([wav], { type: 'audio/wav' }), 'audio.wav')
  form.append('model', model)
  const r = await fetch('https://openrouter.ai/api/v1/audio/transcriptions', {
    method: 'POST',
    headers: { Authorization: `Bearer ${key}` },
    body: form
  })
  if (!r.ok) {
    const t = await r.text().catch(() => '')
    throw new Error(`OpenRouter ${r.status}: ${t.slice(0, 200)}`)
  }
  const j = await r.json().catch(() => ({}))
  return (j.text || '').trim()
}

// ===== 百度短语音 ASR (中文免费, 月 5 万次) =====
async function baiduTranscribe(keys, wav) {
  const ak = keys.baidu_api_key
  const sk = keys.baidu_secret_key
  if (!ak || !sk) throw new Error('缺少 baidu_api_key / baidu_secret_key (请在系统后台配置)')
  // 1) 获取 access_token
  const tkResp = await fetch(
    `https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=${encodeURIComponent(ak)}&client_secret=${encodeURIComponent(sk)}`
  )
  const tk = await tkResp.json().catch(() => ({}))
  if (!tk.access_token) throw new Error(`百度 token 获取失败: ${tk.error_description || tk.error || ''}`)
  // 2) 语音识别
  const speech = arrayBufferToBase64(wav)
  const r = await fetch('https://vop.baidu.com/server_api', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      format: 'wav',
      rate: 16000,
      channel: 1,
      cuid: 'classpet-device',
      token: tk.access_token,
      speech,
      len: wav.byteLength
    })
  })
  const j = await r.json().catch(() => ({}))
  if (j.err_no !== 0) throw new Error(`百度识别失败 err ${j.err_no}: ${j.err_msg || ''}`)
  return (j.result && j.result[0]) || ''
}

// ============ LLM (意图分类) ============

const SYSTEM_PROMPT = `你是一个智能班级助手意图分类器。你需要将用户的语音输入分类，并以标准的 JSON 格式输出，不要包含任何 markdown 标记（如 \`\`\`json）或多余解释。
JSON 输出格式：
{
  "action": "apply_task" | "query_status" | "create_schedule" | "none",
  "task_name": "申报任务名称，如'认真打扫卫生'（仅在 action 为 apply_task 时需要，去除'我完成了'等废话前缀）",
  "schedule_info": {
    "days": [1, 2, 3, 4, 5, 6, 7],
    "time": "HH:MM",
    "task_desc": "日程提醒的内容，如 '背英语单词'"
  },
  "reply_text": "给学生的口语化鼓励或确认回复，字数控制在 25 字以内（适合设备语音播放，如：已为您安排好了日程）"
}

意图分类规则：
1. 申请加分/申报完成了某件事，如"我扫地了"、"完成作业了"，action 为 apply_task。
2. 查询宠物等级/几分/经验，如"我多少分了"、"我几级了"，action 为 query_status。
3. 设定日程提醒，如"周三下午两点半提醒我背单词"、"每天早上八点叫我起床"，action 为 create_schedule。
4. 其它无法识别或无意义的内容，action 为 none，reply_text 可作简短闲聊回复。`

function buildUserPrompt(text, context, ownerContext) {
  let prompt = ''
  if (ownerContext && ownerContext.length) {
    prompt += `【宠物主人记忆(用于个性化陪伴与帮助学习, 不要原样复述)】\n${ownerContext}\n\n`
  }
  if (context && context.length) {
    prompt += `【近期对话记忆, 用于保持连贯, 不要复述已说过的内容】\n${context}\n\n`
  }
  prompt += `用户现在说: ${text}`
  return prompt
}

// 正则降级分类器（大模型不可用时）
export function regexClassifyIntent(text) {
  const t = text.trim()
  if (/(听|播放|唱|来一首|放一首|点歌|搜歌|音乐)/.test(t)) {
    return { action: 'none', reply_text: '抱歉，当前系统的音乐播放功能已关闭。' }
  }
  if (/(状态|几分|等级|多少级|经验|多少分|徽章|宠物)/.test(t)) {
    return { action: 'query_status', reply_text: '正在为您检索您和宠物的最新积分数据。' }
  }
  const taskName = t.replace(/(我完成了|帮我申请|我要申请|帮我申报|申报|申请|完成了|做完了|我做了)/g, '').trim() || '日常任务'
  return { action: 'apply_task', task_name: taskName, reply_text: `已提交申报任务：${taskName}` }
}

function extractJson(content) {
  let c = content.trim()
  if (c.startsWith('```')) c = c.replace(/^```json\s*/i, '').replace(/```$/, '').trim()
  return JSON.parse(c)
}

// 意图分类（大模型优先，失败降级正则）
export async function classifyIntent(env, text, context = '', ownerContext = '') {
  try {
    const result = await env.AI.run(INTENT_MODEL, {
      messages: [
        { role: 'system', content: SYSTEM_PROMPT },
        { role: 'user', content: buildUserPrompt(text, context, ownerContext) }
      ],
      temperature: 0.1
    })
    const content = extractText(result)
    if (!content) throw new Error('empty LLM response')
    return extractJson(content)
  } catch (err) {
    console.warn('⚠️ [AI] 意图分类失败，降级正则:', err && err.message)
    return regexClassifyIntent(text)
  }
}

// Web 语音伙伴(宠物闲聊) —— 非结构化对话
const PET_SYSTEM = `你是小朋友的班级宠物小搭档，名字叫"宠物伙伴"。语气可爱、鼓励、简短(不超过 40 字)，用中文回答。可以陪小朋友聊天、鼓励学习、提醒好习惯。`

export async function petChat(env, message, history = '', ownerContext = '') {
  let system = PET_SYSTEM
  let userContent = history ? `${history}\n小朋友: ${message}` : message
  if (ownerContext && ownerContext.length) {
    system += `\n\n【宠物主人记忆(用于个性化陪伴与帮助学习, 不要原样复述)】\n${ownerContext}`
  }
  try {
    const result = await env.AI.run(CHAT_MODEL, {
      messages: [
        { role: 'system', content: system },
        { role: 'user', content: userContent }
      ],
      temperature: 0.8,
      max_tokens: 120
    })
    const text = extractText(result).trim()
    return text || '我听到啦～能再说清楚一点吗？'
  } catch (e) {
    console.warn('⚠️ [AI] petChat 失败, 兜底:', e && e.message)
    return '哎呀，我刚才走神了，能再跟我说一遍吗？🐾'
  }
}

// ============ TTS (keyless Google TTS 代理) ============

export async function fetchTtsMp3(text) {
  const enc = encodeURIComponent(text)
  const gUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=zh-CN&client=tw-ob&q=${enc}`
  try {
    const r = await fetch(gUrl, { headers: { 'User-Agent': 'Mozilla/5.0' } })
    if (r.ok && (r.headers.get('content-type') || '').includes('audio')) {
      const buf = await r.arrayBuffer()
      if (buf.byteLength) return { mp3: buf, url: gUrl, provider: 'google' }
    }
  } catch (e) {
    console.warn('⚠️ [TTS] Google 失败:', e.message)
  }
  // 有道兜底
  try {
    const fUrl = `https://tts.youdao.com/fanyivoice?word=${enc}&le=zh&keyfrom=speaker-target`
    const fb = await fetch(fUrl)
    if (fb.ok) {
      const buf = await fb.arrayBuffer()
      if (buf.byteLength) return { mp3: buf, url: fUrl, provider: 'youdao' }
    }
  } catch (e) {
    console.warn('⚠️ [TTS] 有道失败:', e.message)
  }
  return null
}
