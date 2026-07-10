<script setup lang="ts">
import { ref } from 'vue'
import { useClassStore } from '@/stores/useClassStore'
import { useStudentStore } from '@/stores/useStudentStore'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'

const emit = defineEmits<{
  createClass: []
  editClass: []
  deleteClass: []
  addStudent: []
  importStudents: []
  deleteStudents: []
  batchEval: []
  showRank: []
  showRecords: []
  showRules: []
  showSystem: []
  showLogo: []
  showAuth: []
}>()

const classStore = useClassStore()
const studentStore = useStudentStore()
const { isGuest, username, logout } = useAuth()
const toast = useToast()

const currentRole = ref<'student' | 'teacher' | 'admin'>('student')

// 从 user 中获取角色
const { user } = useAuth()
if (user.value && !user.value.isGuest) {
  currentRole.value = user.value.role || 'student'
}

// 菜单状态
const showUserMenu = ref(false)
const showClassMenu = ref(false)
const showStudentMenu = ref(false)
const showEvalMenu = ref(false)
const showPetMenu = ref(false)
const showSortMenu = ref(false)

function toggleSortMenu() {
  showSortMenu.value = !showSortMenu.value
}

function setSort(by: 'name' | 'studentNo' | 'progress', order: 'asc' | 'desc') {
  studentStore.sortBy = by
  studentStore.sortOrder = order
  showSortMenu.value = false
}

function handleLogout() {
  logout()
  showUserMenu.value = false
  classStore.loadClasses()
  toast.success('已退出登录')
}
</script>

