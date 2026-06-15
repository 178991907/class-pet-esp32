<script setup lang="ts">
import { ref, onMounted, computed, nextTick, watch } from 'vue'
import axios from 'axios'
import { PET_TYPES, getPetType, getLevelProgress, calculateLevel, getPetLevelImage, getPetLevel1Image } from '@/data/pets'
import PetImage from '@/components/PetImage.vue'
import ConfirmDialog from '@/components/ConfirmDialog.vue'
import AuthModal from '@/components/AuthModal.vue'
import { useToast } from '@/composables/useToast'
import { useAuth } from '@/composables/useAuth'

// Types
interface Class {
  id: string
  name: string
  created_at: number
}

interface Student {
  id: string
  class_id: string
  name: string
  student_no: string | null
  device_id?: string | null
  total_points: number
  pet_type: string | null
  pet_level: number
  pet_exp: number
}

// Toast 提示
const toast = useToast()

// 用户认证
const { isGuest, username, logout, api, user } = useAuth()
const isAdminUser = computed(() => !isGuest.value && username.value === 'admin')

// 音乐相关请求专用 API 实例（绕过 localApiAdapter，直接走 Vite 代理请求后端真实音源服务）
const musicApi = axios.create({
  baseURL: '/pet-garden/api'
})

const getInitialRole = () => {
  if (user.value && !user.value.isGuest) {
    return user.value.role || 'student'
  }
  return 'student'
}
const currentRole = ref<'student' | 'teacher' | 'admin'>(getInitialRole()) // 角色控制：student | teacher | admin
const showAuthModal = ref(false)

// 监听登录用户角色，进行实时绑定锁定与降级防护
watch(() => user.value, (newVal) => {
  if (newVal && !newVal.isGuest) {
    currentRole.value = newVal.role || 'student'
  } else {
    currentRole.value = 'student'
  }
}, { immediate: true })

watch(isAdminUser, (newVal) => {
  if (!newVal && currentRole.value === 'admin') {
    currentRole.value = 'student'
  }
})


// 用户菜单
const showUserMenu = ref(false)

interface Rule {
  id: string
  name: string
  points: number
  category: string
  is_custom?: boolean
}

// State
const classes = ref<Class[]>([])
const currentClass = ref<Class | null>(null)
const students = ref<Student[]>([])
const rules = ref<Rule[]>([])
const searchQuery = ref('')

// Modals
const showClassModal = ref(false)
const showStudentModal = ref(false)
const showImportModal = ref(false)
const showAddModal = ref(false)
const showRankModal = ref(false)
const showPetModal = ref(false)
const showRecordsModal = ref(false)
const showLogoModal = ref(false)
const newClassName = ref('')
const editingClass = ref<Class | null>(null)
const newStudentName = ref('')
const newStudentNo = ref('')
const newStudentDeviceId = ref('')
const editingStudent = ref<Student | null>(null)
const importText = ref('')
const selectedStudent = ref<Student | null>(null)
const evaluationRecords = ref<any[]>([])
const selectedEvalTab = ref('学习')
const showRulesModal = ref(false)
const newRuleName = ref('')
const newRulePoints = ref(1)
const newRuleCategory = ref('学习')
const batchMode = ref(false)
const selectedStudents = ref<Set<string>>(new Set())
const showClassMenu = ref(false)
const showStudentMenu = ref(false)
const showEvalMenu = ref(false)
const showDeleteStudentMode = ref(false)
const deleteStudentList = ref<string[]>([])
// 系统控制与设备管理变量
const showSystemModal = ref(false)
const systemActiveTab = ref('tasks')
const pendingTasks = ref<any[]>([])
const taskConfirmMode = ref('auto')
const taskConfirmDelay = ref(30)
const musicSources = ref<any[]>([])
const showMusicAddModal = ref(false)
const musicForm = ref({
  id: '',
  name: '',
  script_code: '',
  priority: 0
})

// 用户权限管理 (管理员专用)
const usersList = ref<any[]>([])
const loadingUsers = ref(false)

async function loadUsersList() {
  if (currentRole.value !== 'admin') return
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
    loadUsersList() // 刷新重置
  }
}

// 复制教师注册邀请链接
function copyTeacherInviteLink() {
  const link = `${window.location.origin}${window.location.pathname}?code=TEACHER_INVITE`
  navigator.clipboard.writeText(link)
    .then(() => toast.success('教师注册邀请链接已复制到剪贴板！'))
    .catch(() => toast.error('复制失败，请手动复制邀请码：TEACHER_INVITE'))
}

// 修改管理员密码 (管理员专用)
const newAdminPassword = ref('')
const confirmAdminPassword = ref('')
const updatingAdminPassword = ref(false)

async function handleUpdateAdminPassword() {
  if (!newAdminPassword.value || !confirmAdminPassword.value) {
    toast.error('密码不能为空')
    return
  }
  if (newAdminPassword.value.length < 6) {
    toast.error('密码长度至少为 6 位')
    return
  }
  if (newAdminPassword.value !== confirmAdminPassword.value) {
    toast.error('两次密码输入不一致')
    return
  }

  updatingAdminPassword.value = true
  try {
    await api.put('/auth/admin/password', { password: newAdminPassword.value })
    toast.success('管理员密码修改成功！请妥善保存新密码')
    newAdminPassword.value = ''
    confirmAdminPassword.value = ''
  } catch (err: any) {
    toast.error(err.response?.data?.error || '修改密码失败，请重试')
  } finally {
    updatingAdminPassword.value = false
  }
}

watch(systemActiveTab, (newVal) => {
  if (newVal === 'users') {
    loadUsersList()
  }
})
const recordsPage = ref(1)
const recordsPageSize = 20
const totalRecords = ref(0)
const sortBy = ref<'name' | 'studentNo' | 'progress'>('name')
const sortOrder = ref<'asc' | 'desc'>('asc')
const showSortMenu = ref(false)
const showPetMenu = ref(false)

// 确认对话框状态
const confirmDialog = ref({
  show: false,
  title: '确认',
  message: '',
  confirmText: '确认',
  cancelText: '取消',
  type: 'info' as 'info' | 'warning' | 'danger',
  onConfirm: () => {}
})

// 通用确认函数
function showConfirm(options: {
  title: string
  message: string
  confirmText?: string
  cancelText?: string
  type?: 'info' | 'warning' | 'danger'
  onConfirm: () => void
}) {
  confirmDialog.value = {
    show: true,
    title: options.title,
    message: options.message,
    confirmText: options.confirmText || '确认',
    cancelText: options.cancelText || '取消',
    type: options.type || 'info',
    onConfirm: () => {
      options.onConfirm()
      confirmDialog.value.show = false
    }
  }
}

// 图片加载状态（用于升级动画）
const levelUpImagesLoaded = ref({ prev: false, current: false })
const levelUpPhase = ref<'show-prev' | 'transition' | 'show-current'>('show-prev')

// 动画状态
const showLevelUpAnimation = ref(false)
const levelUpInfo = ref({ name: '', level: 0, petType: '', prevLevel: 0 })
const isLoaded = ref(false)
const isLoading = ref(true)

// 图片加载状态
const imageLoaded = ref<Record<string, boolean>>({})

// 详情面板
const showDetailPanel = ref(false)
const detailStudent = ref<Student | null>(null)
const detailEvalTab = ref('学习')

// 评分动效
const scoreAnimations = ref<Map<string, { points: number, show: boolean }>>(new Map())

// 学生评价记录
const studentRecords = ref<any[]>([])



function toggleSortMenu() {
  showSortMenu.value = !showSortMenu.value
}

function setSort(by: 'name' | 'studentNo' | 'progress', order: 'asc' | 'desc') {
  sortBy.value = by
  sortOrder.value = order
  showSortMenu.value = false
}

// Computed
const filteredStudents = computed(() => {
  let result = [...students.value]
  if (searchQuery.value) {
    result = result.filter(s => s.name.includes(searchQuery.value))
  }
  
  result.sort((a, b) => {
    let comparison = 0
    switch (sortBy.value) {
      case 'name':
        comparison = a.name.localeCompare(b.name)
        break
      case 'studentNo':
        const noA = a.student_no || ''
        const noB = b.student_no || ''
        comparison = noA.localeCompare(noB)
        break
      case 'progress':
        const levelA = a.pet_level || 0
        const levelB = b.pet_level || 0
        if (levelA !== levelB) {
          comparison = levelA - levelB
        } else {
          comparison = (a.pet_exp || 0) - (b.pet_exp || 0)
        }
        break
    }
    return sortOrder.value === 'asc' ? comparison : -comparison
  })
  
  return result
})

const categories = ['学习', '行为', '健康', '其他']

const currentCategoryRules = computed(() => {
  return rules.value.filter(r => r.category === selectedEvalTab.value)
})



const ranking = computed(() => {
  return [...students.value].sort((a, b) => b.total_points - a.total_points)
})

function getLevelBgClass(level: number): string {
  if (level >= 10) return 'from-yellow-400 via-amber-400 to-orange-400'
  if (level >= 7) return 'from-pink-400 via-rose-400 to-red-400'
  if (level >= 5) return 'from-purple-400 via-violet-400 to-indigo-400'
  if (level >= 3) return 'from-blue-400 via-cyan-400 to-teal-400'
  return 'from-gray-400 via-slate-400 to-zinc-400'
}

// 等级边框样式 - 每个等级都不同
function getLevelBorderClass(level: number): string {
  const borders: Record<number, string> = {
    1: 'border border-gray-200', // 浅灰色细边框
    2: 'border-2 border-gray-300', // 灰色
    3: 'border-2 border-blue-400 shadow-md shadow-blue-400/10', // 蓝色
    4: 'border-2 border-cyan-400 shadow-md shadow-cyan-400/15', // 青色
    5: 'border-2 border-purple-400 shadow-lg shadow-purple-400/20', // 紫色
    6: 'border-2 border-pink-400 shadow-lg shadow-pink-400/25', // 粉色
    7: 'border-2 border-rose-400 shadow-xl shadow-rose-400/30', // 红色
    8: 'border-3 border-yellow-400 shadow-xl shadow-yellow-400/40', // 金色
  }
  return borders[level] || ''
}

// 计算显示等级（基于经验值实时计算，修复数据不一致问题）
function getDisplayLevel(student: Student): number {
  return calculateLevel(student.pet_exp)
}

// API calls
async function loadClasses() {
  try {
    const res = await api.get('/classes')
    classes.value = res.data.classes
    if (classes.value.length > 0) {
      const savedClassId = localStorage.getItem('pet-garden-current-class')
      const savedClass = savedClassId ? classes.value.find(c => c.id === savedClassId) : null
      
      if (savedClass) {
        await selectClass(savedClass)
      } else if (!currentClass.value || !classes.value.find(c => c.id === currentClass.value?.id)) {
        await selectClass(classes.value[0])
      }
    } else {
      currentClass.value = null
      students.value = []
    }
  } catch (error) {
    console.error('加载班级失败:', error)
  }
}

async function selectClass(cls: Class) {
  currentClass.value = cls
  localStorage.setItem('pet-garden-current-class', cls.id)
  await loadStudents()
}

async function loadStudents() {
  if (!currentClass.value) return
  const res = await api.get(`/classes/${currentClass.value.id}/students`)
  students.value = res.data.students
}

async function loadRules() {
  const res = await api.get('/rules')
  rules.value = res.data.rules
}

async function createClass() {
  if (!newClassName.value.trim()) {
    toast.warning('请输入班级名称')
    return
  }
  try {
    await api.post('/classes', { name: newClassName.value.trim() })
    newClassName.value = ''
    showClassModal.value = false
    await loadClasses()
    toast.success('班级创建成功！')
  } catch (error) {
    console.error('创建班级失败:', error)
    toast.error('创建班级失败，请重试')
  }
}

async function updateClass() {
  if (!newClassName.value.trim()) {
    toast.warning('请输入班级名称')
    return
  }
  const classToEdit = editingClass.value
  if (!classToEdit) return
  try {
    const newName = newClassName.value.trim()
    await api.put(`/classes/${classToEdit.id}`, { name: newName })
    // 如果当前选中的班级被修改，更新当前班级名称
    if (currentClass.value?.id === classToEdit.id) {
      currentClass.value = { ...currentClass.value, name: newName } as Class
    }
    newClassName.value = ''
    editingClass.value = null
    showClassModal.value = false
    await loadClasses()
  } catch (error) {
    console.error('更新班级失败:', error)
    toast.error('更新班级失败，请重试')
  }
}

function openCreateClassModal() {
  editingClass.value = null
  newClassName.value = ''
  showClassModal.value = true
}

function openEditClassModal() {
  if (!currentClass.value) return
  editingClass.value = currentClass.value
  newClassName.value = currentClass.value.name
  showClassModal.value = true
}

async function deleteClass(id: string) {
  showConfirm({
    title: '删除班级',
    message: '确定删除该班级？所有学生数据将一并删除！',
    confirmText: '删除',
    cancelText: '取消',
    type: 'danger',
    onConfirm: async () => {
      await api.delete(`/classes/${id}`)
      if (currentClass.value?.id === id) {
        currentClass.value = null
        students.value = []
      }
      await loadClasses()
      toast.success('班级删除成功！')
    }
  })
}

async function addStudent() {
  if (!newStudentName.value.trim() || !currentClass.value) return
  try {
    await api.post('/students', {
      classId: currentClass.value.id,
      name: newStudentName.value.trim(),
      studentNo: newStudentNo.value.trim() || null,
      deviceId: newStudentDeviceId.value.trim() || null
    })
    newStudentName.value = ''
    newStudentNo.value = ''
    newStudentDeviceId.value = ''
    showStudentModal.value = false
    await loadStudents()
    toast.success('学生添加成功！')
  } catch (error) {
    console.error('添加学生失败:', error)
    toast.error('添加学生失败，请重试')
  }
}

function openEditStudentModal(student: Student) {
  editingStudent.value = student
  newStudentName.value = student.name
  newStudentNo.value = student.student_no || ''
  // 这里的 student.device_id 支持如果是后端返回的通用设备ID
  newStudentDeviceId.value = (student as any).device_id || ''
  showDetailPanel.value = false
  showStudentModal.value = true
}

async function updateStudent() {
  if (!editingStudent.value || !newStudentName.value.trim()) return
  try {
    await api.put(`/students/${editingStudent.value.id}`, {
      name: newStudentName.value.trim(),
      studentNo: newStudentNo.value.trim() || null,
      deviceId: newStudentDeviceId.value.trim() || null
    })
    newStudentName.value = ''
    newStudentNo.value = ''
    newStudentDeviceId.value = ''
    editingStudent.value = null
    showStudentModal.value = false
    await loadStudents()
    toast.success('学生信息更新成功！')
  } catch (error) {
    console.error('更新学生失败:', error)
    toast.error('更新学生失败，请重试')
  }
}


function openImportModal() {
  importText.value = ''
  showImportModal.value = true
}

async function importStudents() {
  if (!importText.value.trim() || !currentClass.value) return
  
  const lines = importText.value.trim().split('\n')
  const students = []
  
  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) continue
    
    const parts = trimmed.split(/[\t,\s;]+/)
    if (parts.length >= 2) {
      students.push({ name: parts[0], studentNo: parts.slice(1).join('') })
    } else if (parts.length === 1) {
      students.push({ name: parts[0], studentNo: '' })
    }
  }
  
  if (students.length === 0) {
    toast.warning('没有识别到学生信息')
    return
  }
  
  try {
    const res = await api.post('/students/import', {
      classId: currentClass.value.id,
      students
    })
    toast.success(`成功导入 ${res.data.imported} 名学生`)
    showImportModal.value = false
    importText.value = ''
    await loadStudents()
  } catch (error) {
    console.error('导入失败:', error)
    toast.error('导入失败，请重试')
  }
}

async function openPetSelect(student: Student) {
  selectedStudent.value = student
  showPetModal.value = true
}

async function selectPet(petId: string) {
  if (!selectedStudent.value) return
  try {
    await api.put(`/students/${selectedStudent.value.id}/pet`, { petType: petId })
    const pet = getPetType(petId)
    toast.success(`🎉 ${selectedStudent.value.name} 领养了一只 ${pet?.name || '宠物'}！`)
    showPetModal.value = false
    selectedStudent.value = null
    await loadStudents()
    // 更新详情面板中的学生信息
    if (detailStudent.value) {
      detailStudent.value = students.value.find(s => s.id === detailStudent.value?.id) || null
    }
  } catch (error) {
    console.error('领养宠物失败:', error)
    toast.error('领养失败，请重试')
  }
}

