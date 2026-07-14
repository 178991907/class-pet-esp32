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

// 日历事件（学生个人日历）
export interface CalendarEvent {
  id: string
  student_id?: string
  title: string
  event_date: string        // YYYY-MM-DD
  time_str: string | null   // HH:MM 或 null
  description?: string
  created_at?: number
}

// 清单/待办项
export interface ChecklistItem {
  id: string
  student_id?: string
  content: string
  is_done: number           // 0 | 1
  created_at?: number
}

// 闹铃/定时（复用 schedules 表）
export interface Schedule {
  id: string
  student_id?: string
  day_of_week: number       // 0=周日 ... 6=周六
  time_str: string          // HH:MM
  task_desc: string
  is_active?: number        // 0 | 1
  created_at?: number
}

// 宠物主人记忆
export interface OwnerEmotionEntry {
  ts: number
  mood?: string
  note?: string
}
export interface OwnerLearningEntry {
  ts: number
  subject?: string
  content?: string
  status?: string
}
export interface OwnerProfile {
  profile: Record<string, any> | null
  emotion_log: OwnerEmotionEntry[]
  learning_log: OwnerLearningEntry[]
}