<script setup lang="ts">
defineProps<{ show: boolean }>()
const emit = defineEmits<{ close: [] }>()
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="fixed inset-0 bg-black/75 backdrop-blur-md flex items-center justify-center z-50 p-4" @click.self="emit('close')">
      <div class="bg-white/95 backdrop-blur-xl border border-white/20 rounded-3xl w-full max-w-lg shadow-2xl overflow-hidden p-6 text-center transform transition-all duration-300 relative">
        <button @click="emit('close')" class="absolute top-4 right-4 w-8 h-8 rounded-full bg-gray-100 hover:bg-gray-200 flex items-center justify-center text-gray-500 hover:text-gray-800 text-lg transition-colors">×</button>
        <div class="aspect-square w-full rounded-2xl overflow-hidden border border-gray-100 bg-white mb-4 mt-2 shadow-inner">
          <img src="/images/logo.png" class="w-full h-full object-contain" alt="英语全科启蒙+AI 高清原图" />
        </div>
        <h3 class="text-xl font-bold text-gray-800">英语全科启蒙+AI</h3>
        <p class="text-xs text-gray-400 mt-1">主品牌官方授权高清 3D LOGO 预览</p>
      </div>
    </div>
  </Transition>
</template>
