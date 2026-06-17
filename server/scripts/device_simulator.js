/**
 * @file device_simulator.js
 * @brief 班级宠物园通用开发板 - 全功能网络通信模拟器
 * @note 用于在无真实硬件时自测防重放签名、自动确认、意图匹配及多音源熔断等核心 API
 */

import crypto from 'crypto'
import readline from 'readline'

// ================= 配置参数 =================
const SERVER_URL = 'http://localhost:3002'
const DEVICE_ID = 'AA:BB:CC:DD:11:22' // 模拟设备物理 MAC 地址
const DEVICE_SECRET = 'class-pet-device-secret' // 必须与后端一致

// 离线任务申报暂存队列 (模拟 NVS/Flash 环形队列)
const offlineQueue = []

// 彩色输出终端控制码
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m',
  cyan: '\x1b[36m',
  red: '\x1b[31m',
}

// 终端交互输入句柄
const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout
})

// ================= 核心工具函数 =================

/**
 * 计算防重放 HMAC-SHA256 签名
 * @param {string} timestamp 毫秒时间戳
 * @param {string} body 请求体内容
 * @returns {string} 64位十六进制小写签名字符串
 */
function calculateSignature(timestamp, body = '') {
  const data = `${DEVICE_ID}${timestamp}${body}`
  return crypto.createHmac('sha256', DEVICE_SECRET).update(data).digest('hex')
}

/**
 * 通用网络请求发送器 (自动注入 X-Device 头信息与安全签名)
 * @param {string} path 请求路径
 * @param {string} method 请求方法
 * @param {object|null} body 请求体 (JSON)
 * @param {number|null} forceTimestamp 强制指定过期时间戳 (用于漏洞测试)
 */
async function sendRequest(path, method = 'GET', body = null, forceTimestamp = null) {
  const url = `${SERVER_URL}${path}`
  
  // 生产环境及安全校验必须使用毫秒级别时间戳
  const timestamp = forceTimestamp !== null ? String(forceTimestamp) : String(Date.now())
  const requestBodyStr = body ? JSON.stringify(body) : ''
  const signature = calculateSignature(timestamp, requestBodyStr)

  const headers = {
    'X-Device-ID': DEVICE_ID,
    'X-Device-Timestamp': timestamp,
    'X-Device-Signature': signature,
    'Content-Type': 'application/json'
  }

  const options = {
    method,
    headers,
  }

  if (body) {
    options.body = requestBodyStr
  }

  try {
    const res = await fetch(url, options)
    const status = res.status
    let data
    try {
      data = await res.json()
    } catch {
      data = { error: '未返回有效的 JSON 数据' }
    }

    return { status, data }
  } catch (err) {
    throw new Error(`无法连接到本地服务器: ${err.message}`)
  }
}

// ================= 模拟业务场景 =================

/**
 * 场景1: 获取设备绑定状态与宠物数据
 */
async function testGetStatus() {
  console.log(`\n[${colors.cyan}场景 1: 查询设备绑定与宠物积分状态${colors.reset}]`)
  try {
    const { status, data } = await sendRequest(`/api/device/status?device_id=${DEVICE_ID}`)
    
    if (status === 200) {
      if (data.status === 'unbound') {
        console.log(`${colors.yellow}⚠️ 警告: 该设备尚未绑定任何学生！${colors.reset}`)
        console.log(`提示: 请在教师后台“学生管理”弹窗中绑定 Device ID: ${colors.bright}${DEVICE_ID}${colors.reset}`)
      } else {
        console.log(`${colors.green}✅ 成功获取学生与宠物数据:${colors.reset}`)
        console.log(`   - 班级名称: ${data.student.class_name}`)
        console.log(`   - 学生姓名: ${data.student.name}`)
        console.log(`   - 累计积分: ${data.student.total_points} 分`)
        console.log(`   - 宠物类型: ${data.student.pet_type} (当前等级: Lv.${data.student.pet_level})`)
        if (data.student.is_max_level) {
          console.log(`   - 🎓 恭喜！该宠物已达 Lv.8 毕业级！`)
        } else {
          console.log(`   - 📈 经验进度: ${data.student.exp_progress} / ${data.student.exp_required} (还需 ${data.student.exp_required - data.student.exp_progress} 经验升级)`)
        }
      }
    } else {
      console.log(`${colors.red}❌ 服务器返回异常状态: ${status}${colors.reset}`, data)
    }
  } catch (err) {
    console.log(`${colors.red}❌ 连接异常:${colors.reset} ${err.message}`)
  }
  pressEnterToContinue()
}

