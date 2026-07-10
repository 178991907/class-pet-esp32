<script setup lang="ts">
import { useStudentStore } from '@/stores/useStudentStore'
import { useConfirm } from '@/composables/useConfirm'
import { useAuth } from '@/composables/useAuth'

defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()

const studentStore = useStudentStore()
const { showConfirm } = useConfirm()
const { isGuest } = useAuth()

function handleUndo(recordId?: string) {
  showConfirm({
    title: '撤回评价',
    message: '确定要撤回这条评价吗？',
    confirmText: '撤回',
    cancelText: '取消',
    type: 'warning',
    onConfirm: () => studentStore.undoLastEvaluation(recordId)
  })
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
      <div class="bg-white rounded-3xl p-8 w-full max-w-4xl max-h-[85vh] overflow-auto shadow-2xl animate-scale-in">
        <div class="flex items-center justify-between mb-6">
          <h3 class="text-xl font-bold flex items-center gap-2">
            <span class="text-2xl">📋</span> 评价记录
          </h3>
          <button
            v-if="!isGuest"
            @click="handleUndo()"
            class="px-4 py-2 text-sm text-orange-500 hover:bg-orange-50 rounded-xl font-medium transition-colors flex items-center gap-1"
          >
            ↩️ 撤回最新
          </button>
        </div>

        <div v-if="studentStore.evaluationRecords.length === 0" class="text-center py-16 text-gray-500">
          <div class="text-5xl mb-4">📝</div>
          暂无记录
        </div>

        <div v-else class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          <div
            v-for="record in studentStore.evaluationRecords"
            :key="record.id"
            class="p-4 bg-white border border-gray-100 rounded-2xl shadow-sm hover:shadow-md transition-shadow"
          >
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
            <div class="text-sm text-gray-600 mb-3 line-clamp-2">{{ record.reason }}</div>
            <div class="flex items-center justify-between text-xs text-gray-400">
              <span class="px-2 py-1 bg-gray-100 rounded-lg">{{ record.category }}</span>
              <div class="flex items-center gap-2">
                <span>{{ new Date(record.timestamp).toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' }) }}</span>
                <button
                  v-if="!isGuest"
                  @click="handleUndo(record.id)"
                  class="text-orange-500 hover:text-orange-600 hover:bg-orange-50 px-2 py-1 rounded transition-colors"
                  title="撤回"
                >↩️</button>
              </div>
            </div>
          </div>
        </div>

        <!-- 分页 -->
        <div v-if="studentStore.totalPages > 1" class="flex items-center justify-between mt-6 pt-6 border-t border-gray-100">
          <div class="text-sm text-gray-500">
            共 <span class="font-medium">{{ studentStore.totalRecords }}</span> 条记录
          </div>
          <div class="flex gap-2">
            <button
              @click="studentStore.prevPage()"
              :disabled="studentStore.recordsPage <= 1"
              class="px-4 py-2 rounded-xl border border-gray-200 text-sm disabled:opacity-50 disabled:cursor-not-allowed hover:bg-gray-50 transition-colors"
            >← 上一页</button>
            <div class="flex gap-1">
              <button
                v-for="page in Math.min(studentStore.totalPages, 5)"
                :key="page"
                @click="studentStore.goToPage(page)"
                class="px-4 py-2 rounded-xl text-sm min-w-[40px] font-medium transition-all"
                :class="studentStore.recordsPage === page ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg' : 'border border-gray-200 hover:bg-gray-50'"
              >{{ page }}</button>
            </div>
            <button
              @click="studentStore.nextPage()"
              :disabled="studentStore.recordsPage >= studentStore.totalPages"
              class="px-4 py-2 rounded-xl border border-gray-200 text-sm disabled:opacity-50 disabled:cursor-not-allowed hover:bg-gray-50 transition-colors"
            >下一页 →</button>
          </div>
        </div>

        <div class="flex justify-end mt-6">
          <button @click="emit('close')" class="px-6 py-3 bg-gray-100 text-gray-700 rounded-xl font-medium hover:bg-gray-200 transition-colors">关闭</button>
        </div>
      </div>
    </div>
  </Transition>
</template>