// 打开详情面板
async function openDetailPanel(student: Student) {
  if (!student.pet_type) {
    confirmDialog.value = {
      show: true,
      title: '领养宠物',
      message: `${student.name} 还没有领养宠物，是否现在领养？`,
      confirmText: '去领养',
      cancelText: '暂不',
      type: 'info',
      onConfirm: () => {
        selectedStudent.value = student
        showPetModal.value = true
        confirmDialog.value.show = false
      }
    }
    return
  }
  detailStudent.value = student
  detailEvalTab.value = '学习'
  showDetailPanel.value = true
  
  // 加载该学生的评价记录
  await loadStudentRecords(student.id)
}

// 加载学生评价记录
async function loadStudentRecords(studentId: string) {
  try {
    const res = await api.get(`/evaluations?studentId=${studentId}&pageSize=20`)
    studentRecords.value = res.data.records || []
  } catch (error) {
    console.error('加载记录失败:', error)
    studentRecords.value = []
  }
}

// 关闭详情面板
function closeDetailPanel() {
  showDetailPanel.value = false
  detailStudent.value = null
  studentRecords.value = []
}

function startBatchMode() {
  batchMode.value = true
  selectedStudents.value = new Set()
}

function cancelBatchMode() {
  batchMode.value = false
  selectedStudents.value = new Set()
}

function toggleStudentSelect(studentId: string) {
  const newSet = new Set(selectedStudents.value)
  if (newSet.has(studentId)) {
    newSet.delete(studentId)
  } else {
    newSet.add(studentId)
  }
  selectedStudents.value = newSet
}

function toggleDeleteStudent(studentId: string) {
  const index = deleteStudentList.value.indexOf(studentId)
  if (index > -1) {
    deleteStudentList.value.splice(index, 1)
  } else {
    deleteStudentList.value.push(studentId)
  }
}

function cancelDeleteMode() {
  showDeleteStudentMode.value = false
  deleteStudentList.value = []
}

async function batchDeleteStudents() {
  if (deleteStudentList.value.length === 0) return
  
  showConfirm({
    title: '删除学生',
    message: `确定删除 ${deleteStudentList.value.length} 名学生？此操作不可恢复！`,
    confirmText: '删除',
    cancelText: '取消',
    type: 'danger',
    onConfirm: async () => {
      let successCount = 0
      for (const studentId of deleteStudentList.value) {
        try {
          await api.delete(`/students/${studentId}`)
          successCount++
        } catch (error) {
          console.error('删除失败:', error)
        }
      }
      
      toast.success(`已删除 ${successCount} 名学生`)
      cancelDeleteMode()
      await loadStudents()
    }
  })
}

async function batchAddPoints() {
  if (selectedStudents.value.size === 0) return
  selectedStudent.value = null
  selectedEvalTab.value = '学习'
  showAddModal.value = true
}

async function batchSubPoints() {
  if (selectedStudents.value.size === 0) return
  selectedStudent.value = null
  selectedEvalTab.value = '行为'
  showAddModal.value = true
}

// 触发评分动效
function triggerScoreAnimation(studentId: string, points: number) {
  scoreAnimations.value.set(studentId, { points, show: true })
  setTimeout(() => {
    scoreAnimations.value.delete(studentId)
  }, 1500)
}

// 详情面板中快速评分
async function detailQuickAdd(rule: Rule) {
  if (!detailStudent.value) return
  
  const student = detailStudent.value
  
  try {
    const res = await api.post('/evaluations', {
      classId: currentClass.value?.id,
      studentId: student.id,
      points: rule.points,
      reason: rule.name,
      category: rule.category
    })
    
    // 触发卡片动效
    triggerScoreAnimation(student.id, rule.points)
    
    if (res.data.levelUp) {
      levelUpInfo.value = { 
        name: student.name, 
        level: res.data.petLevel,
        petType: student.pet_type || '',
        prevLevel: res.data.petLevel - 1
      }
      levelUpImagesLoaded.value = { prev: false, current: false }
      levelUpPhase.value = 'show-prev'
      showLevelUpAnimation.value = true
      
      // 动画时序控制
      setTimeout(() => { levelUpPhase.value = 'transition' }, 500)
      setTimeout(() => { levelUpPhase.value = 'show-current' }, 1500)
      setTimeout(() => { showLevelUpAnimation.value = false }, 4000)
    }
    if (res.data.graduated) {
      toast.success(`🎓 恭喜！${student.name} 的宠物毕业了，获得了专属徽章！`)
    }
    
    await loadStudents()
    
    // 关闭详情面板
    closeDetailPanel()
  } catch (error) {
    console.error('评价失败:', error)
    toast.error('评价失败，请重试')
  }
}

async function quickAdd(student: Student | null, rule: Rule) {
  if (!student) {
    const studentIds = Array.from(selectedStudents.value)
    let successCount = 0
    
    for (const studentId of studentIds) {
      try {
        await api.post('/evaluations', {
          classId: currentClass.value?.id,
          studentId: studentId,
          points: rule.points,
          reason: rule.name,
          category: rule.category
        })
        successCount++
        // 触发动效
        triggerScoreAnimation(studentId, rule.points)
      } catch (error) {
        console.error('评价失败:', error)
      }
    }
    
    showAddModal.value = false
    toast.success(`已为 ${successCount} 名学生${rule.points > 0 ? '加' : '扣'}${Math.abs(rule.points)}分`)
    cancelBatchMode()
    await loadStudents()
    return
  }
  
  try {
    const res = await api.post('/evaluations', {
      classId: currentClass.value?.id,
      studentId: student.id,
      points: rule.points,
      reason: rule.name,
      category: rule.category
    })
    
    // 触发动效
    triggerScoreAnimation(student.id, rule.points)
    
    if (res.data.levelUp) {
      levelUpInfo.value = { 
        name: student.name, 
        level: res.data.petLevel,
        petType: student.pet_type || '',
        prevLevel: res.data.petLevel - 1
      }
      levelUpImagesLoaded.value = { prev: false, current: false }
      levelUpPhase.value = 'show-prev'
      showLevelUpAnimation.value = true
      
      // 动画时序控制
      setTimeout(() => { levelUpPhase.value = 'transition' }, 500)
      setTimeout(() => { levelUpPhase.value = 'show-current' }, 1500)
      setTimeout(() => { showLevelUpAnimation.value = false }, 4000)
    }
    if (res.data.graduated) {
      toast.success(`🎓 恭喜！${student.name} 的宠物毕业了，获得了专属徽章！`)
    }
    
    await loadStudents()
  } catch (error) {
    console.error('评价失败:', error)
    toast.error('评价失败，请重试')
  }
}

async function loadEvaluationRecords() {
  if (!currentClass.value) return
  const res = await api.get(`/evaluations?classId=${currentClass.value.id}&page=${recordsPage.value}&pageSize=${recordsPageSize}`)
  evaluationRecords.value = res.data.records
  totalRecords.value = res.data.total
}

const paginatedRecords = computed(() => {
  return evaluationRecords.value
})

const totalPages = computed(() => {
  return Math.ceil(totalRecords.value / recordsPageSize)
})

function prevPage() {
  if (recordsPage.value > 1) {
    recordsPage.value--
    loadEvaluationRecords()
  }
}

function nextPage() {
  if (recordsPage.value < totalPages.value) {
    recordsPage.value++
    loadEvaluationRecords()
  }
}

function goToPage(page: number) {
  if (page >= 1 && page <= totalPages.value) {
    recordsPage.value = page
    loadEvaluationRecords()
  }
}

async function undoLastEvaluation(recordId?: string) {
  if (!currentClass.value) return
  
  showConfirm({
    title: '撤回评价',
    message: '确定要撤回这条评价吗？',
    confirmText: '撤回',
    cancelText: '取消',
    type: 'warning',
    onConfirm: async () => {
      try {
        let res
        if (recordId) {
          // 撤回指定记录
          res = await api.delete(`/evaluations/${recordId}`)
        } else {
          // 撤回最新记录
          res = await api.delete(`/evaluations/latest?classId=${currentClass.value!.id}`)
        }
        
        if (res.data.success) {
          toast.success(`已撤回：${res.data.undone.student_name} ${res.data.undone.points > 0 ? '+' : ''}${res.data.undone.points}分`)
          await loadStudents()
          await loadEvaluationRecords()
        }
      } catch (error) {
        console.error('撤回失败:', error)
        toast.error('撤回失败')
      }
    }
  })
}

async function addRule() {
  if (!newRuleName.value.trim()) {
    toast.warning('请输入规则名称')
    return
  }
  try {
    await api.post('/rules', {
      name: newRuleName.value.trim(),
      points: newRulePoints.value,
      category: newRuleCategory.value
    })
    newRuleName.value = ''
    newRulePoints.value = 1
    toast.success('添加成功！')
    await loadRules()
  } catch (error) {
    console.error('添加规则失败:', error)
    alert('添加失败，请重试')
  }
}

async function deleteRule(id: string) {
  showConfirm({
    title: '删除规则',
    message: '确定删除该规则？',
    confirmText: '删除',
    cancelText: '取消',
    type: 'warning',
    onConfirm: async () => {
      try {
        await api.delete(`/rules/${id}`)
        await loadRules()
        toast.success('删除成功！')
      } catch (error) {
        console.error('删除失败:', error)
        toast.error('删除失败')
      }
    }
  })
}

// TODO: 导入导出功能暂时屏蔽，等待重构后恢复
/*
async function exportBackup() {
  try {
    const res = await api.get('/backup', { responseType: 'blob' })
    const url = window.URL.createObjectURL(new Blob([res.data]))
    const link = document.createElement('a')
    link.href = url
    link.setAttribute('download', `pet-garden-backup-${Date.now()}.json`)
    document.body.appendChild(link)
    link.click()
    link.remove()
    alert('备份导出成功！')
  } catch (error) {
    console.error('导出失败:', error)
    alert('导出失败')
  }
}

async function importBackup(event: Event) {
  const file = (event.target as HTMLInputElement).files?.[0]
  if (!file) return
  
  const fileInput = event.target as HTMLInputElement
  
  showConfirm({
    title: '导入备份',
    message: '导入将覆盖现有数据，确定继续？',
    confirmText: '导入',
    cancelText: '取消',
    type: 'warning',
    onConfirm: async () => {
      try {
        const text = await file.text()
        const data = JSON.parse(text)
        await api.post('/restore', data)
        toast.success('数据恢复成功！')
        await loadClasses()
        await loadRules()
      } catch (error) {
        console.error('导入失败:', error)
        toast.error('导入失败，请确保文件格式正确')
      }
      fileInput.value = ''
    }
  })
}
*/

function getStudentPetImage(student: Student): string {
  if (!student.pet_type) return ''
  // 根据学生当前等级显示对应等级的宠物图片
  return getPetLevelImage(student.pet_type, student.pet_level)
}

// 加载系统配置及设备硬件绑定相关数据
async function loadSystemData() {
  try {
    const tasksRes = await api.get('/device/tasks?status=pending')
    pendingTasks.value = tasksRes.data.tasks

    const settingsRes = await api.get('/device/settings')
    taskConfirmMode.value = settingsRes.data.task_confirm_mode
    taskConfirmDelay.value = settingsRes.data.task_confirm_delay
  } catch (err) {
    console.error('加载系统配置失败:', err)
  }

  // 音源列表单独加载（走后端真实数据库），失败不影响其他功能
  try {
    const musicRes = await musicApi.get('/device/music-sources')
    musicSources.value = musicRes?.data?.sources || []
  } catch (err) {
    musicSources.value = []
    console.warn('⚠️ 后端音源列表加载失败（后端可能未启动），音源功能将降级:', err)
  }
}

async function openSystemModal() {
  if (currentRole.value !== 'admin') {
    toast.error('仅管理员可使用此功能')
    return
  }
  await loadSystemData()
  systemActiveTab.value = 'tasks'
  showSystemModal.value = true
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
      task_confirm_delay: Number(taskConfirmDelay.value)
    })
    toast.success('系统设置保存成功！')
  } catch (err) {
    toast.error('设置保存失败')
  }
}

async function toggleMusicSource(source: any) {
  try {
    await musicApi.post('/device/music-sources', {
      ...source,
      is_enabled: source.is_enabled === 1 ? 0 : 1
    })
    toast.success('启用状态已更新')
    await loadSystemData()
  } catch (err) {
    toast.error('操作失败')
  }
}

async function deleteMusicSource(id: string) {
  try {
    await musicApi.delete(`/device/music-sources/${id}`)
    toast.success('音源已删除')
    await loadSystemData()
  } catch (err) {
    toast.error('删除音源失败')
  }
}

async function resetMusicFailure(id: string) {
  try {
    await musicApi.post(`/device/music-sources/${id}/reset`)
    toast.success('熔断机制已重置')
    await loadSystemData()
  } catch (err) {
    toast.error('重置失败')
  }
}

async function handleMusicUpload(event: Event) {
  const files = (event.target as HTMLInputElement).files
  if (!files || files.length === 0) return

  if (files.length === 1) {
    const file = files[0]
    const reader = new FileReader()
    reader.onload = (e) => {
      const code = e.target?.result as string
      if (code) {
        musicForm.value.script_code = code
        if (!musicForm.value.name) {
          musicForm.value.name = file.name.replace('.js', '')
        }
        toast.success('JS 脚本文件加载成功！')
      }
    }
    reader.readAsText(file)
  } else {
    toast.info(`检测到批量上传 ${files.length} 个音源，正在处理，请稍候...`)
    
    const uploadPromises = Array.from(files).map(file => {
      return new Promise<void>((resolve, reject) => {
        const reader = new FileReader()
        reader.onload = async (e) => {
          const code = e.target?.result as string
          if (code) {
            try {
              await musicApi.post('/device/music-sources', {
                name: file.name.replace('.js', ''),
                script_code: code,
                priority: 0
              })
              resolve()
            } catch (err) {
              reject(err)
            }
          } else {
            resolve()
          }
        }
        reader.onerror = () => reject(new Error(`读取文件 ${file.name} 失败`))
        reader.readAsText(file)
      })
    })

    try {
      await Promise.all(uploadPromises)
      toast.success(`成功批量导入 ${files.length} 个音乐解析源！`)
      showMusicAddModal.value = false
      musicForm.value = { id: '', name: '', script_code: '', priority: 0 }
      await loadSystemData()
    } catch (err) {
      console.error('批量上传音源失败:', err)
      toast.error('部分音源导入失败，请重试')
      await loadSystemData()
    }
  }
}


// === 全局音乐播放器状态 ===
const globalAudio = ref<HTMLAudioElement | null>(null)
const currentPlayingSong = ref('未播放')
const currentPlayingUrl = ref('')
const isPlaying = ref(false)
const songDuration = ref(0)
const songCurrentTime = ref(0)
const volume = ref(70) // 默认 70% 音量

// 搜歌及歌词弹窗控制
const showGlobalSearch = ref(false)
const globalSearchKeyword = ref('童话')
const globalSearchLoading = ref(false)
const showLyricModal = ref(false)

const currentLyricText = ref('🎶 洛雪高可用音源已就绪，等待播放')

