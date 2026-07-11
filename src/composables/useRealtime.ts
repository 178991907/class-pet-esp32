import { ref, onUnmounted } from 'vue'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'
import { useStudentStore } from '@/stores/useStudentStore'
import { useClassStore } from '@/stores/useClassStore'

export interface RealtimeEvent {
  type: string
  payload: any
  timestamp: number
}

const REFRESH_DEBOUNCE = 600

/**
 * 班级宠物园 · Web 实时刷新 (P3)
 *
 * - 云端模式 (配置了 VITE_API_URL)：通过 EventSource 订阅服务端 SSE 端点 /api/events，
 *   实时接收 evaluation / levelup / voice_session 事件，自动刷新学生数据并提示老师。
 * - 本地模式 (未配置云端，使用 mock 数据)：无真实服务端，启用「设备活动模拟器」，
 *   周期性模拟某台设备完成一次语音互动并真实写入评价，从而演示实时刷新效果。
 */
export function useRealtime() {
  const { api } = useAuth()
  const toast = useToast()
  const studentStore = useStudentStore()
  const classStore = useClassStore()

  const connected = ref(false)
  const realtimeLog = ref<RealtimeEvent[]>([])
  const lastEvent = ref<RealtimeEvent | null>(null)
  const simulated = ref(false)

  let es: EventSource | null = null
  let mockTimer: ReturnType<typeof setInterval> | null = null
  let refreshTimer: ReturnType<typeof setTimeout> | null = null

  function pushLog(ev: RealtimeEvent) {
    lastEvent.value = ev
    realtimeLog.value.unshift(ev)
    if (realtimeLog.value.length > 40) realtimeLog.value.pop()
  }

  function scheduleRefresh() {
    if (refreshTimer) clearTimeout(refreshTimer)
    refreshTimer = setTimeout(() => {
      studentStore.loadStudents().catch(() => {})
    }, REFRESH_DEBOUNCE)
  }

  function toastForEvent(ev: RealtimeEvent) {
    const p = ev.payload || {}
    if (ev.type === 'evaluation') {
      const s = studentStore.students.find((x) => x.id === p.studentId)
      const name = s?.name || p.studentName || '学生'
      toast.show({
        message: `${name} ${p.points > 0 ? '+' : ''}${p.points} 分 · ${p.reason || ''}`,
        type: p.points >= 0 ? 'success' : 'error',
        duration: 2600
      })
    } else if (ev.type === 'levelup') {
      toast.success(`🎉 ${p.studentName || '学生'} 的宠物升到 ${p.petLevel} 级啦！`, 3200)
    } else if (ev.type === 'voice_session') {
      const preview = p.reply ? (p.reply.length > 36 ? p.reply.slice(0, 36) + '…' : p.reply) : ''
      toast.show({
        message: `🎤 ${p.studentName || '设备'} 完成一次语音互动${preview ? '：' + preview : ''}`,
        type: 'info',
        duration: 3200
      })
    }
  }

  function handleEvent(ev: RealtimeEvent) {
    if (!ev || !ev.type) return
    pushLog(ev)
    toastForEvent(ev)
    scheduleRefresh()
  }

  function connect() {
    disconnect()
    const cloudUrl = (import.meta.env.VITE_API_URL as string) || ''

    // ---------- 云端模式：真实 SSE ----------
    if (cloudUrl) {
      try {
        const url = `${cloudUrl.replace(/\/$/, '')}/api/events`
        es = new EventSource(url)
        es.onopen = () => { connected.value = true; simulated.value = false }
        es.onerror = () => { connected.value = false }
        const onData = (e: MessageEvent) => {
          try { handleEvent(JSON.parse(e.data)) } catch { /* ignore */ }
        }
        // 命名事件
        ;(['connected', 'evaluation', 'levelup', 'voice_session'] as const).forEach((t) => {
          es!.addEventListener(t, onData as EventListener)
        })
        return
      } catch {
        es = null
      }
    }

    // ---------- 本地模式：模拟设备实时活动 ----------
    connected.value = true
    simulated.value = true
    const reasons = ['主动举手发言', '认真听讲', '帮助同学', '完成朗读任务', '积极思考', '遵守纪律']
    mockTimer = setInterval(async () => {
      const list = studentStore.students
      if (!classStore.currentClass || list.length === 0) return
      const s = list[Math.floor(Math.random() * list.length)]
      const pts = Math.floor(Math.random() * 3) + 1
      const reason = reasons[Math.floor(Math.random() * reasons.length)]
      try {
        await api.post('/evaluations', {
          classId: classStore.currentClass.id,
          studentId: s.id,
          points: pts,
          reason: `语音互动·模拟`,
          category: '其他'
        })
        handleEvent({
          type: 'voice_session',
          payload: {
            studentId: s.id,
            studentName: s.name,
            reply: `我刚才${reason}，感觉自己又进步了一点点！`,
            awarded: true
          },
          timestamp: Date.now()
        })
      } catch {
        /* 模拟写入失败则仅记录日志 */
        pushLog({
          type: 'voice_session',
          payload: { studentId: s.id, studentName: s.name, reply: reason },
          timestamp: Date.now()
        })
      }
    }, 9000)
  }

  function disconnect() {
    if (es) { es.close(); es = null }
    if (mockTimer) { clearInterval(mockTimer); mockTimer = null }
    connected.value = false
    simulated.value = false
  }

  onUnmounted(disconnect)

  return { connected, simulated, realtimeLog, lastEvent, connect, disconnect }
}
