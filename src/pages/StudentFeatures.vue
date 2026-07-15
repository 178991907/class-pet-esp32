<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuth } from '@/composables/useAuth'
import { useStudentStore } from '@/stores/useStudentStore'
import { useToast } from '@/composables/useToast'
import { playChime } from '@/utils/sound'
import type { CalendarEvent, Schedule, OwnerProfile } from '@/types'

const route = useRoute()
const router = useRouter()
const { api, isGuest } = useAuth()
const studentStore = useStudentStore()
const toast = useToast()

const studentId = computed(() => route.params.id as string)
const student = computed(() => studentStore.students.find(s => s.id === studentId.value))

const WEEKDAYS = ['周日', '周一', '周二', '周三', '周四', '周五', '周六']
const WEEKDAY_SHORT = ['日', '一', '二', '三', '四', '五', '六']
const PRESETS = [
  { label: '每天', mask: 0b1111111 },
  { label: '工作日', mask: 0b0111110 },
  { label: '周末', mask: 0b1000001 }
]

function daysFromMask(mask: number) {
  const list: number[] = []
  for (let i = 0; i < 7; i++) if (mask & (1 << i)) list.push(i)
  return list
}
function firstDayOfMask(mask: number) {
  return daysFromMask(mask)[0] ?? 1
}

// ===== 标签 =====
type TabKey = 'calendar' | 'checklist' | 'alarm' | 'memory'
const allowedTabs = new Set<TabKey>(['calendar', 'checklist', 'alarm', 'memory'])
const activeTab = ref<TabKey>('calendar')
watch(() => route.query.tab as string, (t) => {
  if (t && allowedTabs.has(t as TabKey)) activeTab.value = t as TabKey
}, { immediate: true })
const tabs: { key: TabKey; label: string; icon: string }[] = [
  { key: 'calendar', label: '日历', icon: '📅' },
  { key: 'checklist', label: '清单', icon: '✅' },
  { key: 'alarm', label: '闹铃', icon: '⏰' },
  { key: 'memory', label: '主人记忆', icon: '🧠' }
]

// ===== 加载态 =====
const loading = ref(false)

// ===== 日历：月/周/日视图 =====
const calendar = ref<CalendarEvent[]>([])
const calForm = ref({ title: '', event_date: '', time_str: '', description: '' })
const calAdding = ref(false)

const calView = ref<'month' | 'week' | 'day'>('month')
const calCursor = ref(new Date())
const selectedDate = ref(toYMD(new Date()))

const todayStr = computed(() => toYMD(new Date()))
const cursorLabel = computed(() => {
  const d = calCursor.value
  if (calView.value === 'month') return `${d.getFullYear()} 年 ${d.getMonth() + 1} 月`
  if (calView.value === 'week') {
    const ws = weekStart(d)
    const we = new Date(ws); we.setDate(ws.getDate() + 6)
    return `${ws.getMonth() + 1}月${ws.getDate()}日 – ${we.getMonth() + 1}月${we.getDate()}日`
  }
  return `${d.getFullYear()} 年 ${d.getMonth() + 1} 月 ${d.getDate()} 日`
})

