// NLP 意图分类服务
// 优先调用大模型 (OpenRouter)，失败后自动降级为内置正则提取器

import fetch from 'node-fetch'
import { getAsync } from '../db.js'

// ================= 正则降级分类器 =================

/**
 * 正则表达式降级意图提取引擎
 * 当大模型 API 不可用时使用
 */
export function regexClassifyIntent(text) {
  const t = text.trim()

  // 1. 音乐搜索判定 (已停用)
  if (/(听|播放|唱|来一首|放一首|点歌|搜歌|音乐)/.test(t)) {
    return {
      action: 'none',
      reply_text: '抱歉，当前系统的音乐播放功能已关闭。'
    }
  }

  // 2. 状态查询判定
  if (/(状态|几分|等级|多少级|经验|多少分|徽章|宠物)/.test(t)) {
    return {
      action: 'query_status',
      reply_text: '正在为您检索您和宠物的最新积分数据。'
    }
  }

  // 3. 默认判定为加分任务申报
  const taskName = t.replace(/(我完成了|帮我申请|我要申请|帮我申报|申报|申请|完成了|做完了|我做了)/g, '').trim() || '日常任务'
  return {
    action: 'apply_task',
    task_name: taskName,
    reply_text: `已提交申报任务：${taskName}`
  }
}

// ================= 大模型意图分类 =================

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

// 拼接带"近期记忆"的用户提示词 (P2: 多轮上下文, 让宠物有连续对话的陪伴感)
function buildUserPrompt(text, context) {
  if (context && context.length) {
    return `【近期对话记忆, 用于保持连贯, 不要复述已说过的内容】\n${context}\n\n用户现在说: ${text}`
  }
  return text
}

// 从(可能未闭合的)流式累积文本中提取 reply_text (P1: 边生成边显示)
function extractReplyText(raw) {
  const cleaned = raw.replace(/^```json\s*/i, '').replace(/```$/, '')
  const m = cleaned.match(/"reply_text"\s*:\s*"([^"]*)/)
  return m ? m[1] : null
}

/**
 * 调用大模型进行意图分类
 * @param {string} text - 用户输入文本
 * @param {string} context - 近期对话记忆 (P2)
 * @returns {Promise<object>} 分类结果 { action, task_name?, schedule_info?, reply_text }
 */
export async function askLLMIntent(text, context = '') {
  const keyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
  const modelRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_model'")

  let apiKey = keyRow ? JSON.parse(keyRow.value) : ''
  let model = modelRow ? JSON.parse(modelRow.value) : 'openrouter/free'

  if (!apiKey) {
    apiKey = process.env.OPENROUTER_API_KEY || ''
  }

  if (!apiKey) {
    throw new Error('OpenRouter API Key not provided in settings or env')
  }

  const response = await fetch('https://openrouter.ai/api/v1/chat/completions', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${apiKey}`,
      'Content-Type': 'application/json',
      'HTTP-Referer': 'https://github.com/class-pet-garden',
      'X-Title': 'ClassPetGarden'
    },
    body: JSON.stringify({
      model,
      messages: [
        { role: 'system', content: SYSTEM_PROMPT },
        { role: 'user', content: buildUserPrompt(text, context) }
      ],
      temperature: 0.1
    })
  })

  if (!response.ok) {
    const errorText = await response.text()
    throw new Error(`OpenRouter API response error: ${response.status} - ${errorText}`)
  }

  const data = await response.json()
  const content = data.choices?.[0]?.message?.content?.trim()
  if (!content) {
    throw new Error('Empty response from OpenRouter')
  }

  let cleanJson = content
  if (cleanJson.startsWith('```')) {
    cleanJson = cleanJson.replace(/^```json\s*/i, '').replace(/```$/, '').trim()
  }

  return JSON.parse(cleanJson)
}

/**
 * P1: 流式意图分类 —— 开启 stream:true, 边接收 token 边提取 reply_text 并回调 onDelta,
 * 最终返回完整 JSON。若流式失败则抛出, 由 classifyIntentStreaming 降级。
 */
export async function askLLMIntentStreaming(text, context, onDelta) {
  const keyRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_api_key'")
  const modelRow = await getAsync("SELECT value FROM settings WHERE key = 'openrouter_model'")

  let apiKey = keyRow ? JSON.parse(keyRow.value) : ''
  let model = modelRow ? JSON.parse(modelRow.value) : 'openrouter/free'
  if (!apiKey) apiKey = process.env.OPENROUTER_API_KEY || ''
  if (!apiKey) throw new Error('OpenRouter API Key not provided')

  const response = await fetch('https://openrouter.ai/api/v1/chat/completions', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${apiKey}`,
      'Content-Type': 'application/json',
      'HTTP-Referer': 'https://github.com/class-pet-garden',
      'X-Title': 'ClassPetGarden'
    },
    body: JSON.stringify({
      model,
      messages: [
        { role: 'system', content: SYSTEM_PROMPT },
        { role: 'user', content: buildUserPrompt(text, context) }
      ],
      temperature: 0.1,
      stream: true
    })
  })

  if (!response.ok) {
    const errorText = await response.text()
    throw new Error(`OpenRouter stream error: ${response.status} - ${errorText}`)
  }

  const decoder = new TextDecoder()
  let buffer = ''
  let raw = ''
  let lastReply = null

  for await (const chunk of response.body) {
    buffer += decoder.decode(chunk, { stream: true })
    let nl
    while ((nl = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, nl).trim()
      buffer = buffer.slice(nl + 1)
      if (!line.startsWith('data:')) continue
      const data = line.slice(5).trim()
      if (data === '[DONE]') continue
      try {
        const j = JSON.parse(data)
        const delta = j.choices?.[0]?.delta?.content || ''
        if (!delta) continue
        raw += delta
        const reply = extractReplyText(raw)
        if (reply !== null && reply !== lastReply) {
          lastReply = reply
          onDelta(reply)
        }
      } catch {
        // 跳过非 JSON 行 (如心跳/注释)
      }
    }
  }

  const cleaned = raw.replace(/^```json\s*/i, '').replace(/```$/, '')
  return JSON.parse(cleaned)
}

// ================= 统一入口 (带降级) =================

/**
 * 意图分类统一入口
 * 优先调用大模型，失败后自动降级为正则分类器
 */
export async function classifyIntent(text, context = '') {
  try {
    return await askLLMIntent(text, context)
  } catch (err) {
    console.warn('⚠️ 大模型 API 调用失败，自动降级为内置正则分类器:', err.message)
    return regexClassifyIntent(text)
  }
}

/**
 * P1: 流式意图分类统一入口。开启 LLM_STREAM=1 时逐字回传 reply_text;
 * 任何失败都降级为正则分类器 (并补发一次完整回复)。
 */
export async function classifyIntentStreaming(text, context, onDelta) {
  try {
    return await askLLMIntentStreaming(text, context, onDelta)
  } catch (err) {
    console.warn('⚠️ 流式大模型调用失败，降级为内置正则分类器:', err.message)
    const r = regexClassifyIntent(text)
    if (onDelta) onDelta(r.reply_text)
    return r
  }
}
