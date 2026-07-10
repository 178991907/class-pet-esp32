<script setup lang="ts">
import { useStudentStore } from '@/stores/useStudentStore'
import { useLevelUp } from '@/composables/useLevelUp'
import { useToast } from '@/composables/useToast'
import type { Rule, Student } from '@/types'

defineProps<{ show: boolean, selectedStudent: Student | null }>()
const emit = defineEmits<{ close: [] }>()

const studentStore = useStudentStore()
const { triggerLevelUp } = useLevelUp()
const toast = useToast()

async function handleQuickAdd(rule: Rule) {
  const result = await studentStore.quickAdd(studentStore.selectedStudent, rule)
  if (result?.levelUp) {
    const student = studentStore.selectedStudent
    if (student) {
      triggerLevelUp(student.name, result.petLevel, student.pet_type || '', result.petLevel - 1)
    }
  }
  if (result?.graduated) {
    const student = studentStore.selectedStudent
    toast.success(`🎓 恭喜！${student?.name} 的宠物毕业了，获得了专属徽章！`)
  }
  emit('close')
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
      <div class="bg-white rounded-3xl p-8 w-full max-w-3xl max-h-[85vh] overflow-auto shadow-2xl animate-scale-in">
        <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
          <span class="text-2xl">⭐</span>
          <template v-if="selectedStudent">
            为 <span class="text-gradient">{{ selectedStudent?.name }}</span> 评价
          </template>
          <template v-else>
            批量评价 <span class="text-purple-500">{{ studentStore.selectedStudents.size }}</span> 名学生
          </template>
        </h3>

        <!-- 分类标签 -->
        <div class="flex gap-2 mb-6 flex-wrap">
          <button
            v-for="cat in studentStore.categories"
            :key="cat"
            @click="studentStore.selectedEvalTab = cat"
            class="px-5 py-2.5 rounded-xl text-sm font-bold transition-all"
            :class="studentStore.selectedEvalTab === cat
              ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg'
              : 'bg-gray-100 text-gray-600 hover:bg-gray-200'"
          >
            {{ cat }}
          </button>
        </div>

        <!-- 规则网格 -->
        <div class="h-[590px] overflow-y-auto pr-2 custom-scrollbar">
          <div class="grid grid-cols-2 md:grid-cols-4 gap-3 content-start">
            <button
              v-for="rule in studentStore.currentCategoryRules"
              :key="rule.id"
              @click="handleQuickAdd(rule)"
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
          <button @click="emit('close')" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
        </div>
      </div>
    </div>
  </Transition>
</template>
