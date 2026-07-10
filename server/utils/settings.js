// 通用 settings 表读写工具
// 消除 device.js 中大量重复的 SELECT/INSERT/UPDATE 模式

import { getAsync, runAsync } from '../db.js'

/**
 * 从 settings 表读取一个值 (自动 JSON.parse)
 * @param {string} key - settings key
 * @param {*} defaultValue - 未找到时的默认值
 * @returns {*} 解析后的值或默认值
 */
export async function getSetting(key, defaultValue = '') {
  const row = await getAsync('SELECT value FROM settings WHERE key = ?', key)
  return row ? JSON.parse(row.value) : defaultValue
}

/**
 * 向 settings 表写入一个值 (自动 JSON.stringify，支持 upsert)
 * @param {string} key - settings key
 * @param {*} value - 要写入的值
 */
export async function setSetting(key, value) {
  const jsonValue = JSON.stringify(value)
  const existing = await getAsync('SELECT key FROM settings WHERE key = ?', key)
  if (existing) {
    await runAsync('UPDATE settings SET value = ? WHERE key = ?', jsonValue, key)
  } else {
    await runAsync('INSERT INTO settings (key, value) VALUES (?, ?)', key, jsonValue)
  }
}

/**
 * 批量读取多个 settings key
 * @param {string[]} keys - 要读取的 key 数组
 * @param {object} defaults - { key: defaultValue } 映射
 * @returns {object} { key: value } 映射
 */
export async function getSettings(keys, defaults = {}) {
  const result = {}
  for (const key of keys) {
    result[key] = await getSetting(key, defaults[key] !== undefined ? defaults[key] : '')
  }
  return result
}

/**
 * 批量写入 settings (仅写入 req.body 中存在的字段)
 * @param {object} body - 请求体，包含要写入的字段
 * @param {string[]} allowedKeys - 允许写入的 key 列表
 * @param {object} transforms - { key: (val) => transformedVal } 可选的值转换函数
 */
export async function setSettings(body, allowedKeys, transforms = {}) {
  for (const key of allowedKeys) {
    if (body[key] !== undefined) {
      const val = transforms[key] ? transforms[key](body[key]) : body[key]
      await setSetting(key, val)
    }
  }
}
