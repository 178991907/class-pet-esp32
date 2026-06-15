import { calculateLevel } from '@/data/pets'

// 接口类型定义（完全匹配 SQLite 的 snake_case 蛇形命名）
export interface User {
  id: string
  username: string
  password_hash: string
  is_guest: boolean
  role: 'student' | 'teacher' | 'admin'
  created_at: number
  is_password_modified?: boolean // 密码是否已被用户显式修改过
}

export interface Class {
  id: string
  user_id: string
  name: string
  created_at: number
  updated_at: number
}

export interface Student {
  id: string
  class_id: string
  name: string
  student_no?: string | null
  device_id?: string | null
  total_points: number
  pet_type?: string | null
  pet_level: number
  pet_exp: number
  created_at: number
}

export interface Badge {
  id: string
  student_id: string
  pet_type: string
  earned_at: number
}

export interface EvaluationRule {
  id: string
  name: string
  points: number
  category: string
  is_custom: boolean
  created_at: number
}

export interface EvaluationRecord {
  id: string
  class_id: string
  student_id: string
  points: number
  reason: string
  category: string
  timestamp: number
}

// 模拟数据库结构
interface LocalDbData {
  users: User[]
  classes: Class[]
  students: Student[]
  badges: Badge[]
  rules: EvaluationRule[]
  records: EvaluationRecord[]
  settings: Record<string, any>
}

const DB_KEY = 'class_pet_garden_local_db'

