<script setup lang="ts">
import { getPetLevelImage } from '@/data/pets'
import { useLevelUp } from '@/composables/useLevelUp'

const { showLevelUpAnimation, levelUpInfo, levelUpPhase } = useLevelUp()
</script>

<template>
  <Transition name="fade">
    <div v-if="showLevelUpAnimation" class="fixed inset-0 bg-black/70 backdrop-blur-md flex items-center justify-center z-[200]">
      <div class="relative">
        <!-- 背景光晕 -->
        <div class="absolute inset-0 bg-gradient-to-r from-orange-400 via-pink-400 to-purple-400 rounded-full blur-3xl opacity-60 animate-pulse"></div>

        <!-- 主内容 -->
        <div class="relative bg-white/95 backdrop-blur-xl rounded-3xl p-10 text-center shadow-2xl border-4 max-w-md"
          :class="levelUpInfo.level >= 8 ? 'border-yellow-400 shadow-yellow-400/50' : levelUpInfo.level >= 5 ? 'border-purple-400 shadow-purple-400/50' : 'border-orange-400 shadow-orange-400/50'"
        >
          <h2 class="text-3xl font-bold mb-2"
            :class="levelUpInfo.level >= 8 ? 'text-gradient bg-gradient-to-r from-yellow-400 via-amber-400 to-orange-400 bg-clip-text text-transparent' : 'text-gradient'"
          >
            {{ levelUpInfo.level >= 8 ? '恭喜毕业！' : '升级啦！' }}
          </h2>

          <!-- 宠物进化动画区域 -->
          <div class="relative w-48 h-48 mx-auto my-6">
            <div class="absolute inset-0 rounded-3xl bg-gradient-to-r from-orange-300 via-pink-300 to-purple-300 opacity-50 animate-spin" style="animation-duration: 3s"></div>
            <div class="absolute inset-2 rounded-3xl bg-gradient-to-r from-yellow-300 via-orange-300 to-pink-300 opacity-40 animate-spin" style="animation-duration: 2s; animation-direction: reverse"></div>
            <div class="absolute inset-4 rounded-2xl overflow-hidden bg-gradient-to-br from-orange-100 to-pink-100 shadow-inner">
              <img
                :src="getPetLevelImage(levelUpInfo.petType, levelUpInfo.prevLevel)"
                class="absolute inset-0 w-full h-full object-contain p-2 transition-all duration-1000"
                :class="levelUpPhase === 'show-prev' ? 'opacity-100 scale-100' : 'opacity-0 scale-50'"
              />
              <img
                :src="getPetLevelImage(levelUpInfo.petType, levelUpInfo.level)"
                class="absolute inset-0 w-full h-full object-contain p-2 transition-all duration-1000"
                :class="levelUpPhase === 'show-current' ? 'opacity-100 scale-100' : 'opacity-0 scale-150'"
              />
            </div>
            <div class="absolute -bottom-2 left-1/2 -translate-x-1/2 flex items-center gap-2 bg-white rounded-full px-4 py-1 shadow-lg">
              <span class="text-sm text-gray-400">Lv.{{ levelUpInfo.prevLevel }}</span>
              <span class="text-lg">➜</span>
              <span class="text-xl font-bold"
                :class="levelUpInfo.level >= 8 ? 'text-yellow-500' : levelUpInfo.level >= 5 ? 'text-purple-500' : 'text-orange-500'"
              >Lv.{{ levelUpInfo.level }}</span>
            </div>
          </div>

          <p class="text-lg text-gray-600 mb-1">
            <span class="font-bold text-purple-500">{{ levelUpInfo.name }}</span> 的宠物
          </p>
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
</template>
