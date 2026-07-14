<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'
import { useDeviceStore } from '@/stores/useDeviceStore'
import { useStudentStore } from '@/stores/useStudentStore'
import { useClassStore } from '@/stores/useClassStore'
import type { Device } from '@/types'

const router = useRouter()
const { user } = useAuth()
const toast = useToast()
const deviceStore = useDeviceStore()
const studentStore = useStudentStore()
const classStore = useClassStore()

const currentRole = computed<'student' | 'teacher' | 'admin'>(() => {
  if (user.value && !user.value.isGuest) return user.value.role || 'student'
  return 'student'
})

const selected = ref<Device | null>(null)
const registerId = ref('')
const registerName = ref('')
const savingConfig = ref(false)
const bindingStudentId = ref('')

const isOnline = (d?: Device) => {
  if (!d?.last_seen) return false
  return Date.now() - d.last_seen < 5 * 60 * 1000
}
function fmtSeen(ts?: number | null) {
  if (!ts) return '未连接'
  return new Date(ts).toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
}

const config = ref({ screen_brightness: 80, screen_sleep_seconds: 15, asr_provider: 'workers-ai' })

async function selectDevice(d: Device) {
  selected.value = d
  bindingStudentId.value = d.student_id || ''
  try {
    const res = await deviceStore.loadDeviceSettings(d.device_id)
    config.value = {
      screen_brightness: res.settings.screen_brightness ?? 80,
      screen_sleep_seconds: res.settings.screen_sleep_seconds ?? 15,
      asr_provider: res.settings.asr_provider ?? 'workers-ai'
    }
  } catch {
    config.value = { screen_brightness: 80, screen_sleep_seconds: 15, asr_provider: 'workers-ai' }
  }
}

async function saveConfig() {
  if (!selected.value) return
  savingConfig.value = true
  try {
    await deviceStore.saveDeviceSettings(selected.value.device_id, { ...config.value })
    toast.success('设备配置已保存')
  } catch (e: any) {
    toast.error('保存失败：' + (e?.response?.data?.error || e.message))
  } finally {
    savingConfig.value = false
  }
}

async function bindStudent() {
  if (!selected.value) return
  const ok = await deviceStore.updateDevice(selected.value.device_id, { student_id: bindingStudentId.value || null })
  if (ok) {
    selected.value = deviceStore.devices.find(d => d.device_id === selected.value!.device_id) || null
    if (selected.value) await selectDevice(selected.value)
  }
}

async function renameDevice() {
  if (!selected.value || !selected.value.name) return
  await deviceStore.updateDevice(selected.value.device_id, { name: selected.value.name })
}

async function removeDevice() {
  if (!selected.value) return
  if (!confirm(`确认删除设备 ${selected.value.device_id}？该设备将解除与学生绑定。`)) return
  const ok = await deviceStore.deleteDevice(selected.value.device_id)
  if (ok) selected.value = null
}

async function doRegister() {
  const id = registerId.value.trim()
  if (!id) { toast.error('请输入设备机器码 (MAC)'); return }
  const ok = await deviceStore.registerDevice(id, registerName.value.trim() || id, classStore.currentClass?.id || undefined)
  if (ok) {
    registerId.value = ''
    registerName.value = ''
  }
}

function goBack() {
  if (window.history.length > 1) router.back()
  else router.push('/')
}

onMounted(async () => {
  if (currentRole.value === 'student') { router.replace('/'); return }
  await deviceStore.loadDevices()
  if (classStore.currentClass) await studentStore.loadStudents()
})
</script>