// 默认评价规则
const DEFAULT_RULES: Omit<EvaluationRule, 'id' | 'created_at'>[] = [
  // 学习类 - 加分
  { name: '作业完成优秀', points: 1, category: '学习', is_custom: false },
  { name: '平时测验满分', points: 3, category: '学习', is_custom: false },
  { name: '平时测验达优秀', points: 2, category: '学习', is_custom: false },
  { name: '默写全对', points: 1, category: '学习', is_custom: false },
  { name: '订正态度认真', points: 1, category: '学习', is_custom: false },
  { name: '优秀作业,值得表扬', points: 1, category: '学习', is_custom: false },
  { name: '近期学习状态进步', points: 1, category: '学习', is_custom: false },
  { name: '被老师点名表扬', points: 1, category: '学习', is_custom: false },
  { name: '单元测验显著进步', points: 2, category: '学习', is_custom: false },
  // 学习类 - 扣分
  { name: '不交作业', points: -1, category: '学习', is_custom: false },
  { name: '未完成作业', points: -2, category: '学习', is_custom: false },
  { name: '作业潦草', points: -1, category: '学习', is_custom: false },
  { name: '订正不认真', points: -2, category: '学习', is_custom: false },
  { name: '抄袭作业', points: -5, category: '学习', is_custom: false },
  { name: '考试作弊', points: -5, category: '学习', is_custom: false },
  { name: '学习显著退步', points: -2, category: '学习', is_custom: false },
  // 行为类 - 加分
  { name: '早读认真专注', points: 1, category: '行为', is_custom: false },
  { name: '课前准备充分', points: 1, category: '行为', is_custom: false },
  { name: '眼保健操全程认真', points: 1, category: '行为', is_custom: false },
  { name: '升旗仪式安静整齐', points: 1, category: '行为', is_custom: false },
  { name: '守纪表现优秀(被表扬)', points: 2, category: '行为', is_custom: false },
  { name: '主动帮助同学', points: 2, category: '行为', is_custom: false },
  { name: '拾金不昧(一般物品)', points: 2, category: '行为', is_custom: false },
  { name: '拾金不昧(贵重物品)', points: 5, category: '行为', is_custom: false },
  { name: '主动帮助生病同学', points: 3, category: '行为', is_custom: false },
  { name: '主动调解同学矛盾、化解冲突', points: 3, category: '行为', is_custom: false },
  { name: '做好人好事被学校提出表扬', points: 3, category: '行为', is_custom: false },
  { name: '积极参与校内外志愿服务', points: 3, category: '行为', is_custom: false },
  { name: '犯错主动认错,积极协商', points: 1, category: '行为', is_custom: false },
  // 行为类 - 扣分
  { name: '无故迟到或早退', points: -1, category: '行为', is_custom: false },
  { name: '未佩戴红领巾,不穿校服', points: -1, category: '行为', is_custom: false },
  { name: '私自旷课或课间操', points: -3, category: '行为', is_custom: false },
  { name: '上课讲话、开小差', points: -1, category: '行为', is_custom: false },
  { name: '扰乱课堂', points: -3, category: '行为', is_custom: false },
  { name: '课间追逐打闹', points: -3, category: '行为', is_custom: false },
  { name: '追逐打闹(酿成事故)', points: -3, category: '行为', is_custom: false },
  { name: '中午自习说话、随意走动', points: -1, category: '行为', is_custom: false },
  { name: '私自带玩具或零食或危险物品', points: -3, category: '行为', is_custom: false },
  { name: '排队时说话或小动作不停,被点名', points: -1, category: '行为', is_custom: false },
  { name: '传播脏话或不良歌谣', points: -5, category: '行为', is_custom: false },
  { name: '撒谎、隐瞒真实情况', points: -2, category: '行为', is_custom: false },
  { name: '说脏话,骂人,起绰号', points: -2, category: '行为', is_custom: false },
  { name: '欺负、推搡、伤害同学', points: -10, category: '行为', is_custom: false },
  { name: '挑拨离间、拉帮结派', points: -3, category: '行为', is_custom: false },
  { name: '不尊重同学、孤立他人', points: -3, category: '行为', is_custom: false },
  { name: '为私欲包庇犯错者', points: -3, category: '行为', is_custom: false },
  { name: '恶意举报、诬陷他人', points: -3, category: '行为', is_custom: false },
  { name: '破坏校园设施', points: -5, category: '行为', is_custom: false },
  // 健康类 - 加分
  { name: '认真完成包干区值日', points: 1, category: '健康', is_custom: false },
  { name: '主动为班级擦黑板', points: 1, category: '健康', is_custom: false },
  { name: '主动整理讲台', points: 1, category: '健康', is_custom: false },
  { name: '主动整理黑板粉笔槽', points: 1, category: '健康', is_custom: false },
  { name: '主动倒垃圾并套垃圾袋', points: 2, category: '健康', is_custom: false },
  { name: '座位整洁无涂画,桌椅干净', points: 1, category: '健康', is_custom: false },
  { name: '座位周围无垃圾', points: 1, category: '健康', is_custom: false },
  // 健康类 - 扣分
  { name: '打扫包干区时间玩耍,不认真', points: -2, category: '健康', is_custom: false },
  { name: '个人座位卫生不合格', points: -1, category: '健康', is_custom: false },
  { name: '校园内乱扔垃圾', points: -1, category: '健康', is_custom: false },
  { name: '桌洞脏乱、物品杂乱', points: -1, category: '健康', is_custom: false },
  { name: '破坏卫生、乱涂乱画', points: -2, category: '健康', is_custom: false },
  { name: '浪费粮食', points: -2, category: '健康', is_custom: false },
  { name: '破坏班级绿植、把玩绿植', points: -3, category: '健康', is_custom: false },
  // 其他类 - 加分
  { name: '主动整理图书、摆放整齐', points: 2, category: '其他', is_custom: false },
  { name: '主动帮同学更换桌椅', points: 2, category: '其他', is_custom: false },
  { name: '主动承担班级任务', points: 2, category: '其他', is_custom: false },
  { name: '积极参加班级墙面布置', points: 2, category: '其他', is_custom: false },
  { name: '积极参加班级或学校活动', points: 1, category: '其他', is_custom: false },
  { name: '活动中表现优秀', points: 2, category: '其他', is_custom: false },
  { name: '代表班级参赛', points: 3, category: '其他', is_custom: false },
  { name: '校级比赛:一等奖', points: 5, category: '其他', is_custom: false },
  { name: '校级比赛:二等奖', points: 4, category: '其他', is_custom: false },
  { name: '校级比赛:三等奖', points: 3, category: '其他', is_custom: false },
  { name: '区级及以上:一等奖', points: 8, category: '其他', is_custom: false },
  { name: '区级及以上:二等奖', points: 6, category: '其他', is_custom: false },
  { name: '区级及以上:三等奖', points: 4, category: '其他', is_custom: false },
  { name: '联欢会或文艺汇演积极参与', points: 2, category: '其他', is_custom: false },
  { name: '为班级争得荣誉', points: 5, category: '其他', is_custom: false },
  { name: '小组全周无违纪、全员交作业', points: 2, category: '其他', is_custom: false },
  // 其他类 - 扣分
  { name: '损坏公物、乱刻乱画', points: -1, category: '其他', is_custom: false },
  { name: '浪费水电、屡教不改', points: -1, category: '其他', is_custom: false },
  { name: '故意玩弄损坏公共电器', points: -3, category: '其他', is_custom: false },
  { name: '故意损坏卫生工具', points: -2, category: '其他', is_custom: false },
  { name: '扣分严重/打架/作弊/严重违纪', points: -8, category: '其他', is_custom: false },
]

