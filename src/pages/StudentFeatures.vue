<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuth } from '@/composables/useAuth'
import { useStudentStore } from '@/stores/useStudentStore'
import { useToast } from '@/composables/useToast'
import type { CalendarEvent, ChecklistItem, Schedule, OwnerProfile } from '@/types'

const route = useRoute()
const router = useRouter()
const { api, isGuest } = useAuth()
const studentStore = useStudentStore()
const toast = useToast()

const studentId = computed(() => route.params.id as string)
const student = computed(() => studentStore.students.find(s => s.id === studentId.value))

const WEEKDAYS = ['周日', '周一', '周二', '周三', '周四', '周五', '周六']

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

// ===== 日历 =====
const calendar = ref<CalendarEvent[]>([])
const calForm = ref({ title: '', event_date: '', time_str: '', description: '' })
const calAdding = ref(false)

// ===== 清单 =====
const checklist = ref<ChecklistItem[]>([])
const listForm = ref({ content: '' })
const listAdding = ref(false)

// ===== 闹铃 =====
const alarms = ref<Schedule[]>([])
const alarmForm = ref({ day_of_week: 1, time_str: '08:00', task_desc: '' })
const alarmAdding = ref(false)

// ===== 主人记忆 =====
const memory = ref<OwnerProfile>({ profile: null, emotion_log: [], learning_log: [] })
const profileForm = ref({ grade: '', personality: '', goal: '', note: '' })
const emotionForm = ref({ mood: '开心', note: '' })
const learningForm = ref({ subject: '', content: '', status: '进行中' })
const savingProfile = ref(false)

async function loadAll() {
  loading.value = true
  try {
    const [calRes, listRes, alarmRes, memRes] = await Promise.all([
      api.get(`/students/${studentId.value}/calendar`),
      api.get(`/students/${studentId.value}/checklist`),
      api.get(`/device/schedules/student/${studentId.value}`),
      api.get(`/students/${studentId.value}/owner-profile`)
    ])
    calendar.value = calRes.data.events || calRes.data.calendar || []
    checklist.value = listRes.data.items || listRes.data.checklist || []
    alarms.value = alarmRes.data.schedules || alarmRes.data.items || []
    memory.value = {
      profile: memRes.data.profile || null,
      emotion_log: memRes.data.emotion_log || [],
      learning_log: memRes.data.learning_log || []
    }
    // 画像表单回填
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
    await api.post(`/students/${studentId.value}/checklist`, { ...listForm.value })
    toast.success('已添加到清单')
    listForm.value = { content: '' }
    await loadAll()
  } catch (e: any) {
    toast.error('添加失败：' + (e?.response?.data?.error || e.message))
  } finally {
    listAdding.value = false
  }
}
async function toggleChecklist(item: ChecklistItem) {
  try {
    await api.put(`/students/${studentId.value}/checklist/${item.id}`, { is_done: item.is_done ? 0 : 1 })
    item.is_done = item.is_done ? 0 : 1
  } catch (e: any) {
    toast.error('更新失败：' + (e?.response?.data?.error || e.message))
  }
}
async function delChecklist(id: string) {
  try {
    await api.delete(`/students/${studentId.value}/checklist/${id}`)
    checklist.value = checklist.value.filter(e => e.id !== id)
    toast.success('已删除')
  } catch (e: any) {
    toast.error('删除失败：' + (e?.response?.data?.error || e.message))
  }
}

