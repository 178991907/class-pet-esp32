-- 班级宠物园 · 闹铃支持多选星期
-- schedules 表从单 day_of_week 扩展为 days_of_week 位掩码（bit 0=周日，bit 1=周一，…，bit 6=周六）

-- 1. 新增位掩码字段，默认 0
ALTER TABLE schedules ADD COLUMN days_of_week INTEGER DEFAULT 0;

-- 2. 把历史单天记录迁移为位掩码：1 << day_of_week
UPDATE schedules SET days_of_week = 1 << day_of_week WHERE days_of_week = 0 OR days_of_week IS NULL;

-- 3. 为 device_events 预留类型，方便设备端触发不同提醒（可选，配合后续功能）
-- ALTER TABLE schedules ADD COLUMN sound_type TEXT DEFAULT 'alarm';
-- 暂不加，先保持最小迁移。
