<script setup lang="ts">
import { ref, computed } from 'vue'
import { PET_TYPES, getPetLevelImage, loadPets } from '@/data/pets'
import PetImage from '@/components/PetImage.vue'

// 分类标签
const categories = [
  { id: 'all', name: '全部' },
  { id: 'normal', name: '普通动物' },
  { id: 'mythical', name: '神兽' }
]

const currentCategory = ref('all')
const selectedPet = ref<string | null>(null)
const selectedLevel = ref(1)

// 新增宠物状态
const showAddPetModal = ref(false)
const newPetName = ref('')
const newPetId = ref('')
const newPetCategory = ref<'normal' | 'mythical'>('normal')
const newPetSourceType = ref<'clone' | 'custom_url' | 'upload'>('clone')
const selectedCloneSkinId = ref('west-highland')
const customImagesBaseUrl = ref('')
const addPetError = ref('')
const showLogoModal = ref(false)

// 用于存储 1-8 级手动上传的 Base64 图片
const uploadedImages = ref<Record<number, string>>({})
// 存储每个级别的图片压缩状态
const isCompressing = ref<Record<number, boolean>>({})

// 用于选择克隆模版的默认内置宠物列表（仅包含 ID 和名称）
const defaultPetsList = computed(() => {
  return PET_TYPES.filter(p => !p.id.startsWith('custom_'))
})

// 普通动物
const normalPets = computed(() => PET_TYPES.filter(p => p.category === 'normal'))

// 神兽
const mythicalPets = computed(() => PET_TYPES.filter(p => p.category === 'mythical'))

// 打开添加宠物弹窗
function openAddPetModal() {
  newPetName.value = ''
  newPetId.value = ''
  newPetCategory.value = 'normal'
  newPetSourceType.value = 'clone'
  selectedCloneSkinId.value = 'west-highland'
  customImagesBaseUrl.value = ''
  uploadedImages.value = {}
  isCompressing.value = {}
  addPetError.value = ''
  showAddPetModal.value = true
}

// 纯前端图片等比例压缩工具（限制宽或高最大 256px，大幅度缩减 Base64 占用，避免超出 LocalStorage 限制）
function compressImage(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = (e) => {
      const img = new Image()
      img.onload = () => {
        const canvas = document.createElement('canvas')
        const max_size = 256
        let width = img.width
        let height = img.height
        
        if (width > height) {
          if (width > max_size) {
            height *= max_size / width
            width = max_size
          }
        } else {
          if (height > max_size) {
            width *= max_size / height
            height = max_size
          }
        }
        
        canvas.width = width
        canvas.height = height
        const ctx = canvas.getContext('2d')
        if (ctx) {
          ctx.drawImage(img, 0, 0, width, height)
          // 采用 jpeg 质量 0.7 压缩，通常单张图片大小只有 5KB-10KB，极致节省本地存储空间
          const dataUrl = canvas.toDataURL('image/jpeg', 0.7)
          resolve(dataUrl)
        } else {
          reject(new Error('Canvas 绘制环境获取失败'))
        }
      }
      img.onerror = () => reject(new Error('加载图片原画失败'))
      img.src = e.target?.result as string
    }
    reader.onerror = () => reject(new Error('读取本地文件失败'))
    reader.readAsDataURL(file)
  })
}

// 监听处理文件选择上传并触发 Canvas 压缩
async function handleImageUpload(event: Event, level: number) {
  const target = event.target as HTMLInputElement
  const file = target.files?.[0]
  if (!file) return

  // 基础类型校验
  if (!file.type.startsWith('image/')) {
    alert('请选择有效的图片文件！')
    return
  }

  isCompressing.value[level] = true
  try {
    const compressedBase64 = await compressImage(file)
    uploadedImages.value[level] = compressedBase64
  } catch (err: any) {
    console.error(`第 ${level} 级形态压缩失败:`, err)
    alert(`第 ${level} 级形态图片处理失败: ${err.message || err}`)
  } finally {
    isCompressing.value[level] = false
  }
}