// 简单 UUID 生成器（浏览器环境）
function uuid(): string {
  return 'local_' + Math.random().toString(36).substr(2, 9) + '_' + Date.now().toString(36)
}

// 初始化数据库
function getDbData(): LocalDbData {
  const data = localStorage.getItem(DB_KEY)
  if (data) {
    try {
      const parsed = JSON.parse(data)
      // 兼容性补充与数据迁移（如果以前保存的是驼峰命名，需要平滑过渡到蛇形）
      if (!parsed.users) parsed.users = []
      if (!parsed.classes) parsed.classes = []
      if (!parsed.students) parsed.students = []
      if (!parsed.badges) parsed.badges = []
      if (!parsed.rules) parsed.rules = []
      if (!parsed.records) parsed.records = []
      // 自动补齐与纠正默认管理员账号（解决历史抢注/错乱数据导致无法登录的问题）
      const adminUser = parsed.users.find((u: any) => u.username === 'admin')
      let migrated = false
      if (!adminUser) {
        parsed.users.push({
          id: 'admin',
          username: 'admin',
          password_hash: 'admin123', // 本地 Mock DB 中保存为明文密码，与 server 端哈希后密码有所区别
          is_guest: false,
          role: 'admin',
          created_at: Date.now(),
          is_password_modified: false
        })
        migrated = true
      } else {
        // 管理员绝对不能是游客，且必须具有管理员角色
        if (adminUser.role !== 'admin' || adminUser.is_guest !== false) {
          adminUser.role = 'admin'
          adminUser.is_guest = false
          migrated = true
        }
        // 未被显式修改过密码且密码不为默认密码时，强制纠错并重置为默认密码 admin123
        const isModified = adminUser.is_password_modified || (adminUser as any).isPasswordModified
        if (!isModified && adminUser.password_hash !== 'admin123') {
          adminUser.password_hash = 'admin123'
          adminUser.is_password_modified = false
          migrated = true
        }
      }

      // 平滑迁移历史驼峰数据到蛇形
      parsed.classes = parsed.classes.map((c: any) => {
        if (c.userId) { c.user_id = c.userId; delete c.userId; migrated = true; }
        if (c.createdAt) { c.created_at = c.createdAt; delete c.createdAt; migrated = true; }
        if (c.updatedAt) { c.updated_at = c.updatedAt; delete c.updatedAt; migrated = true; }
        return c
      })

      parsed.students = parsed.students.map((s: any) => {
        if (s.classId) { s.class_id = s.classId; delete s.classId; migrated = true; }
        if (s.studentNo !== undefined) { s.student_no = s.studentNo; delete s.studentNo; migrated = true; }
        if (s.totalPoints !== undefined) { s.total_points = s.totalPoints; delete s.totalPoints; migrated = true; }
        if (s.petType !== undefined) { s.pet_type = s.petType; delete s.petType; migrated = true; }
        if (s.petLevel !== undefined) { s.pet_level = s.petLevel; delete s.petLevel; migrated = true; }
        if (s.petExp !== undefined) { s.pet_exp = s.petExp; delete s.petExp; migrated = true; }
        if (s.createdAt) { s.created_at = s.createdAt; delete s.createdAt; migrated = true; }
        return s
      })

      parsed.badges = parsed.badges.map((b: any) => {
        if (b.studentId) { b.student_id = b.studentId; delete b.studentId; migrated = true; }
        if (b.petType) { b.pet_type = b.petType; delete b.petType; migrated = true; }
        if (b.earnedAt) { b.earned_at = b.earnedAt; delete b.earnedAt; migrated = true; }
        return b
      })

      parsed.rules = parsed.rules.map((r: any) => {
        if (r.isCustom !== undefined) { r.is_custom = r.isCustom; delete r.isCustom; migrated = true; }
        if (r.createdAt) { r.created_at = r.createdAt; delete r.createdAt; migrated = true; }
        return r
      })

      parsed.records = parsed.records.map((rec: any) => {
        if (rec.classId) { rec.class_id = rec.classId; delete rec.classId; migrated = true; }
        if (rec.studentId) { rec.student_id = rec.studentId; delete rec.studentId; migrated = true; }
        return rec
      })

      if (migrated) {
        localStorage.setItem(DB_KEY, JSON.stringify(parsed))
      }

      return parsed
    } catch {
      // 解析失败
    }
  }

  // 默认初始数据
  const initialRules: EvaluationRule[] = DEFAULT_RULES.map((r, i) => ({
    id: `rule_local_${i}`,
    name: r.name,
    points: r.points,
    category: r.category,
    is_custom: r.is_custom,
    created_at: Date.now()
  }))

  const initialSettings = {
    levelConfig: [40, 60, 80, 100, 120, 140, 160]
  }

  const defaultDb: LocalDbData = {
    users: [
      {
        id: 'guest',
        username: 'guest',
        password_hash: '',
        is_guest: true,
        role: 'student',
        created_at: Date.now()
      },
      {
        id: 'admin',
        username: 'admin',
        password_hash: 'admin123',
        is_guest: false,
        role: 'admin',
        created_at: Date.now()
      }
    ],
    classes: [],
    students: [],
    badges: [],
    rules: initialRules,
    records: [],
    settings: initialSettings
  }

  localStorage.setItem(DB_KEY, JSON.stringify(defaultDb))
  return defaultDb
}

