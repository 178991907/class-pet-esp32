import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import type { Student, Rule } from '@/types'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'
import { useClassStore } from '@/stores/useClassStore'
import { getPetType } from '@/data/pets'

export const useStudentStore = defineStore('student', () => {
  const { api } = useAuth()
  const toast = useToast()
  const classStore = useClassStore()

  // 核心数据
  const students = ref<Student[]>([])
  const rules = ref<Rule[]>([])
  const searchQuery = ref('')
  const sortBy = ref<'name' | 'studentNo' | 'progress'>('name')
  const sortOrder = ref<'asc' | 'desc'>('asc')

  // 评价相关
  const selectedEvalTab = ref('学习')
  const evaluationRecords = ref<any[]>([])
  const recordsPage = ref(1)
  const recordsPageSize = 20
  const totalRecords = ref(0)

  // 批量操作
  const batchMode = ref(false)
  const selectedStudents = ref<Set<string>>(new Set())
  const showDeleteStudentMode = ref(false)
  const deleteStudentList = ref<string[]>([])

  // 宠物选择
  const selectedStudent = ref<Student | null>(null)

  // 详情面板
  const detailStudent = ref<Student | null>(null)
  const detailEvalTab = ref('学习')
  const studentRecords = ref<any[]>([])

  // 评分动效
  const scoreAnimations = ref<Map<string, { points: number, show: boolean }>>(new Map())

  // 未绑定设备
  const unboundDevicesList = ref<any[]>([])

  // 计算属性
  const categories = ['学习', '行为', '健康', '其他']

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
          comparison = (a.student_no || '').localeCompare(b.student_no || '')
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

  const currentCategoryRules = computed(() => {
    return (rules.value || []).filter(r => r.category === selectedEvalTab.value)
  })

  const ranking = computed(() => {
    return [...students.value].sort((a, b) => b.total_points - a.total_points)
  })

  const totalPages = computed(() => {
    return Math.ceil(totalRecords.value / recordsPageSize)
  })

  // API 操作
  async function loadStudents() {
    if (!classStore.currentClass) return
    try {
      const res = await api.get(`/classes/${classStore.currentClass.id}/students`)
      students.value = res.data.students
    } catch (error) {
      console.error('加载学生失败:', error)
    }
  }

  async function loadRules() {
    try {
      const res = await api.get('/rules')
      rules.value = res?.data?.rules || []
    } catch (error) {
      rules.value = []
      console.error('加载评价规则失败:', error)
    }
  }

  async function loadUnboundDevices() {
    try {
      const res = await api.get('/devices/unbound')
      unboundDevicesList.value = res.data.devices || []
    } catch (err) {
      console.error('加载未绑定设备失败:', err)
    }
  }

  async function addStudent(name: string, studentNo: string, deviceId: string) {
    if (!name.trim() || !classStore.currentClass) return false
    try {
      await api.post('/students', {
        classId: classStore.currentClass.id,
        name: name.trim(),
        studentNo: studentNo.trim() || null,
        deviceId: deviceId.trim() || null
      })
      await loadStudents()
      toast.success('学生添加成功！')
      return true
    } catch (error) {
      console.error('添加学生失败:', error)
      toast.error('添加学生失败，请重试')
      return false
    }
  }

  async function updateStudent(studentId: string, name: string, studentNo: string, deviceId: string) {
    if (!name.trim()) return false
    try {
      await api.put(`/students/${studentId}`, {
        name: name.trim(),
        studentNo: studentNo.trim() || null,
        deviceId: deviceId.trim() || null
      })
      await loadStudents()
      toast.success('学生信息更新成功！')
      return true
    } catch (error) {
      console.error('更新学生失败:', error)
      toast.error('更新学生失败，请重试')
      return false
    }
  }

  async function importStudents(importText: string) {
    if (!importText.trim() || !classStore.currentClass) return false

    const lines = importText.trim().split('\n')
    const studentsToImport: { name: string, studentNo: string }[] = []

    for (const line of lines) {
      const trimmed = line.trim()
      if (!trimmed) continue
      const parts = trimmed.split(/[\t,\s;]+/)
      if (parts.length >= 2) {
        studentsToImport.push({ name: parts[0], studentNo: parts.slice(1).join('') })
      } else if (parts.length === 1) {
        studentsToImport.push({ name: parts[0], studentNo: '' })
      }
    }

    if (studentsToImport.length === 0) {
      toast.warning('没有识别到学生信息')
      return false
    }

    try {
      const res = await api.post('/students/import', {
        classId: classStore.currentClass.id,
        students: studentsToImport
      })
      toast.success(`成功导入 ${res.data.imported} 名学生`)
      await loadStudents()
      return true
    } catch (error) {
      console.error('导入失败:', error)
      toast.error('导入失败，请重试')
      return false
    }
  }

  async function selectPet(petId: string) {
    const student = selectedStudent.value
    if (!student) return
    try {
      await api.put(`/students/${student.id}/pet`, { petType: petId })
      const pet = getPetType(petId)
      toast.success(`🎉 ${student.name} 领养了一只 ${pet?.name || '宠物'}！`)
      selectedStudent.value = null
      await loadStudents()
      if (detailStudent.value) {
        detailStudent.value = students.value.find(s => s.id === detailStudent.value?.id) || null
      }
    } catch (error) {
      console.error('领养宠物失败:', error)
      toast.error('领养失败，请重试')
    }
  }

  // 详情面板
  async function openDetailPanel(student: Student) {
    detailStudent.value = student
    detailEvalTab.value = '学习'
    await loadStudentRecords(student.id)
  }

  async function loadStudentRecords(studentId: string) {
    try {
      const res = await api.get(`/evaluations?studentId=${studentId}&pageSize=20`)
      studentRecords.value = res.data.records || []
    } catch (error) {
      console.error('加载记录失败:', error)
      studentRecords.value = []
    }
  }

  function closeDetailPanel() {
    detailStudent.value = null
    studentRecords.value = []
  }

  // 批量操作
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

  function batchAddPoints() {
    if (selectedStudents.value.size === 0) return
    selectedStudent.value = null
    selectedEvalTab.value = '学习'
  }

  function batchSubPoints() {
    if (selectedStudents.value.size === 0) return
    selectedStudent.value = null
    selectedEvalTab.value = '行为'
  }

  // 评分动效
  function triggerScoreAnimation(studentId: string, points: number) {
    scoreAnimations.value.set(studentId, { points, show: true })
    setTimeout(() => {
      scoreAnimations.value.delete(studentId)
    }, 1500)
  }

  // 评价操作 - 返回结果供组件处理动画
  async function quickAdd(student: Student | null, rule: Rule): Promise<{ levelUp: boolean, petLevel: number, graduated: boolean } | null> {
    if (!student) {
      // 批量评价
      const studentIds = Array.from(selectedStudents.value)
      let successCount = 0
      for (const studentId of studentIds) {
        try {
          await api.post('/evaluations', {
            classId: classStore.currentClass?.id,
            studentId,
            points: rule.points,
            reason: rule.name,
            category: rule.category
          })
          successCount++
          triggerScoreAnimation(studentId, rule.points)
        } catch (error) {
          console.error('评价失败:', error)
        }
      }
      toast.success(`已为 ${successCount} 名学生${rule.points > 0 ? '加' : '扣'}${Math.abs(rule.points)}分`)
      cancelBatchMode()
      await loadStudents()
      return null
    }

    try {
      const res = await api.post('/evaluations', {
        classId: classStore.currentClass?.id,
        studentId: student.id,
        points: rule.points,
        reason: rule.name,
        category: rule.category
      })
      triggerScoreAnimation(student.id, rule.points)
      await loadStudents()
      return {
        levelUp: res.data.levelUp,
        petLevel: res.data.petLevel,
        graduated: res.data.graduated
      }
    } catch (error) {
      console.error('评价失败:', error)
      toast.error('评价失败，请重试')
      return null
    }
  }

  async function detailQuickAdd(rule: Rule): Promise<{ levelUp: boolean, petLevel: number, graduated: boolean } | null> {
    if (!detailStudent.value) return null
    const student = detailStudent.value
    try {
      const res = await api.post('/evaluations', {
        classId: classStore.currentClass?.id,
        studentId: student.id,
        points: rule.points,
        reason: rule.name,
        category: rule.category
      })
      triggerScoreAnimation(student.id, rule.points)
      await loadStudents()
      closeDetailPanel()
      return {
        levelUp: res.data.levelUp,
        petLevel: res.data.petLevel,
        graduated: res.data.graduated
      }
    } catch (error) {
      console.error('评价失败:', error)
      toast.error('评价失败，请重试')
      return null
    }
  }

  // 评价记录
  async function loadEvaluationRecords() {
    if (!classStore.currentClass) return
    const res = await api.get(`/evaluations?classId=${classStore.currentClass.id}&page=${recordsPage.value}&pageSize=${recordsPageSize}`)
    evaluationRecords.value = res.data.records
    totalRecords.value = res.data.total
  }

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
    if (!classStore.currentClass) return
    try {
      let res
      if (recordId) {
        res = await api.delete(`/evaluations/${recordId}`)
      } else {
        res = await api.delete(`/evaluations/latest?classId=${classStore.currentClass.id}`)
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

  // 规则管理
  async function addRule(name: string, points: number, category: string) {
    if (!name.trim()) {
      toast.warning('请输入规则名称')
      return false
    }
    try {
      await api.post('/rules', { name: name.trim(), points, category })
      toast.success('添加成功！')
      await loadRules()
      return true
    } catch (error) {
      console.error('添加规则失败:', error)
      toast.error('添加失败，请重试')
      return false
    }
  }

  async function deleteRule(id: string) {
    try {
      await api.delete(`/rules/${id}`)
      await loadRules()
      toast.success('删除成功！')
    } catch (error) {
      console.error('删除失败:', error)
      toast.error('删除失败')
    }
  }

  return {
    // 数据
    students,
    rules,
    searchQuery,
    sortBy,
    sortOrder,
    categories,
    selectedEvalTab,
    evaluationRecords,
    recordsPage,
    totalRecords,
    batchMode,
    selectedStudents,
    showDeleteStudentMode,
    deleteStudentList,
    selectedStudent,
    detailStudent,
    detailEvalTab,
    studentRecords,
    scoreAnimations,
    unboundDevicesList,
    // 计算属性
    filteredStudents,
    currentCategoryRules,
    ranking,
    totalPages,
    // 操作
    loadStudents,
    loadRules,
    loadUnboundDevices,
    addStudent,
    updateStudent,
    importStudents,
    selectPet,
    openDetailPanel,
    loadStudentRecords,
    closeDetailPanel,
    startBatchMode,
    cancelBatchMode,
    toggleStudentSelect,
    toggleDeleteStudent,
    cancelDeleteMode,
    batchDeleteStudents,
    batchAddPoints,
    batchSubPoints,
    triggerScoreAnimation,
    quickAdd,
    detailQuickAdd,
    loadEvaluationRecords,
    prevPage,
    nextPage,
    goToPage,
    undoLastEvaluation,
    addRule,
    deleteRule
  }
})
