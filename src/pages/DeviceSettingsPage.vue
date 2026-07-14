<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuth } from '@/composables/useAuth'
import { useStudentStore } from '@/stores/useStudentStore'
import { useToast } from '@/composables/useToast'

const route = useRoute()
const router = useRouter()
const { api, isGuest } = useAuth()
const studentStore = useStudentStore()
const toast = useToast()

const studentId = computed(() => route.params.id as string)
const student = computed(() => studentStore.students.find(s => s.id === studentId.value))

// 设备固件当前编译版本（与固件 DeviceSettings.FIRMWARE_VERSION 一致）
const CURRENT_FIRMWARE = '2.1.0'

const loading = ref(false)
const latestFirmware = ref('')
const settings = ref({ screen_brightness: 80, screen_sleep_seconds: 15 })
const saving = ref(false)
const brightnessPreview = ref(80)

const isOnline = computed(() => {
  const ls = student.value?.last_seen
  if (!ls) return false
  return Date.now() - ls < 5 * 60 * 1000
})

function fmtSeen(ts?: number | null) {
  if (!ts) return '未连接'
  const d = new Date(ts)
  return d.toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
}

const sysInfo = [
  { label: '主控芯片', value: 'ESP32-S3 (N16R8)' },
  { label: 'Flash 存储', value: '16 MB' },
  { label: 'PSRAM', value: '8 MB (OPI)' },
  { label: '显示屏', value: '2.8" 240×320 SPI' },
  { label: '触摸屏', value: 'FT6336G 电容触控' },
  { label: '音频编解码', value: 'ES8311' },
  { label: '无线连接', value: 'Wi-Fi 2.4G' },
  { label: '宠物存储', value: 'TF 卡 (8GB)' }
]

async function load() {
  loading.value = true
  try {
    const [setRes, fwRes] = await Promise.all([
      api.get('/device/settings'),
      api.get('/device/firmware-version').catch(() => null)
    ])
    const s = setRes.data || {}
    settings.value = {
      screen_brightness: s.screen_brightness ?? 80,
      screen_sleep_seconds: s.screen_sleep_seconds ?? 15
    }
    brightnessPreview.value = settings.value.screen_brightness
    if (fwRes && fwRes.data) latestFirmware.value = fwRes.data.latest_version || ''
  } catch (e: any) {
    toast.error('加载失败：' + (e?.response?.data?.error || e.message))
  } finally {
    loading.value = false
  }
}

async function saveSettings() {
  saving.value = true
  try {
    await api.post('/device/settings', {
      screen_brightness: settings.value.screen_brightness,
      screen_sleep_seconds: settings.value.screen_sleep_seconds
    })
    toast.success('设备设置已保存')
  } catch (e: any) {
    toast.error('保存失败：' + (e?.response?.data?.error || e.message))
  } finally {
    saving.value = false
  }
}

function goBack() {
  if (window.history.length > 1) router.back()
  else router.push('/')
}

onMounted(load)
</script>