// 保存自定义宠物
function saveCustomPet() {
  addPetError.value = ''
  const nameTrimmed = newPetName.value.trim()
  const rawId = newPetId.value.trim().toLowerCase()

  if (!nameTrimmed) {
    addPetError.value = '请输入宠物名称'
    return
  }
  if (!rawId) {
    addPetError.value = '请输入唯一的宠物英文ID'
    return
  }
  if (!/^[a-zA-Z0-9_-]+$/.test(rawId)) {
    addPetError.value = 'ID 仅能包含英文、数字、中划线或下划线'
    return
  }

  const finalId = `custom_${rawId}`
  // 检查是否冲突
  if (PET_TYPES.some(p => p.id === finalId || p.id === rawId)) {
    addPetError.value = '该宠物 ID 已存在，请更换 ID'
    return
  }

  // 组装宠物数据
  let newPetData: any = {
    id: finalId,
    name: nameTrimmed,
    category: newPetCategory.value
  }

  if (newPetSourceType.value === 'clone') {
    // 克隆已有的内置宠物外形
    const templatePet = PET_TYPES.find(p => p.id === selectedCloneSkinId.value)
    if (!templatePet) {
      addPetError.value = '选中的模板宠物不存在'
      return
    }
    newPetData.image = templatePet.image
    newPetData.levelImages = { ...templatePet.levelImages }
  } else if (newPetSourceType.value === 'custom_url') {
    // 使用自定义的图片基路径
    if (!customImagesBaseUrl.value.trim()) {
      addPetError.value = '请输入自定义图片 Base URL'
      return
    }
    const cleanUrl = customImagesBaseUrl.value.trim().replace(/\/$/, '')
    newPetData.image = `${cleanUrl}/lv1.png`
    
    const levelImages: Record<number, string> = {}
    for (let i = 1; i <= 8; i++) {
      levelImages[i] = `${cleanUrl}/lv${i}.png`
    }
    newPetData.levelImages = levelImages
  } else {
    // 直接手动上传本地 1-8 级原图
    const levelImages: Record<number, string> = {}
    for (let i = 1; i <= 8; i++) {
      if (!uploadedImages.value[i]) {
        addPetError.value = `请上传第 ${i} 级 (${getLevelName(i)}) 的宠物图片`
        return
      }
      levelImages[i] = uploadedImages.value[i]
    }
    newPetData.image = uploadedImages.value[1]
    newPetData.levelImages = levelImages
  }

  try {
    const data = localStorage.getItem('class_pet_garden_custom_pets')
    const customs = data ? JSON.parse(data) : []
    customs.push(newPetData)
    localStorage.setItem('class_pet_garden_custom_pets', JSON.stringify(customs))
    
    // 动态重新加载宠物列表数据
    loadPets()
    
    showAddPetModal.value = false
    alert(`🎉 恭喜！新宠物「${nameTrimmed}」添加成功！`)
  } catch (e: any) {
    addPetError.value = `保存失败: ${e.message || e}`
  }
}

// 删除自定义宠物
function deleteCustomPet(petId: string) {
  if (!petId.startsWith('custom_')) return

  const petName = PET_TYPES.find(p => p.id === petId)?.name || '自定义宠物'

  if (!confirm(`确定要删除自定义宠物「${petName}」吗？此操作不可逆，且已领养该宠物的学生将恢复未领养状态！`)) {
    return
  }

  try {
    const data = localStorage.getItem('class_pet_garden_custom_pets')
    if (data) {
      let customs = JSON.parse(data)
      customs = customs.filter((p: any) => p.id !== petId)
      localStorage.setItem('class_pet_garden_custom_pets', JSON.stringify(customs))
    }

    // 动态重新加载宠物列表数据
    loadPets()
    alert(`👋 宠物「${petName}」已成功删除！`)
  } catch (e: any) {
    alert(`删除失败: ${e.message || e}`)
  }
}

// 获取等级颜色
function getLevelColor(level: number): string {
  const colors: Record<number, string> = {
    1: 'from-gray-400 to-gray-500',
    2: 'from-gray-400 to-slate-500',
    3: 'from-blue-400 to-cyan-500',
    4: 'from-cyan-400 to-teal-500',
    5: 'from-purple-400 to-violet-500',
    6: 'from-pink-400 to-rose-500',
    7: 'from-rose-400 to-red-500',
    8: 'from-yellow-400 via-amber-400 to-orange-500'
  }
  return colors[level] || 'from-gray-400 to-gray-500'
}

