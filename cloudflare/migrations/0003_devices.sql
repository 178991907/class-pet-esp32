-- 方案 B: 设备独立表 + 学生与设备解耦
-- 每个物理终端(device_id = MAC)是独立实体, 可单独配置, 通过 student_id 关联学生(可空 = 未绑定)
-- 设备解析全部走 devices 表, students.device_id 仅作为冗余镜像, 保证向后兼容

CREATE TABLE IF NOT EXISTS devices (
  device_id      TEXT PRIMARY KEY,           -- 物理终端唯一标识 (MAC)
  name           TEXT,                        -- 设备别名, 如 "1号座位机"
  student_id     TEXT,                        -- 绑定学生 (可空 = 未绑定)
  class_id       TEXT,                        -- 所属班级 (便于权限筛选)
  firmware_version TEXT,                      -- 固件版本
  pet_type       TEXT,                        -- 当前宠物
  pet_level      INTEGER DEFAULT 1,
  battery_level  INTEGER DEFAULT 100,
  is_charging    INTEGER DEFAULT 0,
  last_seen      INTEGER,
  created_at     INTEGER,
  FOREIGN KEY (student_id) REFERENCES students(id) ON DELETE SET NULL,
  FOREIGN KEY (class_id) REFERENCES classes(id)
);
CREATE INDEX IF NOT EXISTS idx_devices_student ON devices(student_id);
CREATE INDEX IF NOT EXISTS idx_devices_class ON devices(class_id);

-- 设备级配置(覆盖全局 settings)
CREATE TABLE IF NOT EXISTS device_settings (
  device_id  TEXT NOT NULL,
  key        TEXT NOT NULL,
  value      TEXT NOT NULL,
  updated_at INTEGER,
  PRIMARY KEY (device_id, key),
  FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
);

-- 从已有 students.device_id 迁移: 为每个已绑定设备创建 devices 行 (幂等)
INSERT OR IGNORE INTO devices (device_id, student_id, class_id, pet_type, pet_level, battery_level, is_charging, last_seen, created_at)
SELECT device_id, id, class_id, pet_type, pet_level, COALESCE(battery_level, 100), COALESCE(is_charging, 0), last_seen, created_at
FROM students
WHERE device_id IS NOT NULL AND TRIM(device_id) <> ''
  AND UPPER(device_id) NOT IN (SELECT UPPER(device_id) FROM devices);
