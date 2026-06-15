import Database from 'better-sqlite3'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'
import { hashPassword } from './utils/password.js'

const __filename = fileURLToPath(import.meta.url)
const __dirname = dirname(__filename)

const dbPath = join(__dirname, 'pet-garden.db')
export const db = new Database(dbPath)

// 初始化数据库表
export function initDb() {
  db.exec(`
    -- 系统设置表
    CREATE TABLE IF NOT EXISTS settings (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );

    -- 用户表
    CREATE TABLE IF NOT EXISTS users (
      id TEXT PRIMARY KEY,
      username TEXT UNIQUE NOT NULL,
      password_hash TEXT NOT NULL,
      is_guest INTEGER DEFAULT 0,
      role TEXT DEFAULT 'student',
      created_at INTEGER
    );

    -- 班级表
    CREATE TABLE IF NOT EXISTS classes (
      id TEXT PRIMARY KEY,
      user_id TEXT,
      name TEXT NOT NULL,
      created_at INTEGER,
      updated_at INTEGER
    );

    -- 学生表
    CREATE TABLE IF NOT EXISTS students (
      id TEXT PRIMARY KEY,
      class_id TEXT NOT NULL,
      name TEXT NOT NULL,
      student_no TEXT,
      total_points INTEGER DEFAULT 0,
      pet_type TEXT,
      pet_level INTEGER DEFAULT 1,
      pet_exp INTEGER DEFAULT 0,
      created_at INTEGER,
      FOREIGN KEY (class_id) REFERENCES classes(id)
    );

    -- 徽章表
    CREATE TABLE IF NOT EXISTS badges (
      id TEXT PRIMARY KEY,
      student_id TEXT NOT NULL,
      pet_type TEXT NOT NULL,
      earned_at INTEGER,
      FOREIGN KEY (student_id) REFERENCES students(id)
    );

    -- 评价规则表
    CREATE TABLE IF NOT EXISTS evaluation_rules (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      points INTEGER NOT NULL,
      category TEXT NOT NULL,
      is_custom INTEGER DEFAULT 0,
      created_at INTEGER
    );

    -- 评价记录表
    CREATE TABLE IF NOT EXISTS evaluation_records (
      id TEXT PRIMARY KEY,
      class_id TEXT NOT NULL,
      student_id TEXT NOT NULL,
      points INTEGER NOT NULL,
      reason TEXT NOT NULL,
      category TEXT NOT NULL,
      timestamp INTEGER,
      FOREIGN KEY (class_id) REFERENCES classes(id),
      FOREIGN KEY (student_id) REFERENCES students(id)
    );

    -- 插入默认评价规则
    INSERT OR IGNORE INTO evaluation_rules (id, name, points, category, is_custom, created_at) VALUES
      ('rule_1', '课堂积极发言', 2, '学习', 0, 1704067200000),
      ('rule_2', '作业完成优秀', 3, '学习', 0, 1704067200000),
      ('rule_3', '帮助同学', 2, '行为', 0, 1704067200000),
      ('rule_4', '遵守纪律', 1, '行为', 0, 1704067200000),
      ('rule_5', '迟到', -1, '行为', 0, 1704067200000),
      ('rule_6', '未完成作业', -2, '学习', 0, 1704067200000),
      ('rule_7', '课堂捣乱', -3, '行为', 0, 1704067200000),
      ('rule_8', '主动打扫卫生', 2, '健康', 0, 1704067200000),
      ('rule_9', '坚持运动', 2, '健康', 0, 1704067200000),
      ('rule_10', '不讲卫生', -1, '健康', 0, 1704067200000);

    -- 优化查询性能的相关索引
    CREATE INDEX IF NOT EXISTS idx_classes_user_id ON classes(user_id);
    CREATE INDEX IF NOT EXISTS idx_students_class_id ON students(class_id);
    CREATE INDEX IF NOT EXISTS idx_badges_student_id ON badges(student_id);
    CREATE INDEX IF NOT EXISTS idx_evaluation_records_student_id ON evaluation_records(student_id);
    CREATE INDEX IF NOT EXISTS idx_evaluation_records_class_id ON evaluation_records(class_id);
    CREATE INDEX IF NOT EXISTS idx_evaluation_records_timestamp ON evaluation_records(timestamp DESC);

    -- 任务申报表
    CREATE TABLE IF NOT EXISTS student_task_applications (
      id TEXT PRIMARY KEY,
      student_id TEXT NOT NULL,
      task_name TEXT NOT NULL,
      points INTEGER NOT NULL,
      status TEXT DEFAULT 'pending',
      auto_confirm_at INTEGER,
      created_at INTEGER NOT NULL,
      FOREIGN KEY (student_id) REFERENCES students(id)
    );

    -- 多音源配置表
    CREATE TABLE IF NOT EXISTS music_sources (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      script_code TEXT NOT NULL,
      priority INTEGER DEFAULT 0,
      is_enabled INTEGER DEFAULT 1,
      failure_count INTEGER DEFAULT 0,
      last_failure_at INTEGER,
      created_at INTEGER NOT NULL
    );
  `)

  // 动态检查并添加 students 的 device_id 字段
  try {
    const tableInfo = db.prepare("PRAGMA table_info(students)").all();
    const hasDeviceId = tableInfo.some(col => col.name === 'device_id');
    if (!hasDeviceId) {
      db.exec("ALTER TABLE students ADD COLUMN device_id TEXT");
      db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_students_device_id ON students(device_id)");
      console.log("✅ 动态为 students 表添加 device_id 字段并创建唯一索引");
    }
  } catch (err) {
    console.error("❌ 检查或添加 device_id 字段失败:", err);
  }

  // 动态检查并添加 users 的 role 字段
  try {
    const tableInfo = db.prepare("PRAGMA table_info(users)").all();
    const hasRole = tableInfo.some(col => col.name === 'role');
    if (!hasRole) {
      db.exec("ALTER TABLE users ADD COLUMN role TEXT DEFAULT 'student'");
      console.log("✅ 动态为 users 表添加 role 字段");
    }
  } catch (err) {
    console.error("❌ 检查或添加 users 的 role 字段失败:", err);
  }

  // 初始化设备配置与自动审核设置
  try {
    const confirmMode = db.prepare("SELECT value FROM settings WHERE key = 'task_confirm_mode'").get();
    if (!confirmMode) {
      db.prepare("INSERT INTO settings (key, value) VALUES ('task_confirm_mode', ?)").run(JSON.stringify('auto'));
    }
    const confirmDelay = db.prepare("SELECT value FROM settings WHERE key = 'task_confirm_delay'").get();
    if (!confirmDelay) {
      db.prepare("INSERT INTO settings (key, value) VALUES ('task_confirm_delay', ?)").run(JSON.stringify(30)); // 默认 30 分钟
    }
    // 初始化固件版本设置
    const fwVersion = db.prepare("SELECT value FROM settings WHERE key = 'firmware_latest_version'").get();
    if (!fwVersion) {
      db.prepare("INSERT INTO settings (key, value) VALUES ('firmware_latest_version', ?)").run(JSON.stringify('2.0.0'));
    }
    const fwUrl = db.prepare("SELECT value FROM settings WHERE key = 'firmware_download_url'").get();
    if (!fwUrl) {
      db.prepare("INSERT INTO settings (key, value) VALUES ('firmware_download_url', ?)").run(JSON.stringify('/firmware/latest.bin'));
    }
    const fwChecksum = db.prepare("SELECT value FROM settings WHERE key = 'firmware_checksum'").get();
    if (!fwChecksum) {
      db.prepare("INSERT INTO settings (key, value) VALUES ('firmware_checksum', ?)").run(JSON.stringify('dummy_checksum_sha256'));
    }
    // 初始化教师邀请码设置
    const inviteCode = db.prepare("SELECT value FROM settings WHERE key = 'teacher_invite_code'").get();
    if (!inviteCode) {
      db.prepare("INSERT INTO settings (key, value) VALUES ('teacher_invite_code', ?)").run(JSON.stringify('TEACHER_INVITE'));
    }
  } catch (err) {
    console.error("❌ 初始化任务设置失败:", err);
  }

  // 初始化默认管理员用户
  try {
    const adminUser = db.prepare("SELECT id, role FROM users WHERE username = 'admin'").get();
    if (!adminUser) {
      const adminId = 'admin-default-id';
      const adminPwdHash = hashPassword('admin123');
      db.prepare("INSERT INTO users (id, username, password_hash, is_guest, role, created_at) VALUES (?, ?, ?, ?, ?, ?)")
        .run(adminId, 'admin', adminPwdHash, 0, 'admin', Date.now());
      console.log("👑 [ClassPetGarden] 已自动预置默认管理员账户: admin / admin123");
    } else if (adminUser.role !== 'admin') {
      const adminPwdHash = hashPassword('admin123');
      db.prepare("UPDATE users SET role = 'admin', password_hash = ? WHERE username = 'admin'").run(adminPwdHash);
      console.log("👑 [ClassPetGarden] 已重置预置管理员账户的 role 为 admin 并强制将密码重设为 admin123");
    }
  } catch (err) {
    console.error("❌ 初始化管理员账号失败:", err);
  }
}

// 异步数据库 API 包装器，为未来迁移到 PostgreSQL 提供通用接口
export async function getAsync(sql, ...params) {
  return new Promise((resolve, reject) => {
    try {
      const result = db.prepare(sql).get(...params);
      resolve(result);
    } catch (err) {
      reject(err);
    }
  });
}

export async function allAsync(sql, ...params) {
  return new Promise((resolve, reject) => {
    try {
      const result = db.prepare(sql).all(...params);
      resolve(result);
    } catch (err) {
      reject(err);
    }
  });
}

export async function runAsync(sql, ...params) {
  return new Promise((resolve, reject) => {
    try {
      const result = db.prepare(sql).run(...params);
      resolve(result);
    } catch (err) {
      reject(err);
    }
  });
}

