/**
 * @file sqlite_to_pg.js
 * @brief 班级宠物园 (ClassPetGarden) - SQLite 到 Neon PostgreSQL 全自动数据迁移脚本
 * @note 自动读取本地 pet-garden.db，并在配置了 DATABASE_URL 时一键导入远程 Neon 实例
 */

import Database from 'better-sqlite3'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

const DB_PATH = path.join(__dirname, '../pet-garden.db')
const SQL_SCHEMA_PATH = path.join(__dirname, 'pg_init.sql')
const DATABASE_URL = process.env.DATABASE_URL

// 彩色终端控制
const colors = {
  reset: '\x1b[0m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  red: '\x1b[31m',
  cyan: '\x1b[36m'
}

async function runMigration() {
  console.log(`${colors.cyan}🚀 开始执行 SQLite -> Neon PostgreSQL 数据平滑迁移...${colors.reset}`)

  // 1. 校验环境变量
  if (!DATABASE_URL) {
    console.error(`\n${colors.red}❌ 错误: 未检测到 DATABASE_URL 环境变量！${colors.reset}`)
    console.log(`请配置远程 Neon PostgreSQL 数据库连接串，例如:`)
    console.log(`export DATABASE_URL="postgresql://username:password@ep-dummy-1234.us-east-2.aws.neon.tech/neondb?sslmode=require"`)
    console.log(`然后重新运行: node server/scripts/sqlite_to_pg.js\n`)
    process.exit(1)
  }

  // 2. 检测本地 SQLite 文件
  if (!fs.existsSync(DB_PATH)) {
    console.error(`${colors.red}❌ 错误: 本地 SQLite 数据库文件不存在，路径: ${DB_PATH}${colors.reset}`)
    process.exit(1)
  }

  // 3. 动态加载 pg 库，若未安装则友好提示
  let Client
  try {
    const pgModule = await import('pg')
    Client = pgModule.default.Client
  } catch (err) {
    console.error(`\n${colors.red}❌ 错误: 缺失 PostgreSQL 驱动库 (pg)${colors.reset}`)
    console.log(`请先在 server 目录下安装此库:`)
    console.log(`${colors.yellow}npm install pg${colors.reset}\n`)
    process.exit(1)
  }

  // 4. 连接 PostgreSQL 数据库
  const pgClient = new Client({
    connectionString: DATABASE_URL,
    ssl: {
      rejectUnauthorized: false // 允许 Neon SSL 连接
    }
  })

  try {
    await pgClient.connect()
    console.log(`${colors.green}✅ 成功建立远程 PostgreSQL 连接！${colors.reset}`)
  } catch (err) {
    console.error(`${colors.red}❌ 连接 PostgreSQL 失败:${colors.reset}`, err.message)
    process.exit(1)
  }

  // 5. 读取 pg_init.sql 并初始化表结构
  try {
    console.log('📦 正在远程数据库上创建表结构及优化索引...')
    const schemaSql = fs.readFileSync(SQL_SCHEMA_PATH, 'utf8')
    await pgClient.query(schemaSql)
    console.log(`${colors.green}✅ 表结构及索引初始化成功。${colors.reset}`)
  } catch (err) {
    console.error(`${colors.red}❌ 表结构创建失败:${colors.reset}`, err.message)
    await pgClient.end()
    process.exit(1)
  }

  // 6. 连接 SQLite 数据库
  const sqliteDb = new Database(DB_PATH)
  const tables = [
    'settings',
    'users',
    'classes',
    'students',
    'badges',
    'evaluation_rules',
    'evaluation_records',
    'student_task_applications'
  ]

  console.log('🔄 开始平移各表数据...')

  try {
    for (const table of tables) {
      // a. 查询 SQLite 数据
      const rows = sqliteDb.prepare(`SELECT * FROM ${table}`).all()
      if (rows.length === 0) {
        console.log(`   - 表 [${table}] 在本地为空，跳过。`)
        continue
      }

      console.log(`   - 正在同步 [${table}]，共 ${rows.length} 行数据...`)

      // b. 获取列信息
      const columns = Object.keys(rows[0])
      const colNames = columns.join(', ')
      
      // 先清空目标表（防冲突重复）
      await pgClient.query(`TRUNCATE TABLE ${table} CASCADE`)

      // c. 批量插入到 PostgreSQL (使用参数化查询)
      for (const row of rows) {
        const valPlaceholders = columns.map((_, idx) => `$${idx + 1}`).join(', ')
        const values = columns.map(col => {
          const val = row[col]
          // SQLite 的 boolean 通常是 0/1，在 PG 里由于我们建表用的是 INTEGER/VARCHAR，可保持原样；若是时间戳则保持大整数
          return val
        })

        const insertQuery = `INSERT INTO ${table} (${colNames}) VALUES (${valPlaceholders})`
        await pgClient.query(insertQuery, values)
      }
      
      console.log(`   ${colors.green}✔ 表 [${table}] 同步完毕。${colors.reset}`)
    }

    console.log('\n==================================================')
    console.log(`${colors.green}🎉 恭喜！本地 SQLite 数据已全自动安全导入远程 Neon PostgreSQL！${colors.reset}`)
    console.log('==================================================')
  } catch (err) {
    console.error(`\n${colors.red}❌ 迁移中途出错，已中止:${colors.reset}`, err.message)
  } finally {
    sqliteDb.close()
    await pgClient.end()
  }
}

// 启动迁移
runMigration()
