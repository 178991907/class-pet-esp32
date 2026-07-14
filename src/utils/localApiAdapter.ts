import type { AxiosRequestConfig, AxiosResponse } from 'axios'
import { localDb } from './localDb'

// 模拟延迟（ms）以使体验更真实
const MOCK_DELAY = 100

// 辅助函数：解析 URL 查询参数
function parseQueryParams(url: string): Record<string, string> {
  const params: Record<string, string> = {}
  const queryString = url.split('?')[1]
  if (queryString) {
    const parts = queryString.split('&')
    for (const part of parts) {
      const [key, value] = part.split('=')
      if (key) {
        params[decodeURIComponent(key)] = decodeURIComponent(value || '')
      }
    }
  }
  return params
}

// 辅助函数：解析 Authorization header 获取当前用户 id
function getUserIdFromConfig(config: AxiosRequestConfig): string {
  const authHeader = (config.headers?.Authorization || config.headers?.authorization) as string
  if (authHeader && authHeader.startsWith('Bearer ')) {
    const token = authHeader.replace('Bearer ', '')
    const user = localDb.getUserByToken(token)
    if (user) return user.id
  }
  // 缺省为游客
  return 'guest'
}

// 模拟 Axios 响应辅助函数
function createMockResponse(config: AxiosRequestConfig, status: number, data: any): Promise<AxiosResponse> {
  return new Promise((resolve, reject) => {
    setTimeout(() => {
      if (status >= 200 && status < 300) {
        resolve({
          data,
          status,
          statusText: 'OK',
          headers: {},
          config
        } as AxiosResponse)
      } else {
        reject({
          response: {
            data: data || { error: '请求失败' },
            status,
            statusText: 'Error',
            headers: {},
            config
          }
        })
      }
    }, MOCK_DELAY)
  })
}

