<script setup lang="ts">
import { ref } from 'vue'
import { useSystemStore } from '@/stores/useSystemStore'

defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()

const systemStore = useSystemStore()
const newScheduleDay = ref(1)
const newScheduleTime = ref('08:00')
const newScheduleTask = ref('')

async function handleAddSchedule() {
  const success = await systemStore.addSchedule(newScheduleDay.value, newScheduleTime.value, newScheduleTask.value)
  if (success) {
    newScheduleTask.value = ''
  }
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show && systemStore.schedulingStudent" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4" @click.self="emit('close')">
      <div class="bg-white rounded-3xl w-full max-w-2xl max-h-[85vh] overflow-hidden flex flex-col shadow-2xl animate-scale-in">
        <div class="p-6 bg-gradient-to-r from-orange-400 to-pink-500 text-white flex justify-between items-center">
          <div>
            <h3 class="text-xl font-bold">📅 定时日程提醒管理</h3>
            <p class="text-white/80 text-xs mt-1">学生: {{ systemStore.schedulingStudent.name }} | 设置学习闹钟，到点后设备端会自动语音播报</p>
          </div>
          <button @click="emit('close')" class="w-8 h-8 rounded-full bg-white/20 hover:bg-white/30 flex items-center justify-center text-xl transition-colors">×</button>
        </div>

        <div class="flex-1 overflow-auto p-6 flex flex-col gap-6 bg-gray-50/50">
          <!-- 添加日程表单 -->
          <div class="bg-white p-5 rounded-2xl border border-gray-100 shadow-sm">
            <h4 class="font-bold text-gray-700 mb-3.5 text-sm flex items-center gap-1.5">
              <span>➕</span> 新增日程闹钟
            </h4>
            <div class="grid grid-cols-1 sm:grid-cols-3 gap-4">
              <div>
                <span class="text-xs text-gray-400 block mb-1.5 font-bold">重复日期</span>
                <select v-model="newScheduleDay" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm bg-white focus:outline-none focus:border-orange-400 font-bold text-gray-700">
                  <option :value="1">每周一</option>
                  <option :value="2">每周二</option>
                  <option :value="3">每周三</option>
                  <option :value="4">每周四</option>
                  <option :value="5">每周五</option>
                  <option :value="6">每周六</option>
                  <option :value="7">每周日</option>
                </select>
              </div>
              <div>
                <span class="text-xs text-gray-400 block mb-1.5 font-bold">提醒时间</span>
                <input v-model="newScheduleTime" type="time" class="w-full border border-gray-200 rounded-xl px-3 py-2 text-sm focus:outline-none focus:border-orange-400 font-bold text-gray-700" />
              </div>
              <div class="sm:col-span-1 flex items-end">
                <button @click="handleAddSchedule" class="w-full bg-gradient-to-r from-orange-400 to-pink-500 hover:from-orange-500 hover:to-pink-600 text-white py-2.5 rounded-xl font-bold text-sm shadow-md transition-all">添加闹钟</button>
              </div>
            </div>
            <div class="mt-4">
              <span class="text-xs text-gray-400 block mb-1.5 font-bold">提醒要做的事</span>
              <input v-model="newScheduleTask" type="text" placeholder="例如：该背英语单词啦，该去浇花啦" class="w-full border border-gray-200 rounded-xl px-4 py-2 text-sm focus:outline-none focus:border-orange-400" />
            </div>
          </div>

          <!-- 已有日程列表 -->
          <div class="flex-grow flex flex-col min-h-0">
            <h4 class="font-bold text-gray-700 mb-3 text-sm flex items-center justify-between">
              <span>📋 当前日程安排列表</span>
              <span class="text-xs text-gray-400 font-normal">设备在联网状态下会自动同步这套行程</span>
            </h4>
            <div v-if="systemStore.loadingSchedules" class="py-8 text-center text-gray-400 text-sm">
              <span class="animate-spin text-lg mr-1.5">🔄</span> 载入日程中...
            </div>
            <div v-else-if="systemStore.schedulesList.length === 0" class="py-12 border-2 border-dashed border-gray-100 rounded-3xl flex flex-col items-center justify-center text-gray-400 text-sm bg-white shadow-sm">
              <span class="text-3xl mb-1.5">⏰</span>
              <p class="font-bold">暂无定时提醒</p>
              <p class="text-xs text-gray-300 mt-1">您也可以对着设备说："周三早上八点半提醒我交作业"进行语音录入</p>
            </div>
            <div v-else class="flex flex-col gap-3">
              <div v-for="sch in systemStore.schedulesList" :key="sch.id" class="bg-white border border-gray-100 rounded-2xl p-4 flex items-center justify-between shadow-sm hover:shadow-md transition-shadow">
                <div class="flex items-center gap-4">
                  <div class="w-12 h-12 rounded-xl bg-orange-50 text-orange-600 flex items-center justify-center font-extrabold text-sm flex-shrink-0">
                    周{{ ['一','二','三','四','五','六','日'][sch.day_of_week - 1] }}
                  </div>
                  <div>
                    <span class="font-bold text-gray-800 text-base mr-2">{{ sch.time_str }}</span>
                    <span class="text-gray-600 text-sm font-medium">{{ sch.task_desc }}</span>
                  </div>
                </div>
                <button @click="systemStore.deleteSchedule(sch.id)" class="text-red-500 hover:text-red-700 p-2 text-sm font-bold transition-colors">删除</button>
              </div>
            </div>
          </div>
        </div>

        <div class="p-6 border-t border-gray-100 bg-white flex justify-end">
          <button @click="emit('close')" class="px-6 py-2.5 bg-gray-100 hover:bg-gray-200 text-gray-700 rounded-xl font-bold transition-all text-sm">关闭</button>
        </div>
      </div>
    </div>
  </Transition>
</template>
