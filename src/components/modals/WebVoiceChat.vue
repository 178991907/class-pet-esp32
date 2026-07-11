<script setup lang="ts">
import { ref, computed, nextTick, watch } from 'vue'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'
import { useStudentStore } from '@/stores/useStudentStore'
import type { Student } from '@/types'

const props = defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()

const { api } = useAuth()
const toast = useToast()
const studentStore = useStudentStore()

// ===== 状态 =====
const input = ref('')
const sending = ref(false)
const listening = ref(false)
const awardPoints = ref(false)
const selectedStudentId = ref<string>('')
const messages = ref<{ role: 'user' | 'pet'; text: string }[]>([])
const scrollEl = ref<HTMLElement | null>(null)

// 默认把「当前详情学生」作为对话对象；否则下拉选择
const activeStudent = computed<Student | null>(() => {
  if (!selectedStudentId.value) return null
  return studentStore.students.find((s) => s.id === selectedStudentId.value) || null
})

// 打开时初始化对话对象与欢迎语
watch(() => props.show, (v) => {
  if (v) {
    selectedStudentId.value = studentStore.detailStudent?.id || ''
    if (messages.value.length === 0) {
      const name = studentStore.detailStudent?.name
      messages.value.push({
        role: 'pet',
        text: name ? `嗨 ${name}！我是你的宠物小搭档🐾，想聊点什么都可以跟我说～` : '嗨！我是你的宠物小搭档🐾，想聊点什么都可以跟我说～'
      })
    }
  }
})

// ===== 语音识别 (Web Speech API) =====
const SpeechRecognitionCtor =
  (window as any).SpeechRecognition || (window as any).webkitSpeechRecognition
const recognition = SpeechRecognitionCtor ? new SpeechRecognitionCtor() : null
if (recognition) {
  recognition.lang = 'zh-CN'
  recognition.interimResults = false
  recognition.maxAlternatives = 1
  recognition.onresult = (e: any) => {
    const text = e.results?.[0]?.[0]?.transcript || ''
    input.value = text
  }
  recognition.onend = () => {
    listening.value = false
    if (input.value.trim()) send()
  }
  recognition.onerror = () => {
    listening.value = false
    toast.error('语音识别出错，请直接打字')
  }
}

function toggleMic() {
  if (!recognition) {
    toast.warning('当前浏览器不支持语音输入，请直接打字（推荐 Chrome）')
    return
  }
  if (listening.value) {
    recognition.stop()
    return
  }
  try {
    recognition.start()
    listening.value = true
  } catch {
    /* 重复 start 可能抛错，忽略 */
  }
}

// ===== 发送与流式展示 =====
const delay = (ms: number) => new Promise((r) => setTimeout(r, ms))

async function scrollToBottom() {
  await nextTick()
  if (scrollEl.value) scrollEl.value.scrollTop = scrollEl.value.scrollHeight
}

async function streamReply(text: string) {
  const bubble = { role: 'pet' as const, text: '' }
  messages.value.push(bubble)
  for (const ch of text) {
    bubble.text += ch
    await delay(26)
    if (bubble.text.length % 3 === 0) scrollToBottom()
  }
  scrollToBottom()
}

async function send() {
  const text = input.value.trim()
  if (!text || sending.value) return
  input.value = ''
  messages.value.push({ role: 'user', text })
  sending.value = true
  try {
    const res = await api.post('/chat', {
      message: text,
      studentId: activeStudent.value?.id || null,
      studentName: activeStudent.value?.name || null,
      award: awardPoints.value
    })
    const reply: string = res.data?.reply || '（我没有听懂，再说一次好不好～）'
    await streamReply(reply)
    if (awardPoints.value) {
      studentStore.loadStudents().catch(() => {})
    }
  } catch {
    await streamReply('哎呀，我刚才走神了，再说一次好不好～')
  } finally {
    sending.value = false
  }
}

function clearChat() {
  messages.value = []
  const name = activeStudent.value?.name
  messages.value.push({
    role: 'pet',
    text: name ? `嗨 ${name}！我们重新开始聊天吧🐾` : '嗨！我们重新开始聊天吧🐾'
  })
}

