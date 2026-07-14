// D1 数据库助手 —— 与 server/db.js 的 getAsync/allAsync/runAsync 语义保持一致
// 模块级 DB 由 worker.js 在启动时 bindDb(env.DB) 设置；voice-do.js 用 q.* 显式传 db。

let DB = null

export function bindDb(db) {
  DB = db
}

// 显式传入 db 的底层助手（供 Durable Object 使用，避免模块全局状态冲突）
export const q = {
  async get(db, sql, ...params) {
    const r = await db.prepare(sql).bind(...params).first()
    return r || null
  },
  async all(db, sql, ...params) {
    const r = await db.prepare(sql).bind(...params).all()
    return r.results || []
  },
  async run(db, sql, ...params) {
    const r = await db.prepare(sql).bind(...params).run()
    return { changes: (r.meta && r.meta.changes) || 0, lastInsertRowid: r.meta && r.meta.last_row_id }
  }
}

// 模块级（供 worker.js 的主路由使用）
export async function getAsync(sql, ...params) {
  const r = await DB.prepare(sql).bind(...params).first()
  return r || null
}

export async function allAsync(sql, ...params) {
  const r = await DB.prepare(sql).bind(...params).all()
  return r.results || []
}

export async function runAsync(sql, ...params) {
  const r = await DB.prepare(sql).bind(...params).run()
  return { changes: (r.meta && r.meta.changes) || 0, lastInsertRowid: r.meta && r.meta.last_row_id }
}

// 便捷：读取 settings(key) 的 JSON 值
export async function getSetting(key, fallback = null) {
  const row = await getAsync('SELECT value FROM settings WHERE key = ?', key)
  if (!row) return fallback
  try {
    return JSON.parse(row.value)
  } catch {
    return row.value
  }
}

export async function setSetting(key, value) {
  const v = JSON.stringify(value)
  await runAsync(
    'INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value',
    key,
    v
  )
}

// ============ 设备级配置 (方案 B) ============
// 优先读 device_settings, 回退全局 settings
export async function getDeviceSettings(deviceId, keys) {
  const out = {}
  for (const k of keys) {
    let v = null
    if (deviceId) {
      const row = await getAsync('SELECT value FROM device_settings WHERE device_id = ? AND key = ?', deviceId, k)
      if (row) {
        try { v = JSON.parse(row.value) } catch { v = row.value }
      }
    }
    if (v === null) v = await getSetting(k)
    out[k] = v
  }
  return out
}

export async function setDeviceSetting(deviceId, key, value) {
  if (!deviceId) return
  await runAsync(
    'INSERT INTO device_settings (device_id, key, value, updated_at) VALUES (?, ?, ?, ?) ON CONFLICT(device_id, key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at',
    deviceId,
    key,
    JSON.stringify(value),
    Date.now()
  )
}

// ============ 宠物主人记忆 (owner_profiles) ============
export async function getOwnerProfile(db, studentId) {
  return await q.get(db, 'SELECT * FROM owner_profiles WHERE student_id = ?', studentId)
}

// 把 owner_profiles 行转成一段给 LLM 的上下文文本(画像 + 近期情绪 + 近期学习情况)
export function ownerProfileToContext(profile) {
  if (!profile) return ''
  const parts = []
  if (profile.profile_json) {
    try {
      const p = JSON.parse(profile.profile_json)
      parts.push('【学生画像】' + JSON.stringify(p))
    } catch {}
  }
  if (profile.emotion_log) {
    try {
      const arr = JSON.parse(profile.emotion_log)
      if (Array.isArray(arr) && arr.length) {
        parts.push('【近期情绪】' + JSON.stringify(arr.slice(-5)))
      }
    } catch {}
  }
  if (profile.learning_log) {
    try {
      const arr = JSON.parse(profile.learning_log)
      if (Array.isArray(arr) && arr.length) {
        parts.push('【学习情况】' + JSON.stringify(arr.slice(-5)))
      }
    } catch {}
  }
  return parts.join('\n')
}
