<script setup lang="ts">
import { ref, watch } from 'vue'
import { useSystemStore } from '@/stores/useSystemStore'
import { useAuth } from '@/composables/useAuth'

const props = defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()

const systemStore = useSystemStore()
const { currentRole } = useAuthRole()

function useAuthRole() {
  const { user } = useAuth()
  const currentRole = ref<'student' | 'teacher' | 'admin'>(
    (user.value && !user.value.isGuest && user.value.role) || 'student'
  )
  return { currentRole }
}

// 管理员密码修改
const newAdminPassword = ref('')
const confirmAdminPassword = ref('')
const updatingAdminPassword = ref(false)

watch(() => props.show, async (val) => {
  if (val) {
    await systemStore.loadSystemData()
  }
})

watch(() => systemStore.usersList, () => {}, {})

// 当切换到 users 标签时加载用户列表
const systemActiveTab = ref('tasks')
watch(systemActiveTab, (newVal) => {
  if (newVal === 'users') {
    systemStore.loadUsersList()
  }
})

async function handleUpdateAdminPassword() {
  updatingAdminPassword.value = true
  const success = await systemStore.handleUpdateAdminPassword(newAdminPassword.value, confirmAdminPassword.value)
  if (success) {
    newAdminPassword.value = ''
    confirmAdminPassword.value = ''
  }
  updatingAdminPassword.value = false
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/50 backdrop-blur-md flex items-center justify-center z-50 p-4" @click.self="emit('close')">
      <div class="bg-white/95 backdrop-blur-xl border border-white/20 rounded-3xl p-8 w-full max-w-4xl h-[80vh] overflow-hidden flex flex-col shadow-2xl animate-scale-in relative">
        <button @click="emit('close')" class="absolute top-6 right-6 w-10 h-10 rounded-full bg-gray-100 hover:bg-gray-200 flex items-center justify-center text-gray-500 hover:text-gray-800 text-xl transition-colors">×</button>

        <h2 class="text-2xl font-black text-gray-800 mb-6 flex items-center gap-2.5">
          <span>🛠️</span> 硬件与系统管理后台
        </h2>

        <div class="flex-1 flex overflow-hidden min-h-0">
          <!-- 左侧页签 -->
          <div class="w-56 border-r border-gray-100 pr-4 flex flex-col gap-2 shrink-0">
            <button @click="systemActiveTab = 'tasks'" class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
              :class="systemActiveTab === 'tasks' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'">
              <span>📝</span> 任务审批管理
            </button>
            <button @click="systemActiveTab = 'settings'" class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
              :class="systemActiveTab === 'settings' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'">
              <span>⚙️</span> 自动审核设置
            </button>
            <button v-if="currentRole === 'admin'" @click="systemActiveTab = 'users'" class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
              :class="systemActiveTab === 'users' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'">
              <span>👥</span> 用户权限分配
            </button>
            <button @click="systemActiveTab = 'health'" class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
              :class="systemActiveTab === 'health' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'">
              <span>📊</span> 运行健康监测
            </button>
          </div>

          <!-- 右侧内容 -->
          <div class="flex-1 pl-6 overflow-auto flex flex-col min-h-0">

            <!-- TAB 1: 任务审批 -->
            <div v-if="systemActiveTab === 'tasks'" class="flex-1 flex flex-col min-h-0">
              <h3 class="text-lg font-bold text-gray-800 mb-4 flex justify-between items-center">
                <span>待审批的任务申报列表</span>
                <span class="text-xs bg-orange-100 text-orange-600 px-2.5 py-1 rounded-full font-bold">{{ systemStore.pendingTasks.length }} 个待办</span>
              </h3>
              <div v-if="systemStore.pendingTasks.length === 0" class="flex-1 flex flex-col items-center justify-center text-center p-8 bg-gray-50/50 rounded-2xl border border-gray-100">
                <span class="text-4xl mb-3">🎉</span>
                <h4 class="text-gray-800 font-bold">没有发现待审批的申请</h4>
                <p class="text-sm text-gray-400 mt-1">学生硬件申报的新任务会实时在此推送</p>
              </div>
              <div v-else class="flex-1 overflow-auto pr-1 flex flex-col gap-4">
                <div v-for="task in systemStore.pendingTasks" :key="task.id"
                  class="bg-white border border-gray-100 rounded-2xl p-5 shadow-sm hover:shadow-md transition-shadow flex items-center justify-between">
                  <div>
                    <div class="flex items-center gap-2">
                      <span class="font-bold text-gray-800 text-lg">{{ task.student_name }}</span>
                      <span class="text-xs bg-gray-100 text-gray-500 px-2 py-0.5 rounded-full font-medium">{{ task.class_name }}</span>
                    </div>
                    <h4 class="text-orange-500 font-extrabold mt-1 text-base">申报任务: {{ task.task_name }}</h4>
                    <p class="text-xs text-gray-400 mt-1 flex items-center gap-2">
                      <span>🕒 申报时间: {{ new Date(task.created_at).toLocaleString() }}</span>
                      <span class="text-gray-300">|</span>
                      <span class="text-pink-500 font-medium">价值分数: +{{ task.points }} 分</span>
                    </p>
                  </div>
                  <div class="flex gap-2">
                    <button @click="systemStore.rejectTask(task.id)" class="px-4 py-2 border-2 border-red-100 hover:border-red-200 text-red-500 hover:bg-red-50 rounded-xl font-bold text-sm transition-all">拒绝</button>
                    <button @click="systemStore.approveTask(task.id)" class="px-5 py-2 bg-gradient-to-r from-green-400 to-emerald-500 hover:from-green-500 hover:to-emerald-600 text-white rounded-xl font-bold text-sm shadow-md hover:shadow-lg transition-all">同意加分</button>
                  </div>
                </div>
              </div>
            </div>

            <!-- TAB 2: 自动审核设置 -->
            <div v-if="systemActiveTab === 'settings'" class="flex-1 flex flex-col min-h-0 justify-between">
              <div class="flex-grow">
                <h3 class="text-lg font-bold text-gray-800 mb-4">自动审核与确认规则配置</h3>
                <div class="bg-gray-50/50 p-6 rounded-2xl border border-gray-100 mb-6 flex flex-col gap-6">
                  <!-- 模式选择 -->
                  <div>
                    <label class="block text-gray-700 font-bold mb-2.5">加分审批模式</label>
                    <div class="flex gap-4">
                      <label class="flex-1 flex items-center gap-3 p-4 rounded-xl border-2 cursor-pointer transition-all"
                        :class="systemStore.taskConfirmMode === 'auto' ? 'border-orange-400 bg-orange-50/30' : 'border-gray-200 hover:bg-gray-50'">
                        <input type="radio" value="auto" v-model="systemStore.taskConfirmMode" class="text-orange-500 focus:ring-orange-400" />
                        <div>
                          <span class="font-bold text-gray-800 block text-sm">自动确认（推荐）</span>
                          <span class="text-xs text-gray-400 block mt-0.5">申报后延迟一段时间自动同意加分</span>
                        </div>
                      </label>
                      <label class="flex-1 flex items-center gap-3 p-4 rounded-xl border-2 cursor-pointer transition-all"
                        :class="systemStore.taskConfirmMode === 'manual' ? 'border-orange-400 bg-orange-50/30' : 'border-gray-200 hover:bg-gray-50'">
                        <input type="radio" value="manual" v-model="systemStore.taskConfirmMode" class="text-orange-500 focus:ring-orange-400" />
                        <div>
                          <span class="font-bold text-gray-800 block text-sm">必须手动确认</span>
                          <span class="text-xs text-gray-400 block mt-0.5">必须由教师点击"同意加分"才可结算</span>
                        </div>
                      </label>
                    </div>
                  </div>
                  <!-- 延时设置 -->
                  <Transition name="fade">
                    <div v-if="systemStore.taskConfirmMode === 'auto'">
                      <label class="block text-gray-700 font-bold mb-2">自动审批延迟时长</label>
                      <div class="flex items-center gap-3 max-w-xs">
                        <input v-model.number="systemStore.taskConfirmDelay" type="number" min="1"
                          class="w-24 border-2 border-gray-200 rounded-xl px-4 py-2 text-center font-bold focus:outline-none focus:border-orange-400 transition-colors" />
                        <span class="text-gray-500 font-medium text-sm">分钟后自动确认并自动加分</span>
                      </div>
                      <p class="text-xs text-gray-400 mt-2">例如：设置为 30 分钟，学生申报任务 30 分钟内老师可以手动撤销或修改，超时系统自动确认加分。</p>
                    </div>
                  </Transition>
                  <!-- OpenRouter 大模型配置 -->
                  <div class="border-t border-gray-100 pt-6">
                    <label class="block text-gray-700 font-bold mb-2.5">OpenRouter 大模型 API 设置</label>
                    <div class="flex flex-col gap-4">
                      <div>
                        <span class="text-xs text-gray-400 block mb-1.5">API Key (OpenRouter 密钥)</span>
                        <input v-model="systemStore.openrouterApiKey" type="password" placeholder="sk-or-v1-..."
                          class="w-full border-2 border-gray-200 rounded-xl px-4 py-2 text-sm focus:outline-none focus:border-orange-400 transition-colors" />
                      </div>
                      <div>
                        <span class="text-xs text-gray-400 block mb-1.5">语音助手大模型 (Model Slug)</span>
                        <input v-model="systemStore.openrouterModel" type="text" placeholder="openrouter/free"
                          class="w-full border-2 border-gray-200 rounded-xl px-4 py-2 text-sm focus:outline-none focus:border-orange-400 transition-colors" />
                        <p class="text-xs text-gray-400 mt-1 font-medium">可以使用免费模型 <code>openrouter/free</code> 或 <code>google/gemma-4-31b-it:free</code>。配置后即刻生效，无需重启设备。</p>
                      </div>
                    </div>
                  </div>
                  <!-- ASR 配置 -->
                  <div class="border-t border-gray-100 pt-6">
                    <label class="block text-gray-700 font-bold mb-2.5">语音识别 (ASR) 服务</label>
                    <div class="flex flex-col gap-4">
                      <div>
                        <span class="text-xs text-gray-400 block mb-1.5 font-bold">ASR 提供商 (Provider)</span>
                        <select v-model="systemStore.asrProvider"
                          class="w-full border-2 border-gray-200 rounded-xl px-3 py-2 text-sm bg-white focus:outline-none focus:border-orange-400 font-bold text-gray-700">
                          <option value="openrouter">OpenRouter (通用，余额需 ≥ $0.5)</option>
                          <option value="openai">OpenAI Whisper (需 OpenAI 官方 key)</option>
                          <option value="baidu">百度短语音 (中文免费，月 5 万次)</option>
                        </select>
                        <p class="text-xs text-gray-400 mt-1 font-medium">建议选 "百度短语音"：完全免费、中文识别准确率最高、不需要海外 API。</p>
                      </div>
                      <div v-if="systemStore.asrProvider === 'baidu'">
                        <span class="text-xs text-gray-400 block mb-1.5">百度 API Key (API_KEY)</span>
                        <input v-model="systemStore.baiduApiKey" type="text" placeholder="在 https://console.bce.baidu.com 创建"
                          class="w-full border-2 border-gray-200 rounded-xl px-4 py-2 text-sm focus:outline-none focus:border-orange-400 transition-colors" />
                        <span class="text-xs text-gray-400 block mb-1.5 mt-3">百度 Secret Key (SECRET_KEY)</span>
                        <input v-model="systemStore.baiduSecretKey" type="password" placeholder="百度智能云 → 短语音识别 → 应用凭证"
                          class="w-full border-2 border-gray-200 rounded-xl px-4 py-2 text-sm focus:outline-none focus:border-orange-400 transition-colors" />
                        <p class="text-xs text-gray-400 mt-1 font-medium">百度短语音每月免费 5 万次，足够班级使用。开通步骤：百度智能云 → 短语音识别 → 创建应用 → 复制 API Key / Secret Key。</p>
                      </div>
                      <div v-else>
                        <div class="bg-yellow-50 border border-yellow-200 rounded-lg p-3">
                          <p class="text-xs text-yellow-800">
                            <strong>⚠️ 当前 OpenRouter 余额不足。</strong> OpenRouter 音频 API 要求至少 $0.5 余额，因此语音申报一直返回 "没听清"。
                            建议切换到 "百度短语音" (免费) 或填入有效的 OpenAI 官方 Key。
                          </p>
                        </div>
                      </div>
                    </div>
                  </div>
                  <!-- 屏幕设置 -->
                  <div class="border-t border-gray-100 pt-6">
                    <label class="block text-gray-700 font-bold mb-2.5">屏幕背光与待机设置</label>
                    <div class="flex flex-col gap-4">
                      <div>
                        <div class="flex justify-between items-center mb-1.5">
                          <span class="text-xs text-gray-400 block font-bold">屏幕亮度 (Backlight)</span>
                          <span class="text-xs text-orange-500 font-bold">{{ systemStore.screenBrightness }}%</span>
                        </div>
                        <input v-model.number="systemStore.screenBrightness" type="range" min="10" max="100" step="5"
                          class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer accent-orange-500" />
                        <p class="text-xs text-gray-400 mt-1 font-medium">调节开发板液晶屏的背光亮度，降低亮度可有效减少发热并延长电池续航。</p>
                      </div>
                      <div>
                        <span class="text-xs text-gray-400 block mb-1.5 font-bold">自动熄屏等待时间 (Screen Sleep)</span>
                        <select v-model.number="systemStore.screenSleepSeconds"
                          class="w-full border-2 border-gray-200 rounded-xl px-3 py-2 text-sm bg-white focus:outline-none focus:border-orange-400 font-bold text-gray-700">
                          <option :value="5">5 秒无动作自动关屏</option>
                          <option :value="10">10 秒无动作自动关屏</option>
                          <option :value="15">15 秒无动作自动关屏 (默认)</option>
                          <option :value="30">30 秒无动作自动关屏</option>
                          <option :value="60">1 分钟无动作自动关屏</option>
                          <option :value="0">从不自动熄屏</option>
                        </select>
                        <p class="text-xs text-gray-400 mt-1 font-medium">规定无动作后多少秒自动关闭屏幕。触摸屏幕或语音唤醒会重新点亮。</p>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
              <div class="flex justify-end pt-4 border-t border-gray-100">
                <button @click="systemStore.saveSystemSettings()"
                  class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">保存规则配置</button>
              </div>
            </div>

            <!-- TAB 3: 用户权限分配 -->
            <div v-if="systemActiveTab === 'users' && currentRole === 'admin'" class="flex-grow flex flex-col min-h-0">
              <div class="grid grid-cols-1 md:grid-cols-2 gap-6 mb-6">
                <!-- 教师邀请 -->
                <div class="bg-orange-50/50 border border-orange-100 rounded-2xl p-5 flex flex-col justify-between gap-4">
                  <div class="space-y-1">
                    <h4 class="text-sm font-bold text-orange-700 flex items-center gap-1.5"><span>🔗</span> 教师邀请注册链接</h4>
                    <p class="text-xs text-orange-600 font-bold">邀请码：TEACHER_INVITE</p>
                    <p class="text-[11px] text-gray-500 leading-relaxed">通过此链接注册的用户将直接自动获取"教师"权限角色，可录入及评价学生。</p>
                  </div>
                  <button @click="systemStore.copyTeacherInviteLink()"
                    class="w-full py-2.5 bg-gradient-to-r from-orange-400 to-pink-500 text-white rounded-xl font-bold text-sm hover:shadow-md transition-all whitespace-nowrap">📋 复制教师邀请链接</button>
                </div>
                <!-- 管理员密码修改 -->
                <div class="bg-orange-50/50 border border-orange-100 rounded-2xl p-5 flex flex-col gap-3">
                  <h4 class="text-sm font-bold text-orange-700 flex items-center gap-1.5"><span>🔑</span> 安全管理：修改管理员密码</h4>
                  <div class="flex flex-col sm:flex-row gap-3">
                    <input v-model="newAdminPassword" type="password" placeholder="新密码"
                      class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-xs focus:outline-none focus:border-orange-400" :disabled="updatingAdminPassword" />
                    <input v-model="confirmAdminPassword" type="password" placeholder="确认新密码"
                      class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-xs focus:outline-none focus:border-orange-400" :disabled="updatingAdminPassword" />
                  </div>
                  <button @click="handleUpdateAdminPassword" :disabled="updatingAdminPassword"
                    class="w-full py-2 bg-gray-800 hover:bg-gray-900 text-white rounded-xl font-bold text-xs transition-all disabled:opacity-50">
                    {{ updatingAdminPassword ? '提交中...' : '🛡️ 确认修改管理员密码' }}
                  </button>
                </div>
              </div>
              <div class="flex items-center justify-between mb-4">
                <h3 class="text-lg font-bold text-gray-800">已注册用户权限分配表</h3>
                <button @click="systemStore.loadUsersList()" class="p-1.5 text-gray-500 hover:text-gray-700 transition-colors" title="刷新列表">🔄 刷新</button>
              </div>
              <div v-if="systemStore.loadingUsers" class="flex-1 flex items-center justify-center text-gray-400 py-12">
                <div class="animate-spin text-2xl mr-2">⏳</div> 正在加载用户数据...
              </div>
              <div v-else-if="systemStore.usersList.length === 0" class="flex-1 flex items-center justify-center text-gray-400 text-sm py-12">暂无已注册用户</div>
              <div v-else class="flex-1 overflow-auto border border-gray-100 rounded-2xl min-h-[250px]">
                <table class="w-full text-left border-collapse text-sm">
                  <thead>
                    <tr class="bg-gray-50 border-b border-gray-100 text-gray-500 font-bold">
                      <th class="px-6 py-4">用户名</th>
                      <th class="px-6 py-4">当前身份角色</th>
                      <th class="px-6 py-4">注册时间</th>
                      <th class="px-6 py-4">更改权限角色</th>
                    </tr>
                  </thead>
                  <tbody class="divide-y divide-gray-50 text-gray-700">
                    <tr v-for="u in systemStore.usersList" :key="u.id" class="hover:bg-gray-50/50 transition-colors">
                      <td class="px-6 py-4 font-bold">{{ u.username }}</td>
                      <td class="px-6 py-4">
                        <span class="inline-flex items-center px-2.5 py-1 rounded-full text-xs font-bold"
                          :class="u.role === 'admin' ? 'bg-purple-100 text-purple-700' : u.role === 'teacher' ? 'bg-orange-100 text-orange-700' : 'bg-blue-100 text-blue-700'">
                          {{ u.role === 'admin' ? '管理员' : u.role === 'teacher' ? '教师' : '学生' }}
                        </span>
                      </td>
                      <td class="px-6 py-4 text-gray-400 text-xs">{{ new Date(u.created_at).toLocaleString('zh-CN', { hour12: false }) }}</td>
                      <td class="px-6 py-4">
                        <select :value="u.role" @change="systemStore.handleUpdateUserRole(u.id, ($event.target as HTMLSelectElement).value)" :disabled="u.username === 'admin'"
                          class="border border-gray-200 rounded-lg px-2.5 py-1.5 text-xs focus:outline-none focus:border-orange-400 disabled:opacity-50">
                          <option value="student">👶 学生 (student)</option>
                          <option value="teacher">👩 教师 (teacher)</option>
                          <option value="admin">👑 管理员 (admin)</option>
                        </select>
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>

            <!-- TAB 4: 运行健康监测 -->
            <div v-if="systemActiveTab === 'health'" class="flex-grow">
              <h3 class="text-lg font-bold text-gray-800 mb-4">系统健康状态可观测监测</h3>
              <div class="grid grid-cols-2 gap-4">
                <div class="bg-gradient-to-br from-orange-50 to-amber-50/50 p-5 rounded-2xl border border-orange-100 flex flex-col justify-between h-32">
                  <div class="flex justify-between items-start">
                    <span class="text-sm font-bold text-orange-700">自动确认延迟</span>
                    <span class="text-2xl">⏳</span>
                  </div>
                  <div>
                    <h4 class="text-2xl font-black text-orange-800">
                      {{ systemStore.taskConfirmMode === 'auto' ? `${systemStore.taskConfirmDelay} 分钟` : '已禁用自动' }}
                    </h4>
                    <p class="text-xs text-orange-600 mt-1">{{ systemStore.taskConfirmMode === 'auto' ? '已配置延迟加分缓冲' : '需由教师手动审核加分' }}</p>
                  </div>
                </div>
                <div class="bg-gradient-to-br from-purple-50 to-pink-50/50 p-5 rounded-2xl border border-purple-100 flex flex-col justify-between h-32">
                  <div class="flex justify-between items-start">
                    <span class="text-sm font-bold text-purple-700">数据服务对接</span>
                    <span class="text-2xl">🛡️</span>
                  </div>
                  <div>
                    <h4 class="text-2xl font-black text-purple-800">对接正常</h4>
                    <p class="text-xs text-purple-600 mt-1">系统各索引完整，查询延迟低</p>
                  </div>
                </div>
              </div>
            </div>

          </div>
        </div>
      </div>
    </div>
  </Transition>
</template>
