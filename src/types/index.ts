// 班级
export interface Class {
  id: string
  name: string
  created_at: number
  updated_at?: number
  user_id?: string
}

// 学生
export interface Student {
  id: string
  class_id: string
  name: string
  student_no: string | null
  device_id?: string | null
  total_points: number
  pet_type: string | null
  pet_level: number
  pet_exp: number
  created_at?: number
  battery_level?: number | null
  is_charging?: number | null
  last_seen?: number | null
}

// 评价规则
export interface Rule {
  id: string
  name: string
  points: number
  category: string
  is_custom?: boolean
  created_at?: number
}

// 评价记录
export interface EvaluationRecord {
  id: string
  class_id: string
  student_id: string
  points: number
  reason: string
  category: string
  timestamp: number
  student_name?: string
}

// 徽章
export interface Badge {
  id: string
  student_id: string
  pet_type: string
  earned_at: number
}

// 用户
export interface User {
  id: string
  username: string
  isGuest: boolean
}

// API 响应类型
export interface ApiResponse<T = unknown> {
  success?: boolean
  error?: string
  data?: T
}

// 分页响应
export interface PaginatedResponse<T> {
  records: T[]
  total: number
  page: number
  pageSize: number
  totalPages: number
}