/**
 * @file auto_test_device.js
 * @brief 班级宠物园通用硬件 API - 自动化集成测试脚本
 * @note 用于测试签名机制、防重放校验、自动确认懒加载、意图分类与音乐源熔断等核心 API 的正确性
 */

import Database from 'better-sqlite3'
import crypto from 'crypto'

const SERVER_URL = 'http://localhost:3002'
const DEVICE_ID = 'AA:BB:CC:DD:11:22'
const DEVICE_SECRET = 'class-pet-device-secret'
const DB_PATH = './server/pet-garden.db'

// 测试运行时需要的一些临时 IDs
let testStudentId = 'test-student-uuid-123'
let testClassId = 'test-class-uuid-123'
let testUserId = 'test-user-uuid-123'
let testMusicSourceId = 'test-music-source-uuid-123'

// 辅助工具：计算签名
function calculateSignature(timestamp, body = '') {
  const data = `${DEVICE_ID}${timestamp}${body}`
  return crypto.createHmac('sha256', DEVICE_SECRET).update(data).digest('hex')
}

// 辅助工具：发起带签名的 HTTP 请求
async function sendRequest(path, method = 'GET', body = null, forceTimestamp = null) {
  const url = `${SERVER_URL}${path}`
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

  const res = await fetch(url, options)
  const status = res.status
  let data
  try {
    data = await res.json()
  } catch {
    data = { text: await res.text() }
  }

  return { status, data }
}

// ================= 第一步: 初始化测试数据环境 =================
function setupTestData() {
  cleanupTestData() // 先行彻底清空残留数据
  
  console.log('📦 正在连接 SQLite 准备测试数据...')
  const db = new Database(DB_PATH)

  // 1. 确保有一个测试教师用户
  db.prepare(`
    INSERT OR IGNORE INTO users (id, username, password_hash, is_guest, created_at)
    VALUES (?, 'test_teacher', 'dummy_hash', 0, ?)
  `).run(testUserId, Date.now())

  // 获取真实的用户 ID，如果是 guest 用户也可以
  const user = db.prepare("SELECT id FROM users WHERE username = 'test_teacher'").get()
  testUserId = user.id

  // 2. 确保有一个测试班级
  db.prepare(`
    INSERT OR IGNORE INTO classes (id, name, user_id, created_at)
    VALUES (?, '模拟测试班', ?, ?)
  `).run(testClassId, testUserId, Date.now())

  // 3. 确保有一个测试学生，并绑定 DEVICE_ID
  // 先把其他占用了该设备ID的记录清除
  db.prepare("UPDATE students SET device_id = NULL WHERE device_id = ?").run(DEVICE_ID)
  
  db.prepare(`
    INSERT OR IGNORE INTO students (id, class_id, name, total_points, pet_type, pet_level, pet_exp, device_id, created_at)
    VALUES (?, ?, '小明', 10, 'dragon', 1, 10, ?, ?)
  `).run(testStudentId, testClassId, DEVICE_ID, Date.now())
  
  // 强制确保该学生的设备 ID 处于绑定状态
  db.prepare("UPDATE students SET device_id = ? WHERE id = ?").run(DEVICE_ID, testStudentId)

  // 4. 确保初始化了任务审批延迟设置
  db.prepare("INSERT OR IGNORE INTO settings (key, value) VALUES ('task_confirm_mode', '\"auto\"')").run()
  db.prepare("INSERT OR IGNORE INTO settings (key, value) VALUES ('task_confirm_delay', '30')").run()

  // 5. 插入一条测试音乐源，用来执行 Failover 熔断测试
  db.prepare("DELETE FROM music_sources WHERE id = ?").run(testMusicSourceId)
  db.prepare(`
    INSERT INTO music_sources (id, name, script_code, priority, is_enabled, failure_count, created_at)
    VALUES (?, '测试故障音源', 'throw new Error("模拟音源出错");', 10, 1, 0, ?)
  `).run(testMusicSourceId, Date.now())

  db.close()
  console.log('✅ 测试环境初始化成功。学生:小明 (已绑定设备:AA:BB:CC:DD:11:22)。')
}

