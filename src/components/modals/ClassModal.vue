<script setup lang="ts">
import { ref, watch } from 'vue'
import { useClassStore } from '@/stores/useClassStore'

const props = defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()

const classStore = useClassStore()
const newClassName = ref('')
const editingId = ref<string | null>(null)

watch(() => props.show, (val) => {
  if (val) {
    // 打开时由父组件设置 editingId
  }
})

function open(id?: string, name?: string) {
  editingId.value = id || null
  newClassName.value = name || ''
}

defineExpose({ open })

async function handleSubmit() {
  if (editingId.value) {
    const success = await classStore.updateClass(editingId.value, newClassName.value)
    if (success) {
      emit('close')
      newClassName.value = ''
      editingId.value = null
    }
  } else {
    const success = await classStore.createClass(newClassName.value)
    if (success) {
      emit('close')
      newClassName.value = ''
    }
  }
}
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4">
      <div class="bg-white rounded-3xl p-8 w-full max-w-md shadow-2xl animate-scale-in">
        <h3 class="text-xl font-bold mb-6 flex items-center gap-2">
          <span class="text-2xl">🏫</span> {{ editingId ? '编辑班级' : '创建班级' }}
        </h3>
        <input
          v-model="newClassName"
          type="text"
          placeholder="输入班级名称..."
          class="w-full border-2 border-gray-200 rounded-xl px-5 py-3 mb-6 text-lg focus:outline-none focus:border-orange-400 transition-colors"
          @keyup.enter="handleSubmit"
        />
        <div class="flex gap-3 justify-end">
          <button @click="emit('close'); editingId = null; newClassName = ''" class="px-6 py-3 text-gray-500 hover:text-gray-700 font-medium transition-colors">取消</button>
          <button @click="handleSubmit" class="bg-gradient-to-r from-orange-400 to-pink-500 text-white px-8 py-3 rounded-xl font-bold hover:shadow-lg transition-all">
            {{ editingId ? '保存' : '创建' }}
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>