// 获取等级名称
function getLevelName(level: number): string {
  const names: Record<number, string> = {
    1: '初生',
    2: '成长',
    3: '优秀',
    4: '进阶',
    5: '稀有',
    6: '精英',
    7: '史诗',
    8: '传说'
  }
  return names[level] || `Lv.${level}`
}

// 选择宠物
function selectPet(petId: string) {
  selectedPet.value = petId
  selectedLevel.value = 1
}

// 关闭详情
function closeDetail() {
  selectedPet.value = null
}
</script>

<template>
  <div class="min-h-screen bg-gradient-to-br from-orange-50 via-pink-50 to-purple-50 p-6">
    <!-- 头部 -->
    <header class="mb-8">
      <div class="flex items-center justify-between">
        <div>
          <h1 class="text-3xl font-bold text-gradient flex items-center gap-3.5">
            <!-- 智能 CSS 裁剪与缩放后的精美 3D LOGO 容器 -->
            <div class="relative group cursor-pointer shrink-0" @click="showLogoModal = true">
              <!-- 内层裁剪容器：带有 overflow-hidden，负责图片底部的智能 CSS 裁剪，并在悬停时平滑放大 1.1 倍 -->
              <div class="w-11 h-11 rounded-2xl overflow-hidden bg-white/40 border border-white/60 shadow-md flex items-center justify-center relative transition-all duration-300 group-hover:scale-110 group-hover:rotate-2 group-hover:shadow-lg">
                <img 
                  src="/images/logo.png" 
                  alt="Logo"
                  class="w-[130%] h-[130%] object-cover object-top absolute -top-0.5"
                />
              </div>
              
              <!-- 超精美 Hover 悬浮气泡大卡片（现可完美、自由地在容器下方滑出，绝无阻挡） -->
              <div class="absolute top-14 left-0 z-50 w-64 p-3 bg-white/95 backdrop-blur-xl border border-gray-200/50 rounded-2xl shadow-2xl transition-all duration-300 origin-top-left opacity-0 scale-95 pointer-events-none group-hover:opacity-100 group-hover:scale-100 group-hover:pointer-events-auto text-left font-normal normal-case tracking-normal">
                <div class="aspect-square w-full rounded-xl overflow-hidden bg-gray-50 mb-2 border border-gray-100">
                  <img src="/images/logo.png" class="w-full h-full object-contain" />
                </div>
                <div>
                  <div class="font-bold text-gray-800 text-sm">英语全科启蒙+AI</div>
                  <div class="text-[10px] text-gray-400 mt-1">💡 鼠标点击图标可查看超高清原尺寸大图</div>
                </div>
                <!-- 小气泡箭头 -->
                <div class="absolute -top-1.5 left-5 w-3 h-3 bg-white border-l border-t border-gray-200/50 rotate-45"></div>
              </div>
            </div>
            <!-- 右侧极致清晰的标题：点击快速返回首页 -->
            <router-link to="/" class="font-extrabold tracking-wide text-3xl hover:opacity-90 active:scale-95 transition-all cursor-pointer">宠物图鉴</router-link>
          </h1>
          <p class="text-gray-500 mt-2">预览所有宠物的成长形态</p>
        </div>
        <div class="flex items-center gap-3">
          <button 
            @click="openAddPetModal" 
            class="px-6 py-3 bg-gradient-to-r from-orange-400 to-pink-500 hover:from-orange-500 hover:to-pink-600 rounded-xl shadow-lg hover:shadow-xl transition-all font-bold text-white flex items-center gap-2"
          >
            <span>✨</span> 新增宠物
          </button>
          <router-link to="/" class="px-6 py-3 bg-white rounded-xl shadow-md hover:shadow-lg transition-all font-medium text-gray-700">
            ← 返回首页
          </router-link>
        </div>
      </div>
    </header>

    <!-- 分类标签 -->
    <div class="flex gap-3 mb-8">
      <button
        v-for="cat in categories"
        :key="cat.id"
        @click="currentCategory = cat.id"
        class="px-6 py-3 rounded-xl font-bold transition-all"
        :class="currentCategory === cat.id 
          ? 'bg-gradient-to-r from-orange-400 to-pink-500 text-white shadow-lg' 
          : 'bg-white text-gray-600 hover:bg-gray-50 shadow-md'"
      >
        {{ cat.name }}
      </button>
    </div>

    <!-- 普通动物区域 -->
    <section v-if="currentCategory === 'all' || currentCategory === 'normal'" class="mb-10">
      <h2 class="text-xl font-bold text-gray-700 mb-4 flex items-center gap-2">
        <span class="w-8 h-8 rounded-full bg-gradient-to-r from-blue-400 to-cyan-400 flex items-center justify-center text-white text-sm">🐕</span>
        普通动物
      </h2>
      <div class="grid grid-cols-4 md:grid-cols-6 lg:grid-cols-8 xl:grid-cols-10 gap-4">
        <div
          v-for="pet in normalPets"
          :key="pet.id"
          @click="selectPet(pet.id)"
          class="bg-white rounded-2xl p-3 shadow-card hover:shadow-card-hover transition-all cursor-pointer group"
        >
          <div class="aspect-square rounded-xl overflow-hidden bg-gradient-to-br from-orange-100 to-pink-100 mb-2 relative">
            <PetImage
              :src="getPetLevelImage(pet.id, 1)"
              :alt="pet.name"
              size="full"
              :rounded="false"
              :hover-scale="true"
              :fixed-emoji-size="true"
            />
            <!-- 如果是自定义宠物，显示删除按钮 -->
            <button 
              v-if="pet.id.startsWith('custom_')"
              @click.stop="deleteCustomPet(pet.id)"
              class="absolute top-1.5 right-1.5 z-20 w-6 h-6 bg-red-500 hover:bg-red-600 rounded-full flex items-center justify-center text-white text-xs shadow-md transition-all scale-0 group-hover:scale-100 duration-200"
              title="注销此自定义宠物"
            >
              ×
            </button>
          </div>
          <div class="text-center">
            <div class="font-bold text-sm text-gray-800 group-hover:text-orange-500 transition-colors">{{ pet.name }}</div>
            <div class="text-xs text-gray-400">点击查看</div>
          </div>
        </div>
      </div>
    </section>

    <!-- 神兽区域 -->
    <section v-if="currentCategory === 'all' || currentCategory === 'mythical'">
      <h2 class="text-xl font-bold text-gray-700 mb-4 flex items-center gap-2">
        <span class="w-8 h-8 rounded-full bg-gradient-to-r from-purple-400 to-pink-400 flex items-center justify-center text-white text-sm">✨</span>
        神兽
      </h2>
      <div class="grid grid-cols-4 md:grid-cols-6 lg:grid-cols-8 xl:grid-cols-10 gap-4">
        <div
          v-for="pet in mythicalPets"
          :key="pet.id"
          @click="selectPet(pet.id)"
          class="bg-white rounded-2xl p-3 shadow-card hover:shadow-card-hover transition-all cursor-pointer group"
        >
          <div class="aspect-square rounded-xl overflow-hidden bg-gradient-to-br from-purple-100 to-pink-100 mb-2 relative">
            <PetImage
              :src="getPetLevelImage(pet.id, 1)"
              :alt="pet.name"
              size="full"
              :rounded="false"
              :hover-scale="true"
              :fixed-emoji-size="true"
            />
            <!-- 如果是自定义宠物，显示删除按钮 -->
            <button 
              v-if="pet.id.startsWith('custom_')"
              @click.stop="deleteCustomPet(pet.id)"
              class="absolute top-1.5 right-1.5 z-20 w-6 h-6 bg-red-500 hover:bg-red-600 rounded-full flex items-center justify-center text-white text-xs shadow-md transition-all scale-0 group-hover:scale-100 duration-200"
              title="注销此自定义宠物"
            >
              ×
            </button>
          </div>
          <div class="text-center">
            <div class="font-bold text-sm text-gray-800 group-hover:text-purple-500 transition-colors">{{ pet.name }}</div>
            <div class="text-xs text-gray-400">点击查看</div>
          </div>
        </div>
      </div>
    </section>

    <!-- 宠物详情弹窗 -->
    <Transition name="modal">
      <div v-if="selectedPet" class="fixed inset-0 bg-black/50 backdrop-blur-sm flex items-center justify-center z-50 p-4" @click.self="closeDetail">
        <div class="bg-white rounded-3xl w-full max-w-5xl max-h-[90vh] overflow-auto shadow-2xl">
          <!-- 头部 -->
          <div class="bg-gradient-to-r from-orange-400 via-pink-400 to-purple-400 p-6 rounded-t-3xl flex items-center justify-between">
            <div class="flex items-center gap-4">
              <div class="w-16 h-16 rounded-2xl overflow-hidden bg-white/20 flex items-center justify-center">
                <PetImage
                  :src="getPetLevelImage(selectedPet, selectedLevel)"
                  size="md"
                  :rounded="true"
                  :show-loading="false"
                  :fixed-emoji-size="true"
                />
              </div>
              <div class="text-white">
                <h2 class="text-2xl font-bold">{{ PET_TYPES.find(p => p.id === selectedPet)?.name }}</h2>
                <p class="text-white/80">
                  {{ PET_TYPES.find(p => p.id === selectedPet)?.category === 'mythical' ? '神兽' : '普通动物' }}
                  · Lv.{{ selectedLevel }} {{ getLevelName(selectedLevel) }}
                </p>
              </div>
            </div>
            <div class="flex items-center gap-3">
              <button 
                v-if="selectedPet && selectedPet.startsWith('custom_')"
                @click="deleteCustomPet(selectedPet); closeDetail()"
                class="px-4 py-2 bg-red-500/20 hover:bg-red-500/40 border border-red-400/30 hover:border-red-400/50 rounded-xl text-xs font-bold text-red-100 flex items-center gap-1.5 transition-all shadow-inner"
              >
                <span>🗑️</span> 注销宠物
              </button>
              <button @click="closeDetail" class="w-10 h-10 bg-white/20 hover:bg-white/30 rounded-full flex items-center justify-center text-white text-2xl transition-colors">
                ×
              </button>
            </div>
          </div>

          <!-- 内容 -->
          <div class="p-6">
            <!-- 当前等级大图 -->
            <div class="flex flex-col md:flex-row gap-6 mb-8">
              <div class="w-full md:w-1/2">
                <div class="aspect-square rounded-3xl overflow-hidden bg-gradient-to-br from-orange-100 via-pink-100 to-purple-100 shadow-inner relative">
                  <PetImage
                    :src="getPetLevelImage(selectedPet, selectedLevel)"
                    :alt="PET_TYPES.find(p => p.id === selectedPet)?.name"
                    size="full"
                    :rounded="false"
                    :show-loading="true"
                    :fixed-emoji-size="true"
                  />
                  <div class="absolute top-4 right-4 font-bold px-4 py-2 rounded-full shadow-lg text-white text-lg bg-gradient-to-r"
                    :class="getLevelColor(selectedLevel)"
                  >
                    Lv.{{ selectedLevel }}
                  </div>
                </div>
              </div>
              <div class="w-full md:w-1/2 flex flex-col justify-center">
                <h3 class="text-xl font-bold text-gray-800 mb-4">等级信息</h3>
                <div class="space-y-3">
                  <div class="flex items-center justify-between p-3 bg-gray-50 rounded-xl">
                    <span class="text-gray-600">当前等级</span>
                    <span class="font-bold text-lg" :class="`text-gradient bg-gradient-to-r ${getLevelColor(selectedLevel)} bg-clip-text text-transparent`">
                      Lv.{{ selectedLevel }} {{ getLevelName(selectedLevel) }}
                    </span>
                  </div>
                  <div class="flex items-center justify-between p-3 bg-gray-50 rounded-xl">
                    <span class="text-gray-600">宠物类型</span>
                    <span class="font-bold text-gray-800">
                      {{ PET_TYPES.find(p => p.id === selectedPet)?.category === 'mythical' ? '神兽' : '普通动物' }}
                    </span>
                  </div>
                </div>
              </div>
            </div>

            <!-- 等级选择 -->
            <h3 class="text-lg font-bold text-gray-800 mb-4">选择等级预览</h3>
            <div class="grid grid-cols-4 md:grid-cols-8 gap-3">
              <button
                v-for="level in 8"
                :key="level"
                @click="selectedLevel = level"
                class="relative aspect-square rounded-2xl overflow-hidden transition-all"
                :class="selectedLevel === level ? 'ring-4 ring-orange-400 scale-105' : 'hover:scale-105'"
              >
                <div class="absolute inset-0 bg-gradient-to-br" :class="getLevelColor(level)"></div>
                <PetImage
                  :src="getPetLevelImage(selectedPet, level)"
                  size="full"
                  :rounded="false"
                  :show-loading="false"
                  :fixed-emoji-size="true"
                  class="relative z-10"
                />
                <div class="absolute bottom-1 left-1 right-1 z-20">
                  <div class="bg-white/90 rounded-lg py-1 text-center">
                    <div class="text-xs font-bold text-gray-800">Lv.{{ level }}</div>
                  </div>
                </div>
              </button>
            </div>
          </div>
        </div>
      </div>
    </Transition>

    <!-- 新增宠物弹窗 -->
    <Transition name="modal">
      <div v-if="showAddPetModal" class="fixed inset-0 bg-black/60 backdrop-blur-md flex items-center justify-center z-50 p-4" @click.self="showAddPetModal = false">
        <div class="bg-white/95 backdrop-blur-xl border border-white/20 rounded-3xl w-full max-w-xl shadow-2xl overflow-hidden transform transition-all duration-300">
          <!-- Header -->
          <div class="bg-gradient-to-r from-orange-400 via-pink-400 to-purple-400 p-6 flex items-center justify-between text-white">
            <h2 class="text-xl font-bold flex items-center gap-2">
              <span class="text-2xl">✨</span> 新增自定义宠物
            </h2>
            <button @click="showAddPetModal = false" class="w-8 h-8 rounded-full bg-white/20 hover:bg-white/30 flex items-center justify-center text-white text-xl transition-colors">
              ×
            </button>
          </div>

          <!-- Body -->
          <div class="p-6 max-h-[70vh] overflow-y-auto space-y-4 text-left">
            <!-- 宠物名称 -->
            <div>
              <label class="block text-sm font-bold text-gray-700 mb-1.5">宠物名称</label>
              <input 
                v-model="newPetName" 
                type="text" 
                placeholder="例如：极地雪狼、熔岩火凤" 
                class="w-full px-4 py-2.5 rounded-xl border border-gray-200 focus:border-orange-400 focus:ring focus:ring-orange-200/50 outline-none transition-all font-medium text-gray-800"
              />
            </div>

            <!-- 宠物英文 ID -->
            <div>
              <label class="block text-sm font-bold text-gray-700 mb-1.5">宠物英文 ID</label>
              <input 
                v-model="newPetId" 
                type="text" 
                placeholder="例如：snow-wolf, lava-phoenix" 
                class="w-full px-4 py-2.5 rounded-xl border border-gray-200 focus:border-orange-400 focus:ring focus:ring-orange-200/50 outline-none transition-all font-medium text-gray-800"
              />
              <p class="text-xs text-gray-400 mt-1">仅限英文、数字和中划线，系统会自动加入专属前缀避免冲突。</p>
            </div>

            <!-- 宠物类型 -->
            <div>
              <label class="block text-sm font-bold text-gray-700 mb-1.5">分类档位</label>
              <div class="flex gap-6">
                <label class="flex items-center gap-2 cursor-pointer group">
                  <input type="radio" value="normal" v-model="newPetCategory" class="accent-orange-500 w-4 h-4 cursor-pointer" />
                  <span class="text-gray-700 font-bold group-hover:text-orange-500 transition-colors">普通动物</span>
                </label>
                <label class="flex items-center gap-2 cursor-pointer group">
                  <input type="radio" value="mythical" v-model="newPetCategory" class="accent-purple-500 w-4 h-4 cursor-pointer" />
                  <span class="text-gray-700 font-bold group-hover:text-purple-500 transition-colors">神秘神兽</span>
                </label>
              </div>
            </div>

            <!-- 外观图像来源 -->
            <div>
              <label class="block text-sm font-bold text-gray-700 mb-1.5">外观形态图片来源</label>
              <div class="flex rounded-xl bg-gray-100 p-1 mb-4">
                <button 
                  type="button" 
                  @click="newPetSourceType = 'clone'"
                  class="flex-1 py-2 rounded-lg text-[11px] font-bold transition-all"
                  :class="newPetSourceType === 'clone' ? 'bg-white text-gray-800 shadow-sm' : 'text-gray-500 hover:text-gray-700'"
                >
                  克隆已有皮肤 (推荐)
                </button>
                <button 
                  type="button" 
                  @click="newPetSourceType = 'upload'"
                  class="flex-1 py-2 rounded-lg text-[11px] font-bold transition-all"
                  :class="newPetSourceType === 'upload' ? 'bg-white text-gray-800 shadow-sm' : 'text-gray-500 hover:text-gray-700'"
                >
                  手动上传 8 级图片
                </button>
                <button 
                  type="button" 
                  @click="newPetSourceType = 'custom_url'"
                  class="flex-1 py-2 rounded-lg text-[11px] font-bold transition-all"
                  :class="newPetSourceType === 'custom_url' ? 'bg-white text-gray-800 shadow-sm' : 'text-gray-500 hover:text-gray-700'"
                >
                  自定义图片 CDN 路径
                </button>
              </div>

              <!-- 选项一：皮肤克隆网格 -->
              <div v-if="newPetSourceType === 'clone'" class="border border-gray-100 rounded-2xl p-3 bg-gray-50/50">
                <label class="block text-xs font-bold text-gray-400 mb-2 text-left">选择一个内置的宠物外形模版：</label>
                <div class="grid grid-cols-5 gap-2 max-h-[150px] overflow-y-auto p-1">
                  <div 
                    v-for="p in defaultPetsList" 
                    :key="p.id"
                    @click="selectedCloneSkinId = p.id"
                    class="border rounded-xl p-1.5 cursor-pointer text-center transition-all bg-white hover:border-orange-300"
                    :class="selectedCloneSkinId === p.id ? 'border-2 border-orange-500 shadow-sm shadow-orange-500/10' : 'border-gray-200'"
                  >
                    <div class="aspect-square w-full rounded-lg overflow-hidden bg-gray-50 mb-1">
                      <img :src="getPetLevelImage(p.id, 1)" class="w-full h-full object-cover" />
                    </div>
                    <div class="text-[10px] font-bold text-gray-700 truncate">{{ p.name }}</div>
                  </div>
                </div>
              </div>

              <!-- 选项二：手动上传 8 级本地图片网格 -->
              <div v-else-if="newPetSourceType === 'upload'" class="border border-gray-100 rounded-2xl p-3 bg-gray-50/50 space-y-2">
                <label class="block text-xs font-bold text-gray-400 text-left">分别上传宠物 1 - 8 级（初生到满级毕业）的超萌原画：</label>
                <div class="grid grid-cols-4 gap-2.5">
                  <div 
                    v-for="level in 8" 
                    :key="level"
                    class="relative aspect-square rounded-xl border-2 border-dashed border-gray-300 hover:border-orange-400 bg-white transition-all overflow-hidden flex flex-col items-center justify-center p-1 cursor-pointer group"
                    @click="($event.currentTarget as HTMLElement).querySelector('input')?.click()"
                  >
                    <!-- 隐藏的文件输入框 -->
                    <input 
                      type="file" 
                      accept="image/*"
                      @change="handleImageUpload($event, level)"
                      class="hidden"
                    />
                    
                    <!-- 压缩 Loading 状态 -->
                    <div v-if="isCompressing[level]" class="absolute inset-0 bg-white/80 backdrop-blur-[1px] flex items-center justify-center z-20">
                      <span class="animate-spin text-orange-500 text-lg">⏳</span>
                    </div>

                    <!-- 已有图片预览 -->
                    <template v-if="uploadedImages[level]">
                      <img :src="uploadedImages[level]" class="w-full h-full object-contain rounded-lg transition-transform group-hover:scale-105" />
                      <div class="absolute inset-x-0 bottom-0 bg-black/60 text-white text-[9px] font-bold py-0.5 text-center transition-opacity opacity-0 group-hover:opacity-100">
                        重新上传
                      </div>
                    </template>

                    <!-- 未上传状态 -->
                    <template v-else>
                      <span class="text-gray-400 group-hover:text-orange-500 transition-colors text-base font-bold">+</span>
                      <span class="text-[9px] font-bold text-gray-500 mt-0.5">{{ getLevelName(level) }}</span>
                      <span class="text-[8px] text-gray-400">Lv.{{ level }}</span>
                    </template>
                  </div>
                </div>
              </div>

              <!-- 选项三：自定义 Base URL -->
              <div v-else class="space-y-2">
                <label class="block text-xs font-bold text-gray-400 text-left">请输入包含 8 级头像的图片总路径：</label>
                <input 
                  v-model="customImagesBaseUrl" 
                  type="text" 
                  placeholder="如: https://mycdn.com/images/fox" 
                  class="w-full px-4 py-2.5 rounded-xl border border-gray-200 focus:border-orange-400 focus:ring focus:ring-orange-200/50 outline-none transition-all text-sm font-medium text-gray-800"
                />
                <p class="text-[10px] text-gray-400 leading-relaxed text-left">
                  系统会自动在该 URL 后面追加并在成长线中渲染 `lv1.png` ~ `lv8.png` 格式的图片。请确保您在该 CDN 下正确放置了这 8 个图片。
                </p>
              </div>
            </div>

            <!-- 错误信息 -->
            <div v-if="addPetError" class="p-3 bg-red-50 text-red-500 rounded-xl text-xs font-bold flex items-center gap-1.5">
              <span>⚠️</span> {{ addPetError }}
            </div>
          </div>

          <!-- Footer -->
          <div class="p-6 bg-gray-50 border-t border-gray-100 flex gap-3">
            <button 
              @click="showAddPetModal = false" 
              class="flex-1 py-3 bg-white border border-gray-200 text-gray-600 rounded-xl font-bold hover:bg-gray-100 transition-colors shadow-sm"
            >
              取消
            </button>
            <button 
              @click="saveCustomPet" 
              class="flex-1 py-3 bg-gradient-to-r from-orange-400 to-pink-500 hover:from-orange-500 hover:to-pink-600 text-white rounded-xl font-bold transition-all shadow-md hover:shadow-lg"
            >
              保存并发布
            </button>
          </div>
        </div>
      </div>
    </Transition>
    
    <!-- 高清 LOGO 预览弹窗 -->
    <Transition name="modal">
      <div v-if="showLogoModal" class="fixed inset-0 bg-black/75 backdrop-blur-md flex items-center justify-center z-50 p-4" @click.self="showLogoModal = false">
        <div class="bg-white/95 backdrop-blur-xl border border-white/20 rounded-3xl w-full max-w-lg shadow-2xl overflow-hidden p-6 text-center transform transition-all duration-300 relative font-normal normal-case tracking-normal">
          <!-- 关闭按钮 -->
          <button @click="showLogoModal = false" class="absolute top-4 right-4 w-8 h-8 rounded-full bg-gray-100 hover:bg-gray-200 flex items-center justify-center text-gray-500 hover:text-gray-800 text-lg transition-colors">
            ×
          </button>
          
          <!-- LOGO 大图 -->
          <div class="aspect-square w-full rounded-2xl overflow-hidden border border-gray-100 bg-white mb-4 mt-2 shadow-inner">
            <img src="/images/logo.png" class="w-full h-full object-contain" alt="英语全科启蒙+AI 高清原图" />
          </div>
          
          <h3 class="text-xl font-bold text-gray-800">英语全科启蒙+AI</h3>
          <p class="text-xs text-gray-400 mt-1">主品牌官方授权高清 3D LOGO 预览</p>
        </div>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.text-gradient {
  background-clip: text;
  -webkit-background-clip: text;
}

.modal-enter-active,
.modal-leave-active {
  transition: all 0.3s ease;
}

.modal-enter-from,
.modal-leave-to {
  opacity: 0;
}

.modal-enter-from > div,
.modal-leave-to > div {
  transform: scale(0.9);
}
</style>