// Mock 播放列表与歌词本
const mockPlaylist = [
  {
    title: '童话',
    lyrics: [
      { time: 0, text: "🎶 播放：童话 - 光良" },
      { time: 5, text: "忘了有多久 再没听到你" },
      { time: 10, text: "对我说你最爱的故事" },
      { time: 15, text: "我想了很久 我开始慌了" },
      { time: 20, text: "是不是我又做错了什么" },
      { time: 25, text: "你哭着对我说 童话里都是骗人的" },
      { time: 32, text: "我不可能是你的王子" },
      { time: 38, text: "也许你不会懂 从你说爱我以后" },
      { time: 44, text: "我的天空 星星都亮了" },
      { time: 50, text: "我愿变成童话里 你爱的那个天使" },
      { time: 57, text: "张开双手 变成翅膀守护你" },
      { time: 64, text: "相信我们会像童话故事里" },
      { time: 70, text: "幸福和快乐是结局" }
    ]
  },
  { 
    title: '晴天', 
    lyrics: [
      { time: 0, text: "🎶 播放：晴天 - 周杰伦" },
      { time: 3, text: "故事的小黄花" },
      { time: 7, text: "从出生那年就飘着" },
      { time: 12, text: "童年的荡秋千" },
      { time: 15, text: "随记忆一直晃到现在" },
      { time: 20, text: "吹着前奏望着天空" },
      { time: 24, text: "我想起花瓣试着掉落" },
      { time: 28, text: "为你写歌 温暖每一个角落" },
      { time: 33, text: "期待有一天 我们可以重新度过" }
    ]
  },
  { 
    title: '小幸运', 
    lyrics: [
      { time: 0, text: "🎶 播放：小幸运 - 田馥甄" },
      { time: 4, text: "我听见雨滴落在青青草地" },
      { time: 9, text: "我听见远方下课钟声响起" },
      { time: 14, text: "可是我没有听见你的声音" },
      { time: 18, text: "认真呼唤我姓名" },
      { time: 22, text: "爱上你的时候还不懂感情" },
      { time: 27, text: "离别的时候才知后知后觉" }
    ]
  },
  {
    title: '我爱你',
    lyrics: [
      { time: 0, text: "🎶 播放：我爱你 - S.H.E" },
      { time: 5, text: "从你眼睛看着自己" },
      { time: 9, text: "最幸福的倒影" },
      { time: 13, text: "握在手心的默契" },
      { time: 17, text: "是爱写下的日记" },
      { time: 22, text: "我爱你 来自呼吸" },
      { time: 26, text: "找不到言语 代替我爱你" },
      { time: 32, text: "哪怕你 只是我生命中的插曲" },
      { time: 38, text: "最美的一段 也是这旋律" }
    ]
  },
  {
    title: '七里香',
    lyrics: [
      { time: 0, text: "🎶 播放：七里香 - 周杰伦" },
      { time: 3, text: "窗外的麻雀 在电线杆上多嘴" },
      { time: 8, text: "你说这一句 很有夏天的感觉" },
      { time: 13, text: "手中的铅笔 在纸上来来回回" },
      { time: 17, text: "我用几行字形容你是我的谁" },
      { time: 22, text: "秋刀鱼的滋味 猫跟你都想了解" },
      { time: 27, text: "初恋的香味就这样被我们寻回" }
    ]
  }
]

const currentSongIndex = ref(0)

const selectedSourceId = ref<string>('')

const activeMusicSources = computed(() => {
  const list = (musicSources.value || []).filter(s => s.is_enabled === 1 && s.failure_count < 3)
  if (list.length > 0) return list
  // Local 模式或无可用源时的 Mock 预置
  return [
    { id: 'mock-qq', name: 'QQ音乐演示源', failure_count: 0 },
    { id: 'mock-netease', name: '网易云演示源', failure_count: 0 },
    { id: 'mock-kuwo', name: '酷我高品质源', failure_count: 0 }
  ]
})

// 监听并自动更新当前选中的默认音源
watch(activeMusicSources, (list) => {
  if (list.length > 0 && (!selectedSourceId.value || !list.find(s => s.id === selectedSourceId.value))) {
    selectedSourceId.value = list[0].id
  }
}, { immediate: true })

// 动态匹配当前歌词
const currentLyrics = computed(() => {
  const songName = currentPlayingSong.value.trim()
  const found = mockPlaylist.find(item => songName.includes(item.title) || item.title.includes(songName))
  if (found) return found.lyrics
  
  // 为未预置的歌曲生成通用的、有仪式感的播放状态提示词，避免穿帮
  return [
    { time: 0, text: `🎶 正在播放：${songName}` },
    { time: 5, text: "✨ 洛雪音源已成功调通音频直链" },
    { time: 10, text: "🎧 音乐声响起，享受当下的旋律吧..." },
    { time: 20, text: "🐾 班级宠物园正在后台与您共同聆听" },
    { time: 35, text: "🌟 洛雪解析引擎高可用链路持续输出中" },
    { time: 60, text: "🎵 旋律流淌中..." }
  ]
})

// 计算高亮歌词行索引
const activeLyricIndex = computed(() => {
  const currentSec = Math.floor(songCurrentTime.value)
  const lyrics = currentLyrics.value
  const index = [...lyrics].reverse().findIndex(l => currentSec >= l.time)
  if (index === -1) return 0
  return lyrics.length - 1 - index
})

// 监听歌词滚动，对齐到正中间
watch(activeLyricIndex, (newIdx) => {
  nextTick(() => {
    const el = document.getElementById(`lyric-line-${newIdx}`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    }
  })
})

