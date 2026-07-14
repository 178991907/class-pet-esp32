<script setup lang="ts">
import { ref, computed, onMounted, watch, nextTick } from 'vue'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'
import { useConfirm } from '@/composables/useConfirm'
import { useClassStore } from '@/stores/useClassStore'
import { useStudentStore } from '@/stores/useStudentStore'
import { useSystemStore } from '@/stores/useSystemStore'

import LoadingScreen from '@/components/LoadingScreen.vue'
import LevelUpAnimation from '@/components/LevelUpAnimation.vue'
import ConfirmDialog from '@/components/ConfirmDialog.vue'
import AuthModal from '@/components/AuthModal.vue'
import StudentCard from '@/components/StudentCard.vue'
import AppHeader from '@/components/layout/AppHeader.vue'

import ClassModal from '@/components/modals/ClassModal.vue'
import StudentModal from '@/components/modals/StudentModal.vue'
import ImportModal from '@/components/modals/ImportModal.vue'
import EvalModal from '@/components/modals/EvalModal.vue'
import PetSelectModal from '@/components/modals/PetSelectModal.vue'
import RankModal from '@/components/modals/RankModal.vue'
import RecordsModal from '@/components/modals/RecordsModal.vue'
import RulesModal from '@/components/modals/RulesModal.vue'
import LogoModal from '@/components/modals/LogoModal.vue'
import SystemModal from '@/components/modals/SystemModal.vue'
import ChatLogsModal from '@/components/modals/ChatLogsModal.vue'
import StudentDetailPanel from '@/components/modals/StudentDetailPanel.vue'
import WebVoiceChat from '@/components/modals/WebVoiceChat.vue'
import { useRealtime } from '@/composables/useRealtime'

import type { Student } from '@/types'

// ===== 基础依赖 =====
const toast = useToast()
const { isGuest, user } = useAuth()
const { confirmDialog, showConfirm } = useConfirm()
const classStore = useClassStore()
const studentStore = useStudentStore()
const systemStore = useSystemStore()

// P3: 实时刷新（设备互动实时推送到网页）
const realtime = useRealtime()
const rtConnected = realtime.connected
const rtSimulated = realtime.simulated
const rtLastEvent = realtime.lastEvent
const showVoiceChat = ref(false)

// ===== 角色管理 =====
const currentRole = computed<'student' | 'teacher' | 'admin'>(() => {
  if (user.value && !user.value.isGuest) {
    return user.value.role || 'student'
  }
  return 'student'
})

// ===== 加载状态 =====
const isLoaded = ref(false)
const isLoading = ref(true)

// ===== 模态框可见性 =====
const showClassModal = ref(false)
const showStudentModal = ref(false)
const showImportModal = ref(false)
const showAddModal = ref(false)
const showPetModal = ref(false)
const showRankModal = ref(false)
const showRecordsModal = ref(false)
const showRulesModal = ref(false)
const showLogoModal = ref(false)
const showSystemModal = ref(false)
const showChatLogsModal = ref(false)
const showDetailPanel = ref(false)
const showAuthModal = ref(false)

// 组件 ref（用于调用 exposed 方法）
const classModalRef = ref<InstanceType<typeof ClassModal>>()
const studentModalRef = ref<InstanceType<typeof StudentModal>>()

// ===== 监听：班级切换时自动加载学生 =====
watch(() => classStore.currentClass, () => {
  studentStore.loadStudents()
})

// ===== 监听：日程/聊天模态框跟随 store 状态 =====
watch(() => systemStore.auditingStudent, (val) => {
  showChatLogsModal.value = !!val
})

// ===== 监听：详情面板跟随 store 状态 =====
watch(() => studentStore.detailStudent, (val) => {
  showDetailPanel.value = !!val
})

// ===== 事件处理 =====

