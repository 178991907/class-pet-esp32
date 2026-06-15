-- =========================================================================
-- @file pg_init.sql
-- @brief 班级宠物园 (ClassPetGarden) - Neon PostgreSQL 数据库初始化脚本
-- @note 包含与 SQLite 对应的表结构定义、索引优化及默认评价规则导入
-- =========================================================================

-- 1. 系统设置表
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- 2. 用户表
CREATE TABLE IF NOT EXISTS users (
    id VARCHAR(64) PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    is_guest INTEGER DEFAULT 0,
    created_at BIGINT
);

-- 3. 班级表
CREATE TABLE IF NOT EXISTS classes (
    id VARCHAR(64) PRIMARY KEY,
    user_id VARCHAR(64) REFERENCES users(id) ON DELETE SET NULL,
    name VARCHAR(128) NOT NULL,
    created_at BIGINT,
    updated_at BIGINT
);

-- 4. 学生表 (新增 device_id 通用设备绑定字段)
CREATE TABLE IF NOT EXISTS students (
    id VARCHAR(64) PRIMARY KEY,
    class_id VARCHAR(64) NOT NULL REFERENCES classes(id) ON DELETE CASCADE,
    name VARCHAR(128) NOT NULL,
    student_no VARCHAR(64),
    total_points INTEGER DEFAULT 0,
    pet_type VARCHAR(64),
    pet_level INTEGER DEFAULT 1,
    pet_exp INTEGER DEFAULT 0,
    device_id VARCHAR(64) UNIQUE, -- 通用设备唯一标识索引
    created_at BIGINT
);

-- 5. 徽章表
CREATE TABLE IF NOT EXISTS badges (
    id VARCHAR(64) PRIMARY KEY,
    student_id VARCHAR(64) NOT NULL REFERENCES students(id) ON DELETE CASCADE,
    pet_type VARCHAR(64) NOT NULL,
    earned_at BIGINT
);

-- 6. 评价规则表
CREATE TABLE IF NOT EXISTS evaluation_rules (
    id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(256) NOT NULL,
    points INTEGER NOT NULL,
    category VARCHAR(64) NOT NULL,
    is_custom INTEGER DEFAULT 0,
    created_at BIGINT
);

-- 7. 评价记录表
CREATE TABLE IF NOT EXISTS evaluation_records (
    id VARCHAR(64) PRIMARY KEY,
    class_id VARCHAR(64) NOT NULL REFERENCES classes(id) ON DELETE CASCADE,
    student_id VARCHAR(64) NOT NULL REFERENCES students(id) ON DELETE CASCADE,
    points INTEGER NOT NULL,
    reason TEXT NOT NULL,
    category VARCHAR(64) NOT NULL,
    timestamp BIGINT
);

-- 8. 任务申报表 (阶段一、三核心表)
CREATE TABLE IF NOT EXISTS student_task_applications (
    id VARCHAR(64) PRIMARY KEY,
    student_id VARCHAR(64) NOT NULL REFERENCES students(id) ON DELETE CASCADE,
    task_name VARCHAR(256) NOT NULL,
    points INTEGER NOT NULL,
    status VARCHAR(32) DEFAULT 'pending',
    auto_confirm_at BIGINT,
    created_at BIGINT NOT NULL
);

-- 9. 多音源配置表 (阶段二、三核心表)
CREATE TABLE IF NOT EXISTS music_sources (
    id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    script_code TEXT NOT NULL,
    priority INTEGER DEFAULT 0,
    is_enabled INTEGER DEFAULT 1,
    failure_count INTEGER DEFAULT 0,
    last_failure_at BIGINT,
    created_at BIGINT NOT NULL
);

-- ================= 索引优化 (提升高频 API 响应) =================
CREATE INDEX IF NOT EXISTS idx_classes_user_id ON classes(user_id);
CREATE INDEX IF NOT EXISTS idx_students_class_id ON students(class_id);
CREATE INDEX IF NOT EXISTS idx_badges_student_id ON badges(student_id);
CREATE INDEX IF NOT EXISTS idx_evaluation_records_student_id ON evaluation_records(student_id);
CREATE INDEX IF NOT EXISTS idx_evaluation_records_class_id ON evaluation_records(class_id);
CREATE INDEX IF NOT EXISTS idx_evaluation_records_timestamp ON evaluation_records(timestamp DESC);

-- ================= 导入基础评价规则 =================
INSERT INTO evaluation_rules (id, name, points, category, is_custom, created_at) VALUES
  ('rule_1', '课堂积极发言', 2, '学习', 0, 1704067200000),
  ('rule_2', '作业完成优秀', 3, '学习', 0, 1704067200000),
  ('rule_3', '帮助同学', 2, '行为', 0, 1704067200000),
  ('rule_4', '遵守纪律', 1, '行为', 0, 1704067200000),
  ('rule_5', '迟到', -1, '行为', 0, 1704067200000),
  ('rule_6', '未完成作业', -2, '学习', 0, 1704067200000),
  ('rule_7', '课堂捣乱', -3, '行为', 0, 1704067200000),
  ('rule_8', '主动打扫卫生', 2, '健康', 0, 1704067200000),
  ('rule_9', '坚持运动', 2, '健康', 0, 1704067200000),
  ('rule_10', '不讲卫生', -1, '健康', 0, 1704067200000)
ON CONFLICT (id) DO NOTHING;
