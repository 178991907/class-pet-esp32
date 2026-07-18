-- 班级宠物园 · D1 初始化(对应 server/db.js 的 SQLite schema)
-- D1 即 SQLite, 语法基本兼容; 用 ? 占位符的不是问题(这里直接用字面量 seed)。

CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS users (
  id TEXT PRIMARY KEY,
  username TEXT UNIQUE NOT NULL,
  password_hash TEXT NOT NULL,
  is_guest INTEGER DEFAULT 0,
  role TEXT DEFAULT 'student',
  created_at INTEGER
);
CREATE TABLE IF NOT EXISTS classes (
  id TEXT PRIMARY KEY,
  user_id TEXT,
  name TEXT NOT NULL,
  created_at INTEGER,
  updated_at INTEGER
);
CREATE TABLE IF NOT EXISTS students (
  id TEXT PRIMARY KEY,
  class_id TEXT NOT NULL,
  name TEXT NOT NULL,
  student_no TEXT,
  total_points INTEGER DEFAULT 0,
  pet_type TEXT,
  pet_level INTEGER DEFAULT 1,
  pet_exp INTEGER DEFAULT 0,
  device_id TEXT,
  battery_level INTEGER DEFAULT 100,
  is_charging INTEGER DEFAULT 0,
  last_seen INTEGER,
  created_at INTEGER,
  FOREIGN KEY (class_id) REFERENCES classes(id)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_students_device_id ON students(device_id);
CREATE TABLE IF NOT EXISTS badges (
  id TEXT PRIMARY KEY,
  student_id TEXT NOT NULL,
  pet_type TEXT NOT NULL,
  earned_at INTEGER,
  FOREIGN KEY (student_id) REFERENCES students(id)
);
CREATE TABLE IF NOT EXISTS evaluation_rules (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  points INTEGER NOT NULL,
  category TEXT NOT NULL,
  is_custom INTEGER DEFAULT 0,
  created_at INTEGER
);
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
CREATE TABLE IF NOT EXISTS chat_logs (
  id TEXT PRIMARY KEY,
  student_id TEXT NOT NULL,
  user_message TEXT NOT NULL,
  ai_response TEXT NOT NULL,
  timestamp INTEGER NOT NULL,
  FOREIGN KEY (student_id) REFERENCES students(id)
);
CREATE TABLE IF NOT EXISTS schedules (
  id TEXT PRIMARY KEY,
  student_id TEXT NOT NULL,
  day_of_week INTEGER NOT NULL,
  time_str TEXT NOT NULL,
  task_desc TEXT NOT NULL,
  is_active INTEGER DEFAULT 1,
  created_at INTEGER NOT NULL,
  FOREIGN KEY (student_id) REFERENCES students(id)
);
CREATE TABLE IF NOT EXISTS device_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  type TEXT NOT NULL,
  payload TEXT NOT NULL,
  timestamp INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_classes_user_id ON classes(user_id);
CREATE INDEX IF NOT EXISTS idx_students_class_id ON students(class_id);
CREATE INDEX IF NOT EXISTS idx_evaluation_records_student_id ON evaluation_records(student_id);
CREATE INDEX IF NOT EXISTS idx_evaluation_records_class_id ON evaluation_records(class_id);

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

INSERT OR IGNORE INTO settings (key, value) VALUES
  ('task_confirm_mode', '"auto"'),
  ('task_confirm_delay', '30'),
  ('firmware_latest_version', '"2.2.0"'),
  ('firmware_download_url', '"/firmware/latest.bin"'),
  ('firmware_checksum', '"dummy_checksum_sha256"'),
  ('teacher_invite_code', '"TEACHER_INVITE"'),
  ('openrouter_api_key', '""'),
  ('openrouter_model', '"openrouter/free"'),
  ('asr_provider', '"workers-ai"'),
  ('groq_api_key', '""'),
  ('baidu_api_key', '""'),
  ('baidu_secret_key', '""'),
  ('screen_brightness', '80'),
  ('screen_sleep_seconds', '15'),
  ('levelConfig', '[40,60,80,100,120,140,160]');
