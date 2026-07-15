-- 班级宠物园 · 清单与日历事件同步
-- checklist_items 增加目标日期与来源字段，使「清单」成为每日待办统一视图

ALTER TABLE checklist_items ADD COLUMN target_date TEXT;
ALTER TABLE checklist_items ADD COLUMN source_id TEXT;
ALTER TABLE checklist_items ADD COLUMN source_type TEXT;

-- 历史数据回填：按 GMT+8 把 created_at 转成日期
UPDATE checklist_items
SET target_date = date(created_at / 1000, 'unixepoch', '+8 hours')
WHERE target_date IS NULL;
