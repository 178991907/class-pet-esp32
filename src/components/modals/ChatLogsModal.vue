<script setup lang="ts">
import { useSystemStore } from '@/stores/useSystemStore'

defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()

const systemStore = useSystemStore()
</script>

<template>
  <Transition name="modal">
    <div v-if="show && systemStore.auditingStudent" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4" @click.self="emit('close')">
      <div class="bg-white rounded-3xl w-full max-w-2xl max-h-[85vh] overflow-hidden flex flex-col shadow-2xl animate-scale-in">
        <div class="p-6 bg-gradient-to-r from-orange-400 to-pink-500 text-white flex justify-between items-center">
          <div>
            <h3 class="text-xl font-bold">💬 对话历史审计</h3>
            <p class="text-white/80 text-xs mt-1">学生: {{ systemStore.auditingStudent.name }} | 展示最近对话记录，自动滚动保留最近 30 天</p>
          </div>
          <button @click="emit('close')" class="w-8 h-8 rounded-full bg-white/20 hover:bg-white/30 flex items-center justify-center text-xl transition-colors">×</button>
        </div>

        <div class="flex-1 overflow-auto p-6 bg-gray-50 flex flex-col gap-4">
          <div v-if="systemStore.loadingChatLogs" class="flex-1 flex items-center justify-center text-gray-400">
            <span class="animate-spin text-2xl mr-2">🔄</span> 正在读取对话日志...
          </div>
          <div v-else-if="systemStore.chatLogsList.length === 0" class="flex-1 flex flex-col items-center justify-center text-center py-12 text-gray-400">
            <span class="text-4xl mb-2">💤</span>
            <p class="font-bold">暂无对话记录</p>
            <p class="text-xs text-gray-300 mt-1">当学生使用硬件进行大模型交互时，聊天记录将自动汇总展示在此</p>
          </div>
          <div v-else class="flex flex-col gap-4">
            <div v-for="log in systemStore.chatLogsList" :key="log.id" class="flex flex-col gap-2">
              <div class="text-center text-[10px] text-gray-400 my-1 font-bold">
                {{ new Date(log.timestamp).toLocaleString() }}
              </div>
              <div class="flex justify-end items-start gap-2.5">
                <div class="bg-gradient-to-br from-orange-400 to-pink-400 text-white px-4 py-2.5 rounded-2xl rounded-tr-none text-sm max-w-[80%] shadow-sm font-medium">
                  {{ log.user_message }}
                </div>
                <div class="w-8 h-8 rounded-full bg-orange-100 flex items-center justify-center text-sm font-bold text-orange-600 flex-shrink-0 shadow-sm">学</div>
              </div>
              <div class="flex justify-start items-start gap-2.5">
                <div class="w-8 h-8 rounded-full bg-purple-100 flex items-center justify-center text-sm font-bold text-purple-600 flex-shrink-0 shadow-sm">宠</div>
                <div class="bg-white border border-gray-100 text-gray-800 px-4 py-2.5 rounded-2xl rounded-tl-none text-sm max-w-[80%] shadow-sm leading-relaxed font-medium">
                  {{ log.ai_response }}
                </div>
              </div>
            </div>
          </div>
        </div>
        <div class="p-4 border-t border-gray-100 bg-white flex justify-end">
          <button @click="emit('close')" class="px-6 py-2.5 bg-gray-100 hover:bg-gray-200 text-gray-700 rounded-xl font-bold transition-all text-sm">关闭</button>
        </div>
      </div>
    </div>
  </Transition>
</template>
