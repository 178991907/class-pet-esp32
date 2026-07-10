import { defineStore } from 'pinia'
import { ref } from 'vue'
import type { Class } from '@/types'
import { useAuth } from '@/composables/useAuth'
import { useToast } from '@/composables/useToast'

export const useClassStore = defineStore('class', () => {
  const { api } = useAuth()
  const toast = useToast()

  const classes = ref<Class[]>([])
  const currentClass = ref<Class | null>(null)

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
      }
    } catch (error) {
      console.error('加载班级失败:', error)
    }
  }

  async function selectClass(cls: Class) {
    currentClass.value = cls
    localStorage.setItem('pet-garden-current-class', cls.id)
  }

  async function createClass(name: string) {
    if (!name.trim()) {
      toast.warning('请输入班级名称')
      return false
    }
    try {
      await api.post('/classes', { name: name.trim() })
      await loadClasses()
      toast.success('班级创建成功！')
      return true
    } catch (error) {
      console.error('创建班级失败:', error)
      toast.error('创建班级失败，请重试')
      return false
    }
  }

  async function updateClass(id: string, name: string) {
    if (!name.trim()) {
      toast.warning('请输入班级名称')
      return false
    }
    try {
      await api.put(`/classes/${id}`, { name: name.trim() })
      if (currentClass.value?.id === id) {
        currentClass.value = { ...currentClass.value, name: name.trim() } as Class
      }
      await loadClasses()
      toast.success('班级更新成功！')
      return true
    } catch (error) {
      console.error('更新班级失败:', error)
      toast.error('更新班级失败，请重试')
      return false
    }
  }

  async function deleteClass(id: string) {
    try {
      await api.delete(`/classes/${id}`)
      if (currentClass.value?.id === id) {
        currentClass.value = null
      }
      await loadClasses()
      toast.success('班级删除成功！')
    } catch (error) {
      console.error('删除班级失败:', error)
      toast.error('删除班级失败，请重试')
    }
  }

  return {
    classes,
    currentClass,
    loadClasses,
    selectClass,
    createClass,
    updateClass,
    deleteClass
  }
})