<template>
  <header class="bg-gradient-to-r from-amber-400 via-orange-400 to-rose-400 shadow-lg px-4 py-3 flex items-center justify-between sticky top-0 z-30">
    <!-- Left -->
    <div class="flex items-center gap-3">
      <h1 class="text-xl font-bold text-white drop-shadow-lg flex items-center gap-2.5">
        <!-- LOGO -->
        <div class="relative group cursor-pointer shrink-0" @click="emit('showLogo')">
          <div class="w-10 h-10 rounded-xl overflow-hidden bg-white/10 border border-white/30 shadow-md flex items-center justify-center relative transition-all duration-300 group-hover:scale-110 group-hover:rotate-2 group-hover:shadow-lg">
            <img src="/images/logo.png" alt="Logo" class="w-[130%] h-[130%] object-cover object-top absolute -top-0.5" />
          </div>
          <div class="absolute top-12 left-0 z-50 w-64 p-3 bg-white/95 backdrop-blur-xl border border-gray-200/50 rounded-2xl shadow-2xl transition-all duration-300 origin-top-left opacity-0 scale-95 pointer-events-none group-hover:opacity-100 group-hover:scale-100 group-hover:pointer-events-auto text-left">
            <div class="aspect-square w-full rounded-xl overflow-hidden bg-gray-50 mb-2 border border-gray-100">
              <img src="/images/logo.png" class="w-full h-full object-contain" />
            </div>
            <div>
              <div class="font-bold text-gray-800 text-sm">英语全科启蒙+AI</div>
              <div class="text-[10px] text-gray-400 mt-1">💡 鼠标点击图标可查看超高清原尺寸大图</div>
            </div>
            <div class="absolute -top-1.5 left-4 w-3 h-3 bg-white border-l border-t border-gray-200/50 rotate-45"></div>
          </div>
        </div>
        <router-link to="/" class="text-gradient font-extrabold tracking-wide text-2xl hover:opacity-90 active:scale-95 transition-all cursor-pointer">班级宠物园</router-link>
      </h1>
      <select
        v-if="classStore.classes.length > 0"
        class="border-0 rounded-xl px-4 py-2 text-sm bg-white/95 hover:bg-white shadow-md backdrop-blur cursor-pointer font-medium"
        :value="classStore.currentClass?.id"
        @change="classStore.selectClass(classStore.classes.find(c => c.id === ($event.target as HTMLSelectElement).value)!)"
      >
        <option v-for="cls in classStore.classes" :key="cls.id" :value="cls.id">{{ cls.name }}</option>
      </select>
      <span class="text-sm text-white/90 font-medium bg-white/20 px-3 py-1 rounded-full">
        {{ studentStore.students.length }} 人
      </span>
    </div>

    <!-- Right -->
    <div class="flex items-center gap-1.5">
      <!-- Search -->
      <input
        v-model="studentStore.searchQuery"
        type="text"
        placeholder="🔍 搜索..."
        class="border-0 rounded-lg px-3 py-1.5 text-sm w-28 bg-white/95 hover:bg-white shadow-md focus:outline-none focus:ring-2 focus:ring-white/50 transition-all"
      />

      <!-- Pet Menu -->
      <div class="relative" v-if="!studentStore.batchMode">
        <button @click="showPetMenu = !showPetMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">🐕 宠物 ▾</button>
        <div v-if="showPetMenu" @click="showPetMenu = false" class="fixed inset-0 z-40"></div>
        <Transition name="dropdown">
          <div v-if="showPetMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
            <router-link to="/preview" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors flex items-center gap-2">📖 图鉴</router-link>
          </div>
        </Transition>
      </div>

      <!-- Sort Menu -->
      <div class="relative" v-if="!studentStore.batchMode">
        <button @click="toggleSortMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">📊 排序 ▾</button>
        <div v-if="showSortMenu" @click="showSortMenu = false" class="fixed inset-0 z-40"></div>
        <Transition name="dropdown">
          <div v-if="showSortMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
            <button @click="setSort('name', 'asc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="studentStore.sortBy === 'name' && studentStore.sortOrder === 'asc' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">🔤 A-Z</button>
            <button @click="setSort('name', 'desc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="studentStore.sortBy === 'name' && studentStore.sortOrder === 'desc' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">🔤 Z-A</button>
            <button @click="setSort('studentNo', 'asc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="studentStore.sortBy === 'studentNo' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">🔢 学号</button>
            <button @click="setSort('progress', 'desc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="studentStore.sortBy === 'progress' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">⭐ 进度</button>
          </div>
        </Transition>
      </div>

      <!-- Class Menu -->
      <div class="relative" v-if="!studentStore.batchMode && currentRole !== 'student' && !isGuest">
        <button @click="showClassMenu = !showClassMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">📚 班级 ▾</button>
        <div v-if="showClassMenu" @click="showClassMenu = false" class="fixed inset-0 z-40"></div>
        <Transition name="dropdown">
          <div v-if="showClassMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
            <button @click="emit('createClass')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">➕ 新建</button>
            <button v-if="classStore.currentClass" @click="emit('editClass')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">✏️ 重命名</button>
            <button v-if="classStore.currentClass" @click="emit('deleteClass')" class="w-full text-left px-3 py-2 text-sm text-red-500 hover:bg-red-50 transition-colors">🗑️ 删除</button>
          </div>
        </Transition>
      </div>

      <!-- Student Menu -->
      <div class="relative" v-if="classStore.currentClass && !studentStore.batchMode && currentRole !== 'student' && !isGuest">
        <button @click="showStudentMenu = !showStudentMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">👤 学生 ▾</button>
        <div v-if="showStudentMenu" @click="showStudentMenu = false" class="fixed inset-0 z-40"></div>
        <Transition name="dropdown">
          <div v-if="showStudentMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
            <button @click="emit('addStudent')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">➕ 添加</button>
            <button @click="emit('importStudents')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">📥 导入</button>
            <button @click="emit('deleteStudents')" class="w-full text-left px-3 py-2 text-sm text-red-500 hover:bg-red-50 transition-colors">🗑️ 删除</button>
          </div>
        </Transition>
      </div>

      <!-- Eval Menu -->
      <div class="relative" v-if="!studentStore.batchMode">
        <button @click="showEvalMenu = !showEvalMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">⭐ 评价 ▾</button>
        <div v-if="showEvalMenu" @click="showEvalMenu = false" class="fixed inset-0 z-40"></div>
        <Transition name="dropdown">
          <div v-if="showEvalMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-44 z-50 overflow-hidden text-left">
            <button v-if="currentRole !== 'student' && !isGuest" @click="emit('batchEval')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">✅ 批量评价</button>
            <button @click="emit('showRank')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">🏆 班级排行</button>
            <button @click="emit('showRecords')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">📋 历史记录</button>
            <template v-if="currentRole !== 'student' && !isGuest">
              <hr class="my-1.5 border-gray-100">
              <button @click="emit('showRules')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">⚙️ 规则设置</button>
              <button v-if="currentRole === 'admin'" @click="emit('showSystem')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors text-orange-600 font-medium">🛠️ 设备与系统管理</button>
            </template>
          </div>
        </Transition>
      </div>

      <!-- 取消批量评价 -->
      <button v-if="studentStore.batchMode" @click="studentStore.cancelBatchMode()"
        class="px-3 py-1.5 rounded-lg text-sm bg-red-500 hover:bg-red-600 text-white font-bold shadow-md hover:shadow-lg active:scale-95 transition-all flex items-center gap-1">
        <span>❌</span> 取消批量评价
      </button>

      <!-- User Menu -->
      <div class="relative">
        <button @click="showUserMenu = !showUserMenu" class="w-9 h-9 rounded-full bg-white/95 hover:bg-white shadow-md transition-all flex items-center justify-center overflow-hidden">
          <span v-if="isGuest" class="text-lg">👤</span>
          <span v-else class="w-full h-full rounded-full bg-gradient-to-r from-orange-400 to-pink-500 flex items-center justify-center text-white text-sm font-bold">{{ username.charAt(0).toUpperCase() }}</span>
        </button>
        <div v-if="showUserMenu" @click="showUserMenu = false" class="fixed inset-0 z-40"></div>
        <Transition name="dropdown">
          <div v-if="showUserMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-48 z-50 overflow-hidden text-left">
            <div v-if="isGuest" class="px-3 py-2 text-sm text-gray-500 border-b border-gray-100">当前为游客模式</div>
            <div v-else class="px-3 py-2 text-sm text-gray-500 border-b border-gray-100">已登录: {{ username }}</div>
            <div class="px-3 py-1.5 text-xs font-bold text-gray-400 border-b border-gray-50 bg-gray-50/50">当前权限身份</div>
            <div class="px-3 py-2 text-sm font-bold text-orange-500 bg-orange-50/30 flex items-center gap-1.5">
              <span>{{ isGuest ? '👤 访客 (只读)' : currentRole === 'admin' ? '👑 管理员' : currentRole === 'teacher' ? '👩 教师' : '👶 学生' }}</span>
            </div>
            <hr class="border-gray-100">
            <template v-if="isGuest">
              <button @click="emit('showAuth'); showUserMenu = false" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">🔑 登录 / 注册</button>
            </template>
            <template v-else>
              <button @click="handleLogout" class="w-full text-left px-3 py-2 text-sm text-red-500 hover:bg-red-50 transition-colors">🚪 退出登录</button>
            </template>
          </div>
        </Transition>
      </div>
    </div>
  </header>
</template>
