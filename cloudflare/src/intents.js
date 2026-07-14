// 本地语料库意图匹配 (不调用大模型, 毫秒级, 省 token)
// 设计: 简单操作型指令用正则语料命中, 直接映射到 action 并执行; 只有闲聊/互动才回退到大模型。
// 命中返回 { action, params, reply? }; 未命中(闲聊)返回 null -> 调用方走 LLM 闲聊。
//
// 与 src/data/pets.ts 的 pet_type 对齐: 中文名 -> id

export const PET_TYPES = {
  '西高地': 'west-highland', '比熊': 'bichon', '边牧': 'border-collie', '柴犬': 'shiba',
  '金毛': 'golden-retriever', '萨摩耶': 'samoyed', '哈士奇': 'husky',
  '虎斑猫': 'tabby-cat', '波斯猫': 'persian-cat', '布偶猫': 'ragdoll-cat', '橘猫': 'orange-cat',
  '垂耳兔': 'lop-rabbit', '安哥拉兔': 'angora-rabbit', '仓鼠': 'hamster', '银狐仓鼠': 'winter-hamster',
  '柯尔鸭': 'call-duck', '羊驼': 'alpaca', '小熊猫': 'red-panda', '柯基': 'corgi',
  '小狗': 'shiba', '狗狗': 'shiba', '小猫': 'orange-cat', '小猫猫': 'orange-cat',
  '小兔': 'lop-rabbit', '兔子': 'lop-rabbit', '小狗狗': 'shiba'
}

function petName(id) {
  for (const [k, v] of Object.entries(PET_TYPES)) if (v === id) return k
  return id
}

function extractTaskName(t) {
  return t
    .replace(/(我完成了|帮我申请|我要申请|帮我申报|申报一下|申报|申请一下|申请|完成了|做完了|我做了|我刚|刚刚|已经|今天|刚才|了|哦|啊|呢|吧|嘛|呀|哈|啦|额|嗯|呢)/g, '')
    .replace(/\s+/g, '')
    .trim() || '日常表现'
}