function toYMD(d: Date): string {
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${day}`
}
function weekStart(d: Date): Date {
  const r = new Date(d)
  r.setDate(d.getDate() - d.getDay())
  r.setHours(0, 0, 0, 0)
  return r
}

const monthGrid = computed(() => {
  const cur = calCursor.value
  const first = new Date(cur.getFullYear(), cur.getMonth(), 1)
  const start = new Date(first)
  start.setDate(first.getDate() - first.getDay())
  const cells = []
  for (let i = 0; i < 42; i++) {
    const d = new Date(start)
    d.setDate(start.getDate() + i)
    const ds = toYMD(d)
    cells.push({
      date: ds,
      day: d.getDate(),
      inMonth: d.getMonth() === cur.getMonth(),
      isToday: ds === todayStr.value,
      isSelected: ds === selectedDate.value,
      events: calendar.value.filter(e => e.event_date === ds)
    })
  }
  return cells
})

const weekDays = computed(() => {
  const ws = weekStart(calCursor.value)
  const arr = []
  for (let i = 0; i < 7; i++) {
    const d = new Date(ws); d.setDate(ws.getDate() + i)
    const ds = toYMD(d)
    arr.push({
      date: ds, day: d.getDate(), dow: d.getDay(),
      isToday: ds === todayStr.value, isSelected: ds === selectedDate.value,
      events: calendar.value.filter(e => e.event_date === ds)
    })
  }
  return arr
})

const dayEvents = computed(() => calendar.value.filter(e => e.event_date === selectedDate.value))
const selectedLabel = computed(() => {
  const [, m, d] = selectedDate.value.split('-')
  return `${Number(m)}月${Number(d)}日 ${WEEKDAYS[new Date(selectedDate.value).getDay()]}`
})

function calNav(dir: number) {
  const c = new Date(calCursor.value)
  if (calView.value === 'month') c.setMonth(c.getMonth() + dir)
  else if (calView.value === 'week') c.setDate(c.getDate() + dir * 7)
  else c.setDate(c.getDate() + dir)
  calCursor.value = c
  if (calView.value === 'day') selectedDate.value = toYMD(c)
}
function selectDate(ds: string) {
  selectedDate.value = ds
  calForm.value.event_date = ds
  const [y, m] = ds.split('-').map(Number)
  if (calView.value === 'month' && (calCursor.value.getFullYear() !== y || calCursor.value.getMonth() + 1 !== m)) {
    calCursor.value = new Date(y, m - 1, 1)
  }
}
function setCalView(v: 'month' | 'week' | 'day') {
  calView.value = v
  if (v === 'day') selectedDate.value = selectedDate.value || toYMD(new Date())
}
function goToday() {
  calCursor.value = new Date()
  selectedDate.value = toYMD(new Date())
}

// ===== 清单：每日待办感 =====
const listForm = ref({ content: '' })
const listAdding = ref(false)
const showDone = ref(false)

const activeItems = computed(() => todayItems.value.filter(i => !i.is_done))
const doneItems = computed(() => todayItems.value.filter(i => i.is_done))
const todayLabel = computed(() => {
  const d = new Date()
  return `${d.getMonth() + 1}月${d.getDate()}日 ${WEEKDAYS[d.getDay()]}`
})

// ===== 闹铃 =====
const alarms = ref<Schedule[]>([])
const alarmForm = ref({ days_of_week: 0b0111110, time_str: '08:00', task_desc: '' })
const alarmAdding = ref(false)

// ===== 今日聚合（日历+闹铃+清单）=====
interface TodayItem {
  type: 'alarm' | 'calendar' | 'checklist'
  id: string
  time_str: string | null
  title: string
  is_done: boolean
  source_id: string | null
  source_type: string
  checklist_id?: string | null
}
const todayItems = ref<TodayItem[]>([])
const todayInfo = ref({ date: '', weekday: 0 })

// ===== 主人记忆 =====
const memory = ref<OwnerProfile>({ profile: null, emotion_log: [], learning_log: [] })
const profileForm = ref({ grade: '', personality: '', goal: '', note: '' })
const emotionForm = ref({ mood: '开心', note: '' })
const learningForm = ref({ subject: '', content: '', status: '进行中' })
const savingProfile = ref(false)

async function loadAll() {
  loading.value = true
  try {
    const today = new Date()
    const [calRes, todayRes, alarmRes, memRes] = await Promise.all([
      api.get(`/students/${studentId.value}/calendar`),
      api.get(`/students/${studentId.value}/today`, { params: { date: toYMD(today), weekday: today.getDay() } }),
      api.get(`/device/schedules/student/${studentId.value}`),
      api.get(`/students/${studentId.value}/owner-profile`)
    ])
    calendar.value = calRes.data.events || calRes.data.calendar || []
    todayItems.value = todayRes.data.items || []
    todayInfo.value = { date: todayRes.data.date, weekday: todayRes.data.weekday }
    alarms.value = (alarmRes.data.schedules || alarmRes.data.items || []).map((a: Schedule) => ({
      ...a,
      days_of_week: a.days_of_week || (a.day_of_week !== undefined ? (1 << a.day_of_week) : 0)
    }))
    memory.value = {
      profile: memRes.data.profile || null,
      emotion_log: memRes.data.emotion_log || [],
      learning_log: memRes.data.learning_log || []
    }
    if (memory.value.profile) {
      profileForm.value = {
        grade: memory.value.profile.grade || '',
        personality: memory.value.profile.personality || '',
        goal: memory.value.profile.goal || '',
        note: memory.value.profile.note || ''
      }
    }
  } catch (e: any) {
    toast.error('加载失败：' + (e?.response?.data?.error || e.message))
  } finally {
    loading.value = false
  }
}

// ===== 日历操作 =====
async function addCalendar() {
  if (!calForm.value.title || !calForm.value.event_date) {
    toast.error('请填写标题和日期')
    return
  }
  calAdding.value = true
  try {
    await api.post(`/students/${studentId.value}/calendar`, { ...calForm.value })
    playChime('calendar')
    toast.success('已添加日历事件')
    calForm.value = { title: '', event_date: '', time_str: '', description: '' }
    await loadAll()
  } catch (e: any) {
    toast.error('添加失败：' + (e?.response?.data?.error || e.message))
  } finally {
    calAdding.value = false
  }
}
async function delCalendar(id: string) {
  try {
    await api.delete(`/students/${studentId.value}/calendar/${id}`)
    calendar.value = calendar.value.filter(e => e.id !== id)
    toast.success('已删除')
  } catch (e: any) {
    toast.error('删除失败：' + (e?.response?.data?.error || e.message))
  }
}

// ===== 清单操作 =====
async function addChecklist() {
  if (!listForm.value.content) {
    toast.error('请填写内容')
    return
  }
  listAdding.value = true
  try {
    await api.post(`/students/${studentId.value}/checklist`, {
      content: listForm.value.content,
      target_date: toYMD(new Date())
    })
    playChime('checklist')
    toast.success('已添加到清单')
    listForm.value = { content: '' }
    await loadAll()
  } catch (e: any) {
    toast.error('添加失败：' + (e?.response?.data?.error || e.message))
  } finally {
    listAdding.value = false
  }
}
async function toggleChecklist(item: TodayItem) {
  if (item.type === 'alarm') return
  try {
    const checklistId = (item.type === 'calendar' && item.checklist_id) ? item.checklist_id : item.id
    const newDone = !item.is_done
    await api.put(`/students/${studentId.value}/checklist/${checklistId}`, { is_done: newDone ? 1 : 0 })
    item.is_done = newDone
    if (newDone) playChime('checklist')
  } catch (e: any) {
    toast.error('更新失败：' + (e?.response?.data?.error || e.message))
  }
}
async function delChecklist(item: TodayItem) {
  if (item.type !== 'checklist') return
  try {
    await api.delete(`/students/${studentId.value}/checklist/${item.id}`)
    todayItems.value = todayItems.value.filter(i => i.id !== item.id)
    toast.success('已删除')
  } catch (e: any) {
    toast.error('删除失败：' + (e?.response?.data?.error || e.message))
  }
}

// ===== 闹铃操作 =====
async function addAlarm() {
  if (!alarmForm.value.days_of_week || !alarmForm.value.time_str || !alarmForm.value.task_desc) {
    toast.error('请选择星期并填写时间和任务')
    return
  }
  alarmAdding.value = true
  try {
    await api.post(`/device/schedules/student/${studentId.value}`, {
      // 旧后端只认 day_of_week（取掩码首个选中日作回退），新后端优先 days_of_week
      day_of_week: firstDayOfMask(alarmForm.value.days_of_week),
      days_of_week: alarmForm.value.days_of_week,
      time_str: alarmForm.value.time_str,
      task_desc: alarmForm.value.task_desc
    })
    playChime('alarm')
    toast.success('已添加闹铃')
    alarmForm.value = { days_of_week: 0b0111110, time_str: '08:00', task_desc: '' }
    await loadAll()
  } catch (e: any) {
    toast.error('添加失败：' + (e?.response?.data?.error || e.message))
  } finally {
    alarmAdding.value = false
  }
}
async function delAlarm(id: string) {
  try {
    await api.delete(`/device/schedules/${id}`)
    alarms.value = alarms.value.filter(e => e.id !== id)
    toast.success('已删除闹铃')
  } catch (e: any) {
    toast.error('删除失败：' + (e?.response?.data?.error || e.message))
  }
}

// ===== 主人记忆操作 =====
async function saveProfile() {
  savingProfile.value = true
  try {
    const data = {
      grade: profileForm.value.grade,
      personality: profileForm.value.personality,
      goal: profileForm.value.goal,
      note: profileForm.value.note
    }
    await api.post(`/students/${studentId.value}/owner-profile`, { type: 'profile', data })
    memory.value.profile = data
    toast.success('已保存用户画像')
  } catch (e: any) {
    toast.error('保存失败：' + (e?.response?.data?.error || e.message))
  } finally {
    savingProfile.value = false
  }
}
async function addEmotion() {
  if (!emotionForm.value.note && !emotionForm.value.mood) return
  try {
    await api.post(`/students/${studentId.value}/owner-profile`, { type: 'emotion', data: { mood: emotionForm.value.mood, note: emotionForm.value.note } })
    emotionForm.value = { mood: '开心', note: '' }
    await loadAll()
    toast.success('已记录情绪')
  } catch (e: any) {
    toast.error('记录失败：' + (e?.response?.data?.error || e.message))
  }
}
async function addLearning() {
  if (!learningForm.value.content) {
    toast.error('请填写学习内容')
    return
  }
  try {
    await api.post(`/students/${studentId.value}/owner-profile`, { type: 'learning', data: { subject: learningForm.value.subject, content: learningForm.value.content, status: learningForm.value.status } })
    learningForm.value = { subject: '', content: '', status: '进行中' }
    await loadAll()
    toast.success('已记录学习情况')
  } catch (e: any) {
    toast.error('记录失败：' + (e?.response?.data?.error || e.message))
  }
}

function fmtTime(ts?: number) {
  if (!ts) return ''
  return new Date(ts).toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
}

function goBack() {
  if (window.history.length > 1) router.back()
  else router.push('/')
}

onMounted(loadAll)
</script>

<template>
  <div class="min-h-screen bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 flex flex-col">
    <!-- 顶部 -->
    <header class="bg-gradient-to-r from-amber-400 via-orange-400 to-rose-400 shadow-lg px-4 py-3 flex items-center justify-between sticky top-0 z-30">
      <div class="flex items-center gap-3">
        <button @click="goBack" class="w-9 h-9 rounded-full bg-white/20 hover:bg-white/30 flex items-center justify-center text-white text-xl transition-colors" title="返回">←</button>
        <div>
          <h1 class="text-lg font-bold text-white drop-shadow">🐾 {{ student?.name || '学生' }} 的成长管理</h1>
          <p class="text-white/80 text-xs">日历 · 清单 · 闹铃 · 主人记忆</p>
        </div>
      </div>
      <span v-if="isGuest" class="text-xs text-white/80 bg-white/20 px-2 py-1 rounded-full">演示模式</span>
    </header>

    <!-- 标签栏 -->
    <div class="flex gap-1 px-4 pt-3 sticky top-[60px] z-20 bg-gradient-to-b from-orange-50 to-transparent">
      <button
        v-for="t in tabs" :key="t.key"
        @click="activeTab = t.key"
        class="flex-1 px-2 py-2.5 rounded-xl font-bold text-sm transition-all"
        :class="activeTab === t.key ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg' : 'bg-white/70 text-gray-600 hover:bg-white'"
      >
        {{ t.icon }} {{ t.label }}
      </button>
    </div>

    <main class="flex-1 overflow-auto p-4 pb-12">
      <div v-if="loading" class="text-center text-gray-400 py-16">加载中…</div>

      <!-- ============ 日历 ============ -->
      <section v-else-if="activeTab === 'calendar'" class="flex flex-col gap-4 h-[calc(100vh-180px)] sm:h-[calc(100vh-190px)]">
        <!-- 视图切换 + 导航 -->
        <div class="bg-white rounded-2xl shadow p-2 flex flex-col flex-1 min-h-0">
          <div class="flex items-center justify-between mb-2 shrink-0">
            <div class="flex gap-1 bg-orange-50 rounded-xl p-1">
              <button @click="setCalView('month')" :class="calView==='month' ? 'bg-orange-500 text-white' : 'text-orange-600'" class="px-3 py-1 rounded-lg text-xs font-bold transition-colors">月</button>
              <button @click="setCalView('week')" :class="calView==='week' ? 'bg-orange-500 text-white' : 'text-orange-600'" class="px-3 py-1 rounded-lg text-xs font-bold transition-colors">周</button>
              <button @click="setCalView('day')" :class="calView==='day' ? 'bg-orange-500 text-white' : 'text-orange-600'" class="px-3 py-1 rounded-lg text-xs font-bold transition-colors">日</button>
            </div>
            <div class="flex items-center gap-2">
              <button @click="goToday" class="text-xs px-2 py-1 rounded-lg bg-gray-100 text-gray-600 hover:bg-gray-200">今天</button>
              <button @click="calNav(-1)" class="w-8 h-8 rounded-full bg-gray-100 text-gray-600 hover:bg-gray-200 flex items-center justify-center">‹</button>
              <span class="text-sm font-bold text-gray-700 min-w-[120px] text-center">{{ cursorLabel }}</span>
              <button @click="calNav(1)" class="w-8 h-8 rounded-full bg-gray-100 text-gray-600 hover:bg-gray-200 flex items-center justify-center">›</button>
            </div>
          </div>

          <!-- 月视图 -->
          <div v-if="calView==='month'" class="flex flex-col flex-1 min-h-0">
            <div class="grid grid-cols-7 gap-1 mb-1 shrink-0">
              <div v-for="w in WEEKDAYS" :key="w" class="text-center text-[10px] sm:text-xs font-bold text-gray-400 py-1">{{ w.slice(1) }}</div>
            </div>
            <div class="grid grid-cols-7 grid-rows-6 gap-1 flex-1 min-h-0">
              <button
                v-for="c in monthGrid" :key="c.date"
                @click="selectDate(c.date)"
                class="h-full w-full min-h-0 rounded-lg flex flex-col items-center justify-center transition-all relative overflow-hidden"
                :class="[
                  c.inMonth ? 'bg-white' : 'bg-gray-50',
                  c.isSelected ? 'ring-2 ring-orange-500 bg-orange-50' : 'hover:bg-orange-50',
                  !c.inMonth && 'text-gray-300'
                ]"
              >
                <span :class="c.isToday ? 'bg-rose-500 text-white w-5 h-5 sm:w-6 sm:h-6 rounded-full flex items-center justify-center font-bold text-xs sm:text-sm' : 'font-medium text-gray-700 text-xs sm:text-sm'">{{ c.day }}</span>
                <span v-if="c.events.length" class="absolute bottom-1 sm:bottom-2 flex gap-0.5">
                  <span v-for="n in Math.min(c.events.length,3)" :key="n" class="w-1 h-1 sm:w-1.5 sm:h-1.5 rounded-full bg-orange-400"></span>
                </span>
              </button>
            </div>
          </div>

          <!-- 周视图 -->
          <div v-else-if="calView==='week'" class="overflow-y-auto flex-1 min-h-0 space-y-2">
            <button
              v-for="d in weekDays" :key="d.date"
              @click="selectDate(d.date)"
              class="w-full rounded-xl p-3 flex items-center gap-3 text-left transition-all"
              :class="d.isSelected ? 'ring-2 ring-orange-500 bg-orange-50' : 'bg-white hover:bg-orange-50'"
            >
              <div class="text-center min-w-[44px]">
                <div class="text-xs text-gray-400">{{ WEEKDAY_SHORT[d.dow] }}</div>
                <div :class="d.isToday ? 'bg-rose-500 text-white w-8 h-8 rounded-full flex items-center justify-center font-bold' : 'text-lg font-bold text-gray-700'">{{ d.day }}</div>
              </div>
              <div class="flex-1 min-w-0">
                <div v-if="d.events.length===0" class="text-sm text-gray-300">无安排</div>
                <div v-for="ev in d.events" :key="ev.id" class="text-sm text-gray-700 truncate">🕐 {{ ev.time_str || '全天' }} {{ ev.title }}</div>
              </div>
            </button>
          </div>

          <!-- 日视图 -->
          <div v-else class="overflow-y-auto flex-1 min-h-0 space-y-2">
            <div class="text-center text-sm font-bold text-gray-600 py-1">{{ selectedLabel }}</div>
            <div v-if="dayEvents.length===0" class="text-center text-gray-300 text-sm py-6">这一天还没有安排</div>
            <TransitionGroup name="pop" tag="div" class="space-y-2">
              <div v-for="ev in dayEvents" :key="ev.id" class="bg-white rounded-xl shadow p-3 flex items-center gap-3">
                <div class="text-xs text-orange-500 font-bold min-w-[40px]">{{ ev.time_str || '全天' }}</div>
                <div class="flex-1 min-w-0">
                  <div class="font-bold text-gray-800">{{ ev.title }}</div>
                  <div v-if="ev.description" class="text-xs text-gray-500">{{ ev.description }}</div>
                </div>
                <button @click="delCalendar(ev.id)" class="text-red-400 hover:text-red-600 text-xs px-2">删除</button>
              </div>
            </TransitionGroup>
          </div>
        </div>

        <!-- 选中日期的事件 + 快速添加 -->
        <div class="bg-white rounded-2xl shadow p-3 space-y-3 shrink-0 max-h-[220px] overflow-y-auto">
          <div class="font-bold text-gray-700 flex items-center gap-2 text-sm">📌 {{ selectedLabel }} 的安排</div>
          <div v-if="dayEvents.length===0" class="text-sm text-gray-300 py-2">还没有安排，下面添加一条吧</div>
          <div v-for="ev in dayEvents" :key="'l'+ev.id" class="flex items-center gap-2 text-sm">
            <span class="text-orange-500 font-bold min-w-[40px]">{{ ev.time_str || '全天' }}</span>
            <span class="flex-1 text-gray-700 truncate">{{ ev.title }}</span>
            <button @click="delCalendar(ev.id)" class="text-red-400 hover:text-red-600 text-xs">删</button>
          </div>
          <div class="border-t pt-3 space-y-2">
            <div class="font-medium text-gray-600 text-sm">➕ 在 {{ selectedLabel }} 添加事件</div>
            <input v-model="calForm.title" placeholder="事件标题（如：数学考试）" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            <div class="flex gap-2">
              <input v-model="calForm.event_date" type="date" class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
              <input v-model="calForm.time_str" type="time" class="w-32 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            </div>
            <input v-model="calForm.description" placeholder="备注（可选）" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            <button @click="addCalendar" :disabled="calAdding" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">添加事件</button>
          </div>
        </div>
      </section>

      <!-- ============ 清单 ============ -->
      <section v-else-if="activeTab === 'checklist'" class="space-y-4">
        <div class="bg-white rounded-2xl shadow p-4 flex flex-col gap-1">
          <div class="text-sm font-bold text-gray-700">📋 我的每日清单</div>
          <div class="text-xs text-gray-400">今天 · {{ todayLabel }} · 共 {{ todayItems.length }} 项，已完成 {{ doneItems.length }} 项</div>
        </div>

        <div class="bg-white rounded-2xl shadow p-4 flex gap-2">
          <input v-model="listForm.content" @keyup.enter="addChecklist" placeholder="添加待办事项…" class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <button @click="addChecklist" :disabled="listAdding" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold px-5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">添加</button>
        </div>

        <!-- 未完成 -->
        <div v-if="activeItems.length===0" class="text-center text-gray-400 py-8">🎉 今天的待办都清空啦！</div>
        <TransitionGroup name="pop" tag="div" class="space-y-3">
          <div v-for="item in activeItems" :key="item.id" class="bg-white rounded-2xl shadow p-4 flex items-center gap-3">
            <button v-if="item.type !== 'alarm'" @click="toggleChecklist(item)" class="w-6 h-6 rounded-full border-2 border-orange-300 flex items-center justify-center text-transparent hover:bg-orange-50 transition-all">✓</button>
            <div v-else class="w-6 h-6 rounded-full bg-orange-100 flex items-center justify-center text-orange-500 text-xs">⏰</div>
            <div class="flex-1 min-w-0">
              <div class="flex items-center gap-2">
                <span v-if="item.time_str" class="text-xs font-bold text-orange-500">{{ item.time_str }}</span>
                <span class="text-gray-800">{{ item.title }}</span>
              </div>
              <div class="text-xs text-gray-400 mt-0.5">
                <span v-if="item.type === 'calendar'">📅 来自日历</span>
                <span v-else-if="item.type === 'alarm'">⏰ 定时提醒</span>
              </div>
            </div>
            <button v-if="item.type === 'checklist'" @click="delChecklist(item)" class="text-red-400 hover:text-red-600 text-sm px-2">删除</button>
          </div>
        </TransitionGroup>

        <!-- 已完成（折叠） -->
        <div v-if="doneItems.length" class="space-y-2">
          <button @click="showDone=!showDone" class="w-full text-left text-sm text-gray-500 bg-white/70 rounded-xl px-4 py-2 hover:bg-white transition-colors flex items-center justify-between">
            <span>✅ 已完成（{{ doneItems.length }}）</span>
            <span>{{ showDone ? '收起 ▲' : '展开 ▼' }}</span>
          </button>
          <TransitionGroup v-if="showDone" name="pop" tag="div" class="space-y-2">
            <div v-for="item in doneItems" :key="item.id" class="bg-gray-50 rounded-2xl p-4 flex items-center gap-3 opacity-70">
              <button v-if="item.type !== 'alarm'" @click="toggleChecklist(item)" class="w-6 h-6 rounded-full border-2 bg-green-500 border-green-500 flex items-center justify-center text-white transition-all">✓</button>
              <div v-else class="w-6 h-6 rounded-full bg-orange-100 flex items-center justify-center text-orange-500 text-xs">⏰</div>
              <div class="flex-1 min-w-0">
                <div class="flex items-center gap-2">
                  <span v-if="item.time_str" class="text-xs font-bold text-orange-500">{{ item.time_str }}</span>
                  <span class="line-through text-gray-400">{{ item.title }}</span>
                </div>
              </div>
              <button v-if="item.type === 'checklist'" @click="delChecklist(item)" class="text-red-400 hover:text-red-600 text-sm px-2">删除</button>
            </div>
          </TransitionGroup>
        </div>
      </section>

      <!-- ============ 闹铃 ============ -->
      <section v-else-if="activeTab === 'alarm'" class="space-y-4">
        <div class="bg-white rounded-2xl shadow p-4 space-y-3">
          <div class="font-bold text-gray-700 flex items-center gap-2">➕ 添加定时闹铃</div>
          <!-- 快捷预设 -->
          <div class="flex gap-2 flex-wrap">
            <button
              v-for="p in PRESETS" :key="p.label"
              @click="alarmForm.days_of_week = p.mask"
              :class="alarmForm.days_of_week === p.mask ? 'bg-orange-500 text-white' : 'bg-orange-50 text-orange-600 hover:bg-orange-100'"
              class="px-3 py-1 rounded-full text-xs font-medium transition-colors"
            >{{ p.label }}</button>
            <button
              @click="alarmForm.days_of_week = 0"
              :class="alarmForm.days_of_week === 0 ? 'bg-gray-500 text-white' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'"
              class="px-3 py-1 rounded-full text-xs font-medium transition-colors"
            >自定义</button>
          </div>
          <!-- 星期多选 -->
          <div class="flex gap-1.5 justify-between">
            <button
              v-for="(_, i) in WEEKDAYS" :key="i"
              @click="alarmForm.days_of_week ^= (1 << i)"
              :class="(alarmForm.days_of_week & (1 << i)) ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow' : 'bg-gray-100 text-gray-500 hover:bg-gray-200'"
              class="flex-1 py-2 rounded-xl text-xs font-bold transition-all"
            >{{ WEEKDAY_SHORT[i] }}</button>
          </div>
          <input v-model="alarmForm.time_str" type="time" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <input v-model="alarmForm.task_desc" placeholder="提醒内容（如：起床、练琴、交作业）" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <button @click="addAlarm" :disabled="alarmAdding" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">添加闹铃</button>
        </div>

        <div v-if="alarms.length === 0" class="text-center text-gray-400 py-10">还没有定时闹铃</div>
        <TransitionGroup name="pop" tag="div" class="space-y-3">
          <div
            v-for="a in alarms" :key="a.id"
            class="bg-white rounded-2xl shadow p-4 flex items-center gap-4"
          >
            <div class="w-14 h-14 rounded-2xl bg-gradient-to-br from-orange-400 to-pink-500 flex flex-col items-center justify-center text-white shadow">
              <div class="text-[10px] opacity-90">⏰</div>
              <div class="text-lg font-bold leading-none">{{ a.time_str }}</div>
            </div>
            <div class="flex-1 min-w-0">
              <div class="font-bold text-gray-800">{{ a.task_desc }}</div>
              <div class="flex flex-wrap gap-1 mt-1.5">
                <span
                  v-for="d in daysFromMask(a.days_of_week || 0)" :key="d"
                  class="px-2 py-0.5 rounded-full bg-orange-50 text-orange-600 text-xs font-medium"
                >周{{ WEEKDAY_SHORT[d] }}</span>
                <span v-if="(a.days_of_week||0)===0b1111111" class="px-2 py-0.5 rounded-full bg-orange-100 text-orange-700 text-xs font-medium">每天</span>
                <span v-else-if="(a.days_of_week||0)===0b0111110" class="px-2 py-0.5 rounded-full bg-orange-100 text-orange-700 text-xs font-medium">工作日</span>
                <span v-else-if="(a.days_of_week||0)===0b1000001" class="px-2 py-0.5 rounded-full bg-orange-100 text-orange-700 text-xs font-medium">周末</span>
              </div>
            </div>
            <button @click="delAlarm(a.id)" class="text-red-400 hover:text-red-600 text-sm px-2">删除</button>
          </div>
        </TransitionGroup>
      </section>

      <!-- ============ 主人记忆 ============ -->
      <section v-else-if="activeTab === 'memory'" class="space-y-4">
        <!-- 用户画像 -->
        <div class="bg-white rounded-2xl shadow p-4 space-y-3">
          <div class="font-bold text-gray-700 flex items-center gap-2">🪪 用户画像</div>
          <div class="grid grid-cols-2 gap-2">
            <input v-model="profileForm.grade" placeholder="年级（如：三年级）" class="border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            <input v-model="profileForm.personality" placeholder="性格特点" class="border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          </div>
          <input v-model="profileForm.goal" placeholder="学习目标" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <textarea v-model="profileForm.note" placeholder="其他备注（爱好、习惯等）" rows="2" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300"></textarea>
          <button @click="saveProfile" :disabled="savingProfile" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">保存画像</button>
        </div>

        <!-- 情绪记录 -->
        <div class="bg-white rounded-2xl shadow p-4 space-y-3">
          <div class="font-bold text-gray-700 flex items-center gap-2">💗 情绪记录</div>
          <div class="flex gap-2">
            <select v-model="emotionForm.mood" class="border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300">
              <option>开心</option><option>平静</option><option>焦虑</option><option>沮丧</option><option>兴奋</option><option>疲惫</option>
            </select>
            <input v-model="emotionForm.note" @keyup.enter="addEmotion" placeholder="发生了什么？" class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            <button @click="addEmotion" class="bg-gradient-to-r from-pink-400 to-rose-500 text-white font-bold px-4 rounded-xl hover:shadow-lg transition-all">记</button>
          </div>
          <div v-if="memory.emotion_log.length === 0" class="text-center text-gray-300 text-sm py-3">暂无情绪记录</div>
          <div v-for="(e, i) in memory.emotion_log.slice().reverse()" :key="i" class="text-sm bg-pink-50 rounded-xl px-3 py-2">
            <span class="font-bold text-rose-500">{{ e.mood }}</span>
            <span class="text-gray-600 ml-2">{{ e.note }}</span>
            <span class="text-gray-400 text-xs ml-2">{{ fmtTime(e.ts) }}</span>
          </div>
        </div>

        <!-- 学习情况 -->
        <div class="bg-white rounded-2xl shadow p-4 space-y-3">
          <div class="font-bold text-gray-700 flex items-center gap-2">📚 学习情况</div>
          <div class="flex gap-2">
            <input v-model="learningForm.subject" placeholder="科目" class="w-28 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            <select v-model="learningForm.status" class="border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300">
              <option>进行中</option><option>已完成</option><option>有困难</option><option>需复习</option>
            </select>
          </div>
          <input v-model="learningForm.content" @keyup.enter="addLearning" placeholder="学习内容 / 进展" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <button @click="addLearning" class="w-full bg-gradient-to-r from-violet-400 to-purple-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all">记录学习情况</button>
          <div v-if="memory.learning_log.length === 0" class="text-center text-gray-300 text-sm py-3">暂无学习记录</div>
          <div v-for="(l, i) in memory.learning_log.slice().reverse()" :key="i" class="text-sm bg-purple-50 rounded-xl px-3 py-2">
            <span class="font-bold text-purple-500">{{ l.subject }}</span>
            <span class="text-xs bg-white rounded-full px-2 py-0.5 ml-2 text-purple-600">{{ l.status }}</span>
            <div class="text-gray-600 mt-0.5">{{ l.content }}</div>
            <div class="text-gray-400 text-xs">{{ fmtTime(l.ts) }}</div>
          </div>
        </div>
      </section>
    </main>
  </div>
</template>

<style scoped>
.pop-enter-active {
  animation: pop-in 0.32s cubic-bezier(0.34, 1.56, 0.64, 1);
}
.pop-leave-active {
  animation: pop-in 0.2s reverse;
}
@keyframes pop-in {
  0% { opacity: 0; transform: translateY(8px) scale(0.96); }
  100% { opacity: 1; transform: translateY(0) scale(1); }
}
</style>
