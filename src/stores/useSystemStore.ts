import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'

export const useSystemStore = defineStore('system', () => {
  const { api } = useAuth()
  const toast = useToast()

  // 系统配置
  const pendingTasks = ref<any[]>([])
  const taskConfirmMode = ref('auto')
  const taskConfirmDelay = ref(30)
  const openrouterApiKey = ref('')
  const openrouterModel = ref('openrouter/free')
  const groqApiKey = ref('')
  const screenBrightness = ref(80)
  const screenSleepSeconds = ref(15)
  const asrProvider = ref('openrouter')
  const baiduApiKey = ref('')
  const baiduSecretKey = ref('')

  // 用户权限管理
  const usersList = ref<any[]>([])
  const loadingUsers = ref(false)

  // 聊天审计
  const chatLogsList = ref<any[]>([])
  const loadingChatLogs = ref(false)
  const auditingStudent = ref<any>(null)

  // 日程管理
  const schedulesList = ref<any[]>([])
  const loadingSchedules = ref(false)
  const schedulingStudent = ref<any>(null)

  // 系统配置加载
  async function loadSystemData() {
    try {
      const tasksRes = await api.get('/device/tasks?status=pending')
      pendingTasks.value = tasksRes.data.tasks

      const settingsRes = await api.get('/device/settings')
      taskConfirmMode.value = settingsRes.data.task_confirm_mode
      taskConfirmDelay.value = settingsRes.data.task_confirm_delay
      openrouterApiKey.value = settingsRes.data.openrouter_api_key || ''
      openrouterModel.value = settingsRes.data.openrouter_model || 'openrouter/free'
      screenBrightness.value = settingsRes.data.screen_brightness ?? 80
      screenSleepSeconds.value = settingsRes.data.screen_sleep_seconds ?? 15
      asrProvider.value = settingsRes.data.asr_provider || 'groq'
      baiduApiKey.value = settingsRes.data.baidu_api_key || ''
      baiduSecretKey.value = settingsRes.data.baidu_secret_key || ''
      groqApiKey.value = settingsRes.data.groq_api_key || ''
    } catch (err) {
      console.error('加载系统配置失败:', err)
    }
  }

  async function approveTask(taskId: string) {
    try {
      await api.post(`/device/tasks/${taskId}/approve`)
      toast.success('审批成功，积分已到账！')
      await loadSystemData()
    } catch (err) {
      toast.error('审批操作失败，请重试')
    }
  }

  async function rejectTask(taskId: string) {
    try {
      await api.post(`/device/tasks/${taskId}/reject`)
      toast.success('已拒绝该加分申请')
      await loadSystemData()
    } catch (err) {
      toast.error('操作失败')
    }
  }

  async function saveSystemSettings() {
    try {
      await api.post('/device/settings', {
        task_confirm_mode: taskConfirmMode.value,
        task_confirm_delay: Number(taskConfirmDelay.value),
        openrouter_api_key: openrouterApiKey.value,
        openrouter_model: openrouterModel.value,
        screen_brightness: Number(screenBrightness.value),
        screen_sleep_seconds: Number(screenSleepSeconds.value),
        asr_provider: asrProvider.value,
        baidu_api_key: baiduApiKey.value,
        baidu_secret_key: baiduSecretKey.value,
        groq_api_key: groqApiKey.value
      })
      toast.success('系统设置保存成功！')
    } catch (err) {
      toast.error('设置保存失败')
    }
  }

  // 用户权限管理
  async function loadUsersList() {
    loadingUsers.value = true
    try {
      const res = await api.get('/auth/users')
      usersList.value = res.data.users || []
    } catch (err) {
      console.error('获取用户列表失败:', err)
      toast.error('加载用户列表失败')
    } finally {
      loadingUsers.value = false
    }
  }

  async function handleUpdateUserRole(userId: string, role: string) {
    try {
      await api.put(`/auth/users/${userId}/role`, { role })
      toast.success('用户权限修改成功')
      loadUsersList()
    } catch (err: any) {
      toast.error(err.response?.data?.error || '修改用户权限失败')
      loadUsersList()
    }
  }

  function copyTeacherInviteLink() {
    const link = `${window.location.origin}${window.location.pathname}?code=TEACHER_INVITE`
    navigator.clipboard.writeText(link)
      .then(() => toast.success('教师注册邀请链接已复制到剪贴板！'))
      .catch(() => toast.error('复制失败，请手动复制邀请码：TEACHER_INVITE'))
  }

  async function handleUpdateAdminPassword(newPassword: string, confirmPassword: string) {
    if (!newPassword || !confirmPassword) {
      toast.error('密码不能为空')
      return false
    }
    if (newPassword.length < 6) {
      toast.error('密码长度至少为 6 位')
      return false
    }
    if (newPassword !== confirmPassword) {
      toast.error('两次密码输入不一致')
      return false
    }
    try {
      await api.put('/auth/admin/password', { password: newPassword })
      toast.success('管理员密码修改成功！请妥善保存新密码')
      return true
    } catch (err: any) {
      toast.error(err.response?.data?.error || '修改密码失败，请重试')
      return false
    }
  }

  // 日程管理
  async function openSchedules(student: any) {
    schedulingStudent.value = student
    loadingSchedules.value = true
    try {
      const res = await api.get(`/device/schedules/student/${student.id}`)
      schedulesList.value = res.data.schedules || []
    } catch (err) {
      toast.error('加载日程失败')
    } finally {
      loadingSchedules.value = false
    }
  }

  async function addSchedule(dayOfWeek: number, timeStr: string, taskDesc: string) {
    if (!schedulingStudent.value) return
    if (!taskDesc.trim()) {
      toast.error('请输入日程提醒内容')
      return false
    }
    try {
      await api.post(`/device/schedules/student/${schedulingStudent.value.id}`, {
        day_of_week: dayOfWeek,
        time_str: timeStr,
        task_desc: taskDesc.trim()
      })
      toast.success('日程添加成功！')
      const res = await api.get(`/device/schedules/student/${schedulingStudent.value.id}`)
      schedulesList.value = res.data.schedules || []
      return true
    } catch (err) {
      toast.error('添加日程失败')
      return false
    }
  }

  async function deleteSchedule(id: string) {
    try {
      await api.delete(`/device/schedules/${id}`)
      toast.success('日程已删除')
      if (schedulingStudent.value) {
        const res = await api.get(`/device/schedules/student/${schedulingStudent.value.id}`)
        schedulesList.value = res.data.schedules || []
      }
    } catch (err) {
      toast.error('删除日程失败')
    }
  }

  // 聊天审计
  async function openChatLogs(student: any) {
    auditingStudent.value = student
    loadingChatLogs.value = true
    try {
      const res = await api.get(`/device/chat-logs/student/${student.id}`)
      chatLogsList.value = res.data.logs || []
    } catch (err) {
      toast.error('加载对话记录失败')
    } finally {
      loadingChatLogs.value = false
    }
  }

  return {
    // 系统配置
    pendingTasks,
    taskConfirmMode,
    taskConfirmDelay,
    openrouterApiKey,
    openrouterModel,
    groqApiKey,
    screenBrightness,
    screenSleepSeconds,
    asrProvider,
    baiduApiKey,
    baiduSecretKey,
    // 用户管理
    usersList,
    loadingUsers,
    // 聊天审计
    chatLogsList,
    loadingChatLogs,
    auditingStudent,
    // 日程管理
    schedulesList,
    loadingSchedules,
    schedulingStudent,
    // 操作
    loadSystemData,
    approveTask,
    rejectTask,
    saveSystemSettings,
    loadUsersList,
    handleUpdateUserRole,
    copyTeacherInviteLink,
    handleUpdateAdminPassword,
    openSchedules,
    addSchedule,
    deleteSchedule,
    openChatLogs
  }
})