// ===== 闹铃操作 =====
async function addAlarm() {
  if (!alarmForm.value.time_str || !alarmForm.value.task_desc) {
    toast.error('请填写时间和任务')
    return
  }
  alarmAdding.value = true
  try {
    await api.post(`/device/schedules/student/${studentId.value}`, {
      day_of_week: Number(alarmForm.value.day_of_week),
      time_str: alarmForm.value.time_str,
      task_desc: alarmForm.value.task_desc
    })
    toast.success('已添加闹铃')
    alarmForm.value = { day_of_week: 1, time_str: '08:00', task_desc: '' }
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

      <!-- 日历 -->
      <section v-else-if="activeTab === 'calendar'" class="space-y-4">
        <div class="bg-white rounded-2xl shadow p-4 space-y-3">
          <div class="font-bold text-gray-700 flex items-center gap-2">➕ 添加日历事件</div>
          <input v-model="calForm.title" placeholder="事件标题（如：数学考试）" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <div class="flex gap-2">
            <input v-model="calForm.event_date" type="date" class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            <input v-model="calForm.time_str" type="time" class="w-32 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          </div>
          <input v-model="calForm.description" placeholder="备注（可选）" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <button @click="addCalendar" :disabled="calAdding" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">添加</button>
        </div>

        <div v-if="calendar.length === 0" class="text-center text-gray-400 py-10">还没有日历事件</div>
        <div
          v-for="ev in calendar" :key="ev.id"
          class="bg-white rounded-2xl shadow p-4 flex items-start gap-3 hover:shadow-md transition-shadow"
        >
          <div class="min-w-[52px] text-center bg-orange-50 rounded-xl py-2">
            <div class="text-xs text-orange-500 font-bold">{{ ev.event_date.slice(5) }}</div>
            <div class="text-xs text-gray-500">{{ ev.time_str || '全天' }}</div>
          </div>
          <div class="flex-1 min-w-0">
            <div class="font-bold text-gray-800">{{ ev.title }}</div>
            <div v-if="ev.description" class="text-sm text-gray-500 mt-0.5">{{ ev.description }}</div>
          </div>
          <button @click="delCalendar(ev.id)" class="text-red-400 hover:text-red-600 text-sm px-2">删除</button>
        </div>
      </section>

      <!-- 清单 -->
      <section v-else-if="activeTab === 'checklist'" class="space-y-4">
        <div class="bg-white rounded-2xl shadow p-4 flex gap-2">
          <input v-model="listForm.content" @keyup.enter="addChecklist" placeholder="添加待办事项…" class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <button @click="addChecklist" :disabled="listAdding" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold px-5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">添加</button>
        </div>
        <div v-if="checklist.length === 0" class="text-center text-gray-400 py-10">清单是空的，加一条吧！</div>
        <div v-for="item in checklist" :key="item.id" class="bg-white rounded-2xl shadow p-4 flex items-center gap-3">
          <button @click="toggleChecklist(item)" class="w-6 h-6 rounded-full border-2 flex items-center justify-center transition-all"
            :class="item.is_done ? 'bg-green-500 border-green-500 text-white' : 'border-gray-300 text-transparent'">✓</button>
          <div class="flex-1" :class="item.is_done ? 'line-through text-gray-400' : 'text-gray-800'">{{ item.content }}</div>
          <button @click="delChecklist(item.id)" class="text-red-400 hover:text-red-600 text-sm px-2">删除</button>
        </div>
      </section>

      <!-- 闹铃 -->
      <section v-else-if="activeTab === 'alarm'" class="space-y-4">
        <div class="bg-white rounded-2xl shadow p-4 space-y-3">
          <div class="font-bold text-gray-700 flex items-center gap-2">➕ 添加定时闹铃</div>
          <div class="flex gap-2">
            <select v-model.number="alarmForm.day_of_week" class="border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300">
              <option v-for="(w, i) in WEEKDAYS" :key="i" :value="i">{{ w }}</option>
            </select>
            <input v-model="alarmForm.time_str" type="time" class="border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          </div>
          <input v-model="alarmForm.task_desc" placeholder="提醒内容（如：该练琴啦）" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          <button @click="addAlarm" :disabled="alarmAdding" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">添加</button>
        </div>
        <div v-if="alarms.length === 0" class="text-center text-gray-400 py-10">还没有定时闹铃</div>
        <div v-for="a in alarms" :key="a.id" class="bg-white rounded-2xl shadow p-4 flex items-center gap-3">
          <div class="text-2xl">⏰</div>
          <div class="flex-1">
            <div class="font-bold text-gray-800">{{ WEEKDAYS[a.day_of_week] }} {{ a.time_str }}</div>
            <div class="text-sm text-gray-500">{{ a.task_desc }}</div>
          </div>
          <button @click="delAlarm(a.id)" class="text-red-400 hover:text-red-600 text-sm px-2">删除</button>
        </div>
      </section>

      <!-- 主人记忆 -->
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