<template>
  <div class="min-h-screen bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 flex flex-col">
    <header class="bg-gradient-to-r from-amber-400 via-orange-400 to-rose-400 shadow-lg px-4 py-3 flex items-center justify-between sticky top-0 z-30">
      <div class="flex items-center gap-3">
        <button @click="goBack" class="w-9 h-9 rounded-full bg-white/20 hover:bg-white/30 flex items-center justify-center text-white text-xl transition-colors" title="返回">←</button>
        <div>
          <h1 class="text-lg font-bold text-white drop-shadow">📟 设备管理</h1>
          <p class="text-white/80 text-xs">按设备终端独立管理与配置</p>
        </div>
      </div>
    </header>

    <main class="flex-1 overflow-auto p-4 pb-12 grid grid-cols-1 lg:grid-cols-2 gap-4">
      <!-- 左: 设备列表 + 登记 -->
      <div class="space-y-4">
        <div class="bg-white rounded-2xl shadow p-4">
          <div class="font-bold text-gray-700 mb-3 flex items-center gap-2">➕ 登记新设备</div>
          <p class="text-xs text-gray-400 mb-3">开发板开机连上 Wi-Fi 后会自动出现在下方列表；也可手动输入机器码 (MAC) 登记。</p>
          <input v-model="registerId" type="text" placeholder="设备机器码 / MAC，如 28:84:85:42:1C:74"
            class="w-full border-2 border-gray-200 rounded-xl px-4 py-2.5 mb-3 text-sm font-mono focus:outline-none focus:border-orange-400 transition-colors" />
          <input v-model="registerName" type="text" placeholder="设备别名（可选），如 1号座位机"
            class="w-full border-2 border-gray-200 rounded-xl px-4 py-2.5 mb-3 text-sm focus:outline-none focus:border-orange-400 transition-colors" />
          <button @click="doRegister" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all">登记设备</button>
        </div>

        <div class="bg-white rounded-2xl shadow p-4">
          <div class="font-bold text-gray-700 mb-3 flex items-center gap-2">📋 全部设备 ({{ deviceStore.devices.length }})</div>
          <div v-if="deviceStore.devices.length === 0" class="text-center text-gray-400 py-8 text-sm">暂无设备，开机或登记后显示</div>
          <div class="space-y-2">
            <button v-for="d in deviceStore.devices" :key="d.device_id" @click="selectDevice(d)"
              class="w-full text-left p-3 rounded-xl border-2 transition-all flex items-center justify-between gap-3"
              :class="selected && selected.device_id === d.device_id ? 'border-orange-400 bg-orange-50/40' : 'border-gray-100 hover:border-orange-200'">
              <div class="min-w-0">
                <div class="font-bold text-gray-800 text-sm truncate">{{ d.name || '未命名设备' }}</div>
                <div class="font-mono text-xs text-gray-400 truncate">{{ d.device_id }}</div>
                <div class="text-xs mt-0.5" :class="d.student_id ? 'text-green-600' : 'text-gray-400'">
                  {{ d.student_name ? '绑定: ' + d.student_name : '未绑定学生' }}
                </div>
              </div>
              <div class="text-right shrink-0">
                <div class="font-bold text-sm" :class="isOnline(d) ? 'text-green-500' : 'text-gray-400'">{{ isOnline(d) ? '● 在线' : '○ 离线' }}</div>
                <div class="text-xs text-gray-400 mt-0.5">
                  {{ d.battery_level != null ? d.battery_level + '%' : '—' }}
                  <span v-if="d.is_charging" class="text-green-500">🔌</span>
                </div>
              </div>
            </button>
          </div>
        </div>
      </div>

      <!-- 右: 选中设备详情 + 配置 -->
      <div>
        <div v-if="!selected" class="bg-white rounded-2xl shadow p-8 text-center text-gray-400">
          <div class="text-4xl mb-3">📟</div>
          从左侧选择一个设备以查看状态并配置
        </div>
        <div v-else class="bg-white rounded-2xl shadow p-4 space-y-5">
          <div class="flex items-center justify-between">
            <div>
              <div class="font-bold text-gray-800">{{ selected.name || '未命名设备' }}</div>
              <div class="font-mono text-xs text-gray-400">{{ selected.device_id }}</div>
            </div>
            <span class="text-xs font-bold px-2 py-1 rounded-full" :class="isOnline(selected) ? 'bg-green-100 text-green-600' : 'bg-gray-100 text-gray-400'">
              {{ isOnline(selected) ? '● 在线' : '○ 离线' }}
            </span>
          </div>

          <!-- 设备信息 -->
          <div class="grid grid-cols-2 gap-3 text-sm">
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">最后在线</div>
              <div class="text-gray-800 mt-0.5">{{ fmtSeen(selected.last_seen) }}</div>
            </div>
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">电量 / 充电</div>
              <div class="text-gray-800 mt-0.5">{{ selected.battery_level != null ? selected.battery_level + '%' : '—' }} <span v-if="selected.is_charging" class="text-green-500">🔌</span></div>
            </div>
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">固件版本</div>
              <div class="text-gray-800 mt-0.5">{{ selected.firmware_version || '—' }}</div>
            </div>
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">绑定学生</div>
              <div class="text-gray-800 mt-0.5 truncate">{{ selected.student_name || '未绑定' }}</div>
            </div>
          </div>

          <!-- 绑定学生 -->
          <div class="border-t border-gray-100 pt-4">
            <label class="block text-gray-700 font-bold mb-2">绑定学生</label>
            <div class="flex gap-2">
              <select v-model="bindingStudentId" class="flex-1 border-2 border-gray-200 rounded-xl px-3 py-2 text-sm bg-white focus:outline-none focus:border-orange-400">
                <option value="">— 不绑定 —</option>
                <option v-for="s in studentStore.students" :key="s.id" :value="s.id">{{ s.name }} <template v-if="s.student_no">({{ s.student_no }})</template></option>
              </select>
              <button @click="bindStudent" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-4 py-2 rounded-xl font-bold text-sm">保存</button>
            </div>
          </div>

          <!-- 重命名 -->
          <div>
            <label class="block text-gray-700 font-bold mb-2">设备别名</label>
            <div class="flex gap-2">
              <input v-model="selected.name" type="text" placeholder="设备别名"
                class="flex-1 border-2 border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:border-orange-400" />
              <button @click="renameDevice" class="bg-gray-800 text-white px-4 py-2 rounded-xl font-bold text-sm">改名</button>
            </div>
          </div>

          <!-- 设备级服务端配置 -->
          <div class="border-t border-gray-100 pt-4 space-y-4">
            <div class="font-bold text-gray-700 flex items-center gap-2">🎚️ 该设备专属设置</div>
            <div>
              <div class="flex justify-between text-sm mb-1">
                <span class="text-gray-600">屏幕亮度</span>
                <span class="font-bold text-orange-500">{{ config.screen_brightness }}%</span>
              </div>
              <input type="range" min="10" max="100" v-model.number="config.screen_brightness" class="w-full accent-orange-500" />
            </div>
            <div>
              <div class="text-sm text-gray-600 mb-1">息屏时间（秒）</div>
              <input type="number" min="5" max="600" v-model.number="config.screen_sleep_seconds"
                class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
            </div>
            <div>
              <div class="text-sm text-gray-600 mb-1 font-bold">语音识别 (ASR) 提供商</div>
              <select v-model="config.asr_provider"
                class="w-full border-2 border-gray-200 rounded-xl px-3 py-2 text-sm bg-white focus:outline-none focus:border-orange-400 font-bold text-gray-700">
                <option value="groq">Groq Whisper (推荐，免费且极速)</option>
                <option value="openrouter">OpenRouter (通用，余额需 ≥ $0.5)</option>
                <option value="openai">OpenAI Whisper (需 OpenAI 官方 key)</option>
                <option value="baidu">百度短语音 (中文免费，月 5 万次)</option>
                <option value="workers-ai">Workers AI (默认)</option>
              </select>
              <p class="text-xs text-gray-400 mt-1">留空或默认将使用全局设置；此处仅作用于当前设备。</p>
            </div>
            <button @click="saveConfig" :disabled="savingConfig"
              class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">
              保存设备设置
            </button>
          </div>

          <button @click="removeDevice" class="w-full text-red-500 border border-red-200 hover:bg-red-50 rounded-xl py-2.5 text-sm font-medium transition-colors">
            🗑️ 删除该设备
          </button>
        </div>
      </div>
    </main>
  </div>
</template>