function parseDate(t) {
  const now = new Date()
  let d = new Date(now)
  if (/明天/.test(t)) d.setDate(now.getDate() + 1)
  else if (/后天/.test(t)) d.setDate(now.getDate() + 2)
  const m = t.match(/(\d{1,2})\s*月\s*(\d{1,2})\s*日?/)
  if (m) d = new Date(now.getFullYear(), parseInt(m[1]) - 1, parseInt(m[2]))
  const y = String(d.getFullYear())
  const mm = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${y}-${mm}-${dd}`
}

function parseTime(t) {
  const m = t.match(/(\d{1,2})\s*[:：点]\s*(\d{1,2})?\s*分?/)
  if (m) {
    const h = String(parseInt(m[1])).padStart(2, '0')
    const min = m[2] ? String(parseInt(m[2])).padStart(2, '0') : '00'
    return `${h}:${min}`
  }
  if (/(下午|晚上|中午)/.test(t)) {
    const m2 = t.match(/(\d{1,2})\s*点/)
    if (m2) return `${String((parseInt(m2[1]) + 12) % 24).padStart(2, '0')}:00`
  }
  return null
}

export function matchCorpus(text) {
  const t = (text || '').trim()
  if (!t) return null

  // 1) 帮助
  if (/(你能做什么|你会什么|怎么用|帮助|有哪些功能|指令|命令|教我|你会干嘛)/.test(t)) {
    return {
      action: 'help',
      params: {},
      reply: '我可以做这些事：申报任务加分、查询积分和等级、设定日程提醒、添加日历和待办清单、喂食抚摸宠物、开始番茄钟、更换宠物。比如你说“我今天扫地了”或“查一下我的积分”。'
    }
  }

  // 2) 换宠物
  if (/(换宠物|换个宠物|换只宠物|变成|我想养|养只|养一只|更改宠物|换一个宠物|换只)/.test(t)) {
    let petType = null
    for (const [k, v] of Object.entries(PET_TYPES)) {
      if (t.includes(k)) { petType = v; break }
    }
    return {
      action: 'change_pet',
      params: { pet_type: petType },
      reply: petType ? `好的，已为你切换到${petName(petType)}。` : '好的，请告诉我想换成哪种宠物，比如边牧、金毛、橘猫。'
    }
  }

  // 3) 喂食 / 抚摸
  if (/(喂食|喂一下|喂它|喂饭|吃(饭|东西)|抚摸|摸摸|抱抱|亲亲|哄一下|陪玩|陪我玩)/.test(t)) {
    return { action: 'feed_pet', params: {}, reply: '好呀，我来照顾一下小宠物，它很开心～' }
  }

  // 4) 番茄钟
  if (/(番茄钟|专注|计时)/.test(t) || /(\d+)\s*分钟/.test(t)) {
    const m = t.match(/(\d+)\s*分钟/)
    const minutes = Math.min(60, Math.max(1, parseInt(m ? m[1] : 25) || 25))
    return { action: 'start_pomodoro', params: { minutes }, reply: `好的，开始${minutes}分钟番茄钟，专心加油！` }
  }

  // 5) 添加日历 (含 X月Y日 / 明天)
  if (/(日历|记到日历|加到日历|安排|号是)/.test(t) && !/(待办|清单)/.test(t)) {
    const titleM = t.match(/(?:添加|记|加|安排|提醒)?[：: ]?(.+?)(?:在|于|是)?\s*(\d{1,2}\s*月\s*\d{1,2}\s*日?|\d{1,2}\s*[:：点])/)
    const title = (titleM && titleM[1] ? titleM[1] : t)
      .replace(/(添加|记到|加到|日历|安排|提醒|一下|吧|呢|我要|我想|请|帮我)/g, '')
      .trim().slice(0, 40) || '事项'
    return {
      action: 'add_calendar',
      params: { title, event_date: parseDate(t), time_str: parseTime(t) },
      reply: `好的，已添加到日历：${title}。`
    }
  }

  // 6) 完成待办 (勾选)
  if (/(完成|勾掉|勾选|做完|打勾|标记完成).*(待办|清单|任务)|(待办|清单|任务).*(完成|勾掉|勾选|打勾)/.test(t)) {
    const content = t.replace(/(完成|勾掉|勾选|做完|打勾|标记完成|待办|清单|任务|一下|吧|呢|的|帮我)/g, '').trim().slice(0, 40)
    return {
      action: 'check_checklist',
      params: { content },
      reply: content ? `好的，已勾掉待办：${content}。` : '好的，请告诉我完成哪一项待办。'
    }
  }

  // 7) 添加待办
  if (/(待办|清单|任务|记一下|记着|提醒我做|别忘了)/.test(t) && !/(完成|勾掉|查询|查一下|看看|有哪些)/.test(t)) {
    const content = t.replace(/(添加|新增|加个|加一项|记一下|记着|提醒我做|待办|清单|任务|一下|吧|呢|的|帮我|我要|我想|请)/g, '').trim().slice(0, 40)
    if (content) return { action: 'add_checklist', params: { content }, reply: `好的，已添加待办：${content}。` }
  }

  // 8) 查询清单 / 日历
  if (/(我的待办|待办有哪些|清单有什么|我的清单|查看待办|我的日历|日历有什么|近期安排|有什么安排)/.test(t)) {
    const which = /(日历|安排)/.test(t) ? 'calendar' : 'checklist'
    return { action: 'query_list', params: { which }, reply: '' }
  }

  // 9) 查询状态
  if (/(几分|多少分|等级|几级|经验|状态|宠物.*(等级|几分)|积分|排第)/.test(t)) {
    return { action: 'query_status', params: {}, reply: '' }
  }

  // 10) 设定日程提醒
  if (/(提醒|日程|每天|每日|每周|周[一二三四五六日天]|定时)/.test(t)) {
    const days = []
    const dayMap = { '一': 1, '二': 2, '三': 3, '四': 4, '五': 5, '六': 6, '日': 7, '天': 7 }
    const dm = t.match(/周([一二三四五六日天])/)
    if (/每天|每日/.test(t)) days.push(1, 2, 3, 4, 5, 6, 7)
    else if (dm) days.push(dayMap[dm[1]])
    else days.push(new Date().getDay() === 0 ? 7 : new Date().getDay())
    const time = parseTime(t) || '08:00'
    const task_desc = t.replace(/(提醒|日程|每天|每日|每周|周[一二三四五六日天]|早上|下午|晚上|中午|点|分|定时|我|要|记得|一下|吧|呢|的|帮我|请)/g, '').trim().slice(0, 40) || '事项'
    return { action: 'create_schedule', params: { days, time, task_desc }, reply: '' }
  }

  // 11) 申报加分 (需明确动词, 避免闲聊误命中)
  if (/(完成了|做完了|我做了|扫地|作业|帮忙|打扫|申报|申请加分|加分|值日|读书|运动|洗手|整理|背书|练琴|浇花|倒垃圾|表现好|做好事)/.test(t)) {
    return { action: 'apply_task', params: { task_name: extractTaskName(t) }, reply: '' }
  }

  return null // 闲聊 -> 走大模型
}