// ================= 第二步: 执行自动化测试集 =================
async function runTests() {
  console.log('\n🚀 开始运行 API 自动化集成测试...\n')
  let successCount = 0
  let totalCount = 0

  function assert(condition, message) {
    totalCount++
    if (condition) {
      successCount++
      console.log(`   [${successCount}/${totalCount}] ✅ ${message}`)
    } else {
      console.error(`   [ERROR] ❌ 测试断言失败: ${message}`)
      process.exit(1)
    }
  }

  // ---------------- 测试 1: 获取绑定状态 ----------------
  console.log('1. 开始测试: 绑定状态查询接口 (GET /api/device/status)...')
  const statusRes = await sendRequest(`/api/device/status?device_id=${DEVICE_ID}`, 'GET')
  assert(statusRes.status === 200, 'HTTP 状态码应为 200')
  assert(statusRes.data.status === 'ok', '设备状态应该为 ok')
  assert(statusRes.data.student.name === '小明', '绑定的学生姓名应该是 小明')
  assert(statusRes.data.student.pet_type === 'dragon', '宠物品种应该是 dragon')
  console.log('--- 测试 1 完成 ---\n')

  // ---------------- 测试 2: 防重放安全拦截 ----------------
  console.log('2. 开始测试: 防重放安全拦截机制...')
  // 模拟发送 5 分钟前（300000毫秒）的时间戳请求
  const expiredTimestamp = Date.now() - 300000
  const replayRes = await sendRequest(`/api/device/status?device_id=${DEVICE_ID}`, 'GET', null, expiredTimestamp)
  assert(replayRes.status === 403, '过期请求状态码应被拦截并返回 403')
  assert(replayRes.data.error.includes('expired') || replayRes.data.error.includes('anti-replay'), '错误信息应提示请求过期')
  console.log('--- 测试 2 完成 ---\n')

  // ---------------- 测试 3: 语音核销申报与分类意图 ----------------
  console.log('3. 开始测试: 语音文本核销与规则自动分配 (POST /api/device/voice)...')
  const voiceRes = await sendRequest('/api/device/voice', 'POST', { text: '我完成了认真打扫卫生' })
  assert(voiceRes.status === 200, 'HTTP 状态码应为 200')
  assert(voiceRes.data.action === 'apply_task', '意图应该匹配为 apply_task')
  assert(voiceRes.data.reply_text.includes('已为您申报'), '返回文本应包含“已为您申报”')
  assert(voiceRes.data.points === 1, '根据默认规则“认真完成包干区值日/打扫卫生”应自动检索为 1 分')
  const savedTaskId = voiceRes.data.task_id
  assert(!!savedTaskId, '返回中应该附带 task_id')
  console.log('--- 测试 3 完成 ---\n')

  // ---------------- 测试 4: 自动审批懒加载机制 ----------------
  console.log('4. 开始测试: 自动审批懒加载结算校验...')
  
  // 我们通过修改数据库，将刚刚申报的任务的 auto_confirm_at 设为过去的时间，使其呈现“过期未审核”状态
  const db = new Database(DB_PATH)
  db.prepare("UPDATE student_task_applications SET auto_confirm_at = ? WHERE id = ?")
    .run(Date.now() - 10000, savedTaskId) // 设为 10 秒前已过期
  
  // 检查一下数据库当前分值 (小明此时是 10 分)
  const beforePoints = db.prepare("SELECT total_points FROM students WHERE id = ?").get(testStudentId).total_points
  assert(beforePoints === 10, '懒加载前，小明的积分应该保持为初始 10 分')
  db.close()

  console.log('   ℹ️ 正在发起新请求，触发后端 autoConfirmLazyLoad 懒加载核销...')
  const triggerRes = await sendRequest(`/api/device/status?device_id=${DEVICE_ID}`, 'GET')
  assert(triggerRes.status === 200, '状态码应为 200')
  assert(triggerRes.data.student.total_points === 11, '自动核销通过，小明的积分应累加 1 分变成 11 分')

  // 验证数据库状态是否已经标记为 approved
  const db2 = new Database(DB_PATH)
  const taskStatus = db2.prepare("SELECT status FROM student_task_applications WHERE id = ?").get(savedTaskId).status
  assert(taskStatus === 'approved', '申报状态应被懒加载核销更改为 approved')
  db2.close()
  console.log('--- 测试 4 完成 ---\n')

  // ---------------- 测试 5: 音乐 Failover 故障熔断机制 ----------------
  console.log('5. 开始测试: 音乐解析 Failover 降级与熔断机制...')
  
  // 强制向一个会出错的音乐源发送 3 次搜索，引发连续故障以验证熔断
  console.log('   ℹ️ 发送 3 次异常音乐搜索，触发自动熔断...')
  for (let i = 0; i < 3; i++) {
    await sendRequest('/api/device/music/search?keyword=test-failure')
  }

  // 数据库检查该音源的 failure_count 和熔断状态
  const db3 = new Database(DB_PATH)
  const src = db3.prepare("SELECT failure_count FROM music_sources WHERE id = ?").get(testMusicSourceId)
  assert(src.failure_count >= 3, '连续故障后，该测试音源的失败计数应达到并大于 3')
  db3.close()

  // 此时再请求搜索，它应该由于熔断被过滤掉
  const finalSearch = await sendRequest('/api/device/music/search?keyword=test-failure')
  assert(finalSearch.status === 503 || finalSearch.status === 502, '无可用未熔断音源时应返回 502 或 503')
  console.log('--- 测试 5 完成 ---\n')

  // ---------------- 测试 6: 通用固件 OTA 版本检查 ----------------
  console.log('6. 开始测试: 通用固件 OTA 版本及链接返回 (GET /api/device/firmware-version)...')
  const fwRes = await sendRequest(`/api/device/firmware-version?device_id=${DEVICE_ID}`, 'GET')
  assert(fwRes.status === 200, 'HTTP 状态码应为 200')
  assert(fwRes.data.latest_version === '2.0.0', '最新版本号应该是 2.0.0')
  assert(fwRes.data.download_url.includes('/firmware/latest.bin'), '固件下载链接应匹配 /firmware/latest.bin')
  assert(fwRes.data.checksum === 'dummy_checksum_sha256', '校验和应为 dummy_checksum_sha256')
  console.log('--- 测试 6 完成 ---\n')

  // ================= 第三步: 清理测试垃圾数据 =================
  cleanupTestData()

  console.log('==================================================')
  console.log(`🎉 恭喜！自动化集成测试全部通过！共计成功: [${successCount}/${totalCount}]`)
  console.log('==================================================')
}

function cleanupTestData() {
  console.log('🧹 正在清理临时测试数据...')
  const db = new Database(DB_PATH)
  db.prepare("DELETE FROM student_task_applications WHERE student_id = ?").run(testStudentId)
  db.prepare("DELETE FROM evaluation_records WHERE student_id = ?").run(testStudentId)
  db.prepare("DELETE FROM badges WHERE student_id = ?").run(testStudentId)
  db.prepare("DELETE FROM students WHERE id = ?").run(testStudentId)
  db.prepare("DELETE FROM classes WHERE id = ?").run(testClassId)
  db.prepare("DELETE FROM music_sources WHERE id = ?").run(testMusicSourceId)
  db.close()
  console.log('🧹 垃圾清理完成。')
}

// 启动测试
setupTestData()
runTests()
