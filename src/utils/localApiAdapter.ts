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
        const mode = localStorage.getItem('local_task_confirm_mode') || 'auto'
        const delay = Number(localStorage.getItem('local_task_confirm_delay') || 30)
        return createMockResponse(config, 200, {
          task_confirm_mode: mode,
          task_confirm_delay: delay
        })
      }
      if (method === 'post') {
        localStorage.setItem('local_task_confirm_mode', body.task_confirm_mode)
        localStorage.setItem('local_task_confirm_delay', String(body.task_confirm_delay))
        return createMockResponse(config, 200, { success: true })
      }
    }

    // 任务审批/拒绝（本地模式下直接返回成功）
    if (urlWithoutParams.startsWith('device/tasks/') && urlWithoutParams.endsWith('/approve') && method === 'post') {
      return createMockResponse(config, 200, { success: true, message: '已审批通过' })
    }

    if (urlWithoutParams.startsWith('device/tasks/') && urlWithoutParams.endsWith('/reject') && method === 'post') {
      return createMockResponse(config, 200, { success: true, message: '已拒绝' })
    }

    // 默认回包
    return createMockResponse(config, 404, { error: `未匹配到模拟路由: ${method} ${path}` })
  } catch (error: any) {
    console.error('Local API Adapter Error:', error)
    return createMockResponse(config, 500, { error: error.message || '内部模拟服务错误' })
  }
}