// 班级操作
function handleCreateClass() {
  classModalRef.value?.open()
  showClassModal.value = true
}
function handleEditClass() {
  if (!classStore.currentClass) return
  classModalRef.value?.open(classStore.currentClass.id, classStore.currentClass.name)
  showClassModal.value = true
}
function handleDeleteClass() {
  if (!classStore.currentClass) return
  showConfirm({
    title: '删除班级',
    message: '确定删除该班级？所有学生数据将一并删除！',
    confirmText: '删除',
    cancelText: '取消',
    type: 'danger',
    onConfirm: () => classStore.deleteClass(classStore.currentClass!.id)
  })
}

// 学生操作
function handleAddStudent() {
  studentModalRef.value?.open()
  showStudentModal.value = true
}
function handleEditStudent(student: Student) {
  studentStore.closeDetailPanel()
  studentModalRef.value?.open(student)
  showStudentModal.value = true
}
function handleDeleteStudentsMode() {
  studentStore.showDeleteStudentMode = true
  studentStore.deleteStudentList = []
}
function handleBatchDelete() {
  showConfirm({
    title: '删除学生',
    message: `确定删除 ${studentStore.deleteStudentList.length} 名学生？此操作不可恢复！`,
    confirmText: '删除',
    cancelText: '取消',
    type: 'danger',
    onConfirm: () => studentStore.batchDeleteStudents()
  })
}

// 评价操作
function handleBatchEval() {
  studentStore.startBatchMode()
}
function handleBatchAddPoints() {
  studentStore.batchAddPoints()
  showAddModal.value = true
}
function handleBatchSubPoints() {
  studentStore.batchSubPoints()
  showAddModal.value = true
}

// 记录
async function handleShowRecords() {
  await studentStore.loadEvaluationRecords()
  showRecordsModal.value = true
}

// 系统
function handleShowSystem() {
  if (currentRole.value !== 'admin') {
    toast.error('仅管理员可使用此功能')
    return
  }
  showSystemModal.value = true
}

// 学生卡片点击
function handleStudentClick(student: Student) {
  if (!student.pet_type) {
    showConfirm({
      title: '领养宠物',
      message: `${student.name} 还没有领养宠物，是否现在领养？`,
      confirmText: '去领养',
      cancelText: '暂不',
      type: 'info',
      onConfirm: () => {
        studentStore.selectedStudent = student
        showPetModal.value = true
      }
    })
    return
  }
  studentStore.openDetailPanel(student)
}

// 详情面板 - 换宠物
function handleChangePet(student: Student) {
  studentStore.selectedStudent = student
  showPetModal.value = true
}

// 关闭聊天时清理 store
function closeChatLogsModal() {
  showChatLogsModal.value = false
  systemStore.auditingStudent = null
}

// ===== 初始化 =====
onMounted(async () => {
  isLoading.value = true
  try {
    await classStore.loadClasses()
    await studentStore.loadRules()
    await systemStore.loadSystemData()
  } finally {
    isLoading.value = false
    nextTick(() => {
      isLoaded.value = true
    })
    realtime.connect()
  }
})
</script>