function saveDbData(data: LocalDbData) {
  localStorage.setItem(DB_KEY, JSON.stringify(data))
}

// 核心业务处理方法
export const localDb = {
  // 获取当前登录用户
  getUserByToken(token: string): User | null {
    const db = getDbData()
    if (token === 'guest') {
      return db.users.find(u => u.username === 'guest') || null
    }
    if (token.startsWith('mock-token-')) {
      const userId = token.replace('mock-token-', '')
      return db.users.find(u => u.id === userId) || null
    }
    return null
  },

  // 登录
  login(username: string, passwordHash: string): { user: User; token: string } | null {
    const db = getDbData()
    const user = db.users.find(u => u.username === username)
    if (!user) return null
    if (user.password_hash !== passwordHash) return null

    return {
      user,
      token: `mock-token-${user.id}`
    }
  },

  // 注册
  register(username: string, passwordHash: string, inviteCode?: string): User | null {
    if (username.toLowerCase() === 'admin') return null
    const db = getDbData()
    const exists = db.users.some(u => u.username === username)
    if (exists) return null

    const assignedRole = inviteCode === 'TEACHER_INVITE' ? 'teacher' : 'student'

    const newUser: User = {
      id: uuid(),
      username,
      password_hash: passwordHash,
      is_guest: false,
      role: assignedRole,
      created_at: Date.now()
    }
    db.users.push(newUser)
    saveDbData(db)
    return newUser
  },

  // 获取班级列表
  getClasses(userId: string): Class[] {
    const db = getDbData()
    return db.classes.filter(c => c.user_id === userId)
  },

  // 创建班级
  createClass(userId: string, name: string): Class {
    const db = getDbData()
    const newClass: Class = {
      id: uuid(),
      user_id: userId,
      name,
      created_at: Date.now(),
      updated_at: Date.now()
    }
    db.classes.push(newClass)
    saveDbData(db)
    return newClass
  },

  // 修改班级
  updateClass(userId: string, classId: string, name: string): Class | null {
    const db = getDbData()
    const cls = db.classes.find(c => c.id === classId && c.user_id === userId)
    if (!cls) return null
    cls.name = name
    cls.updated_at = Date.now()
    saveDbData(db)
    return cls
  },

  // 删除班级
  deleteClass(userId: string, classId: string): boolean {
    const db = getDbData()
    const index = db.classes.findIndex(c => c.id === classId && c.user_id === userId)
    if (index === -1) return false

    db.classes.splice(index, 1)
    db.students = db.students.filter(s => s.class_id !== classId)
    db.records = db.records.filter(r => r.class_id !== classId)
    saveDbData(db)
    return true
  },

  // 获取班级学生
  getStudentsByClass(classId: string): Student[] {
    const db = getDbData()
    return db.students.filter(s => s.class_id === classId)
  },

  // 创建学生
  createStudent(classId: string, name: string, studentNo?: string, petType?: string, deviceId?: string): Student {
    const db = getDbData()
    const newStudent: Student = {
      id: uuid(),
      class_id: classId,
      name,
      student_no: studentNo || null,
      device_id: deviceId || null,
      total_points: 0,
      pet_type: petType || null,
      pet_level: 1,
      pet_exp: 0,
      created_at: Date.now()
    }
    db.students.push(newStudent)
    saveDbData(db)
    return newStudent
  },

  // 更新学生基本信息及设备绑定
  updateStudentInfo(studentId: string, name: string, studentNo?: string, deviceId?: string): Student | null {
    const db = getDbData()
    const student = db.students.find(s => s.id === studentId)
    if (!student) return null
    student.name = name
    student.student_no = studentNo || null
    student.device_id = deviceId || null
    saveDbData(db)
    return student
  },


  // 删除学生
  deleteStudent(studentId: string): boolean {
    const db = getDbData()
    const index = db.students.findIndex(s => s.id === studentId)
    if (index === -1) return false

    db.students.splice(index, 1)
    db.records = db.records.filter(r => r.student_id !== studentId)
    db.badges = db.badges.filter(b => b.student_id !== studentId)
    saveDbData(db)
    return true
  },

  // 选择/更换宠物
  updateStudentPet(studentId: string, petType: string): Student | null {
    const db = getDbData()
    const student = db.students.find(s => s.id === studentId)
    if (!student) return null

    student.pet_type = petType
    student.pet_level = 1
    student.pet_exp = 0
    student.total_points = 0 // 换宠物重置分数
    saveDbData(db)
    return student
  },

  // 批量导入学生
  importStudents(classId: string, list: { name: string; studentNo?: string }[]): Student[] {
    const db = getDbData()
    const created: Student[] = []
    for (const item of list) {
      const newStudent: Student = {
        id: uuid(),
        class_id: classId,
        name: item.name,
        student_no: item.studentNo || null,
        total_points: 0,
        pet_level: 1,
        pet_exp: 0,
        created_at: Date.now()
      }
      db.students.push(newStudent)
      created.push(newStudent)
    }
    saveDbData(db)
    return created
  },

  // 获取规则列表
  getRules(): EvaluationRule[] {
    const db = getDbData()
    return db.rules
  },

  // 添加自定义规则
  createRule(name: string, points: number, category: string): EvaluationRule {
    const db = getDbData()
    const newRule: EvaluationRule = {
      id: uuid(),
      name,
      points,
      category,
      is_custom: true,
      created_at: Date.now()
    }
    db.rules.push(newRule)
    saveDbData(db)
    return newRule
  },

  // 删除自定义规则
  deleteRule(ruleId: string): boolean {
    const db = getDbData()
    const index = db.rules.findIndex(r => r.id === ruleId)
    if (index === -1) return false
    db.rules.splice(index, 1)
    saveDbData(db)
    return true
  },

  // 添加评价记录（含加减分等核心业务）
  addEvaluation(classId: string, studentIds: string[], points: number, reason: string, category: string) {
    const db = getDbData()
    const now = Date.now()
    const results: any[] = []

    for (const studentId of studentIds) {
      const student = db.students.find(s => s.id === studentId)
      if (!student) continue

      const recordId = uuid()
      // 1. 插入记录
      const newRecord: EvaluationRecord = {
        id: recordId,
        class_id: classId,
        student_id: studentId,
        points,
        reason,
        category,
        timestamp: now
      }
      db.records.push(newRecord)

      // 2. 更新学生分数与经验
      student.total_points += points

      let levelUp = false
      let levelDown = false
      let graduated = false
      let newLevel = student.pet_level
      let newExp = student.pet_exp

      if (student.pet_type) {
        newExp = Math.max(0, student.total_points)
        newLevel = calculateLevel(newExp)

        if (newLevel > student.pet_level) {
          levelUp = true
          if (newLevel === 8 && student.pet_level < 8) {
            const badgeId = uuid()
            const newBadge: Badge = {
              id: badgeId,
              student_id: studentId,
              pet_type: student.pet_type,
              earned_at: now
            }
            db.badges.push(newBadge)
            graduated = true
          }
        } else if (newLevel < student.pet_level) {
          levelDown = true
        }

        student.pet_exp = newExp
        student.pet_level = newLevel
      }

      results.push({
        id: recordId,
        studentId,
        studentName: student.name,
        timestamp: now,
        petLevel: newLevel,
        petExp: newExp,
        levelUp,
        levelDown,
        graduated
      })
    }

    saveDbData(db)
    return results.length === 1 ? results[0] : { success: true, results }
  },

  // 获取评价记录列表
  getEvaluations(filters: { classId?: string; studentId?: string; page: number; pageSize: number }) {
    const db = getDbData()
    let filtered = db.records.slice()

    if (filters.classId) {
      filtered = filtered.filter(r => r.class_id === filters.classId)
    }
    if (filters.studentId) {
      filtered = filtered.filter(r => r.student_id === filters.studentId)
    }

    filtered.sort((a, b) => b.timestamp - a.timestamp)

    const total = filtered.length
    const offset = (filters.page - 1) * filters.pageSize
    const paginated = filtered.slice(offset, offset + filters.pageSize)

    const records = paginated.map(r => {
      const student = db.students.find(s => s.id === r.student_id)
      return {
        ...r,
        student_name: student ? student.name : '未知学生'
      }
    })

    return {
      records,
      total,
      page: filters.page,
      pageSize: filters.pageSize,
      totalPages: Math.ceil(total / filters.pageSize)
    }
  },

  // 撤回班级最近一条评价
  undoLatestEvaluation(classId: string): any {
    const db = getDbData()
    const classRecords = db.records.filter(r => r.class_id === classId)
    if (classRecords.length === 0) return null

    classRecords.sort((a, b) => b.timestamp - a.timestamp)
    const latest = classRecords[0]

    db.records = db.records.filter(r => r.id !== latest.id)

    const student = db.students.find(s => s.id === latest.student_id)
    if (student) {
      student.total_points -= latest.points
      if (student.pet_type) {
        student.pet_exp = Math.max(0, student.total_points)
        student.pet_level = calculateLevel(student.pet_exp)
      }
    }

    saveDbData(db)
    return latest
  },

  // 删除单条评价记录
  deleteEvaluation(recordId: string): any {
    const db = getDbData()
    const index = db.records.findIndex(r => r.id === recordId)
    if (index === -1) return null

    const record = db.records[index]
    db.records.splice(index, 1)

    const student = db.students.find(s => s.id === record.student_id)
    if (student) {
      student.total_points -= record.points
      if (student.pet_type) {
        student.pet_exp = Math.max(0, student.total_points)
        student.pet_level = calculateLevel(student.pet_exp)
      }
    }

    saveDbData(db)
    return record
  },

  // 导出备份数据
  backup(): any {
    const db = getDbData()
    return db
  },

  // 还原备份数据
  restore(data: any): boolean {
    if (!data || typeof data !== 'object') return false
    const restored: LocalDbData = {
      users: Array.isArray(data.users) ? data.users : [],
      classes: Array.isArray(data.classes) ? data.classes : [],
      students: Array.isArray(data.students) ? data.students : [],
      badges: Array.isArray(data.badges) ? data.badges : [],
      rules: Array.isArray(data.rules) ? data.rules : [],
      records: Array.isArray(data.records) ? data.records : [],
      settings: (data.settings && typeof data.settings === 'object') ? data.settings : { levelConfig: [40, 60, 80, 100, 120, 140, 160] }
    }

    saveDbData(restored)
    return true
  },

  // 获取设置项
  getSettings(): any {
    const db = getDbData()
    return db.settings
  },

  // 修复经验值
  fixExp(userId: string): number {
    const db = getDbData()
    const classIds = db.classes.filter(c => c.user_id === userId).map(c => c.id)
    let changes = 0

    for (const student of db.students) {
      if (classIds.includes(student.class_id) && student.pet_type) {
        const expectedExp = Math.max(0, student.total_points)
        if (student.pet_exp !== expectedExp) {
          student.pet_exp = expectedExp
          student.pet_level = calculateLevel(expectedExp)
          changes++
        }
      }
    }

    if (changes > 0) {
      saveDbData(db)
    }
    return changes
  },

  // 获取排行榜
  getRanking(classId: string): any[] {
    const db = getDbData()
    const classStudents = db.students.filter(s => s.class_id === classId)

    const ranking = classStudents.map(s => {
      const badgeCount = db.badges.filter(b => b.student_id === s.id).length
      return {
        id: s.id,
        class_id: s.class_id,
        name: s.name,
        student_no: s.student_no,
        total_points: s.total_points,
        pet_type: s.pet_type,
        pet_level: s.pet_level,
        pet_exp: s.pet_exp,
        created_at: s.created_at,
        badge_count: badgeCount
      }
    })

    ranking.sort((a, b) => {
      if (b.total_points !== a.total_points) {
        return b.total_points - a.total_points
      }
      return b.pet_level - a.pet_level
    })

    return ranking
  },

  // 获取用户列表（管理员专用）
  getUsers(): User[] {
    const db = getDbData()
    return db.users
      .filter(u => u.id !== 'guest')
      .map(u => ({
        ...u,
        role: u.role || (u.username === 'admin' ? 'admin' : 'student')
      }))
  },

  // 修改用户角色（管理员专用）
  updateUserRole(userId: string, role: 'student' | 'teacher' | 'admin'): boolean {
    const db = getDbData()
    const user = db.users.find(u => u.id === userId)
    if (!user) return false
    if (user.username === 'admin' && role !== 'admin') return false
    user.role = role
    saveDbData(db)
    return true
  },

  // 修改管理员密码（管理员专用）
  updateAdminPassword(password: string): boolean {
    const db = getDbData()
    const admin = db.users.find(u => u.username === 'admin')
    if (!admin) return false
    admin.password_hash = password
    admin.is_password_modified = true // 标记密码已被显式修改过
    saveDbData(db)
    return true
  }
}