/**
 * 场景2: 模拟语音指令文字发送 (加分申报、状态查询、音乐播放)
 */
function testVoiceCommand() {
  console.log(`\n[${colors.cyan}场景 2: 模拟语音文本申报 (免 ASR 调试后门)${colors.reset}]`)
  rl.question('请输入想要对设备说的话 (例如: "我完成了认真打扫卫生"、"查询状态"、"播放一首经典童话"): ', async (text) => {
    if (!text.trim()) {
      console.log('输入为空，已取消。')
      pressEnterToContinue()
      return
    }

    try {
      console.log(`🎙️ 模拟录音上传: ${colors.bright}"${text}"${colors.reset}`)
      const { status, data } = await sendRequest('/api/device/voice', 'POST', { text: text.trim() })

      if (status === 200) {
        console.log(`${colors.green}✅ 交互核销成功！${colors.reset}`)
        console.log(`   - 云端动作: ${colors.magenta}${data.action}${colors.reset}`)
        console.log(`   - 同声文字: ${data.reply_text}`)
        
        if (data.audio_url) {
          console.log(`   - 🔊 设备异步播放 TTS 音频地址: ${colors.blue}${data.audio_url}${colors.reset}`)
        }
        

      } else {
        console.log(`${colors.red}❌ 请求核销失败 (状态码 ${status}):${colors.reset}`, data)
      }
    } catch (err) {
      console.log(`${colors.red}❌ 连接异常:${colors.reset} ${err.message}`)
    }
    pressEnterToContinue()
  })
}

/**
 * 场景3: 模拟番茄钟完成上报 (如果在线直接提交，如果在离线状态则压入暂存队列)
 */
async function testPomodoroFinished(isOffline = false) {
  console.log(`\n[${colors.cyan}场景 3: 模拟本地番茄钟 25 分钟学习专注结束${colors.reset}]`)
  
  const taskName = '认真专注学习25分钟'
  const points = 2
  const timestamp = Date.now()

  if (isOffline) {
    console.log(`${colors.yellow}⚠️ 设备当前处于离线状态！积攒的数据将存入 Flash 闪存暂存。${colors.reset}`)
    offlineQueue.push({ taskName, points, timestamp })
    console.log(`💾 成功写入离线队列，当前离线积压数: ${colors.bright}${offlineQueue.length}${colors.reset}`)
    pressEnterToContinue()
  } else {
    console.log(`📶 设备在线，尝试直接向云端提交番茄钟申报...`)
    try {
      const { status, data } = await sendRequest('/api/device/voice', 'POST', { text: `我完成了${taskName}` })
      if (status === 200) {
        console.log(`${colors.green}✅ 番茄钟积分申报提交成功！${colors.reset}`)
        console.log(`   - 回复播报: ${data.reply_text}`)
        console.log(`   - 提示: 教师后台可在“手动模式”下审核或在“自动模式”下等过期后自动结算。`)
      } else {
        console.log(`${colors.red}❌ 提交失败 (状态码 ${status}):${colors.reset}`, data)
      }
    } catch (err) {
      console.log(`${colors.red}❌ 连接异常，自动存入离线重发队列:${colors.reset} ${err.message}`)
      offlineQueue.push({ taskName, points, timestamp })
    }
    pressEnterToContinue()
  }
}

/**
 * 场景4: 模拟离线故障，恢复连接后自动串行安全补传
 */
async function testOfflineReconnectionSync() {
  console.log(`\n[${colors.cyan}场景 4: 模拟断网自愈与离线数据队列同步${colors.reset}]`)
  
  if (offlineQueue.length === 0) {
    console.log(`ℹ️ 当前离线暂存队列为空。现在自动生成 2 条离线专注申报记录...`)
    offlineQueue.push({ taskName: '课后认真值日', points: 1, timestamp: Date.now() - 60000 })
    offlineQueue.push({ taskName: '认真听讲', points: 1, timestamp: Date.now() - 30000 })
  }

  console.log(`💾 当前离线队列深度: ${colors.bright}${offlineQueue.length}${colors.reset} 条。`)
  console.log(`📶 模拟网络恢复，开始串行读取队列，逐条计算安全签名补传中...`)

  while (offlineQueue.length > 0) {
    const task = offlineQueue[0]
    console.log(`\n🔄 正在补传: "${colors.yellow}我完成了${task.taskName}${colors.reset}" ...`)
    try {
      const { status, data } = await sendRequest('/api/device/voice', 'POST', { text: `我完成了${task.taskName}` })
      
      if (status === 200 && data.action === 'apply_task') {
        console.log(`${colors.green}   -> 补传成功！${colors.reset} 回复: ${data.reply_text}`)
        offlineQueue.shift() // 同步成功，移出队列
      } else {
        console.log(`${colors.red}   -> 补传失败 (云端拒绝): ${data.error || '未知响应'}${colors.reset}`)
        break; // 停止同步，等待下次重连
      }
    } catch (err) {
      console.log(`${colors.red}   -> 连网失败: ${err.message}，停止同步。${colors.reset}`)
      break;
    }
    // 短暂间隔
    await new Promise(r => setTimeout(r, 1000))
  }

  console.log(`\n💾 队列同步处理结束。当前剩余未发条数: ${colors.bright}${offlineQueue.length}${colors.reset}`)
  pressEnterToContinue()
}

