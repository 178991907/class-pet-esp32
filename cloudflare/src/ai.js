// Workers AI 封装 + 语音合成代理
// ASR 用 Workers AI Whisper(keyless)；LLM 用 Workers AI Llama(中文)；TTS 复用 keyless 的 Google TTS 代理(设备直接拉 MP3)。

const WHISPER_MODEL = '@cf/openai/whisper'
const LLM_MODEL = '@cf/meta/llama-4-scout-17b-16e-instruct'

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

export async function transcribe(env, pcmChunks, totalBytes) {
  const wav = pcmToWav(pcmChunks, totalBytes)
  const base64 = arrayBufferToBase64(wav)
  const result = await env.AI.run(WHISPER_MODEL, { audio: base64 })
  return (result && result.text) || ''
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

function buildUserPrompt(text, context) {
  if (context && context.length) {
    return `【近期对话记忆, 用于保持连贯, 不要复述已说过的内容】\n${context}\n\n用户现在说: ${text}`
  }
  return text
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
export async function classifyIntent(env, text, context = '') {
  try {
    const result = await env.AI.run(LLM_MODEL, {
      prompt: `${SYSTEM_PROMPT}\n\n${buildUserPrompt(text, context)}`,
      temperature: 0.1
    })
    const content = result?.response || result?.text || ''
    if (!content) throw new Error('empty LLM response')
    return extractJson(content)
  } catch (err) {
    console.warn('⚠️ [AI] 意图分类失败，降级正则:', err && err.message)
    return regexClassifyIntent(text)
  }
}

// Web 语音伙伴(宠物闲聊) —— 非结构化对话
const PET_SYSTEM = `你是小朋友的班级宠物小搭档，名字叫"宠物伙伴"。语气可爱、鼓励、简短(不超过 40 字)，用中文回答。可以陪小朋友聊天、鼓励学习、提醒好习惯。`

export async function petChat(env, message, history = '') {
  const userContent = history ? `${history}\n小朋友: ${message}` : message
  try {
    const result = await env.AI.run(LLM_MODEL, {
      prompt: `${PET_SYSTEM}\n\n${userContent}`,
      temperature: 0.8,
      max_tokens: 120
    })
    const text = (result?.response || result?.text || '').trim()
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
