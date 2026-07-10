<script setup lang="ts">
import { ref } from 'vue'
import type { Student } from '@/types'
import { useStudentStore } from '@/stores/useStudentStore'

defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [], editComplete: [] }>()

const studentStore = useStudentStore()
const newStudentName = ref('')
const newStudentNo = ref('')
const newStudentDeviceId = ref('')
const editingStudent = ref<Student | null>(null)

function open(student?: Student) {
  if (student) {
    editingStudent.value = student
    newStudentName.value = student.name
    newStudentNo.value = student.student_no || ''
    newStudentDeviceId.value = (student as any).device_id || ''
  } else {
    editingStudent.value = null
    newStudentName.value = ''
    newStudentNo.value = ''
    newStudentDeviceId.value = ''
  }
  studentStore.loadUnboundDevices()
}

defineExpose({ open })

async function handleSubmit() {
  if (editingStudent.value) {
    const success = await studentStore.updateStudent(
      editingStudent.value.id,
      newStudentName.value,
      newStudentNo.value,
      newStudentDeviceId.value
    )
    if (success) {
      emit('close')
      emit('editComplete')
    }
  } else {
    const success = await studentStore.addStudent(
      newStudentName.value,
      newStudentNo.value,
      newStudentDeviceId.value
    )
    if (success) {
      emit('close')
    }
  }
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
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

        <!-- 未绑定设备发现池 -->
        <div v-if="studentStore.unboundDevicesList.length > 0" class="mb-6 bg-orange-50/70 border border-orange-100 rounded-2xl p-4 text-xs">
          <span class="font-bold text-orange-800 block mb-2 flex items-center gap-1">
            <span>📡</span> 发现最近开机的未绑定设备:
          </span>
          <div class="flex flex-col gap-2">
            <div v-for="dev in studentStore.unboundDevicesList" :key="dev.deviceId" class="flex justify-between items-center bg-white p-2.5 rounded-xl border border-orange-200/45 shadow-sm">
              <span class="font-mono text-gray-700 font-bold select-all text-xs">{{ dev.deviceId }}</span>
              <button @click="newStudentDeviceId = dev.deviceId" class="text-orange-600 hover:text-orange-700 font-bold px-2.5 py-1 bg-orange-100/50 rounded-lg transition-colors">一键绑定</button>
            </div>
          </div>
          <p class="text-[10px] text-gray-400 mt-2">提示：开发板开机连上 Wi-Fi 时会自动在此列出，点击一键绑定即可，免去手动输入。</p>
        </div>

        <div class="flex gap-3 justify-end">
          <button @click="emit('close'); editingStudent = null" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
          <button @click="handleSubmit" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">
            {{ editingStudent ? '保存' : '添加' }}
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>