<template>
  <div class="min-h-screen bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 flex flex-col">
    <header class="bg-gradient-to-r from-amber-400 via-orange-400 to-rose-400 shadow-lg px-4 py-3 flex items-center justify-between sticky top-0 z-30">
      <div class="flex items-center gap-3">
        <button @click="goBack" class="w-9 h-9 rounded-full bg-white/20 hover:bg-white/30 flex items-center justify-center text-white text-xl transition-colors" title="返回">←</button>
        <div>
          <h1 class="text-lg font-bold text-white drop-shadow">⚙️ 设备设置</h1>
          <p class="text-white/80 text-xs">{{ student?.name || '学生' }} 的设备</p>
        </div>
      </div>
      <span v-if="isGuest" class="text-xs text-white/80 bg-white/20 px-2 py-1 rounded-full">演示模式</span>
    </header>

    <main class="flex-1 overflow-auto p-4 pb-12 space-y-4">
      <div v-if="loading" class="text-center text-gray-400 py-16">加载中…</div>

      <template v-else>
        <!-- 设备状态 -->
        <div class="bg-white rounded-2xl shadow p-4">
          <div class="font-bold text-gray-700 mb-3 flex items-center gap-2">📟 设备状态</div>
          <div class="grid grid-cols-2 gap-3">
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">设备机器码 (MAC)</div>
              <div class="font-mono text-sm text-gray-800 mt-0.5">{{ student?.device_id || '未绑定' }}</div>
            </div>
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">连接状态</div>
              <div class="mt-0.5 font-bold" :class="isOnline ? 'text-green-500' : 'text-gray-400'">
                {{ isOnline ? '● 在线' : '○ 离线' }}
              </div>
            </div>
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">最后在线</div>
              <div class="text-sm text-gray-800 mt-0.5">{{ fmtSeen(student?.last_seen) }}</div>
            </div>
            <div class="bg-gray-50 rounded-xl p-3">
              <div class="text-xs text-gray-400">电量</div>
              <div class="text-sm text-gray-800 mt-0.5">
                {{ student?.battery_level != null ? student.battery_level + '%' : '—' }}
                <span v-if="student?.is_charging" class="text-green-500 ml-1">🔌 充电中</span>
              </div>
            </div>
          </div>
        </div>

        <!-- 固件 & 系统信息 -->
        <div class="bg-white rounded-2xl shadow p-4">
          <div class="font-bold text-gray-700 mb-3 flex items-center gap-2">ℹ️ 固件与系统信息</div>
          <div class="grid grid-cols-2 gap-x-4 gap-y-2">
            <div class="flex justify-between text-sm">
              <span class="text-gray-400">当前固件</span>
              <span class="font-bold text-gray-800">v{{ CURRENT_FIRMWARE }}</span>
            </div>
            <div class="flex justify-between text-sm">
              <span class="text-gray-400">最新固件</span>
              <span class="font-bold text-gray-800">{{ latestFirmware ? 'v' + latestFirmware : 'v' + CURRENT_FIRMWARE }}</span>
            </div>
            <template v-for="info in sysInfo" :key="info.label">
              <div class="flex justify-between text-sm col-span-1">
                <span class="text-gray-400">{{ info.label }}</span>
                <span class="text-gray-800 text-right">{{ info.value }}</span>
              </div>
            </template>
          </div>
        </div>

        <!-- 服务端可下发设置 -->
        <div class="bg-white rounded-2xl shadow p-4 space-y-4">
          <div class="font-bold text-gray-700 flex items-center gap-2">🎚️ 服务端显示设置</div>

          <div>
            <div class="flex justify-between text-sm mb-1">
              <span class="text-gray-600">屏幕亮度</span>
              <span class="font-bold text-orange-500">{{ settings.screen_brightness }}%</span>
            </div>
            <input type="range" min="10" max="100" v-model.number="settings.screen_brightness" @input="brightnessPreview = settings.screen_brightness"
              class="w-full accent-orange-500" />
          </div>

          <div>
            <div class="text-sm text-gray-600 mb-1">息屏时间（秒）</div>
            <input type="number" min="5" max="600" v-model.number="settings.screen_sleep_seconds"
              class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-orange-300" />
          </div>

          <button @click="saveSettings" :disabled="saving"
            class="w-full bg-gradient-to-r from-orange-400 to-pink-500 text-white font-bold py-2.5 rounded-xl hover:shadow-lg transition-all disabled:opacity-50">
            保存设置
          </button>
          <p class="text-xs text-gray-400 leading-relaxed">
            说明：音量、待机时间、息屏时间等本地参数在设备「设置」屏上直接调节并保存在设备内；
            以上亮度 / 息屏秒数为服务端偏好，设备联网后可同步应用。
          </p>
        </div>
      </template>
    </main>
  </div>
</template>