function close() {
  if (recognition && listening.value) recognition.stop()
  emit('close')
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4" @click.self="close">
      <div class="bg-white rounded-3xl w-full max-w-lg max-h-[88vh] flex flex-col shadow-2xl animate-scale-in overflow-hidden">
        <!-- 头部 -->
        <div class="relative bg-gradient-to-r from-violet-400 via-fuchsia-400 to-pink-400 p-5 rounded-t-3xl flex items-center gap-3">
          <div class="w-12 h-12 rounded-2xl bg-white/25 flex items-center justify-center text-2xl">🐾</div>
          <div class="text-white flex-1">
            <h3 class="text-lg font-bold leading-tight">宠物语音伙伴</h3>
            <p class="text-white/85 text-xs">和老师/同学聊聊，宠物会一直陪着你</p>
          </div>
          <button @click="close" class="w-9 h-9 bg-white/20 hover:bg-white/30 rounded-full flex items-center justify-center text-white text-xl transition-colors">×</button>
        </div>

        <!-- 对象选择 + 奖励开关 -->
        <div class="flex items-center gap-3 px-5 py-3 border-b border-gray-100">
          <label class="text-sm text-gray-500">对话对象</label>
          <select
            v-model="selectedStudentId"
            class="flex-1 px-3 py-1.5 rounded-xl border border-gray-200 text-sm text-gray-700 bg-white focus:outline-none focus:ring-2 focus:ring-pink-300"
          >
            <option value="">全班（不指定学生）</option>
            <option v-for="s in studentStore.students" :key="s.id" :value="s.id">{{ s.name }}</option>
          </select>
          <label class="flex items-center gap-1.5 text-xs text-gray-500 cursor-pointer select-none">
            <input type="checkbox" v-model="awardPoints" class="accent-pink-500" />
            互动奖励
          </label>
        </div>

        <!-- 聊天区 -->
        <div ref="scrollEl" class="flex-1 overflow-y-auto px-5 py-4 space-y-3 bg-gradient-to-br from-orange-50/40 to-pink-50/40">
          <div
            v-for="(m, i) in messages"
            :key="i"
            class="flex"
            :class="m.role === 'user' ? 'justify-end' : 'justify-start'"
          >
            <div
              class="max-w-[78%] px-4 py-2.5 rounded-2xl text-sm leading-relaxed whitespace-pre-wrap break-words"
              :class="m.role === 'user'
                ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white rounded-br-md'
                : 'bg-white text-gray-700 shadow-sm rounded-bl-md border border-gray-100'"
            >
              <span v-if="m.role === 'pet'" class="mr-1">🐾</span>{{ m.text }}
            </div>
          </div>
          <div v-if="sending" class="flex justify-start">
            <div class="bg-white text-gray-400 shadow-sm rounded-2xl rounded-bl-md px-4 py-2.5 text-sm border border-gray-100">宠物正在思考…</div>
          </div>
        </div>

        <!-- 输入区 -->
        <div class="p-4 border-t border-gray-100">
          <div class="flex items-end gap-2">
            <button
              @click="toggleMic"
              :disabled="!recognition"
              class="shrink-0 w-12 h-12 rounded-2xl flex items-center justify-center text-xl transition-all"
              :class="listening
                ? 'bg-red-500 text-white animate-pulse'
                : recognition ? 'bg-violet-100 text-violet-600 hover:bg-violet-200' : 'bg-gray-100 text-gray-400'"
              :title="recognition ? (listening ? '停止聆听' : '语音输入') : '浏览器不支持语音'"
            >
              <span v-if="listening">⏹</span><span v-else>🎤</span>
            </button>
            <textarea
              v-model="input"
              @keydown.enter.exact.prevent="send"
              rows="1"
              placeholder="说点什么，或点击🎤语音输入…"
              class="flex-1 resize-none px-4 py-3 rounded-2xl border border-gray-200 text-sm text-gray-700 focus:outline-none focus:ring-2 focus:ring-pink-300 max-h-28"
            ></textarea>
            <button
              @click="send"
              :disabled="sending || !input.trim()"
              class="shrink-0 px-5 py-3 rounded-2xl bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold text-sm disabled:opacity-40 transition-all"
            >发送</button>
          </div>
          <div class="flex justify-between items-center mt-2 px-1">
            <span class="text-[11px] text-gray-400">{{ listening ? '聆听中…请说话' : 'Enter 发送 · 支持语音' }}</span>
            <button @click="clearChat" class="text-[11px] text-gray-400 hover:text-gray-600">清空对话</button>
          </div>
        </div>
      </div>
    </div>
  </Transition>
</template>
