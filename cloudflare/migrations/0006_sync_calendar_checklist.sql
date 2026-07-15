-- 为已有日历事件补创建关联清单项，使旧日历安排也能出现在每日清单中
INSERT INTO checklist_items (id, student_id, content, is_done, target_date, source_id, source_type, created_at)
SELECT
  lower(hex(randomblob(16))),
  ce.student_id,
  ce.title,
  0,
  ce.event_date,
  ce.id,
  'calendar_events',
  ce.created_at
FROM calendar_events ce
LEFT JOIN checklist_items ci ON ci.source_id = ce.id AND ci.source_type = 'calendar_events'
WHERE ci.id IS NULL;
