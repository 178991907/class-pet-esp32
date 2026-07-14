-- 班级宠物园 · 功能扩展迁移 (日历 / 清单 / 宠物主人记忆)
-- 闹铃复用已有的 schedules 表(day_of_week + time_str + task_desc)，无需新建。

CREATE TABLE IF NOT EXISTS calendar_events (
  id TEXT PRIMARY KEY,
  student_id TEXT NOT NULL,
  title TEXT NOT NULL,
  event_date TEXT NOT NULL,   -- YYYY-MM-DD
  time_str TEXT,              -- HH:MM 或 NULL(全天)
  description TEXT,
  created_at INTEGER NOT NULL,
  FOREIGN KEY (student_id) REFERENCES students(id)
);
CREATE TABLE IF NOT EXISTS checklist_items (
  id TEXT PRIMARY KEY,
  student_id TEXT NOT NULL,
  content TEXT NOT NULL,
  is_done INTEGER DEFAULT 0,
  created_at INTEGER NOT NULL,
  FOREIGN KEY (student_id) REFERENCES students(id)
);
CREATE TABLE IF NOT EXISTS owner_profiles (
  student_id TEXT PRIMARY KEY,
  profile_json TEXT,          -- JSON: {name, grade, subjects:[], goals:[], notes}
  emotion_log TEXT,           -- JSON 数组: [{ts, emotion, note}]
  learning_log TEXT,          -- JSON 数组: [{ts, subject, mastery, note}]
  updated_at INTEGER,
  FOREIGN KEY (student_id) REFERENCES students(id)
);

CREATE INDEX IF NOT EXISTS idx_calendar_student ON calendar_events(student_id);
CREATE INDEX IF NOT EXISTS idx_calendar_date ON calendar_events(event_date);
CREATE INDEX IF NOT EXISTS idx_checklist_student ON checklist_items(student_id);