/**
 * 场景6: 安全校验 - 时间戳重放攻击防御测试
 */
async function testReplayAttackPrevention() {
  console.log(`\n[${colors.cyan}场景 5: 安全验证 - 时间戳过期与防重放攻击${colors.reset}]`)
  
  // 模拟当前时间 5 分钟前（300,000毫秒前）的被劫持过时请求
  const expiredTimestamp = Date.now() - 5 * 60 * 1000 
  console.log(`⚠️ 正在伪造五分钟前的请求头，并重新计算签名加密发送...`)

  try {
    const { status, data } = await sendRequest('/api/device/status', 'GET', null, expiredTimestamp)
    
    if (status === 403) {
      console.log(`${colors.green}✅ 防御拦截成功！状态码: ${status}${colors.reset}`)
      console.log(`   - 服务器响应: ${colors.red}${data.error}${colors.reset}`)
      console.log(`   - 结果: 成功拦截了过期的数据拦截/重放攻击请求！`)
    } else {
      console.log(`${colors.red}❌ 安全漏洞！服务器竟然通过了该请求，状态码: ${status}${colors.reset}`, data)
    }
  } catch (err) {
    console.log(`${colors.red}❌ 连接异常: ${err.message}${colors.reset}`)
  }
  pressEnterToContinue()
}

// ================= UI 循环与导航 =================

function displayMenu() {
  console.clear()
  console.log(`${colors.bright}=====================================================${colors.reset}`)
  console.log(`${colors.bright}    班级宠物园 (ClassPetGarden) 通用硬件接口模拟器${colors.reset}`)
  console.log(`${colors.bright}=====================================================${colors.reset}`)
  console.log(`当前绑定的模拟设备 ID (MAC): ${colors.green}${DEVICE_ID}${colors.reset}`)
  console.log(`未补送离线申报缓存深度: ${colors.yellow}${offlineQueue.length}${colors.reset}`)
  console.log(`后端 API 服务地址: ${colors.blue}${SERVER_URL}${colors.reset}`)
  console.log(`-----------------------------------------------------`)
  console.log(`[1] 查询当前绑定的学生及宠物等级/经验 (${colors.green}GET /status${colors.reset})`)
  console.log(`[2] 语音交互与申报测试 (加分/查状态) (${colors.green}POST /voice${colors.reset})`)
  console.log(`[3] 本地番茄钟专注结束 (模拟在线加分 / 离线暂存)`)
  console.log(`[4] 模拟网络故障恢复后的离线数据队列同步续传`)
  console.log(`[5] 模拟时间戳伪造与重放攻击拦截校验测试 (安全拦截)`)
  console.log(`[0] 退出模拟器`)
  console.log(`=====================================================`)
  rl.question('请选择测试场景 [0-5]: ', handleMenuInput)
}

function handleMenuInput(choice) {
  switch (choice.trim()) {
    case '1':
      testGetStatus()
      break
    case '2':
      testVoiceCommand()
      break
    case '3':
      // 询问是否模拟离线
      rl.question('是否在离线状态下专注结束？(y/N): ', (ans) => {
        const isOffline = ans.trim().toLowerCase() === 'y'
        testPomodoroFinished(isOffline)
      })
      break
    case '4':
      testOfflineReconnectionSync()
      break
    case '5':
      testReplayAttackPrevention()
      break
    case '0':
      console.log('正在退出模拟器。祝您开发愉快！')
      rl.close()
      process.exit(0)
    default:
      console.log(`${colors.red}错误: 输入无效，请输入 0 至 6 之间的数字。${colors.reset}`)
      setTimeout(displayMenu, 1500)
      break
  }
}

function pressEnterToContinue() {
  rl.question(`\n按 ${colors.bright}Enter${colors.reset} 键返回主菜单...`, () => {
    displayMenu()
  })
}

// 首次运行启动显示菜单
displayMenu()