<template>
  <div class="min-h-screen bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 flex flex-col">

    <!-- 加载动画 -->
    <LoadingScreen :show="isLoading" />

    <!-- 升级动画 -->
    <LevelUpAnimation />

    <!-- 顶部导航栏 -->
    <AppHeader
      @create-class="handleCreateClass"
      @edit-class="handleEditClass"
      @delete-class="handleDeleteClass"
      @add-student="handleAddStudent"
      @import-students="showImportModal = true"
      @delete-students="handleDeleteStudentsMode"
      @batch-eval="handleBatchEval"
      @show-rank="showRankModal = true"
      @show-records="handleShowRecords"
      @show-rules="showRulesModal = true"
      @show-system="handleShowSystem"
      @show-logo="showLogoModal = true"
      @show-auth="showAuthModal = true"
    />

    <!-- 主内容区 -->
    <main class="flex-1 overflow-auto p-6 pb-28">
      <Transition name="fade" mode="out-in">
        <!-- 无班级状态 -->
        <div v-if="classStore.classes.length === 0" key="no-class" class="flex flex-col items-center justify-center min-h-[60vh]">
          <div class="text-8xl mb-6 animate-float">🏫</div>
          <h3 class="text-2xl font-bold text-gray-700 mb-3">还没有班级</h3>
          <p class="text-gray-500 mb-6 text-lg">
            {{ currentRole === 'student' || isGuest ? '暂无班级档案，请等待老师创建班级。' : '创建一个班级，开启你的宠物园之旅吧！' }}
          </p>
          <button
            v-if="currentRole !== 'student' && !isGuest"
            @click="handleCreateClass"
            class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-2xl hover:shadow-lg hover:scale-105 transition-all font-bold text-lg"
          >✨ 创建第一个班级</button>
        </div>

        <!-- 无学生状态 -->
        <div v-else-if="studentStore.students.length === 0" key="no-student" class="flex flex-col items-center justify-center min-h-[60vh]">
          <div class="text-8xl mb-6 animate-float">👨‍🎓</div>
          <h3 class="text-2xl font-bold text-gray-700 mb-3">还没有学生</h3>
          <p class="text-gray-500 mb-6 text-lg">
            {{ currentRole === 'student' || isGuest ? '暂无学生档案，请等待老师进行班级学生录入。' : '添加学生，让他们领养可爱的宠物吧！' }}
          </p>
          <div v-if="currentRole !== 'student' && !isGuest" class="flex gap-3">
            <button @click="handleAddStudent" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-6 py-3 rounded-2xl hover:shadow-lg hover:scale-105 transition-all font-bold">➕ 添加学生</button>
            <button @click="showImportModal = true" class="bg-white text-gray-700 px-6 py-3 rounded-2xl hover:shadow-lg hover:scale-105 transition-all font-bold border border-gray-200">📥 批量导入</button>
          </div>
        </div>

        <!-- 学生列表 -->
        <div v-else key="students" class="grid grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-5">
          <StudentCard
            v-for="student in studentStore.filteredStudents"
            :key="student.id"
            :student="student"
            @click="handleStudentClick(student)"
          />
        </div>
      </Transition>

      <!-- 批量操作栏 -->
      <Transition name="slide-up">
        <div v-if="studentStore.batchMode" class="fixed bottom-24 left-1/2 -translate-x-1/2 bg-white/95 backdrop-blur-lg rounded-2xl shadow-2xl p-4 flex gap-3 items-center z-40 border border-gray-100 whitespace-nowrap">
          <span class="text-gray-600 px-2 font-bold text-sm">已选择 <span class="text-purple-500 font-black">{{ studentStore.selectedStudents.size }}</span> 人</span>
          <button @click="studentStore.cancelBatchMode()" class="px-5 py-2.5 border border-gray-200 hover:bg-gray-50 text-gray-600 rounded-xl font-bold text-sm transition-all">取消</button>
          <button @click="handleBatchAddPoints" :disabled="studentStore.selectedStudents.size === 0"
            class="bg-gradient-to-r from-green-400 to-emerald-500 text-white px-6 py-2.5 rounded-xl font-bold hover:shadow-lg transition-all disabled:opacity-50 disabled:cursor-not-allowed flex items-center gap-1.5 text-sm">
            <span>⬆️</span> 统一加分
          </button>
          <button @click="handleBatchSubPoints" :disabled="studentStore.selectedStudents.size === 0"
            class="bg-gradient-to-r from-red-400 to-pink-500 text-white px-6 py-2.5 rounded-xl font-bold hover:shadow-lg transition-all disabled:opacity-50 disabled:cursor-not-allowed flex items-center gap-1.5 text-sm">
            <span>⬇️</span> 统一扣分
          </button>
        </div>
      </Transition>

      <!-- 删除学生操作栏 -->
      <Transition name="slide-up">
        <div v-if="studentStore.showDeleteStudentMode" class="fixed bottom-24 left-1/2 -translate-x-1/2 bg-white/95 backdrop-blur-lg rounded-2xl shadow-2xl p-4 flex gap-4 z-40 border border-gray-100">
          <span class="text-gray-600 py-3 font-medium">已选 <span class="text-red-500 font-bold">{{ studentStore.deleteStudentList.length }}</span> 人</span>
          <button @click="handleBatchDelete" :disabled="studentStore.deleteStudentList.length === 0"
            class="bg-gradient-to-r from-red-500 to-pink-600 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg hover:scale-105 transition-all disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:scale-100 flex items-center gap-2">
            <span class="text-xl">🗑️</span> 确认删除
          </button>
          <button @click="studentStore.cancelDeleteMode()" class="bg-gray-100 text-gray-700 px-8 py-3 rounded-xl font-bold hover:bg-gray-200 transition-all">取消</button>
        </div>
      </Transition>
    </main>

    <!-- ===== 模态框 ===== -->

    <ClassModal ref="classModalRef" :show="showClassModal" @close="showClassModal = false" />
    <StudentModal ref="studentModalRef" :show="showStudentModal" @close="showStudentModal = false" @edit-complete="studentStore.loadStudents()" />
    <ImportModal :show="showImportModal" @close="showImportModal = false" />
    <EvalModal :show="showAddModal" :selected-student="studentStore.selectedStudent" @close="showAddModal = false" />
    <PetSelectModal :show="showPetModal" @close="showPetModal = false" />
    <RankModal :show="showRankModal" @close="showRankModal = false" />
    <RecordsModal :show="showRecordsModal" @close="showRecordsModal = false" />
    <RulesModal :show="showRulesModal" @close="showRulesModal = false" />
    <LogoModal :show="showLogoModal" @close="showLogoModal = false" />
    <SystemModal :show="showSystemModal" @close="showSystemModal = false" />
    <StudentDetailPanel :show="showDetailPanel" @close="studentStore.closeDetailPanel()" @edit-student="handleEditStudent" @change-pet="handleChangePet" />
    <ChatLogsModal :show="showChatLogsModal" @close="closeChatLogsModal" />
    <!-- 确认对话框 -->
    <ConfirmDialog
      :show="confirmDialog.show"
      :title="confirmDialog.title"
      :message="confirmDialog.message"
      :confirm-text="confirmDialog.confirmText"
      :cancel-text="confirmDialog.cancelText"
      :type="confirmDialog.type"
      @confirm="confirmDialog.onConfirm"
      @cancel="confirmDialog.show = false"
    />

    <!-- 登录/注册模态框 -->
    <AuthModal
      :show="showAuthModal"
      @close="showAuthModal = false"
      @login="(user: any) => { toast.success(`欢迎，${user.username}！`); classStore.loadClasses() }"
    />

    <!-- P3: 常驻麦克风按钮 + 实时刷新状态指示器 -->
    <div class="fixed bottom-6 right-6 z-40 flex flex-col items-end gap-2">
      <Transition name="fade">
        <div v-if="rtLastEvent" class="max-w-[60vw] bg-white/90 backdrop-blur-lg shadow-lg rounded-2xl px-3 py-1.5 text-xs text-gray-600 border border-gray-100">
          <span class="inline-block w-2 h-2 rounded-full mr-1.5 align-middle" :class="rtConnected ? 'bg-green-500' : 'bg-gray-300'"></span>
          {{ rtSimulated ? '模拟' : '实时' }} · {{ rtLastEvent?.payload?.studentName || '设备' }} 刚有动态
        </div>
      </Transition>
      <button
        @click="showVoiceChat = true"
        class="w-14 h-14 rounded-full bg-gradient-to-r from-violet-500 to-pink-500 text-white text-2xl shadow-xl hover:scale-105 active:scale-95 transition-all flex items-center justify-center"
        title="宠物语音伙伴"
      >🎤</button>
    </div>

    <!-- P3: Web 端宠物语音伙伴弹窗 -->
    <WebVoiceChat :show="showVoiceChat" @close="showVoiceChat = false" />
  </div>
</template>
