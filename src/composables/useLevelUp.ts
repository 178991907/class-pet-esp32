import { ref } from 'vue'

// 升级动画状态
const showLevelUpAnimation = ref(false)
const levelUpInfo = ref({ name: '', level: 0, petType: '', prevLevel: 0 })
const levelUpImagesLoaded = ref({ prev: false, current: false })
const levelUpPhase = ref<'show-prev' | 'transition' | 'show-current'>('show-prev')

export function useLevelUp() {
  function triggerLevelUp(name: string, level: number, petType: string, prevLevel: number) {
    levelUpInfo.value = { name, level, petType, prevLevel }
    levelUpImagesLoaded.value = { prev: false, current: false }
    levelUpPhase.value = 'show-prev'
    showLevelUpAnimation.value = true

    // 动画时序控制
    setTimeout(() => { levelUpPhase.value = 'transition' }, 500)
    setTimeout(() => { levelUpPhase.value = 'show-current' }, 1500)
    setTimeout(() => { showLevelUpAnimation.value = false }, 4000)
  }

  return {
    showLevelUpAnimation,
    levelUpInfo,
    levelUpImagesLoaded,
    levelUpPhase,
    triggerLevelUp
  }
}
