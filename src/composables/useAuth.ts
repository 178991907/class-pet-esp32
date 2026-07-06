import { ref, computed } from 'vue'
import axios from 'axios'

import { localApiAdapter } from '@/utils/localApiAdapter'

interface User {
  id: string
  username: string
  isGuest?: boolean
  is_guest?: boolean
  role?: 'student' | 'teacher' | 'admin'
}

// 全局状态（模块级别单例）
const user = ref<User | null>(null)
const token = ref<string>(localStorage.getItem('token') || '')

// 读取云服务器环境变量
const cloudUrl = import.meta.env.VITE_API_URL

if (!cloudUrl) {
  console.log('📡 [ClassPetGarden] 未配置 VITE_API_URL，当前处于本地存储模式（数据保存在浏览器中）')
} else {
  console.log(`🚀 [ClassPetGarden] 检测到云服务器配置，当前处于云端模式 API 基地址: ${cloudUrl}`)
}

// 创建带认证的 axios 实例（单例）
const isLocalhost = typeof window !== 'undefined' && (window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1');
const api = axios.create({
  baseURL: cloudUrl || '/pet-garden/api',
  adapter: (cloudUrl || !isLocalhost) ? undefined : localApiAdapter
})

// 请求拦截器：自动添加 Authorization header
api.interceptors.request.use((config) => {
  console.log(`📡 [useAuth API Request] ${config.method?.toUpperCase()} ${config.url}`, {
    token: token.value
  })
  if (token.value) {
    config.headers.Authorization = `Bearer ${token.value}`
  }
  return config
})

// 响应拦截器：处理 401 错误自动退回游客模式
api.interceptors.response.use(
  (response) => {
    console.log(`📡 [useAuth API Response Success] ${response.config.method?.toUpperCase()} ${response.config.url}`, response.data)
    return response
  },
  (error) => {
    console.error(`📡 [useAuth API Response Error] ${error.config?.method?.toUpperCase()} ${error.config?.url}`, {
      status: error.response?.status,
      data: error.response?.data
    })
    if (error.response?.status === 401 && !isGuest.value) {
      console.warn('⚠️ [useAuth] 检测到 401 响应且当前非游客状态，强制执行 logout() 退回游客模式！')
      logout()
    }
    return Promise.reject(error)
  }
)

// 初始化用户状态
const savedUser = localStorage.getItem('user')
if (savedUser) {
  try {
    user.value = JSON.parse(savedUser)
  } catch {
    localStorage.removeItem('user')
    localStorage.removeItem('token')
  }
}

// 如果没有用户，自动设置为游客
if (!user.value) {
  const guestUser: User = { id: 'guest', username: '游客', isGuest: true, role: 'student' }
  user.value = guestUser
  token.value = 'guest'
  localStorage.setItem('token', 'guest')
  localStorage.setItem('user', JSON.stringify(guestUser))
}

// 计算属性
const isGuest = computed(() => {
  if (!user.value) return true
  return user.value.isGuest ?? user.value.is_guest ?? true
})

const isLoggedIn = computed(() => !!user.value && !isGuest.value)
const username = computed(() => user.value?.username || '游客')

// 设置用户
function setUser(userData: User, userToken: string) {
  console.log('🔑 [useAuth setUser] 开始设置用户状态:', { userData, userToken })
  const normalizedUser: User = {
    id: userData.id,
    username: userData.username,
    isGuest: userData.isGuest ?? userData.is_guest ?? false,
    role: userData.role || 'student'
  }
  user.value = normalizedUser
  token.value = userToken
  localStorage.setItem('token', userToken)
  localStorage.setItem('user', JSON.stringify(normalizedUser))
  console.log('🔑 [useAuth setUser] 用户状态已持久化。当前 user.value:', user.value, 'isGuest.value:', isGuest.value)
}

// 退出登录（回到游客模式）
function logout() {
  console.log('🚪 [useAuth logout] 执行登出逻辑，清空登录态')
  const guestUser: User = { id: 'guest', username: '游客', isGuest: true, role: 'student' }
  user.value = guestUser
  token.value = 'guest'
  localStorage.setItem('token', 'guest')
  localStorage.setItem('user', JSON.stringify(guestUser))
}

// 获取用户信息（已登录用户）
async function fetchUserInfo() {
  if (isGuest.value) return
  try {
    const res = await api.get('/auth/me')
    user.value = res.data.user
  } catch {
    logout()
  }
}

export function useAuth() {
  return {
    user,
    token,
    isLoggedIn,
    isGuest,
    username,
    api,
    setUser,
    logout,
    fetchUserInfo
  }
}