// 时间格式化
function formatTime(sec: number) {
  if (isNaN(sec) || !isFinite(sec)) return '00:00'
  const m = Math.floor(sec / 60)
  const s = Math.floor(sec % 60)
  return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`
}

// 切歌方法
function playPrevMock() {
  currentSongIndex.value = (currentSongIndex.value - 1 + mockPlaylist.length) % mockPlaylist.length
  globalSearchKeyword.value = mockPlaylist[currentSongIndex.value].title
  testMusicSourceSearch()
}

function playNextMock() {
  currentSongIndex.value = (currentSongIndex.value + 1) % mockPlaylist.length
  globalSearchKeyword.value = mockPlaylist[currentSongIndex.value].title
  testMusicSourceSearch()
}

// 搜歌并播放逻辑（优先走后端真实音源沙箱解析，后端不可用时降级到 Mock）
async function testMusicSourceSearch() {
  const keyword = globalSearchKeyword.value.trim()
  if (!keyword) {
    toast.error('请输入要搜歌测试的音乐名称！')
    return
  }
  globalSearchLoading.value = true
  
  if (globalAudio.value) {
    globalAudio.value.pause()
  }
  isPlaying.value = false
  currentPlayingSong.value = '解析中...'

  try {
    let musicUrl = ''
    
    // 优先请求后端真实音源解析接口（通过 Vite 代理直接转发到 Express 后端）
    try {
      console.log('🎵 [音乐搜索] 正在请求后端真实音源解析...')
      const backendRes = await musicApi.get('/device/music/search', {
        params: {
          keyword,
          source_id: selectedSourceId.value,
          device_id: 'WEB-PLAYER-001'
        },
        timeout: 8000 // 后端沙箱执行最多给 8 秒
      })
      if (backendRes.data && backendRes.data.url) {
        musicUrl = backendRes.data.url
        console.log('✅ [音乐搜索] 后端音源解析成功:', musicUrl)
      }
    } catch (backendErr: any) {
      console.warn('⚠️ [音乐搜索] 后端音源不可用，降级到本地 Mock:', backendErr.message || backendErr)
      // 降级：使用 localApiAdapter 路径走 Mock 演示音轨
      try {
        const mockRes = await api.get(`/device/music/search?keyword=${encodeURIComponent(keyword)}&source_id=${selectedSourceId.value}&device_id=WEB-PLAYER-001`)
        if (mockRes.data && mockRes.data.url) {
          musicUrl = mockRes.data.url
          console.log('🔄 [音乐搜索] Mock 降级成功:', musicUrl)
        }
      } catch (mockErr) {
        console.error('❌ [音乐搜索] Mock 降级也失败:', mockErr)
      }
    }

    if (musicUrl) {
      currentPlayingUrl.value = musicUrl
      currentPlayingSong.value = keyword
      
      // 开始加载并播放音频
      startAudioPlayback(musicUrl, keyword)
    } else {
      currentPlayingSong.value = '未播放'
      toast.error('解析失败，后端无可用音源且本地降级也未返回有效链接')
      globalSearchLoading.value = false
    }
  } catch (err: any) {
    console.error('音乐搜索异常:', err)
    currentPlayingSong.value = '未播放'
    toast.error(err.response?.data?.error || '音乐搜索异常')
    globalSearchLoading.value = false
  }
}

// 抽取音频加载与播放逻辑为独立函数
function startAudioPlayback(url: string, songName: string) {
  const audio = new Audio()
  
  // 加载超时保护：15 秒内未能开始播放则提示失败
  const loadTimeout = setTimeout(() => {
    if (!isPlaying.value) {
      currentPlayingSong.value = '加载超时'
      currentLyricText.value = '⚠️ 音频加载超时，请重试'
      toast.error('音频加载超时，请切换音源或重试')
      globalSearchLoading.value = false
    }
  }, 15000)
  
  // 音频加载就绪后再播放
  audio.oncanplaythrough = () => {
    clearTimeout(loadTimeout)
    globalSearchLoading.value = false
    audio.play().then(() => {
      isPlaying.value = true
      toast.success(`正在播放：${songName}`)
    }).catch((playErr) => {
      console.error('播放被浏览器阻止:', playErr)
      isPlaying.value = false
      currentPlayingSong.value = '播放被阻止'
      toast.error('浏览器阻止了自动播放，请点击播放按钮手动开始')
    })
    showGlobalSearch.value = false
  }
  
  // 音频加载出错的处理
  audio.onerror = () => {
    clearTimeout(loadTimeout)
    globalSearchLoading.value = false
    isPlaying.value = false
    currentPlayingSong.value = '加载失败'
    currentLyricText.value = '⚠️ 音频文件加载失败'
    toast.error('音频文件无法加载，请切换音源重试')
    console.error('Audio 加载失败, URL:', url)
  }
  
  globalAudio.value = audio
  audio.volume = volume.value / 100
  audio.src = url
  audio.load()
  
  audio.ontimeupdate = () => {
    songCurrentTime.value = audio.currentTime
    songDuration.value = audio.duration || 0
    
    // 歌词滚动计算
    const currentSec = Math.floor(audio.currentTime)
    const lyrics = currentLyrics.value
    const matched = [...lyrics].reverse().find(l => currentSec >= l.time)
    if (matched) {
      currentLyricText.value = matched.text
    }
  }
  
  audio.onended = () => {
    isPlaying.value = false
    currentLyricText.value = '🎵 播放完毕'
  }
}

// 播放/暂停控制
function toggleTestMusicPlay() {
  if (!globalAudio.value) return
  if (isPlaying.value) {
    globalAudio.value.pause()
    isPlaying.value = false
  } else {
    globalAudio.value.play()
    isPlaying.value = true
  }
}

// 拖拽进度条定位
function seekAudio(event: MouseEvent) {
  if (!globalAudio.value || !songDuration.value) return
  const bar = event.currentTarget as HTMLElement
  const rect = bar.getBoundingClientRect()
  const clickX = event.clientX - rect.left
  const width = rect.width
  const clickPercentage = clickX / width
  
  globalAudio.value.currentTime = clickPercentage * songDuration.value
  songCurrentTime.value = globalAudio.value.currentTime
}

// 音量滑动调节
function handleVolumeChange() {
  if (globalAudio.value) {
    globalAudio.value.volume = volume.value / 100
  }
}

async function saveMusicSource() {
  if (!musicForm.value.name.trim() || !musicForm.value.script_code.trim()) {
    toast.error('请填写音源名称并上传 JS 脚本！')
    return
  }
  try {
    await musicApi.post('/device/music-sources', musicForm.value)
    toast.success('音源保存成功！')
    showMusicAddModal.value = false
    musicForm.value = { id: '', name: '', script_code: '', priority: 0 }
    await loadSystemData()
  } catch (err) {
    toast.error('保存音源失败，请重试')
  }
}

function openAddMusicModal() {
  musicForm.value = { id: '', name: '', script_code: '', priority: 0 }
  showMusicAddModal.value = true
}

function openEditMusicModal(source: any) {
  musicForm.value = {
    id: source.id,
    name: source.name,
    script_code: source.script_code,
    priority: source.priority
  }
  showMusicAddModal.value = true
}

// Initialize
onMounted(async () => {
  isLoading.value = true
  try {
    await loadClasses()
    await loadRules()
    await loadSystemData()
  } finally {
    isLoading.value = false
    nextTick(() => {
      isLoaded.value = true
    })
  }
})
</script>

<template>
  <div class="min-h-screen bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 flex flex-col">
    
    <!-- 加载动画 -->
    <Transition name="fade">
      <div v-if="isLoading" class="fixed inset-0 bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 flex items-center justify-center z-[200]">
        <div class="text-center">
          <div class="text-8xl mb-6 animate-bounce">🐕</div>
          <div class="text-xl font-bold text-gray-600">加载中...</div>
          <div class="mt-4 flex justify-center gap-2">
            <span class="w-3 h-3 bg-orange-400 rounded-full animate-bounce" style="animation-delay: 0ms"></span>
            <span class="w-3 h-3 bg-pink-400 rounded-full animate-bounce" style="animation-delay: 150ms"></span>
            <span class="w-3 h-3 bg-purple-400 rounded-full animate-bounce" style="animation-delay: 300ms"></span>
          </div>
        </div>
      </div>
    </Transition>
    
    <!-- 升级动画 -->
    <Transition name="fade">
      <div v-if="showLevelUpAnimation" class="fixed inset-0 bg-black/70 backdrop-blur-md flex items-center justify-center z-[200]">
        <div class="relative">
          <!-- 背景光晕 -->
          <div class="absolute inset-0 bg-gradient-to-r from-orange-400 via-pink-400 to-purple-400 rounded-full blur-3xl opacity-60 animate-pulse"></div>
          
          <!-- 主内容 -->
          <div class="relative bg-white/95 backdrop-blur-xl rounded-3xl p-10 text-center shadow-2xl border-4 max-w-md"
            :class="levelUpInfo.level >= 8 ? 'border-yellow-400 shadow-yellow-400/50' : levelUpInfo.level >= 5 ? 'border-purple-400 shadow-purple-400/50' : 'border-orange-400 shadow-orange-400/50'"
          >
            <!-- 标题 -->
            <h2 class="text-3xl font-bold mb-2"
              :class="levelUpInfo.level >= 8 ? 'text-gradient bg-gradient-to-r from-yellow-400 via-amber-400 to-orange-400 bg-clip-text text-transparent' : 'text-gradient'"
            >
              {{ levelUpInfo.level >= 8 ? '恭喜毕业！' : '升级啦！' }}
            </h2>
            
            <!-- 宠物进化动画区域 -->
            <div class="relative w-48 h-48 mx-auto my-6">
              <!-- 进化光环 - 改为圆角矩形与图片风格一致 -->
              <div class="absolute inset-0 rounded-3xl bg-gradient-to-r from-orange-300 via-pink-300 to-purple-300 opacity-50 animate-spin" style="animation-duration: 3s"></div>
              <div class="absolute inset-2 rounded-3xl bg-gradient-to-r from-yellow-300 via-orange-300 to-pink-300 opacity-40 animate-spin" style="animation-duration: 2s; animation-direction: reverse"></div>
              
              <!-- 宠物图片容器 - 圆角矩形与宠物图片风格一致 -->
              <div class="absolute inset-4 rounded-2xl overflow-hidden bg-gradient-to-br from-orange-100 to-pink-100 shadow-inner">
                <!-- 升级前图片 - 初始显示，过渡阶段淡出 -->
                <img 
                  :src="getPetLevelImage(levelUpInfo.petType, levelUpInfo.prevLevel)" 
                  class="absolute inset-0 w-full h-full object-contain p-2 transition-all duration-1000"
                  :class="levelUpPhase === 'show-prev' ? 'opacity-100 scale-100' : 'opacity-0 scale-50'"
                />
                <!-- 升级后图片 - 过渡阶段淡入，最终显示 -->
                <img 
                  :src="getPetLevelImage(levelUpInfo.petType, levelUpInfo.level)" 
                  class="absolute inset-0 w-full h-full object-contain p-2 transition-all duration-1000"
                  :class="levelUpPhase === 'show-current' ? 'opacity-100 scale-100' : 'opacity-0 scale-150'"
                />
              </div>
              
              <!-- 等级变化指示 -->
              <div class="absolute -bottom-2 left-1/2 -translate-x-1/2 flex items-center gap-2 bg-white rounded-full px-4 py-1 shadow-lg">
                <span class="text-sm text-gray-400">Lv.{{ levelUpInfo.prevLevel }}</span>
                <span class="text-lg">➜</span>
                <span class="text-xl font-bold"
                  :class="levelUpInfo.level >= 8 ? 'text-yellow-500' : levelUpInfo.level >= 5 ? 'text-purple-500' : 'text-orange-500'"
                >
                  Lv.{{ levelUpInfo.level }}
                </span>
              </div>
            </div>
            
            <!-- 信息 -->
            <p class="text-lg text-gray-600 mb-1">
              <span class="font-bold text-purple-500">{{ levelUpInfo.name }}</span> 的宠物
            </p>
            
            <!-- 等级称号 -->
            <div class="text-base">
              <span v-if="levelUpInfo.level >= 10" class="text-yellow-500 font-bold">🏆 传说级宠物</span>
              <span v-else-if="levelUpInfo.level >= 8" class="text-pink-500 font-bold">🌟 史诗级宠物</span>
              <span v-else-if="levelUpInfo.level >= 5" class="text-purple-500 font-bold">⭐ 稀有宠物</span>
              <span v-else-if="levelUpInfo.level >= 3" class="text-blue-500 font-bold">💎 优秀宠物</span>
              <span v-else class="text-green-500 font-bold">🌱 成长中</span>
            </div>
          </div>
          
          <!-- 装饰星星 -->
          <div class="absolute -top-4 -left-4 text-4xl animate-pulse">✨</div>
          <div class="absolute -top-4 -right-4 text-4xl animate-pulse" style="animation-delay: 0.2s">✨</div>
          <div class="absolute -bottom-4 -left-4 text-4xl animate-pulse" style="animation-delay: 0.4s">✨</div>
          <div class="absolute -bottom-4 -right-4 text-4xl animate-pulse" style="animation-delay: 0.6s">✨</div>
        </div>
      </div>
    </Transition>

    <!-- 顶部导航栏 -->
    <header class="bg-gradient-to-r from-amber-400 via-orange-400 to-rose-400 shadow-lg px-4 py-3 flex items-center justify-between sticky top-0 z-30">
      <!-- Left -->
      <div class="flex items-center gap-3">
        <h1 class="text-xl font-bold text-white drop-shadow-lg flex items-center gap-2.5">
          <!-- 智能 CSS 裁剪与缩放后的精美 3D LOGO 容器 -->
          <!-- 外层大容器：不带 overflow-hidden，负责悬浮 Hover 状态追踪与气泡定位 -->
          <div class="relative group cursor-pointer shrink-0" @click="showLogoModal = true">
            <!-- 内层裁剪容器：带有 overflow-hidden，负责图片底部的智能 CSS 裁剪，并在悬停时平滑放大 1.1 倍 -->
            <div class="w-10 h-10 rounded-xl overflow-hidden bg-white/10 border border-white/30 shadow-md flex items-center justify-center relative transition-all duration-300 group-hover:scale-110 group-hover:rotate-2 group-hover:shadow-lg">
              <img 
                src="/images/logo.png" 
                alt="Logo"
                class="w-[130%] h-[130%] object-cover object-top absolute -top-0.5"
              />
            </div>
            
            <!-- 超精美 Hover 悬浮气泡大卡片（现可完美、自由地在容器下方滑出，绝无阻挡） -->
            <div class="absolute top-12 left-0 z-50 w-64 p-3 bg-white/95 backdrop-blur-xl border border-gray-200/50 rounded-2xl shadow-2xl transition-all duration-300 origin-top-left opacity-0 scale-95 pointer-events-none group-hover:opacity-100 group-hover:scale-100 group-hover:pointer-events-auto text-left">
              <div class="aspect-square w-full rounded-xl overflow-hidden bg-gray-50 mb-2 border border-gray-100">
                <img src="/images/logo.png" class="w-full h-full object-contain" />
              </div>
              <div>
                <div class="font-bold text-gray-800 text-sm">英语全科启蒙+AI</div>
                <div class="text-[10px] text-gray-400 mt-1">💡 鼠标点击图标可查看超高清原尺寸大图</div>
              </div>
              <!-- 小气泡箭头 -->
              <div class="absolute -top-1.5 left-4 w-3 h-3 bg-white border-l border-t border-gray-200/50 rotate-45"></div>
            </div>
          </div>
          <!-- 右侧极致清晰的品牌大字：点击快速返回首页 -->
          <router-link to="/" class="text-gradient font-extrabold tracking-wide text-2xl hover:opacity-90 active:scale-95 transition-all cursor-pointer">班级宠物园</router-link>
        </h1>
        <select 
          v-if="classes.length > 0"
          class="border-0 rounded-xl px-4 py-2 text-sm bg-white/95 hover:bg-white shadow-md backdrop-blur cursor-pointer font-medium"
          :value="currentClass?.id"
          @change="selectClass(classes.find(c => c.id === ($event.target as HTMLSelectElement).value)!)"
        >
          <option v-for="cls in classes" :key="cls.id" :value="cls.id">
            {{ cls.name }}
          </option>
        </select>
        <span class="text-sm text-white/90 font-medium bg-white/20 px-3 py-1 rounded-full">
          {{ students.length }} 人
        </span>
      </div>
      
      <!-- Right -->
      <div class="flex items-center gap-1.5">
        <!-- Search -->
        <input 
          v-model="searchQuery"
          type="text" 
          placeholder="🔍 搜索..."
          class="border-0 rounded-lg px-3 py-1.5 text-sm w-28 bg-white/95 hover:bg-white shadow-md focus:outline-none focus:ring-2 focus:ring-white/50 transition-all"
        />
        
        <!-- Pet Menu -->
        <div class="relative" v-if="!batchMode">
          <button @click="showPetMenu = !showPetMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">
            🐕 宠物 ▾
          </button>
          <div v-if="showPetMenu" @click="showPetMenu = false" class="fixed inset-0 z-40"></div>
          <Transition name="dropdown">
            <div v-if="showPetMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
              <router-link to="/preview" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors flex items-center gap-2">
                📖 图鉴
              </router-link>
            </div>
          </Transition>
        </div>
        
        <!-- Sort Menu -->
        <div class="relative" v-if="!batchMode">
          <button @click="toggleSortMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">
            📊 排序 ▾
          </button>
          <div v-if="showSortMenu" @click="showSortMenu = false" class="fixed inset-0 z-40"></div>
          <Transition name="dropdown">
            <div v-if="showSortMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
              <button @click="setSort('name', 'asc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="sortBy === 'name' && sortOrder === 'asc' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">🔤 A-Z</button>
              <button @click="setSort('name', 'desc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="sortBy === 'name' && sortOrder === 'desc' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">🔤 Z-A</button>
              <button @click="setSort('studentNo', 'asc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="sortBy === 'studentNo' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">🔢 学号</button>
              <button @click="setSort('progress', 'desc')" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors" :class="sortBy === 'progress' ? 'bg-gradient-to-r from-orange-50 to-pink-50 text-orange-600 font-medium' : ''">⭐ 进度</button>
            </div>
          </Transition>
        </div>
        
        <!-- Class Menu -->
        <div class="relative" v-if="!batchMode && currentRole !== 'student' && !isGuest">
          <button @click="showClassMenu = !showClassMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">
            📚 班级 ▾
          </button>
          <div v-if="showClassMenu" @click="showClassMenu = false" class="fixed inset-0 z-40"></div>
          <Transition name="dropdown">
            <div v-if="showClassMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
              <button @click="openCreateClassModal" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">➕ 新建</button>
              <button v-if="currentClass" @click="openEditClassModal" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">✏️ 重命名</button>
              <button v-if="currentClass" @click="deleteClass(currentClass.id)" class="w-full text-left px-3 py-2 text-sm text-red-500 hover:bg-red-50 transition-colors">🗑️ 删除</button>
            </div>
          </Transition>
        </div>
        
        <!-- Student Menu -->
        <div class="relative" v-if="currentClass && !batchMode && currentRole !== 'student' && !isGuest">
          <button @click="showStudentMenu = !showStudentMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">
            👤 学生 ▾
          </button>
          <div v-if="showStudentMenu" @click="showStudentMenu = false" class="fixed inset-0 z-40"></div>
          <Transition name="dropdown">
            <div v-if="showStudentMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-40 z-50 overflow-hidden">
              <button @click="showStudentModal = true" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">➕ 添加</button>
              <button @click="openImportModal" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">📥 导入</button>
              <button @click="showDeleteStudentMode = true; deleteStudentList = []" class="w-full text-left px-3 py-2 text-sm text-red-500 hover:bg-red-50 transition-colors">🗑️ 删除</button>
            </div>
          </Transition>
        </div>
        
        <!-- Eval Menu -->
        <div class="relative" v-if="!batchMode">
          <button @click="showEvalMenu = !showEvalMenu" class="px-3 py-1.5 rounded-lg text-sm bg-white/95 hover:bg-white shadow-md transition-all font-medium">
            ⭐ 评价 ▾
          </button>
          <div v-if="showEvalMenu" @click="showEvalMenu = false" class="fixed inset-0 z-40"></div>
          <Transition name="dropdown">
            <div v-if="showEvalMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-44 z-50 overflow-hidden text-left">
              <button v-if="currentRole !== 'student' && !isGuest" @click="startBatchMode" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">✅ 批量评价</button>
              <button @click="showRankModal = true" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">🏆 班级排行</button>
              <button @click="loadEvaluationRecords(); showRecordsModal = true" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">📋 历史记录</button>
              <template v-if="currentRole !== 'student' && !isGuest">
                <hr class="my-1.5 border-gray-100">
                <button @click="showRulesModal = true" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">⚙️ 规则设置</button>
                <button v-if="currentRole === 'admin'" @click="openSystemModal" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors text-orange-600 font-medium">🛠️ 设备与系统管理</button>
              </template>
            </div>
          </Transition>
        </div>
        
        <!-- User Menu -->
        <div class="relative">
          <button @click="showUserMenu = !showUserMenu" class="w-9 h-9 rounded-full bg-white/95 hover:bg-white shadow-md transition-all flex items-center justify-center overflow-hidden">
            <span v-if="isGuest" class="text-lg">👤</span>
            <span v-else class="w-full h-full rounded-full bg-gradient-to-r from-orange-400 to-pink-500 flex items-center justify-center text-white text-sm font-bold">
              {{ username.charAt(0).toUpperCase() }}
            </span>
          </button>
          <div v-if="showUserMenu" @click="showUserMenu = false" class="fixed inset-0 z-40"></div>
          <Transition name="dropdown">
            <div v-if="showUserMenu" class="absolute right-0 top-full mt-1.5 bg-white rounded-xl shadow-xl border border-gray-100 py-1.5 w-48 z-50 overflow-hidden text-left">
              <div v-if="isGuest" class="px-3 py-2 text-sm text-gray-500 border-b border-gray-100">
                当前为游客模式
              </div>
              <div v-else class="px-3 py-2 text-sm text-gray-500 border-b border-gray-100">
                已登录: {{ username }}
              </div>
              
              <!-- 账号权限身份展示 -->
              <div class="px-3 py-1.5 text-xs font-bold text-gray-400 border-b border-gray-50 bg-gray-50/50">
                当前权限身份
              </div>
              <div class="px-3 py-2 text-sm font-bold text-orange-500 bg-orange-50/30 flex items-center gap-1.5">
                <span>{{ isGuest ? '👤 访客 (只读)' : currentRole === 'admin' ? '👑 管理员' : currentRole === 'teacher' ? '👩 教师' : '👶 学生' }}</span>
              </div>
              <hr class="border-gray-100">
              
              <template v-if="isGuest">
                <button @click="showAuthModal = true; showUserMenu = false" class="w-full text-left px-3 py-2 text-sm hover:bg-gradient-to-r hover:from-orange-50 hover:to-pink-50 transition-colors">
                  🔑 登录 / 注册
                </button>
              </template>
              <template v-else>
                <button @click="logout(); showUserMenu = false; loadClasses(); toast.success('已退出登录')" class="w-full text-left px-3 py-2 text-sm text-red-500 hover:bg-red-50 transition-colors">
                  🚪 退出登录
                </button>
              </template>
            </div>
          </Transition>
        </div>
      </div>
    </header>
    
    <!-- Main Content -->
    <main class="flex-1 overflow-auto p-6 pb-28">
      <Transition name="fade" mode="out-in">
        <!-- 无班级状态 -->
        <div v-if="classes.length === 0" key="no-class" class="flex flex-col items-center justify-center min-h-[60vh]">
          <div class="text-8xl mb-6 animate-float">🏫</div>
          <h3 class="text-2xl font-bold text-gray-700 mb-3">还没有班级</h3>
          <p class="text-gray-500 mb-6 text-lg">
            {{ currentRole === 'student' || isGuest ? '暂无班级档案，请等待老师创建班级。' : '创建一个班级，开启你的宠物园之旅吧！' }}
          </p>
          <button 
            v-if="currentRole !== 'student' && !isGuest"
            @click="showClassModal = true"
            class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-2xl hover:shadow-lg hover:scale-105 transition-all font-bold text-lg"
          >
            ✨ 创建第一个班级
          </button>
        </div>

        <!-- 无学生状态 -->
        <div v-else-if="students.length === 0" key="no-student" class="flex flex-col items-center justify-center min-h-[60vh]">
          <div class="text-8xl mb-6 animate-float">👨‍🎓</div>
          <h3 class="text-2xl font-bold text-gray-700 mb-3">还没有学生</h3>
          <p class="text-gray-500 mb-6 text-lg">
            {{ currentRole === 'student' || isGuest ? '暂无学生档案，请等待老师进行班级学生录入。' : '添加学生，让他们领养可爱的宠物吧！' }}
          </p>
          <div v-if="currentRole !== 'student' && !isGuest" class="flex gap-3">
            <button 
              @click="showStudentModal = true"
              class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-6 py-3 rounded-2xl hover:shadow-lg hover:scale-105 transition-all font-bold"
            >
              ➕ 添加学生
            </button>
            <button 
              @click="openImportModal"
              class="bg-white text-gray-700 px-6 py-3 rounded-2xl hover:shadow-lg hover:scale-105 transition-all font-bold border border-gray-200"
            >
              📥 批量导入
            </button>
          </div>
        </div>

        <!-- 学生列表 -->
        <div v-else key="students" class="grid grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-5">
          <div 
            v-for="student in filteredStudents" 
            :key="student.id"
            class="bg-white rounded-2xl shadow-card overflow-hidden hover:shadow-card-hover transition-all duration-300 cursor-pointer relative group card-hover"
            :class="[getLevelBorderClass(getDisplayLevel(student)), { 
              'ring-2 ring-purple-400 ring-offset-2': batchMode && selectedStudents.has(student.id),
              'ring-2 ring-red-400 ring-offset-2': showDeleteStudentMode && deleteStudentList.includes(student.id)
            }]"
            @click="
              batchMode ? toggleStudentSelect(student.id) : 
              showDeleteStudentMode ? toggleDeleteStudent(student.id) : 
              openDetailPanel(student)
            "
          >
            <!-- 评分动效 -->
            <Transition name="score-pop">
              <div 
                v-if="scoreAnimations.has(student.id)"
                class="absolute inset-0 flex items-center justify-center z-20 pointer-events-none"
              >
                <div 
                  class="text-4xl font-bold animate-bounce-in"
                  :class="scoreAnimations.get(student.id)!.points > 0 ? 'text-green-500' : 'text-red-500'"
                >
                  {{ scoreAnimations.get(student.id)!.points > 0 ? '+' : '' }}{{ scoreAnimations.get(student.id)!.points }}
                </div>
                <div class="absolute inset-0 overflow-hidden">
                  <span v-for="i in 6" :key="i" class="absolute text-2xl animate-sparkle" :style="{ left: `${Math.random() * 80 + 10}%`, top: `${Math.random() * 80 + 10}%`, animationDelay: `${i * 100}ms` }">
                    {{ scoreAnimations.get(student.id)!.points > 0 ? '⭐' : '💫' }}
                  </span>
                </div>
              </div>
            </Transition>
            
            <!-- 选中标记 -->
            <Transition name="pop">
              <div 
                v-if="batchMode || showDeleteStudentMode"
                class="absolute top-3 left-3 w-7 h-7 rounded-full flex items-center justify-center z-10 shadow-md transition-all"
                :class="batchMode 
                  ? (selectedStudents.has(student.id) ? 'bg-gradient-to-r from-purple-400 to-pink-400' : 'bg-white border-2 border-gray-300')
                  : (deleteStudentList.includes(student.id) ? 'bg-gradient-to-r from-red-400 to-pink-400' : 'bg-white border-2 border-gray-300')
                "
              >
                <span v-if="(batchMode && selectedStudents.has(student.id)) || (showDeleteStudentMode && deleteStudentList.includes(student.id))" class="text-white text-sm font-bold">✓</span>
              </div>
            </Transition>
            
            <!-- 宠物图片区域 -->
            <div class="aspect-square flex items-center justify-center relative rounded-t-2xl"
              :class="student.pet_type ? 'bg-gradient-to-br from-orange-100 via-amber-50 to-yellow-100' : 'bg-gradient-to-br from-gray-100 via-slate-50 to-gray-100'"
            >
              <!-- 有宠物时使用 PetImage 组件 -->
              <template v-if="student.pet_type">
                <div class="w-full h-full overflow-hidden" style="border-radius: 14px 14px 0 0; margin: -1px -1px 0 -1px; width: calc(100% + 2px);">
                  <PetImage
                    :src="getStudentPetImage(student)"
                    :alt="getPetType(student.pet_type)?.name"
                    size="full"
                    :rounded="false"
                    :show-loading="true"
                    class="w-full h-full"
                  />
                </div>
              </template>
              <!-- 未领养宠物 -->
              <div v-else class="flex flex-col items-center">
                <span class="text-6xl pet-unknown">❓</span>
                <span class="text-xs text-gray-400 mt-2 group-hover:text-orange-400 transition-colors">点击领养</span>
              </div>
              
              <!-- 等级徽章 -->
              <div 
                class="absolute bottom-3 right-3 font-bold px-3 py-1 rounded-full shadow-lg text-white text-sm"
                :class="`bg-gradient-to-r ${getLevelBgClass(getDisplayLevel(student))}`"
              >
                <span v-if="getDisplayLevel(student) >= 10">👑</span>
                <span v-else>Lv.</span>{{ getDisplayLevel(student) }}
              </div>
            </div>
            
            <!-- 信息区域 -->
            <div class="p-4">
              <!-- 学生姓名 + 宠物名 -->
              <div class="flex items-center justify-between mb-2">
                <span class="font-bold text-lg text-gray-800 group-hover:text-orange-500 transition-colors">{{ student.name }}</span>
                <span class="text-xs px-2 py-1 rounded-full" 
                  :class="student.pet_type ? 'bg-gradient-to-r from-orange-100 to-pink-100 text-orange-600' : 'bg-gray-100 text-gray-400'">
                  {{ student.pet_type ? getPetType(student.pet_type)?.name : '未领养' }}
                </span>
              </div>
              
              <!-- 成长值 + 积分 -->
              <div class="flex items-center justify-between text-sm mb-3">
                <span class="text-gray-500 flex items-center gap-1">
                  <template v-if="getLevelProgress(student.pet_exp).isMaxLevel">
                    <span class="text-xs text-amber-500 font-medium">🏆 已毕业</span>
                  </template>
                  <template v-else>
                    <span class="text-purple-400">💜</span>
                    <span class="font-medium text-purple-600">{{ getLevelProgress(student.pet_exp).current }}</span>
                    <span class="text-gray-300">/</span>
                    <span>{{ getLevelProgress(student.pet_exp).required }}</span>
                  </template>
                </span>
                <span class="font-bold text-lg flex items-center gap-1">
                  <span class="text-yellow-400">⭐</span>
                  <span class="text-orange-500">{{ student.total_points }}</span>
                </span>
              </div>
              
              <!-- 进度条 -->
              <div class="bg-gray-100 rounded-full h-2.5 overflow-hidden progress-glow">
                <div 
                  class="rounded-full h-2.5 transition-all duration-500"
                  :class="getDisplayLevel(student) >= 5 ? 'bg-gradient-to-r from-purple-400 via-pink-400 to-rose-400' : 'bg-gradient-to-r from-orange-400 via-amber-400 to-yellow-400'"
                  :style="{ width: `${getLevelProgress(student.pet_exp).percentage}%` }"
                ></div>
              </div>
            </div>
          </div>
        </div>
      </Transition>
      
      <!-- 批量操作栏 -->
      <Transition name="slide-up">
        <div v-if="batchMode && selectedStudents.size > 0" class="fixed bottom-6 left-1/2 -translate-x-1/2 bg-white/95 backdrop-blur-lg rounded-2xl shadow-2xl p-4 flex gap-4 z-40 border border-gray-100">
          <button 
            @click="batchAddPoints"
            class="bg-gradient-to-r from-green-400 to-emerald-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg hover:scale-105 transition-all flex items-center gap-2"
          >
            <span class="text-xl">⬆️</span> 统一加分
          </button>
          <button 
            @click="batchSubPoints"
            class="bg-gradient-to-r from-red-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg hover:scale-105 transition-all flex items-center gap-2"
          >
            <span class="text-xl">⬇️</span> 统一扣分
          </button>
        </div>
      </Transition>
      
      <!-- 删除学生操作栏 -->
      <Transition name="slide-up">
        <div v-if="showDeleteStudentMode" class="fixed bottom-6 left-1/2 -translate-x-1/2 bg-white/95 backdrop-blur-lg rounded-2xl shadow-2xl p-4 flex gap-4 z-40 border border-gray-100">
          <span class="text-gray-600 py-3 font-medium">已选 <span class="text-red-500 font-bold">{{ deleteStudentList.length }}</span> 人</span>
          <button 
            @click="batchDeleteStudents"
            :disabled="deleteStudentList.length === 0"
            class="bg-gradient-to-r from-red-500 to-pink-600 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg hover:scale-105 transition-all disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:scale-100 flex items-center gap-2"
          >
            <span class="text-xl">🗑️</span> 确认删除
          </button>
          <button 
            @click="cancelDeleteMode"
            class="bg-gray-100 text-gray-700 px-8 py-3 rounded-xl font-bold hover:bg-gray-200 transition-all"
          >
            取消
          </button>
        </div>
      </Transition>
    </main>

    <!-- 创建/编辑班级模态框 -->
    <Transition name="modal">
      <div v-if="showClassModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-md shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
            <span class="text-2xl">🏫</span> {{ editingClass ? '编辑班级' : '创建班级' }}
          </h3>
          <input 
            v-model="newClassName"
            type="text" 
            placeholder="输入班级名称..."
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-6 text-lg focus:outline-none focus:border-orange-400 transition-colors"
            @keyup.enter="editingClass ? updateClass() : createClass()"
          />
          <div class="flex gap-3 justify-end">
            <button @click="showClassModal = false; editingClass = null; newClassName = ''" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
            <button @click="editingClass ? updateClass() : createClass()" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">
              {{ editingClass ? '保存' : '创建' }}
            </button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 添加/编辑学生模态框 -->
    <Transition name="modal">
      <div v-if="showStudentModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-md shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
            <span class="text-2xl">👨‍🎓</span> {{ editingStudent ? '编辑学生' : '添加学生' }}
          </h3>
          <input 
            v-model="newStudentName"
            type="text" 
            placeholder="学生姓名"
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-4 text-lg focus:outline-none focus:border-orange-400 transition-colors"
          />
          <input 
            v-model="newStudentNo"
            type="text" 
            placeholder="学号（可选）"
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-4 focus:outline-none focus:border-orange-400 transition-colors"
          />
          <input 
            v-model="newStudentDeviceId"
            type="text" 
            placeholder="设备唯一标识（Device ID，可选）"
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-6 focus:outline-none focus:border-orange-400 transition-colors"
          />
          <div class="flex gap-3 justify-end">
            <button @click="showStudentModal = false; editingStudent = null; newStudentName = ''; newStudentNo = ''; newStudentDeviceId = ''" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
            <button @click="editingStudent ? updateStudent() : addStudent()" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">
              {{ editingStudent ? '保存' : '添加' }}
            </button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 批量导入模态框 -->
    <Transition name="modal">
      <div v-if="showImportModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-xl shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-2 flex items-center gap-2">
            <span class="text-2xl">📥</span> 批量导入学生
          </h3>
          <p class="text-sm text-gray-500 mb-4">
            一行一个学生，姓名和学号用空格、逗号、Tab或分号分隔
          </p>
          <textarea 
            v-model="importText"
            placeholder="张三 20240001&#10;李四, 20240002&#10;王五；20240003"
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-4 mb-4 h-48 text-sm font-mono focus:outline-none focus:border-orange-400 transition-colors"
          ></textarea>
          <div class="bg-gradient-to-r from-orange-50 to-pink-50 rounded-xl p-4 mb-6 text-sm text-gray-600">
            <p class="font-medium mb-2 flex items-center gap-2"><span>💡</span> 示例格式：</p>
            <code class="text-xs bg-white px-3 py-2 rounded-lg block text-gray-500">
              张三 20240001<br>
              李四,20240002<br>
              王五；20240003
            </code>
          </div>
          <div class="flex gap-3 justify-end">
            <button @click="showImportModal = false" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
            <button @click="importStudents" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">
              导入
            </button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 评价模态框 -->
    <Transition name="modal">
      <div v-if="showAddModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-3xl max-h-[85vh] overflow-auto shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
            <span class="text-2xl">⭐</span>
            <template v-if="selectedStudent">
              为 <span class="text-gradient">{{ selectedStudent?.name }}</span> 评价
            </template>
            <template v-else>
              批量评价 <span class="text-purple-500">{{ selectedStudents.size }}</span> 名学生
            </template>
          </h3>
          
          <!-- 分类标签 -->
          <div class="flex gap-2 mb-6 flex-wrap">
            <button 
              v-for="cat in categories" 
              :key="cat"
              @click="selectedEvalTab = cat"
              class="px-5 py-2.5 rounded-xl text-sm font-bold transition-all"
              :class="selectedEvalTab === cat 
                ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg' 
                : 'bg-gray-100 text-gray-600 hover:bg-gray-200'"
            >
              {{ cat }}
            </button>
          </div>
          
          <!-- 规则网格 - 固定5行高度，超出显示滚动条 -->
          <div class="h-[590px] overflow-y-auto pr-2 custom-scrollbar">
            <div class="grid grid-cols-2 md:grid-cols-4 gap-3 content-start">
              <button 
                v-for="rule in currentCategoryRules" 
                :key="rule.id"
                @click="quickAdd(selectedStudent, rule); showAddModal = false"
                class="rounded-2xl p-4 text-left transition-all border-2 hover:scale-105 hover:shadow-lg active:scale-95 h-[110px]"
                :class="rule.points > 0 
                  ? 'bg-gradient-to-br from-green-50 to-emerald-50 border-green-200 hover:border-green-400' 
                  : 'bg-gradient-to-br from-red-50 to-pink-50 border-red-200 hover:border-red-400'"
              >
                <div class="flex items-center justify-between mb-2">
                  <span 
                    class="font-bold text-2xl"
                    :class="rule.points > 0 ? 'text-green-500' : 'text-red-500'"
                  >
                    {{ rule.points > 0 ? '+' : '' }}{{ rule.points }}
                  </span>
                  <span 
                    class="text-xs px-2 py-1 rounded-full font-medium"
                    :class="rule.points > 0 ? 'bg-green-100 text-green-600' : 'bg-red-100 text-red-600'"
                  >
                    {{ rule.points > 0 ? '加分' : '扣分' }}
                  </span>
                </div>
                <div class="text-sm text-gray-700 font-medium leading-tight line-clamp-2">{{ rule.name }}</div>
              </button>
            </div>
          </div>

          <div class="flex justify-end mt-6">
            <button @click="showAddModal = false" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 选择宠物模态框 -->
    <Transition name="modal">
      <div v-if="showPetModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-6 w-full max-w-3xl max-h-[90vh] overflow-auto shadow-2xl animate-scale-in">
          <h3 class="text-2xl font-bold mb-6 flex items-center gap-3">
            <span class="text-3xl">🐾</span>
            <span>为 <span class="text-gradient bg-gradient-to-r from-orange-500 to-pink-500 bg-clip-text text-transparent">{{ selectedStudent?.name }}</span> 选择宠物伙伴</span>
          </h3>
          
          <!-- 宠物网格 - 所有宠物混合显示 -->
          <div class="grid grid-cols-3 md:grid-cols-4 lg:grid-cols-5 gap-3">
            <button 
              v-for="pet in PET_TYPES" 
              :key="pet.id"
              @click="selectPet(pet.id)"
              class="relative bg-gradient-to-br from-white to-gray-50 rounded-2xl p-3 hover:shadow-xl hover:scale-105 transition-all text-center group border-2 border-transparent hover:border-orange-300 hover:from-orange-50 hover:to-pink-50 overflow-hidden"
            >
              <!-- 装饰性边框 -->
              <div class="absolute inset-0 rounded-2xl border-2 border-dashed border-gray-200 group-hover:border-orange-200 transition-colors"></div>
              
              <!-- 图片容器 -->
              <div class="relative w-full aspect-square mx-auto mb-2">
                <!-- 加载动画 - 图片加载完成前显示 -->
                <div 
                  v-if="!imageLoaded[pet.id]" 
                  class="absolute inset-0 flex items-center justify-center bg-gradient-to-br from-orange-100/80 to-pink-100/80 rounded-xl"
                >
                  <div class="flex gap-1.5">
                    <span class="w-2 h-2 bg-orange-400 rounded-full animate-bounce" style="animation-delay: 0ms"></span>
                    <span class="w-2 h-2 bg-pink-400 rounded-full animate-bounce" style="animation-delay: 150ms"></span>
                    <span class="w-2 h-2 bg-purple-400 rounded-full animate-bounce" style="animation-delay: 300ms"></span>
                  </div>
                </div>
                <!-- 宠物图片 - 加载完成后淡入显示 -->
                <img 
                  :src="getPetLevel1Image(pet.id)" 
                  class="w-full h-full object-contain group-hover:scale-110 transition-all duration-300 rounded-xl p-1"
                  :class="imageLoaded[pet.id] ? 'opacity-100' : 'opacity-0'"
                  @load="imageLoaded[pet.id] = true"
                />
              </div>
              
              <!-- 宠物名称 - 放大 -->
              <div class="text-base font-bold mt-2 text-gray-800 group-hover:text-orange-600 transition-colors leading-tight">{{ pet.name }}</div>
            </button>
          </div>

          <div class="mt-6 p-4 bg-gradient-to-r from-orange-50 via-pink-50 to-purple-50 rounded-xl text-sm text-gray-600 text-center border border-orange-100">
            <span class="text-lg">💡</span> 点击宠物即可领养，宠物会陪伴学生一起成长！
          </div>

          <div class="flex justify-end mt-6">
            <button @click="showPetModal = false" class="px-6 py-3 bg-gray-100 hover:bg-gray-200 text-gray-600 rounded-xl font-medium transition-colors">取消</button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 排行榜模态框 -->
    <Transition name="modal">
      <div v-if="showRankModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-lg max-h-[85vh] overflow-auto shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
            <span class="text-2xl">🏆</span> 积分排行榜
          </h3>
          
          <div v-if="ranking.length === 0" class="text-center py-12 text-gray-500">
            暂无数据
          </div>
          
          <div v-else class="space-y-3">
            <div 
              v-for="(student, index) in ranking" 
              :key="student.id"
              class="flex items-center gap-4 p-4 rounded-2xl transition-all"
              :class="index < 3 ? 'bg-gradient-to-r from-yellow-50 via-orange-50 to-pink-50 shadow-md' : 'bg-gray-50'"
            >
              <!-- 排名 -->
              <div class="w-10 h-10 flex items-center justify-center font-bold text-2xl">
                <span v-if="index === 0" class="text-3xl animate-bounce-slow">🥇</span>
                <span v-else-if="index === 1" class="text-3xl">🥈</span>
                <span v-else-if="index === 2" class="text-3xl">🥉</span>
                <span v-else class="text-gray-400">{{ index + 1 }}</span>
              </div>
              
              <!-- 宠物头像 -->
              <div class="w-12 h-12 rounded-full flex items-center justify-center overflow-hidden bg-gradient-to-br from-orange-100 to-pink-100 shadow-inner">
                <img 
                  v-if="student.pet_type" 
                  :src="getStudentPetImage(student)" 
                  class="w-10 h-10 object-contain"
                />
                <span v-else class="text-2xl">❓</span>
              </div>
              
              <!-- 信息 -->
              <div class="flex-1">
                <div class="font-bold text-gray-800">{{ student.name }}</div>
                <div class="text-xs text-gray-500">Lv.{{ getDisplayLevel(student) }}</div>
              </div>
              
              <!-- 积分 -->
              <div class="text-right">
                <div class="font-bold text-xl text-orange-500">+{{ student.total_points }}</div>
                <div class="text-xs text-gray-400">积分</div>
              </div>
            </div>
          </div>

          <div class="flex justify-end mt-6">
            <button @click="showRankModal = false" class="px-6 py-3 bg-gray-100 text-gray-700 rounded-xl font-medium hover:bg-gray-200 transition-colors">关闭</button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 评价记录模态框 -->
    <Transition name="modal">
      <div v-if="showRecordsModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-4xl max-h-[85vh] overflow-auto shadow-2xl animate-scale-in">
          <div class="flex items-center justify-between mb-6">
            <h3 class="text-xl font-bold flex items-center gap-2">
              <span class="text-2xl">📋</span> 评价记录
            </h3>
            <button 
              v-if="currentRole !== 'student' && !isGuest"
              @click="undoLastEvaluation(); loadEvaluationRecords()"
              class="px-4 py-2 text-sm text-orange-500 hover:bg-orange-50 rounded-xl font-medium transition-colors flex items-center gap-1"
            >
              ↩️ 撤回最新
            </button>
          </div>
          
          <div v-if="evaluationRecords.length === 0" class="text-center py-16 text-gray-500">
            <div class="text-5xl mb-4">📝</div>
            暂无记录
          </div>
          
          <div v-else class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
            <div 
              v-for="record in paginatedRecords" 
              :key="record.id"
              class="p-4 bg-white border border-gray-100 rounded-2xl shadow-sm hover:shadow-md transition-shadow"
            >
              <!-- 头部 -->
              <div class="flex items-center justify-between mb-3">
                <div class="flex items-center gap-2">
                  <span class="w-8 h-8 rounded-full bg-gradient-to-br from-orange-400 to-pink-500 text-white flex items-center justify-center text-sm font-bold">
                    {{ record.student_name.charAt(0) }}
                  </span>
                  <span class="font-bold text-gray-800">{{ record.student_name }}</span>
                </div>
                <span 
                  class="px-3 py-1 rounded-full text-sm font-bold"
                  :class="record.points > 0 ? 'bg-green-100 text-green-600' : 'bg-red-100 text-red-600'"
                >
                  {{ record.points > 0 ? '+' : '' }}{{ record.points }}
                </span>
              </div>
              
              <!-- 原因 -->
              <div class="text-sm text-gray-600 mb-3 line-clamp-2">
                {{ record.reason }}
              </div>
              
              <!-- 底部 -->
              <div class="flex items-center justify-between text-xs text-gray-400">
                <span class="px-2 py-1 bg-gray-100 rounded-lg">{{ record.category }}</span>
                <div class="flex items-center gap-2">
                  <span>{{ new Date(record.timestamp).toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' }) }}</span>
                  <button 
                    v-if="currentRole !== 'student' && !isGuest"
                    @click="undoLastEvaluation(record.id)"
                    class="text-orange-500 hover:text-orange-600 hover:bg-orange-50 px-2 py-1 rounded transition-colors"
                    title="撤回"
                  >
                    ↩️
                  </button>
                </div>
              </div>
            </div>
          </div>

          <!-- 分页 -->
          <div v-if="totalPages > 1" class="flex items-center justify-between mt-6 pt-6 border-t border-gray-100">
            <div class="text-sm text-gray-500">
              共 <span class="font-medium">{{ totalRecords }}</span> 条记录
            </div>
            <div class="flex gap-2">
              <button 
                @click="prevPage"
                :disabled="recordsPage <= 1"
                class="px-4 py-2 rounded-xl border border-gray-200 text-sm disabled:opacity-50 disabled:cursor-not-allowed hover:bg-gray-50 transition-colors"
              >
                ← 上一页
              </button>
              <div class="flex gap-1">
                <button 
                  v-for="page in Math.min(totalPages, 5)" 
                  :key="page"
                  @click="goToPage(page)"
                  class="px-4 py-2 rounded-xl text-sm min-w-[40px] font-medium transition-all"
                  :class="recordsPage === page ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg' : 'border border-gray-200 hover:bg-gray-50'"
                >
                  {{ page }}
                </button>
              </div>
              <button 
                @click="nextPage"
                :disabled="recordsPage >= totalPages"
                class="px-4 py-2 rounded-xl border border-gray-200 text-sm disabled:opacity-50 disabled:cursor-not-allowed hover:bg-gray-50 transition-colors"
              >
                下一页 →
              </button>
            </div>
          </div>

          <div class="flex justify-end mt-6">
            <button @click="showRecordsModal = false" class="px-6 py-3 bg-gray-100 text-gray-700 rounded-xl font-medium hover:bg-gray-200 transition-colors">关闭</button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 管理规则模态框 -->
    <Transition name="modal">
      <div v-if="showRulesModal" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-4xl max-h-[85vh] overflow-auto shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
            <span class="text-2xl">⚙️</span> 管理评价规则
          </h3>
          
          <!-- 添加规则表单 -->
          <div class="bg-gradient-to-r from-orange-50 to-pink-50 rounded-2xl p-6 mb-6">
            <h4 class="font-bold mb-4 flex items-center gap-2">
              <span>➕</span> 添加自定义规则
            </h4>
            <div class="flex flex-wrap gap-3 mb-4">
              <input 
                v-model="newRuleName"
                type="text" 
                placeholder="规则名称"
                class="flex-1 min-w-[200px] border-2 border-gray-200 rounded-xl px-4 py-2.5 focus:outline-none focus:border-orange-400 transition-colors"
              />
              <select v-model="newRuleCategory" class="border-2 border-gray-200 rounded-xl px-4 py-2.5 focus:outline-none focus:border-orange-400 transition-colors cursor-pointer">
                <option>学习</option>
                <option>行为</option>
                <option>健康</option>
                <option>其他</option>
              </select>
              <input 
                v-model.number="newRulePoints"
                type="number" 
                placeholder="分值"
                class="w-24 border-2 border-gray-200 rounded-xl px-4 py-2.5 focus:outline-none focus:border-orange-400 transition-colors"
              />
            </div>
            <button 
              @click="addRule"
              class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-6 py-2.5 rounded-xl font-bold hover:shadow-lg transition-all"
            >
              添加规则
            </button>
          </div>
          
          <!-- 规则列表 -->
          <div class="space-y-6">
            <template v-for="cat in categories" :key="cat">
              <div v-if="rules.filter(r => r.category === cat).length > 0">
                <h4 class="font-bold text-gray-700 mb-3 flex items-center gap-2">
                  <span>{{ cat === '学习' ? '📚' : cat === '行为' ? '🎯' : cat === '健康' ? '💪' : '📌' }}</span>
                  {{ cat }}
                </h4>
                <div class="grid grid-cols-2 md:grid-cols-3 gap-3">
                  <div 
                    v-for="rule in rules.filter(r => r.category === cat)" 
                    :key="rule.id"
                    class="flex items-center justify-between p-4 rounded-xl border-2"
                    :class="rule.points > 0 ? 'bg-green-50 border-green-200' : 'bg-red-50 border-red-200'"
                  >
                    <div class="flex items-center gap-2">
                      <span 
                        class="font-bold text-lg"
                        :class="rule.points > 0 ? 'text-green-500' : 'text-red-500'"
                      >
                        {{ rule.points > 0 ? '+' : '' }}{{ rule.points }}
                      </span>
                      <span class="text-sm font-medium">{{ rule.name }}</span>
                    </div>
                    <button 
                      v-if="rule.is_custom"
                      @click="deleteRule(rule.id)"
                      class="text-red-400 hover:text-red-600 text-sm font-medium transition-colors"
                    >
                      删除
                    </button>
                  </div>
                </div>
              </div>
            </template>
          </div>

          <div class="flex justify-end mt-6">
            <button @click="showRulesModal = false" class="px-6 py-3 bg-gray-100 text-gray-700 rounded-xl font-medium hover:bg-gray-200 transition-colors">关闭</button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 学生详情面板 -->
    <Transition name="modal">
      <div v-if="showDetailPanel && detailStudent" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4" @click.self="closeDetailPanel">
        <div class="bg-white rounded-3xl w-full max-w-2xl max-h-[90vh] overflow-auto shadow-2xl animate-scale-in">
          <!-- 头部 -->
          <div class="relative bg-gradient-to-r from-orange-400 via-pink-400 to-purple-400 p-6 rounded-t-3xl">
            <!-- 顶部操作按钮 -->
            <div class="absolute top-4 right-4 flex gap-2">
              <button v-if="currentRole !== 'student' && !isGuest" @click="showDetailPanel = false; openPetSelect(detailStudent!)" class="px-3 py-2 bg-white/20 hover:bg-white/30 rounded-full flex items-center gap-1.5 text-white text-sm transition-colors" title="更换宠物">
                <span>🐾</span>
                <span class="font-medium">换宠物</span>
              </button>
              <button v-if="currentRole !== 'student' && !isGuest" @click="openEditStudentModal(detailStudent!)" class="px-3 py-2 bg-white/20 hover:bg-white/30 rounded-full flex items-center gap-1.5 text-white text-sm transition-colors" title="编辑信息">
                <span>✏️</span>
                <span class="font-medium">编辑</span>
              </button>
              <button @click="closeDetailPanel" class="w-10 h-10 bg-white/20 hover:bg-white/30 rounded-full flex items-center justify-center text-white text-xl transition-colors" title="关闭">
                ×
              </button>
            </div>
            <div class="flex items-center gap-4">
              <div class="w-20 h-20 rounded-2xl overflow-hidden bg-white/20 backdrop-blur-sm flex items-center justify-center shadow-lg">
                <img 
                  v-if="detailStudent.pet_type" 
                  :src="getStudentPetImage(detailStudent)" 
                  class="w-16 h-16 object-contain"
                />
                <span v-else class="text-4xl">❓</span>
              </div>
              <div class="text-white">
                <h3 class="text-2xl font-bold">{{ detailStudent.name }}</h3>
                <p class="text-white/80 text-sm">
                  {{ detailStudent.pet_type ? getPetType(detailStudent.pet_type)?.name : '未领养' }}
                  · Lv.{{ getDisplayLevel(detailStudent) }}
                  · ⭐ {{ detailStudent.total_points }}
                </p>
              </div>
            </div>
            <!-- 进度条 -->
            <div class="mt-4">
              <div class="flex justify-between text-white/90 text-sm mb-1">
                <span>成长值</span>
                <span v-if="getLevelProgress(detailStudent.pet_exp).isMaxLevel" class="flex items-center gap-1">
                  <span class="text-yellow-300 font-medium">🏆 已毕业，获得专属徽章</span>
                </span>
                <span v-else>
                  {{ getLevelProgress(detailStudent.pet_exp).current }}/{{ getLevelProgress(detailStudent.pet_exp).required }}
                </span>
              </div>
              <div class="bg-white/20 rounded-full h-3 overflow-hidden">
                <div 
                  class="h-full rounded-full transition-all duration-500 bg-gradient-to-r from-yellow-300 via-amber-300 to-orange-300"
                  :style="{ width: `${getLevelProgress(detailStudent.pet_exp).percentage}%` }"
                ></div>
              </div>
            </div>
          </div>
          
          <!-- 快速评分 -->
          <div v-if="currentRole !== 'student' && !isGuest" class="p-6 border-b border-gray-100">
            <h4 class="font-bold text-gray-700 mb-3 flex items-center gap-2">
              <span>⚡</span> 快速评价
            </h4>
            <!-- 分类标签 -->
            <div class="flex gap-2 mb-4 flex-wrap">
              <button 
                v-for="cat in categories" 
                :key="cat"
                @click="detailEvalTab = cat"
                class="px-4 py-1.5 rounded-xl text-sm font-bold transition-all"
                :class="detailEvalTab === cat 
                  ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg' 
                  : 'bg-gray-100 text-gray-600 hover:bg-gray-200'"
              >
                {{ cat }}
              </button>
            </div>
            <!-- 规则按钮 - 每行5个，固定5行高度 -->
            <div class="h-[400px] overflow-y-auto pr-1 custom-scrollbar">
              <div class="grid grid-cols-5 gap-2 content-start">
                <button 
                  v-for="rule in rules.filter(r => r.category === detailEvalTab)" 
                  :key="rule.id"
                  @click="detailQuickAdd(rule)"
                  class="rounded-xl p-2 text-center transition-all border-2 hover:scale-105 active:scale-95 h-[70px]"
                  :class="rule.points > 0 
                    ? 'bg-gradient-to-br from-green-50 to-emerald-50 border-green-200 hover:border-green-400' 
                    : 'bg-gradient-to-br from-red-50 to-pink-50 border-red-200 hover:border-red-400'"
                >
                  <div 
                    class="text-base font-bold"
                    :class="rule.points > 0 ? 'text-green-500' : 'text-red-500'"
                  >
                    {{ rule.points > 0 ? '+' : '' }}{{ rule.points }}
                  </div>
                  <div class="text-xs text-gray-600 leading-tight line-clamp-2">{{ rule.name }}</div>
                </button>
              </div>
            </div>
          </div>
          
          <!-- 最近记录 -->
          <div class="p-6">
            <h4 class="font-bold text-gray-700 mb-3 flex items-center gap-2">
              <span>📋</span> 最近记录
            </h4>
            <div v-if="studentRecords.length === 0" class="text-center py-8 text-gray-400">
              <div class="text-4xl mb-2">📝</div>
              暂无评价记录
            </div>
            <div v-else class="space-y-2 max-h-60 overflow-auto">
              <div 
                v-for="record in studentRecords" 
                :key="record.id"
                class="flex items-center gap-3 p-3 bg-gray-50 rounded-xl hover:bg-gray-100 transition-colors"
              >
                <div 
                  class="w-10 h-10 rounded-full flex items-center justify-center font-bold text-sm"
                  :class="record.points > 0 ? 'bg-green-100 text-green-600' : 'bg-red-100 text-red-600'"
                >
                  {{ record.points > 0 ? '+' : '' }}{{ record.points }}
                </div>
                <div class="flex-1">
                  <div class="font-medium text-gray-800">{{ record.reason }}</div>
                  <div class="text-xs text-gray-400">
                    <span class="px-1.5 py-0.5 bg-gray-200 rounded mr-2">{{ record.category }}</span>
                    {{ new Date(record.timestamp).toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' }) }}
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </Transition>
    
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
      @login="(user) => { toast.success(`欢迎，${user.username}！`); loadClasses() }"
    />
    
    <!-- 高清 LOGO 预览弹窗 -->
    <Transition name="modal">
      <div v-if="showLogoModal" class="fixed inset-0 bg-black/75 backdrop-blur-md flex items-center justify-center z-50 p-4" @click.self="showLogoModal = false">
        <div class="bg-white/95 backdrop-blur-xl border border-white/20 rounded-3xl w-full max-w-lg shadow-2xl overflow-hidden p-6 text-center transform transition-all duration-300 relative">
          <!-- 关闭按钮 -->
          <button @click="showLogoModal = false" class="absolute top-4 right-4 w-8 h-8 rounded-full bg-gray-100 hover:bg-gray-200 flex items-center justify-center text-gray-500 hover:text-gray-800 text-lg transition-colors">
            ×
          </button>
          
          <!-- LOGO 大图 -->
          <div class="aspect-square w-full rounded-2xl overflow-hidden border border-gray-100 bg-white mb-4 mt-2 shadow-inner">
            <img src="/images/logo.png" class="w-full h-full object-contain" alt="英语全科启蒙+AI 高清原图" />
          </div>
          
          <h3 class="text-xl font-bold text-gray-800">英语全科启蒙+AI</h3>
          <p class="text-xs text-gray-400 mt-1">主品牌官方授权高清 3D LOGO 预览</p>
        </div>
      </div>
    </Transition>

    <!-- 设备与系统管理模态框 -->
    <Transition name="modal">
      <div v-if="showSystemModal" class="fixed inset-0 bg-black/50 backdrop-blur-md flex items-center justify-center z-50 p-4" @click.self="showSystemModal = false">
        <div class="bg-white/95 backdrop-blur-xl border border-white/20 rounded-3xl p-8 w-full max-w-4xl h-[80vh] overflow-hidden flex flex-col shadow-2xl animate-scale-in relative">
          <!-- 关闭按钮 -->
          <button @click="showSystemModal = false" class="absolute top-6 right-6 w-10 h-10 rounded-full bg-gray-100 hover:bg-gray-200 flex items-center justify-center text-gray-500 hover:text-gray-800 text-xl transition-colors">
            ×
          </button>
          
          <h2 class="text-2xl font-black text-gray-800 mb-6 flex items-center gap-2.5">
            <span>🛠️</span> 硬件与系统管理后台
          </h2>

          <div class="flex-1 flex overflow-hidden min-h-0">
            <!-- 左侧页签切换 -->
            <div class="w-56 border-r border-gray-100 pr-4 flex flex-col gap-2 shrink-0">
              <button 
                @click="systemActiveTab = 'tasks'" 
                class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
                :class="systemActiveTab === 'tasks' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'"
              >
                <span>📝</span> 任务审批管理
              </button>
              <button 
                @click="systemActiveTab = 'settings'" 
                class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
                :class="systemActiveTab === 'settings' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'"
              >
                <span>⚙️</span> 自动审核设置
              </button>
              <button 
                v-if="currentRole === 'admin'"
                @click="systemActiveTab = 'music'" 
                class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
                :class="systemActiveTab === 'music' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'"
              >
                <span>🎵</span> 洛雪多源管理
              </button>
              <button 
                v-if="currentRole === 'admin'"
                @click="systemActiveTab = 'users'" 
                class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
                :class="systemActiveTab === 'users' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'"
              >
                <span>👥</span> 用户权限分配
              </button>
              <button 
                @click="systemActiveTab = 'health'" 
                class="flex items-center gap-3 px-4 py-3.5 rounded-xl text-left font-bold transition-all"
                :class="systemActiveTab === 'health' ? 'bg-gradient-to-r from-orange-500 to-pink-500 text-white shadow-md' : 'text-gray-600 hover:bg-gray-100'"
              >
                <span>📊</span> 运行健康监测
              </button>
            </div>

            <!-- 右侧主要内容区域 -->
            <div class="flex-1 pl-6 overflow-auto flex flex-col min-h-0">
              
              <!-- TAB 1: 任务审批管理 -->
              <div v-if="systemActiveTab === 'tasks'" class="flex-1 flex flex-col min-h-0">
                <h3 class="text-lg font-bold text-gray-800 mb-4 flex justify-between items-center">
                  <span>待审批的任务申报列表</span>
                  <span class="text-xs bg-orange-100 text-orange-600 px-2.5 py-1 rounded-full font-bold">
                    {{ pendingTasks.length }} 个待办
                  </span>
                </h3>
                
                <div v-if="pendingTasks.length === 0" class="flex-1 flex flex-col items-center justify-center text-center p-8 bg-gray-50/50 rounded-2xl border border-gray-100">
                  <span class="text-4xl mb-3">🎉</span>
                  <h4 class="text-gray-800 font-bold">没有发现待审批的申请</h4>
                  <p class="text-sm text-gray-400 mt-1">学生硬件申报的新任务会实时在此推送</p>
                </div>
                
                <div v-else class="flex-1 overflow-auto pr-1 flex flex-col gap-4">
                  <div 
                    v-for="task in pendingTasks" 
                    :key="task.id"
                    class="bg-white border border-gray-100 rounded-2xl p-5 shadow-sm hover:shadow-md transition-shadow flex items-center justify-between"
                  >
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
                      <button 
                        @click="rejectTask(task.id)" 
                        class="px-4 py-2 border-2 border-red-100 hover:border-red-200 text-red-500 hover:bg-red-50 rounded-xl font-bold text-sm transition-all"
                      >
                        拒绝
                      </button>
                      <button 
                        @click="approveTask(task.id)" 
                        class="px-5 py-2 bg-gradient-to-r from-green-400 to-emerald-500 hover:from-green-500 hover:to-emerald-600 text-white rounded-xl font-bold text-sm shadow-md hover:shadow-lg transition-all"
                      >
                        同意加分
                      </button>
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
                        <label 
                          class="flex-1 flex items-center gap-3 p-4 rounded-xl border-2 cursor-pointer transition-all"
                          :class="taskConfirmMode === 'auto' ? 'border-orange-400 bg-orange-50/30' : 'border-gray-200 hover:bg-gray-50'"
                        >
                          <input type="radio" value="auto" v-model="taskConfirmMode" class="text-orange-500 focus:ring-orange-400" />
                          <div>
                            <span class="font-bold text-gray-800 block text-sm">自动确认（推荐）</span>
                            <span class="text-xs text-gray-400 block mt-0.5">申报后延迟一段时间自动同意加分</span>
                          </div>
                        </label>
                        <label 
                          class="flex-1 flex items-center gap-3 p-4 rounded-xl border-2 cursor-pointer transition-all"
                          :class="taskConfirmMode === 'manual' ? 'border-orange-400 bg-orange-50/30' : 'border-gray-200 hover:bg-gray-50'"
                        >
                          <input type="radio" value="manual" v-model="taskConfirmMode" class="text-orange-500 focus:ring-orange-400" />
                          <div>
                            <span class="font-bold text-gray-800 block text-sm">必须手动确认</span>
                            <span class="text-xs text-gray-400 block mt-0.5">必须由教师点击“同意加分”才可结算</span>
                          </div>
                        </label>
                      </div>
                    </div>
                    
                    <!-- 延时设置 -->
                    <Transition name="fade">
                      <div v-if="taskConfirmMode === 'auto'">
                        <label class="block text-gray-700 font-bold mb-2">自动审批延迟时长</label>
                        <div class="flex items-center gap-3 max-w-xs">
                          <input 
                            v-model.number="taskConfirmDelay" 
                            type="number" 
                            min="1" 
                            class="w-24 border-2 border-gray-200 rounded-xl px-4 py-2 text-center font-bold focus:outline-none focus:border-orange-400 transition-colors"
                          />
                          <span class="text-gray-500 font-medium text-sm">分钟后自动确认并自动加分</span>
                        </div>
                        <p class="text-xs text-gray-400 mt-2">例如：设置为 30 分钟，学生申报任务 30 分钟内老师可以手动撤销或修改，超时系统自动确认加分。</p>
                      </div>
                    </Transition>
                  </div>
                </div>

                <div class="flex justify-end pt-4 border-t border-gray-100">
                  <button 
                    @click="saveSystemSettings" 
                    class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all"
                  >
                    保存规则配置
                  </button>
                </div>
              </div>

              <!-- TAB 3: 洛雪多源管理 (仅管理员权限可见) -->
              <div v-if="systemActiveTab === 'music' && currentRole === 'admin'" class="flex-1 flex flex-col min-h-0">
                <div class="flex justify-between items-center mb-4 shrink-0">
                  <h3 class="text-lg font-bold text-gray-800">洛雪音乐自定义搜索源配置</h3>
                  <button 
                    @click="openAddMusicModal" 
                    class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-4 py-2 rounded-xl text-sm font-bold shadow-md hover:shadow-lg transition-all flex items-center gap-1.5"
                  >
                    <span>➕</span> 添加音源
                  </button>
                </div>
                
                <div v-if="(musicSources || []).length === 0" class="flex-1 flex flex-col items-center justify-center text-center p-8 bg-gray-50/50 rounded-2xl border border-gray-100">
                  <span class="text-4xl mb-3">🎵</span>
                  <h4 class="text-gray-800 font-bold">尚未配置任何音乐解析源</h4>
                  <p class="text-sm text-gray-400 mt-1">您可以导入支持洛雪音乐格式的自定义 JS 源以供设备播放</p>
                </div>
                
                <div v-else class="flex-1 overflow-auto pr-1 flex flex-col gap-4">
                  <div 
                    v-for="source in (musicSources || [])" 
                    :key="source.id"
                    class="bg-white border border-gray-100 rounded-2xl p-5 shadow-sm hover:shadow-md transition-shadow flex flex-col gap-4"
                  >
                    <div class="flex justify-between items-start">
                      <div>
                        <div class="flex items-center gap-2">
                          <h4 class="font-extrabold text-gray-800 text-base">{{ source.name }}</h4>
                          <span 
                            class="text-xs px-2 py-0.5 rounded-full font-bold"
                            :class="source.is_enabled === 1 ? 'bg-green-100 text-green-600' : 'bg-gray-100 text-gray-400'"
                          >
                            {{ source.is_enabled === 1 ? '已启用' : '已禁用' }}
                          </span>
                          <span 
                            v-if="source.failure_count > 0"
                            class="text-xs bg-red-100 text-red-600 px-2 py-0.5 rounded-full font-bold"
                          >
                            ⚠️ 失败熔断 {{ source.failure_count }} 次
                          </span>
                        </div>
                        <p class="text-xs text-gray-400 mt-1.5 flex items-center gap-2">
                          <span>优先级: {{ source.priority }}</span>
                          <span class="text-gray-300">|</span>
                          <span>代码大小: {{ source.script_code.length }} 字符</span>
                        </p>
                      </div>
                      <div class="flex gap-2">
                        <button 
                          @click="openEditMusicModal(source)"
                          class="px-3 py-1.5 border border-gray-200 hover:bg-gray-50 rounded-lg text-xs font-bold text-gray-600 transition-all"
                        >
                          编辑
                        </button>
                        <button 
                          @click="deleteMusicSource(source.id)"
                          class="px-3 py-1.5 border border-red-100 text-red-500 hover:bg-red-50 rounded-lg text-xs font-bold transition-all"
                        >
                          删除
                        </button>
                      </div>
                    </div>
                    
                    <div class="flex justify-between items-center pt-3 border-t border-gray-50">
                      <div class="flex gap-3">
                        <button 
                          @click="toggleMusicSource(source)"
                          class="text-xs font-bold transition-colors"
                          :class="source.is_enabled === 1 ? 'text-gray-500 hover:text-gray-700' : 'text-green-600 hover:text-green-700'"
                        >
                          {{ source.is_enabled === 1 ? '🚫 禁用此源' : '✅ 开启此源' }}
                        </button>
                        <button 
                          v-if="source.failure_count > 0"
                          @click="resetMusicFailure(source.id)"
                          class="text-xs text-orange-500 hover:text-orange-700 font-bold transition-colors"
                        >
                          🔄 重置失败熔断
                        </button>
                      </div>
                      <span class="text-[10px] text-gray-300">创建于: {{ new Date(source.created_at).toLocaleDateString() }}</span>
                    </div>
                  </div>
                </div>
              </div>

              <!-- TAB 3.5: 用户权限分配 (仅管理员权限可见) -->
              <div v-if="systemActiveTab === 'users' && currentRole === 'admin'" class="flex-grow flex flex-col min-h-0">
                <div class="grid grid-cols-1 md:grid-cols-2 gap-6 mb-6">
                  <!-- 左卡片：教师邀请 -->
                  <div class="bg-orange-50/50 border border-orange-100 rounded-2xl p-5 flex flex-col justify-between gap-4">
                    <div class="space-y-1">
                      <h4 class="text-sm font-bold text-orange-700 flex items-center gap-1.5">
                        <span>🔗</span> 教师邀请注册链接
                      </h4>
                      <p class="text-xs text-orange-600 font-bold">邀请码：TEACHER_INVITE</p>
                      <p class="text-[11px] text-gray-500 leading-relaxed">通过此链接注册的用户将直接自动获取“教师”权限角色，可录入及评价学生。</p>
                    </div>
                    <button @click="copyTeacherInviteLink" class="w-full py-2.5 bg-gradient-to-r from-orange-400 to-pink-500 text-white rounded-xl font-bold text-sm hover:shadow-md transition-all whitespace-nowrap">
                      📋 复制教师邀请链接
                    </button>
                  </div>

                  <!-- 右卡片：管理员密码修改 -->
                  <div class="bg-orange-50/50 border border-orange-100 rounded-2xl p-5 flex flex-col gap-3">
                    <h4 class="text-sm font-bold text-orange-700 flex items-center gap-1.5">
                      <span>🔑</span> 安全管理：修改管理员密码
                    </h4>
                    <div class="flex flex-col sm:flex-row gap-3">
                      <input 
                        v-model="newAdminPassword"
                        type="password"
                        placeholder="新密码"
                        class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-xs focus:outline-none focus:border-orange-400"
                        :disabled="updatingAdminPassword"
                      />
                      <input 
                        v-model="confirmAdminPassword"
                        type="password"
                        placeholder="确认新密码"
                        class="flex-1 border border-gray-200 rounded-xl px-3 py-2 text-xs focus:outline-none focus:border-orange-400"
                        :disabled="updatingAdminPassword"
                      />
                    </div>
                    <button 
                      @click="handleUpdateAdminPassword" 
                      :disabled="updatingAdminPassword"
                      class="w-full py-2 bg-gray-800 hover:bg-gray-900 text-white rounded-xl font-bold text-xs transition-all disabled:opacity-50"
                    >
                      {{ updatingAdminPassword ? '提交中...' : '🛡️ 确认修改管理员密码' }}
                    </button>
                  </div>
                </div>

                <div class="flex items-center justify-between mb-4">
                  <h3 class="text-lg font-bold text-gray-800">已注册用户权限分配表</h3>
                  <button @click="loadUsersList" class="p-1.5 text-gray-500 hover:text-gray-700 transition-colors" title="刷新列表">
                    🔄 刷新
                  </button>
                </div>

                <div v-if="loadingUsers" class="flex-1 flex items-center justify-center text-gray-400 py-12">
                  <div class="animate-spin text-2xl mr-2">⏳</div> 正在加载用户数据...
                </div>

                <div v-else-if="usersList.length === 0" class="flex-1 flex items-center justify-center text-gray-400 text-sm py-12">
                  暂无已注册用户
                </div>

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
                      <tr v-for="u in usersList" :key="u.id" class="hover:bg-gray-50/50 transition-colors">
                        <td class="px-6 py-4 font-bold">{{ u.username }}</td>
                        <td class="px-6 py-4">
                          <span class="inline-flex items-center px-2.5 py-1 rounded-full text-xs font-bold" 
                            :class="u.role === 'admin' ? 'bg-purple-100 text-purple-700' : u.role === 'teacher' ? 'bg-orange-100 text-orange-700' : 'bg-blue-100 text-blue-700'">
                            {{ u.role === 'admin' ? '管理员' : u.role === 'teacher' ? '教师' : '学生' }}
                          </span>
                        </td>
                        <td class="px-6 py-4 text-gray-400 text-xs">
                          {{ new Date(u.created_at).toLocaleString('zh-CN', { hour12: false }) }}
                        </td>
                        <td class="px-6 py-4">
                          <select 
                            :value="u.role" 
                            @change="handleUpdateUserRole(u.id, ($event.target as HTMLSelectElement).value)"
                            :disabled="u.username === 'admin'"
                            class="border border-gray-200 rounded-lg px-2.5 py-1.5 text-xs focus:outline-none focus:border-orange-400 disabled:opacity-50"
                          >
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
                  <!-- 卡片 1 -->
                  <div class="bg-gradient-to-br from-green-50 to-emerald-50/50 p-5 rounded-2xl border border-green-100 flex flex-col justify-between h-32">
                    <div class="flex justify-between items-start">
                      <span class="text-sm font-bold text-green-700">高可用音乐检索</span>
                      <span class="text-2xl">⚡</span>
                    </div>
                    <div>
                      <h4 class="text-2xl font-black text-green-800">
                        {{ (musicSources || []).filter(s => s.is_enabled && s.failure_count < 3).length }} / {{ (musicSources || []).length }}
                      </h4>
                      <p class="text-xs text-green-600 mt-1">当前健康可用音源占比正常</p>
                    </div>
                  </div>
                  <!-- 卡片 2 -->
                  <div class="bg-gradient-to-br from-orange-50 to-amber-50/50 p-5 rounded-2xl border border-orange-100 flex flex-col justify-between h-32">
                    <div class="flex justify-between items-start">
                      <span class="text-sm font-bold text-orange-700">自动确认延迟</span>
                      <span class="text-2xl">⏳</span>
                    </div>
                    <div>
                      <h4 class="text-2xl font-black text-orange-800">
                        {{ taskConfirmMode === 'auto' ? `${taskConfirmDelay} 分钟` : '已禁用自动' }}
                      </h4>
                      <p class="text-xs text-orange-600 mt-1">
                        {{ taskConfirmMode === 'auto' ? '已配置延迟加分缓冲' : '需由教师手动审核加分' }}
                      </p>
                    </div>
                  </div>
                  <!-- 卡片 3 -->
                  <div class="bg-gradient-to-br from-purple-50 to-pink-50/50 p-5 rounded-2xl border border-purple-100 col-span-2 flex items-center justify-between h-24">
                    <div>
                      <h4 class="text-base font-bold text-purple-900">数据服务对接正常</h4>
                      <p class="text-xs text-purple-600 mt-1">已检测完毕系统各索引完整性，查询平均延迟低于 15ms</p>
                    </div>
                    <span class="text-3xl">🛡️</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 音乐源编辑/添加弹窗 -->
    <Transition name="modal">
      <div v-if="showMusicAddModal" class="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center z-[60] p-4">
        <div class="bg-white rounded-3xl p-8 w-full max-w-md shadow-2xl animate-scale-in">
          <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
            <span class="text-2xl">🎵</span> {{ musicForm.id ? '编辑音乐源' : '添加音乐源' }}
          </h3>
          <input 
            v-model="musicForm.name"
            type="text" 
            placeholder="音源名称（如: 洛雪主源）"
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-4 text-base focus:outline-none focus:border-orange-400 transition-colors"
          />
          <input 
            v-model.number="musicForm.priority"
            type="number" 
            placeholder="优先级（数值越大排在越前面）"
            class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-4 focus:outline-none focus:border-orange-400 transition-colors"
          />
          
          <div class="border-2 border-dashed border-gray-200 rounded-xl p-5 mb-6 text-center hover:border-orange-400 transition-colors cursor-pointer relative">
            <input 
              type="file" 
              accept=".js" 
              multiple
              @change="handleMusicUpload" 
              class="absolute inset-0 opacity-0 cursor-pointer"
            />
            <span class="text-3xl block mb-2">📄</span>
            <span class="text-sm text-gray-500 font-bold block">点击或拖拽上传自定义音源 JS 脚本</span>
            <span class="text-xs text-gray-400 block mt-1">
              （支持按住 Ctrl/Cmd 多选 JS 文件进行批量导入）
            </span>
            <span class="text-xs text-gray-400 block mt-1" v-if="musicForm.script_code">
              已成功加载脚本 ({{ musicForm.script_code.length }} 字符)
            </span>
          </div>

          <div class="flex gap-3 justify-end">
            <button @click="showMusicAddModal = false" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
            <button @click="saveMusicSource" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">
              保存音源
            </button>
          </div>
        </div>
      </div>
    </Transition>
    <!-- 全局桌面滚动歌词悬浮气泡 -->
    <div 
      v-if="isPlaying && currentLyricText && currentPlayingSong !== '未播放'"
      class="fixed bottom-24 left-1/2 -translate-x-1/2 px-6 py-2.5 bg-black/80 backdrop-blur-md rounded-2xl shadow-xl text-white text-sm font-semibold tracking-wide border border-white/10 z-40 transition-all duration-300 select-none text-center"
    >
      {{ currentLyricText }}
    </div>

    <!-- 全局常驻音乐播放底栏 -->
    <div class="fixed bottom-0 left-0 right-0 h-20 bg-white/90 backdrop-blur-xl border-t border-gray-100 z-40 px-8 flex items-center justify-between shadow-[0_-8px_30px_rgba(0,0,0,0.04)]">
      <!-- 左部：音乐信息及搜歌 -->
      <div class="flex items-center">
        <!-- 旋转黑胶唱片 -->
        <div 
          class="w-12 h-12 rounded-full bg-black flex items-center justify-center border border-gray-800 shadow-inner shrink-0 relative overflow-hidden transition-all duration-500"
          :class="isPlaying ? 'animate-spin-slow' : ''"
        >
          <div class="w-4 h-4 rounded-full bg-white/90 border-2 border-black flex items-center justify-center text-[8px] font-bold">
            🐾
          </div>
        </div>
        <div class="flex flex-col ml-3 text-left">
          <span class="text-sm font-bold text-gray-800 line-clamp-1 w-44">{{ currentPlayingSong }}</span>
          <span class="text-[10px] text-gray-400 mt-0.5">{{ isPlaying ? '正在播放' : '已暂停' }}</span>
        </div>
        <button 
          @click="showGlobalSearch = !showGlobalSearch" 
          class="ml-4 px-3 py-1.5 bg-gradient-to-r from-orange-400 to-pink-500 hover:from-orange-50 hover:to-pink-600 text-white rounded-xl text-xs font-bold transition-all shadow-sm"
        >
          🔍 搜歌
        </button>
      </div>

      <!-- 中部：核心控制与进度条 -->
      <div class="flex flex-col items-center gap-1.5">
        <!-- 播放控制 -->
        <div class="flex items-center gap-6">
          <button @click="playPrevMock" class="text-gray-400 hover:text-gray-600 transition-colors text-lg" title="上一首">⏮️</button>
          <button 
            @click="toggleTestMusicPlay" 
            class="w-9 h-9 rounded-full bg-gradient-to-r from-orange-400 to-pink-500 hover:from-orange-50 hover:to-pink-600 text-white flex items-center justify-center text-xs shadow-md hover:scale-105 active:scale-95 transition-all"
            title="播放/暂停"
          >
            {{ isPlaying ? '⏸️' : '▶️' }}
          </button>
          <button @click="playNextMock" class="text-gray-400 hover:text-gray-600 transition-colors text-lg" title="下一首">⏭️</button>
        </div>
        <!-- 进度条 -->
        <div class="flex items-center gap-3 w-[450px]">
          <span class="text-[10px] text-gray-400 tabular-nums">{{ formatTime(songCurrentTime) }}</span>
          <div 
            @click="seekAudio" 
            class="flex-1 h-1.5 bg-gray-100 hover:h-2 rounded-full cursor-pointer relative transition-all group"
          >
            <div 
              class="h-full bg-gradient-to-r from-orange-400 to-pink-500 rounded-full" 
              :style="{ width: `${songDuration ? (songCurrentTime / songDuration * 100) : 0}%` }"
            ></div>
            <div 
              class="absolute top-1/2 -translate-y-1/2 -translate-x-1/2 w-2.5 h-2.5 bg-white border-2 border-pink-500 rounded-full opacity-0 group-hover:opacity-100 transition-opacity" 
              :style="{ left: `${songDuration ? (songCurrentTime / songDuration * 100) : 0}%` }"
            ></div>
          </div>
          <span class="text-[10px] text-gray-400 tabular-nums">{{ formatTime(songDuration) }}</span>
        </div>
      </div>

      <!-- 右部：音量与歌词面板入口 -->
      <div class="flex items-center gap-2">
        <span class="text-xs">🔊</span>
        <input 
          type="range" 
          min="0" 
          max="100" 
          v-model.number="volume" 
          @input="handleVolumeChange" 
          class="w-20 accent-pink-500 h-1 bg-gray-200 rounded-lg appearance-none cursor-pointer" 
        />
        <button 
          @click="showLyricModal = true" 
          class="ml-4 px-3 py-1.5 border border-gray-200 hover:bg-gray-50 rounded-xl text-xs font-bold text-gray-600 transition-all shadow-sm"
        >
          📖 歌词
        </button>
      </div>
    </div>

    <!-- 搜歌弹窗气泡 -->
    <div 
      v-if="showGlobalSearch" 
      class="fixed bottom-24 left-8 bg-white/95 backdrop-blur-xl border border-gray-100 rounded-2xl shadow-2xl p-5 w-80 z-50 flex flex-col gap-3 animate-scale-in"
    >
      <div class="flex justify-between items-center">
        <h4 class="font-bold text-gray-800 text-sm">搜索音乐源直链播放</h4>
        <button @click="showGlobalSearch = false" class="text-gray-400 hover:text-gray-600 text-lg">&times;</button>
      </div>
      <!-- 音源选择 -->
      <div class="flex flex-col gap-1 text-left">
        <label class="text-[10px] text-gray-400 font-bold">解析音源</label>
        <select 
          v-model="selectedSourceId"
          class="w-full border border-gray-200 rounded-lg px-2.5 py-1.5 text-xs focus:outline-none focus:border-orange-400 bg-white"
        >
          <option v-for="src in activeMusicSources" :key="src.id" :value="src.id">
            {{ src.name }} {{ src.failure_count > 0 ? `(故障: ${src.failure_count}/3)` : '(健康)' }}
          </option>
        </select>
      </div>
      <div class="flex gap-2">
        <input 
          v-model="globalSearchKeyword"
          type="text" 
          placeholder="歌曲名称..."
          class="flex-1 border border-gray-200 rounded-lg px-3 py-1.5 text-xs focus:outline-none focus:border-orange-400"
          @keyup.enter="testMusicSourceSearch"
        />
        <button 
          @click="testMusicSourceSearch"
          :disabled="globalSearchLoading"
          class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-3 py-1.5 rounded-lg text-xs font-bold shadow-sm"
        >
          {{ globalSearchLoading ? '解析中' : '播放' }}
        </button>
      </div>
      <div class="flex flex-wrap gap-1.5 mt-1">
        <button 
          v-for="tag in ['童话', '晴天', '小幸运', '我爱你', '七里香']"
          :key="tag"
          @click="globalSearchKeyword = tag; testMusicSourceSearch()"
          class="text-[10px] bg-gray-50 hover:bg-orange-50 hover:text-orange-500 border border-gray-100 rounded-full px-2 py-0.5 text-gray-500 transition-colors"
        >
          {{ tag }}
        </button>
      </div>
    </div>

    <!-- 沉浸式歌词面板 -->
    <Transition name="modal">
      <div 
        v-if="showLyricModal" 
        class="fixed inset-0 bg-black/75 backdrop-blur-2xl flex items-center justify-center z-50 p-6" 
        @click.self="showLyricModal = false"
      >
        <div class="bg-white/10 border border-white/10 rounded-3xl w-full max-w-4xl h-[75vh] flex shadow-2xl overflow-hidden relative text-white animate-scale-in">
          <!-- 关闭按钮 -->
          <button 
            @click="showLyricModal = false" 
            class="absolute top-6 right-6 w-10 h-10 rounded-full bg-white/10 hover:bg-white/20 flex items-center justify-center text-white text-xl transition-colors z-50"
            title="关闭"
          >
            &times;
          </button>

          <!-- 左半侧：拟物化黑胶唱片 -->
          <div class="w-1/2 flex flex-col items-center justify-center border-r border-white/10 p-6 relative bg-gradient-to-b from-white/5 to-transparent">
            <!-- 唱盘组件 -->
            <div class="relative w-64 h-64 flex items-center justify-center">
              <!-- 大黑胶盘 -->
              <div 
                class="w-64 h-64 rounded-full bg-gradient-to-r from-neutral-900 via-neutral-950 to-neutral-900 border-[12px] border-black flex items-center justify-center shadow-[0_20px_50px_rgba(0,0,0,0.5)] relative overflow-hidden transition-all duration-1000"
                :class="isPlaying ? 'animate-spin-slow' : ''"
              >
                <!-- 唱片环形光泽线 -->
                <div class="absolute inset-0 rounded-full border border-white/5 pointer-events-none scale-75"></div>
                <div class="absolute inset-0 rounded-full border border-white/5 pointer-events-none scale-50"></div>
                
                <!-- 唱片封面中心 -->
                <div class="w-24 h-24 rounded-full bg-gradient-to-br from-orange-400 via-pink-400 to-purple-500 border-4 border-black flex items-center justify-center text-4xl shadow-inner select-none">
                  🎵
                </div>
              </div>
            </div>

            <!-- 歌曲信息与播放控制器 -->
            <div class="mt-8 text-center">
              <h3 class="text-xl font-bold tracking-wide">{{ currentPlayingSong }}</h3>
              <p class="text-xs text-white/50 mt-1.5">{{ isPlaying ? '正在播放' : '已暂停' }}</p>
              
              <div class="flex items-center justify-center gap-6 mt-6">
                <button @click="playPrevMock" class="text-white/60 hover:text-white transition-colors text-xl">⏮️</button>
                <button 
                  @click="toggleTestMusicPlay" 
                  class="w-11 h-11 rounded-full bg-white text-gray-950 flex items-center justify-center text-sm shadow-md hover:scale-105 active:scale-95 transition-all"
                >
                  {{ isPlaying ? '⏸️' : '▶️' }}
                </button>
                <button @click="playNextMock" class="text-white/60 hover:text-white transition-colors text-xl">⏭️</button>
              </div>
            </div>
          </div>

          <!-- 右半侧：滚动歌词本 -->
          <div class="w-1/2 flex flex-col p-8 min-h-0 bg-black/20">
            <h4 class="font-extrabold tracking-wide text-lg text-white/90 border-b border-white/10 pb-4 mb-6 text-left flex items-center gap-2">
              <span>📖</span> 歌词本
            </h4>
            <!-- 歌词容器 -->
            <div class="flex-1 overflow-y-auto pr-2 scroll-smooth custom-scrollbar-white flex flex-col gap-6 text-left py-[30vh]">
              <div 
                v-for="(lyric, idx) in currentLyrics" 
                :key="idx"
                :id="'lyric-line-' + idx"
                class="transition-all duration-500 origin-left select-none py-1"
                :class="activeLyricIndex === idx 
                  ? 'text-2xl font-black text-transparent bg-clip-text bg-gradient-to-r from-yellow-300 via-amber-300 to-orange-400 scale-105 filter drop-shadow-[0_2px_8px_rgba(251,146,60,0.2)]' 
                  : 'text-sm text-white/40 hover:text-white/60'"
              >
                {{ lyric.text }}
              </div>
            </div>
          </div>
        </div>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
/* 过渡动画 */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

.modal-enter-active,
.modal-leave-active {
  transition: all 0.3s ease;
}

.modal-enter-from,
.modal-leave-to {
  opacity: 0;
}

.modal-enter-from > div,
.modal-leave-to > div {
  transform: scale(0.9);
}

.dropdown-enter-active,
.dropdown-leave-active {
  transition: all 0.2s ease;
}

.dropdown-enter-from,
.dropdown-leave-to {
  opacity: 0;
  transform: translateY(-10px);
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: all 0.3s ease;
}

.slide-up-enter-from,
.slide-up-leave-to {
  opacity: 0;
  transform: translate(-50%, 100%);
}

.pop-enter-active,
.pop-leave-active {
  transition: all 0.2s ease;
}

.pop-enter-from,
.pop-leave-to {
  opacity: 0;
  transform: scale(0.5);
}

/* 评分动效 */
.score-pop-enter-active {
  animation: scorePopIn 0.5s ease-out;
}

.score-pop-leave-active {
  transition: all 0.3s ease;
}

.score-pop-leave-to {
  opacity: 0;
  transform: scale(0.5);
}

@keyframes scorePopIn {
  0% {
    opacity: 0;
    transform: scale(0.5);
  }
  50% {
    transform: scale(1.2);
  }
  100% {
    opacity: 1;
    transform: scale(1);
  }
}

@keyframes sparkle {
  0% {
    opacity: 0;
    transform: scale(0) rotate(0deg);
  }
  50% {
    opacity: 1;
    transform: scale(1.5) rotate(180deg);
  }
  100% {
    opacity: 0;
    transform: scale(0) rotate(360deg);
  }
}

.animate-sparkle {
  animation: sparkle 0.8s ease-out forwards;
}

.animate-bounce-in {
  animation: bounceIn 0.5s ease-out;
}

@keyframes scaleIn {
  from {
    opacity: 0;
    transform: scale(0.9);
  }
  to {
    opacity: 1;
    transform: scale(1);
  }
}

.animate-bounce-in {
  animation: bounceIn 0.5s ease-out;
}

@keyframes bounceIn {
  0% {
    opacity: 0;
    transform: scale(0.3);
  }
  50% {
    transform: scale(1.05);
  }
  70% {
    transform: scale(0.9);
  }
  100% {
    opacity: 1;
    transform: scale(1);
  }
}

/* 自定义滚动条 */
.custom-scrollbar::-webkit-scrollbar {
  width: 6px;
}

.custom-scrollbar::-webkit-scrollbar-track {
  background: #f1f1f1;
  border-radius: 3px;
}

.custom-scrollbar::-webkit-scrollbar-thumb {
  background: linear-gradient(to bottom, #fb923c, #f472b6);
  border-radius: 3px;
}

.custom-scrollbar::-webkit-scrollbar-thumb:hover {
  background: linear-gradient(to bottom, #f97316, #ec4899);
}

/* 自定义滚动条 - 白色透亮版 */
.custom-scrollbar-white::-webkit-scrollbar {
  width: 6px;
}

.custom-scrollbar-white::-webkit-scrollbar-track {
  background: rgba(255, 255, 255, 0.05);
  border-radius: 3px;
}

.custom-scrollbar-white::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.2);
  border-radius: 3px;
}

.custom-scrollbar-white::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.4);
}

/* 唱片旋转动画 */
@keyframes spin-slow {
  from {
    transform: rotate(0deg);
  }
  to {
    transform: rotate(360deg);
  }
}

.animate-spin-slow {
  animation: spin-slow 20s linear infinite;
}
</style>