// 自定义 Axios 适配器
export async function localApiAdapter(config: AxiosRequestConfig): Promise<AxiosResponse> {
  const method = (config.method || 'get').toLowerCase()
  // 去除 baseURL 的前缀
  let path = config.url || ''
  if (config.baseURL && path.startsWith(config.baseURL)) {
    path = path.slice(config.baseURL.length)
  }
  // 去掉前导的斜杠，以及去掉查询参数以方便路径分析
  const urlWithoutParams = path.split('?')[0].replace(/^\//, '')
  const queryParams = parseQueryParams(path)
  const body = typeof config.data === 'string' ? JSON.parse(config.data) : config.data

  const userId = getUserIdFromConfig(config)

  try {
    // === AUTH ROUTING ===
    if (urlWithoutParams === 'auth/register' && method === 'post') {
      const dbUser = localDb.register(body.username, body.password, body.inviteCode)
      if (dbUser) {
        const token = `mock-token-${dbUser.id}`
        const user = {
          id: dbUser.id,
          username: dbUser.username,
          isGuest: !!dbUser.is_guest,
          role: dbUser.role
        }
        return createMockResponse(config, 201, { success: true, token, user })
      }
      return createMockResponse(config, 400, { error: '用户名已存在' })
    }

    if (urlWithoutParams === 'auth/login' && method === 'post') {
      const res = localDb.login(body.username, body.password)
      if (res) {
        const user = {
          id: res.user.id,
          username: res.user.username,
          isGuest: !!res.user.is_guest,
          role: res.user.role || (res.user.username === 'admin' ? 'admin' : 'student')
        }
        return createMockResponse(config, 200, { success: true, token: res.token, user })
      }
      return createMockResponse(config, 400, { error: '用户名或密码错误' })
    }

    if (urlWithoutParams === 'auth/me' && method === 'get') {
      const authHeader = (config.headers?.Authorization || config.headers?.authorization) as string
      if (authHeader && authHeader.startsWith('Bearer ')) {
        const token = authHeader.replace('Bearer ', '')
        const dbUser = localDb.getUserByToken(token)
        if (dbUser) {
          const user = {
            id: dbUser.id,
            username: dbUser.username,
            isGuest: !!dbUser.is_guest,
            role: dbUser.role || (dbUser.username === 'admin' ? 'admin' : 'student')
          }
          return createMockResponse(config, 200, { user })
        }
      }
      return createMockResponse(config, 401, { error: '未授权或登录失效' })
    }

    if (urlWithoutParams === 'auth/users' && method === 'get') {
      const authHeader = (config.headers?.Authorization || config.headers?.authorization) as string
      if (!authHeader || !authHeader.startsWith('Bearer mock-token-admin')) {
        return createMockResponse(config, 403, { error: '无权操作，仅限管理员使用' })
      }
      const users = localDb.getUsers()
      return createMockResponse(config, 200, { users })
    }

    if (urlWithoutParams.startsWith('auth/users/') && urlWithoutParams.endsWith('/role') && method === 'put') {
      const authHeader = (config.headers?.Authorization || config.headers?.authorization) as string
      if (!authHeader || !authHeader.startsWith('Bearer mock-token-admin')) {
        return createMockResponse(config, 403, { error: '无权操作，仅限管理员使用' })
      }
      const targetUserId = urlWithoutParams.split('/')[2]
      const success = localDb.updateUserRole(targetUserId, body.role)
      if (success) return createMockResponse(config, 200, { success: true })
      return createMockResponse(config, 400, { error: '修改用户角色失败' })
    }

    if (urlWithoutParams === 'auth/admin/password' && method === 'put') {
      const authHeader = (config.headers?.Authorization || config.headers?.authorization) as string
      if (!authHeader || !authHeader.startsWith('Bearer mock-token-admin')) {
        return createMockResponse(config, 403, { error: '无权操作，仅限管理员使用' })
      }
      const success = localDb.updateAdminPassword(body.password)
      if (success) return createMockResponse(config, 200, { success: true })
      return createMockResponse(config, 400, { error: '修改管理员密码失败' })
    }

    // === CLASSES ROUTING ===
    if (urlWithoutParams === 'classes' && method === 'get') {
      const classes = localDb.getClasses(userId)
      return createMockResponse(config, 200, { classes })
    }

    if (urlWithoutParams === 'classes' && method === 'post') {
      const cls = localDb.createClass(userId, body.name)
      return createMockResponse(config, 201, cls)
    }

    // PUT /classes/:id 或 DELETE /classes/:id
    if (urlWithoutParams.startsWith('classes/') && !urlWithoutParams.endsWith('students')) {
      const classId = urlWithoutParams.split('/')[1]
      if (method === 'put') {
        const updated = localDb.updateClass(userId, classId, body.name)
        if (updated) return createMockResponse(config, 200, updated)
        return createMockResponse(config, 404, { error: '班级不存在或无权编辑' })
      }
      if (method === 'delete') {
        const success = localDb.deleteClass(userId, classId)
        if (success) return createMockResponse(config, 200, { success: true })
        return createMockResponse(config, 404, { error: '班级不存在或无权删除' })
      }
    }

    // GET /classes/:id/students
    if (urlWithoutParams.startsWith('classes/') && urlWithoutParams.endsWith('/students')) {
      const classId = urlWithoutParams.split('/')[1]
      const students = localDb.getStudentsByClass(classId)
      return createMockResponse(config, 200, { students })
    }

    // === STUDENTS ROUTING ===
    if (urlWithoutParams === 'students' && method === 'post') {
      const student = localDb.createStudent(body.classId, body.name, body.studentNo, body.petType, body.deviceId)
      return createMockResponse(config, 201, student)
    }

    if (urlWithoutParams.startsWith('students/') && !urlWithoutParams.endsWith('/pet') && method === 'put') {
      const studentId = urlWithoutParams.split('/')[1]
      const student = localDb.updateStudentInfo(studentId, body.name, body.studentNo, body.deviceId)
      if (student) return createMockResponse(config, 200, { success: true })
      return createMockResponse(config, 404, { error: '学生不存在' })
    }

    if (urlWithoutParams.startsWith('students/') && urlWithoutParams.endsWith('/pet') && method === 'put') {
      const studentId = urlWithoutParams.split('/')[1]
      const student = localDb.updateStudentPet(studentId, body.petType)
      if (student) return createMockResponse(config, 200, student)
      return createMockResponse(config, 404, { error: '学生不存在' })
    }

    if (urlWithoutParams.startsWith('students/') && method === 'delete') {
      const studentId = urlWithoutParams.split('/')[1]
      const success = localDb.deleteStudent(studentId)
      if (success) return createMockResponse(config, 200, { success: true })
      return createMockResponse(config, 404, { error: '学生不存在' })
    }

    if (urlWithoutParams === 'students/import' && method === 'post') {
      const list = body.students || []
      const students = localDb.importStudents(body.classId, list)
      return createMockResponse(config, 201, { success: true, count: students.length })
    }

    // === EVALUATIONS ROUTING ===
    if (urlWithoutParams === 'evaluations' && method === 'get') {
      const page = Number(queryParams.page || 1)
      const pageSize = Number(queryParams.pageSize || 20)
      const res = localDb.getEvaluations({
        classId: queryParams.classId,
        studentId: queryParams.studentId,
        page,
        pageSize
      })
      return createMockResponse(config, 200, res)
    }

    if (urlWithoutParams === 'evaluations' && method === 'post') {
      // 兼容单人评价及多人评价
      const studentIds = Array.isArray(body.studentId)
        ? body.studentId
        : body.studentIds
          ? body.studentIds
          : [body.studentId]
      const result = localDb.addEvaluation(body.classId, studentIds, body.points, body.reason, body.category)
      return createMockResponse(config, 200, result)
    }

    if (urlWithoutParams === 'evaluations/latest' && method === 'delete') {
      const classId = queryParams.classId
      if (!classId) return createMockResponse(config, 400, { error: 'classId required' })
      const undone = localDb.undoLatestEvaluation(classId)
      if (undone) return createMockResponse(config, 200, { success: true, undone })
      return createMockResponse(config, 404, { error: '未找到可撤销的评价记录' })
    }

    if (urlWithoutParams.startsWith('evaluations/') && method === 'delete') {
      const recordId = urlWithoutParams.split('/')[1]
      const undone = localDb.deleteEvaluation(recordId)
      if (undone) return createMockResponse(config, 200, { success: true, undone })
      return createMockResponse(config, 404, { error: '评价记录不存在' })
    }

    // === RULES ROUTING ===
    if (urlWithoutParams === 'rules') {
      if (method === 'get') {
        const rules = localDb.getRules()
        return createMockResponse(config, 200, { rules })
      }
      if (method === 'post') {
        const rule = localDb.createRule(body.name, body.points, body.category)
        return createMockResponse(config, 201, rule)
      }
    }

    if (urlWithoutParams.startsWith('rules/') && method === 'delete') {
      const ruleId = urlWithoutParams.split('/')[1]
      const success = localDb.deleteRule(ruleId)
      if (success) return createMockResponse(config, 200, { success: true })
      return createMockResponse(config, 404, { error: '评价规则不存在' })
    }

    // === SETTINGS & MISC ===
    if (urlWithoutParams === 'settings' && method === 'get') {
      const settings = localDb.getSettings()
      return createMockResponse(config, 200, settings)
    }

    if (urlWithoutParams === 'settings/fix-exp' && method === 'post') {
      const updated = localDb.fixExp(userId)
      return createMockResponse(config, 200, { success: true, updated })
    }

    if (urlWithoutParams.startsWith('settings/ranking/') && method === 'get') {
      const classId = urlWithoutParams.split('/').pop() || ''
      const ranking = localDb.getRanking(classId)
      return createMockResponse(config, 200, { ranking })
    }

    if (urlWithoutParams === 'backup' && method === 'get') {
      const data = localDb.backup()
      // blob 类型的模拟
      if (config.responseType === 'blob') {
        const jsonStr = JSON.stringify(data)
        const blob = new Blob([jsonStr], { type: 'application/json' })
        return createMockResponse(config, 200, blob)
      }
      return createMockResponse(config, 200, data)
    }

    if (urlWithoutParams === 'restore' && method === 'post') {
      const success = localDb.restore(body)
      if (success) return createMockResponse(config, 200, { success: true })
      return createMockResponse(config, 400, { error: '无效的备份数据' })
    }

    // === DEVICE ROUTING (通用设备与设置管理) ===
    if (urlWithoutParams === 'device/tasks' && method === 'get') {
      return createMockResponse(config, 200, { tasks: [] })
    }

    if (urlWithoutParams === 'device/settings') {
      if (method === 'get') {
        const deviceId = queryParams.device_id || ''
        const asr = localStorage.getItem('local_asr_provider') || 'workers-ai'
        const defBrightness = Number(localStorage.getItem('local_screen_brightness') || 80)
        const defSleep = Number(localStorage.getItem('local_screen_sleep_seconds') || 15)
        let brightness = defBrightness
        let sleep = defSleep
        let devAsr = asr
        if (deviceId) {
          const ds = JSON.parse(localStorage.getItem(`local_device_settings_${deviceId}`) || '{}')
          if (ds.screen_brightness !== undefined) brightness = ds.screen_brightness
          if (ds.screen_sleep_seconds !== undefined) sleep = ds.screen_sleep_seconds
          if (ds.asr_provider !== undefined) devAsr = ds.asr_provider
        }
        return createMockResponse(config, 200, {
          screen_brightness: brightness,
          screen_sleep_seconds: sleep,
          asr_provider: devAsr
        })
      }
      if (method === 'post') {
        const deviceId = queryParams.device_id || ''
        if (deviceId) {
          const key = `local_device_settings_${deviceId}`
          const ds = JSON.parse(localStorage.getItem(key) || '{}')
          if (body.screen_brightness !== undefined) ds.screen_brightness = body.screen_brightness
          if (body.screen_sleep_seconds !== undefined) ds.screen_sleep_seconds = body.screen_sleep_seconds
          if (body.asr_provider !== undefined) ds.asr_provider = body.asr_provider
          localStorage.setItem(key, JSON.stringify(ds))
        } else {
          if (body.screen_brightness !== undefined) localStorage.setItem('local_screen_brightness', String(body.screen_brightness))
          if (body.screen_sleep_seconds !== undefined) localStorage.setItem('local_screen_sleep_seconds', String(body.screen_sleep_seconds))
          if (body.asr_provider !== undefined) localStorage.setItem('local_asr_provider', String(body.asr_provider))
        }
        return createMockResponse(config, 200, { success: true })
      }
    }

    // 全局平台设置 (本地模式)
    if (urlWithoutParams === 'system/settings') {
      if (method === 'get') {
        return createMockResponse(config, 200, {
          task_confirm_mode: localStorage.getItem('local_task_confirm_mode') || 'auto',
          task_confirm_delay: Number(localStorage.getItem('local_task_confirm_delay') || 30),
          openrouter_api_key: localStorage.getItem('local_openrouter_api_key') || '',
          openrouter_model: localStorage.getItem('local_openrouter_model') || 'openrouter/free',
          groq_api_key: localStorage.getItem('local_groq_api_key') || '',
          baidu_api_key: localStorage.getItem('local_baidu_api_key') || '',
          baidu_secret_key: localStorage.getItem('local_baidu_secret_key') || '',
          openai_api_key: localStorage.getItem('local_openai_api_key') || '',
          asr_provider: localStorage.getItem('local_asr_provider') || 'workers-ai',
          firmware_latest_version: localStorage.getItem('local_firmware_latest_version') || '2.1.0',
          firmware_download_url: localStorage.getItem('local_firmware_download_url') || '/firmware/latest.bin',
          firmware_checksum: localStorage.getItem('local_firmware_checksum') || 'dummy'
        })
      }
      if (method === 'post') {
        const map: Record<string, string> = {
          task_confirm_mode: 'local_task_confirm_mode',
          task_confirm_delay: 'local_task_confirm_delay',
          openrouter_api_key: 'local_openrouter_api_key',
          openrouter_model: 'local_openrouter_model',
          groq_api_key: 'local_groq_api_key',
          baidu_api_key: 'local_baidu_api_key',
          baidu_secret_key: 'local_baidu_secret_key',
          openai_api_key: 'local_openai_api_key',
          asr_provider: 'local_asr_provider',
          firmware_latest_version: 'local_firmware_latest_version',
          firmware_download_url: 'local_firmware_download_url',
          firmware_checksum: 'local_firmware_checksum'
        }
        for (const [k, v] of Object.entries(body || {})) {
          if (map[k] !== undefined) localStorage.setItem(map[k], String(v))
        }
        return createMockResponse(config, 200, { success: true })
      }
    }

    // ===== DEVICES (本地模式) =====
    const getLocalDevices = (): any[] => JSON.parse(localStorage.getItem('local_devices') || '[]')
    const setLocalDevices = (d: any[]) => localStorage.setItem('local_devices', JSON.stringify(d))

    if (urlWithoutParams === 'devices' && method === 'get') {
      return createMockResponse(config, 200, { success: true, devices: getLocalDevices() })
    }
    if (urlWithoutParams === 'devices/register' && method === 'post') {
      const devs = getLocalDevices()
      if (!devs.find(d => d.device_id === body.device_id)) {
        devs.push({
          device_id: body.device_id,
          name: body.name || body.device_id,
          student_id: null,
          class_id: body.class_id || null,
          battery_level: 100,
          is_charging: 0,
          last_seen: Date.now(),
          firmware_version: localStorage.getItem('local_firmware_latest_version') || '2.1.0'
        })
        setLocalDevices(devs)
      }
      return createMockResponse(config, 200, { success: true, device_id: body.device_id })
    }
    if (urlWithoutParams === 'devices/unbound' && method === 'get') {
      return createMockResponse(config, 200, { success: true, devices: getLocalDevices().filter(d => !d.student_id) })
    }
    if (urlWithoutParams.startsWith('devices/') && method === 'get') {
      const id = decodeURIComponent(urlWithoutParams.slice('devices/'.length))
      const dev = getLocalDevices().find(d => d.device_id === id)
      if (!dev) return createMockResponse(config, 404, { error: '设备不存在' })
      const ds = JSON.parse(localStorage.getItem(`local_device_settings_${id}`) || '{}')
      return createMockResponse(config, 200, {
        success: true,
        device: dev,
        settings: {
          screen_brightness: ds.screen_brightness ?? 80,
          screen_sleep_seconds: ds.screen_sleep_seconds ?? 15,
          asr_provider: ds.asr_provider ?? 'workers-ai'
        }
      })
    }
    if (urlWithoutParams.startsWith('devices/') && method === 'put') {
      const id = decodeURIComponent(urlWithoutParams.slice('devices/'.length))
      const devs = getLocalDevices()
      const dev = devs.find(d => d.device_id === id)
      if (!dev) return createMockResponse(config, 404, { error: '设备不存在' })
      if (body.student_id !== undefined) dev.student_id = body.student_id || null
      if (body.name !== undefined) dev.name = body.name
      if (body.class_id !== undefined) dev.class_id = body.class_id
      setLocalDevices(devs)
      return createMockResponse(config, 200, { success: true })
    }
    if (urlWithoutParams.startsWith('devices/') && method === 'delete') {
      const id = decodeURIComponent(urlWithoutParams.slice('devices/'.length))
      setLocalDevices(getLocalDevices().filter(d => d.device_id !== id))
      return createMockResponse(config, 200, { success: true })
    }

    // 任务审批/拒绝（本地模式下直接返回成功）
    if (urlWithoutParams.startsWith('device/tasks/') && urlWithoutParams.endsWith('/approve') && method === 'post') {
      return createMockResponse(config, 200, { success: true, message: '已审批通过' })
    }

    if (urlWithoutParams.startsWith('device/tasks/') && urlWithoutParams.endsWith('/reject') && method === 'post') {
      return createMockResponse(config, 200, { success: true, message: '已拒绝' })
    }

    // === CHAT ROUTING (Web 语音伙伴 /chat) ===
    if (urlWithoutParams === 'chat' && method === 'post') {
      const msg = (body?.message || '').trim()
      const name = body?.studentName || '小朋友'
      let reply: string
      if (/你好|您好|hi|hello|在吗/i.test(msg)) {
        reply = `你好呀${name}！我是你的宠物小搭档，今天过得开心吗？😊`
      } else if (/谢谢|感谢|多谢/i.test(msg)) {
        reply = `不客气${name}～能陪你聊天我超开心的！🐾`
      } else if (/作业|学习|考试|背书|默写/i.test(msg)) {
        reply = `学习辛苦啦${name}！认真完成任务的话，宠物会悄悄涨成长值的，加油加油！📚✨`
      } else if (/\?|？|吗|怎么|为什么|什么/i.test(msg)) {
        reply = `这个问题真有意思${name}！让我想想……我觉得你可以先试试看，遇到难题随时来找我呀～🤔`
      } else if (msg.length === 0) {
        reply = `嗯？我好像没听清${name}，再跟我说说好不好～👂`
      } else {
        reply = `我听到啦${name}：「${msg.slice(0, 30)}」～你说得真棒，我记在心里啦！要不要给宠物加点成长值？🌟`
      }
      return createMockResponse(config, 200, { success: true, reply })
    }

    // === 宠物主人记忆 / 日历 / 清单 / 闹铃（学生维度管理功能） ===
    // localStorage 辅助（按学生隔离）
    const lsGet = (k: string, fallback: any) => {
      try { const v = localStorage.getItem(k); return v ? JSON.parse(v) : fallback } catch { return fallback }
    }
    const lsSet = (k: string, v: any) => { localStorage.setItem(k, JSON.stringify(v)) }
    const genId = () => 'mock-' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6)

    // ---- 日历 ----
    let m = urlWithoutParams.match(/^students\/([^/]+)\/calendar\/([^/]+)$/)
    if (m && method === 'delete') {
      const sid = m[1]
      const cid = m[2]
      const arr = lsGet(`cp_calendar_${sid}`, [])
      lsSet(`cp_calendar_${sid}`, arr.filter((e: any) => e.id !== cid))
      return createMockResponse(config, 200, { success: true })
    }
    m = urlWithoutParams.match(/^students\/([^/]+)\/calendar$/)
    if (m) {
      const sid = m[1]
      if (method === 'get') {
        return createMockResponse(config, 200, { success: true, events: lsGet(`cp_calendar_${sid}`, []) })
      }
      if (method === 'post') {
        if (!body.title || !body.event_date) return createMockResponse(config, 400, { error: '缺少 title 或 event_date' })
        const ev = {
          id: genId(),
          student_id: sid,
          title: String(body.title).slice(0, 80),
          event_date: String(body.event_date).slice(0, 10),
          time_str: body.time_str ? String(body.time_str).slice(0, 5) : null,
          description: body.description ? String(body.description).slice(0, 200) : '',
          created_at: Date.now()
        }
        const arr = lsGet(`cp_calendar_${sid}`, [])
        arr.push(ev)
        lsSet(`cp_calendar_${sid}`, arr)
        return createMockResponse(config, 200, { success: true, id: ev.id })
      }
    }

    // ---- 清单 ----
    m = urlWithoutParams.match(/^students\/([^/]+)\/checklist\/([^/]+)$/)
    if (m) {
      const sid = m[1]
      const cid = m[2]
      if (method === 'put') {
        const arr = lsGet(`cp_checklist_${sid}`, [])
        const item = arr.find((e: any) => e.id === cid)
        if (item) { item.is_done = body.is_done ? 1 : 0; lsSet(`cp_checklist_${sid}`, arr) }
        return createMockResponse(config, 200, { success: true })
      }
      if (method === 'delete') {
        const arr = lsGet(`cp_checklist_${sid}`, [])
        lsSet(`cp_checklist_${sid}`, arr.filter((e: any) => e.id !== cid))
        return createMockResponse(config, 200, { success: true })
      }
    }
    m = urlWithoutParams.match(/^students\/([^/]+)\/checklist$/)
    if (m) {
      const sid = m[1]
      if (method === 'get') {
        return createMockResponse(config, 200, { success: true, items: lsGet(`cp_checklist_${sid}`, []) })
      }
      if (method === 'post') {
        if (!body.content) return createMockResponse(config, 400, { error: '缺少 content' })
        const item = {
          id: genId(),
          student_id: sid,
          content: String(body.content).slice(0, 120),
          is_done: 0,
          created_at: Date.now()
        }
        const arr = lsGet(`cp_checklist_${sid}`, [])
        arr.push(item)
        lsSet(`cp_checklist_${sid}`, arr)
        return createMockResponse(config, 200, { success: true, id: item.id })
      }
    }

    // ---- 闹铃 / 定时 ----
    m = urlWithoutParams.match(/^device\/schedules\/student\/([^/]+)$/)
    if (m) {
      const sid = m[1]
      if (method === 'get') {
        return createMockResponse(config, 200, { success: true, schedules: lsGet(`cp_schedules_${sid}`, []) })
      }
      if (method === 'post') {
        if (!body.day_of_week || !body.time_str || !body.task_desc) return createMockResponse(config, 400, { error: '参数缺失' })
        const s = {
          id: genId(),
          student_id: sid,
          day_of_week: Number(body.day_of_week),
          time_str: String(body.time_str).slice(0, 5),
          task_desc: String(body.task_desc).slice(0, 120),
          is_active: 1,
          created_at: Date.now()
        }
        const arr = lsGet(`cp_schedules_${sid}`, [])
        arr.push(s)
        lsSet(`cp_schedules_${sid}`, arr)
        return createMockResponse(config, 200, { success: true, id: s.id })
      }
    }
    m = urlWithoutParams.match(/^device\/schedules\/([^/]+)$/)
    if (m && method === 'delete') {
      const cid = m[1]
      // 需要找到所属学生：遍历所有 cp_schedules_* 删除该 id
      const keys = Object.keys(localStorage).filter(k => k.startsWith('cp_schedules_'))
      for (const k of keys) {
        const arr = lsGet(k, [])
        const filtered = arr.filter((e: any) => e.id !== cid)
        if (filtered.length !== arr.length) { lsSet(k, filtered); break }
      }
      return createMockResponse(config, 200, { success: true })
    }

    // ---- 宠物主人记忆 ----
    m = urlWithoutParams.match(/^students\/([^/]+)\/owner-profile$/)
    if (m) {
      const sid = m[1]
      const key = `cp_owner_${sid}`
      if (method === 'get') {
        const p = lsGet(key, { profile: null, emotion_log: [], learning_log: [] })
        return createMockResponse(config, 200, { success: true, profile: p.profile || null, emotion_log: p.emotion_log || [], learning_log: p.learning_log || [] })
      }
      if (method === 'post') {
        const p = lsGet(key, { profile: null, emotion_log: [], learning_log: [] })
        const now = Date.now()
        if (body.type === 'profile' && body.data) {
          p.profile = body.data
        } else if (body.type === 'emotion' && body.data) {
          p.emotion_log = p.emotion_log || []
          p.emotion_log.push({ ts: now, ...body.data })
          if (p.emotion_log.length > 100) p.emotion_log = p.emotion_log.slice(-100)
        } else if (body.type === 'learning' && body.data) {
          p.learning_log = p.learning_log || []
          p.learning_log.push({ ts: now, ...body.data })
          if (p.learning_log.length > 100) p.learning_log = p.learning_log.slice(-100)
        } else {
          return createMockResponse(config, 400, { error: '未知 type' })
        }
        lsSet(key, p)
        return createMockResponse(config, 200, { success: true })
      }
    }

    // ---- 设备状态（设置页用） ----
    if (urlWithoutParams === 'device/status' && method === 'get') {
      const deviceId = queryParams.device_id || '28:84:85:42:1C:74'
      return createMockResponse(config, 200, {
        status: 'ok',
        student: {
          id: 'mock-device',
          name: '本机设备',
          device_id: deviceId,
          battery_level: 88,
          is_charging: 0,
          last_seen: Date.now()
        }
      })
    }

    // ---- 固件版本（设置页用） ----
    if (urlWithoutParams === 'device/firmware-version' && method === 'get') {
      return createMockResponse(config, 200, {
        latest_version: '2.1.0',
        download_url: '/firmware/latest.bin',
        checksum: 'mock'
      })
    }

    // 默认回包
    return createMockResponse(config, 404, { error: `未匹配到模拟路由: ${method} ${path}` })
  } catch (error: any) {
    console.error('Local API Adapter Error:', error)
    return createMockResponse(config, 500, { error: error.message || '内部模拟服务错误' })
  }
}
