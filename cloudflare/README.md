# 班级宠物园 · Cloudflare 部署（全免费 · 无 VPS）

把后端跑在 **Cloudflare Worker + Durable Object + D1 + Workers AI**，前端托管在 **Cloudflare Pages**。
完全保留你自己的定制：ESP32 固件、宠物 LVGL 界面、Web 管理后台、数据库结构都不变。

## 为什么可行
- **只有 Cloudflare 在边缘原生支持 WebSocket**（`/ws/voice` 长连接由 Durable Object 持有），Vercel 函数不支持长连接。
- **Workers AI 是 keyless 的**：ASR 用 Whisper、LLM 用 Llama，免 API key；TTS 复用 keyless 的 Google TTS 代理。所以**整个语音链路零密钥**。
- 数据库用 **D1**（Cloudflare 原生 SQLite，SQL 与现有代码一致）。

## 架构
```
ESP32 固件 ──WS──▶ Worker + Durable Object (/ws/voice)
                      ├─ Workers AI: Whisper(ASR) → Llama(LLM) → Google TTS(MP3 代理)
                      ├─ D1: students/classes/evaluations/chat_logs/settings
                      └─ SSE /events (网页实时刷新)
Vue 前台 ──Pages──▶ 同一套 Worker REST API (/auth /classes /students /evaluations /chat ...)
```

## 一、准备
```bash
npm install -g wrangler
wrangler login
```

## 二、创建 D1 数据库
```bash
wrangler d1 create classpet
# 输出里有一行 database_id = "xxxx"，复制它
```
把 `cloudflare/wrangler.toml` 里的 `database_id = "REPLACE_WITH_YOUR_D1_ID"` 换成这个值。

## 三、执行数据库迁移
```bash
wrangler d1 execute classpet --local --file=./migrations/0001_init.sql   # 本地试跑(可选)
wrangler d1 execute classpet --remote --file=./migrations/0001_init.sql  # 远端正式
```
> 迁移会建表、预置 10 条评价规则、写入默认设置。管理员 `admin / admin` 由 Worker 首次访问时自动创建。

## 四、（可选）KV 与 R2
- **KV**：设备 token / 缓存。不填也能跑（wrangler.toml 里把那段注释掉即可）。
- **R2**：存放字体 `cjk16.bin`。不填则 `/device/font/cjk16.bin` 返回 404（固件已内嵌字库，通常不需要）。
```bash
wrangler kv namespace create classpet_kv        # 把 id 填入 wrangler.toml
wrangler r2 bucket create classpet-assets       # 然后把 wrangler.toml 里 r2_buckets 段取消注释
wrangler r2 object put classpet-assets/cjk16.bin --file=../tools/cjk16.bin
```

## 五、部署后端 Worker
```bash
cd cloudflare
wrangler deploy
# 记下输出的地址, 形如 https://classpet-api.<subdomain>.workers.dev
```

## 六、部署前端到 Pages
根目录已有 `pages.toml` 与 `public/_redirects`(SPA 回退)。
在 Cloudflare Pages 控制台或用 CLI：
```bash
# 在 Pages 控制台: 连接仓库, 构建命令 npm run build, 输出目录 dist
# 关键: 在 Build settings 增加 构建环境变量  VITE_API_URL = https://classpet-api.<subdomain>.workers.dev
wrangler pages deploy dist --project-name classpet-web
```
> 前端代码已云感知：`VITE_API_URL` 存在时自动用真实 axios（不走本地 mock），并把 `VITE_API_URL` 作为 `baseURL`。

## 七、设备指向 Worker
在设备配网 / 固件里把 `server_url` 设为 Worker 地址（根目录，不要带 `/api`）：
```
https://classpet-api.<subdomain>.workers.dev
```
- 语音走 `wss://.../ws/voice`（设备可放 `device_id` 在 URL 参数或 `x-device-id` 头）
- HTTP 设备端点：`/device/status`、`/device/heartbeat`、`/device/schedules`、`/device/tts-stream`、`/device/firmware-version`、`/device/font/cjk16.bin`

## 八、本地联调
```bash
# 终端1: 起 Worker(本地 D1)
cd cloudflare && wrangler dev          # 默认 http://localhost:8787
# 终端2: 前端用真实 API
cd .. && echo "VITE_API_URL=http://localhost:8787" > .env
npm run dev
```

## 九、免费额度（教室量级绰绰有余）
- Workers AI：1 万 neurons/天
- Durable Objects：约 300 万请求/月 + 390K GB-s/月
- Workers：10 万请求/天
- D1：免费档 5GB / 500 万行读 / 10 万行写 每天
- Pages：免费

## 十、需要你核对的两点
1. **Workers AI 模型 ID**：代码用的是 `@cf/openai/whisper` 与 `@cf/meta/llama-3.1-8b-instruct`。部署后请到 Cloudflare 控制台 AI > Models 确认这两个 ID 当前可用（偶尔会调整/下线），若不可用改成目录里列出的中文模型即可。
2. **TTS 格式**：设备端走 `{tts, url}` 拉 MP3 播放（与现有固件一致）。Google TTS 返回标准 MP3，国内网络可能偶发不稳，可在 `src/ai.js` 的 `fetchTtsMp3` 增加百度/有道 key 兜底。

## 与原 Node 服务的差异（已处理）
- DB：`pg/better-sqlite3` → D1（`?` 占位符、SQLite 语法，逻辑不变）
- AI：`OpenRouter/Baidu` → Workers AI + Google TTS（免密钥）
- WS：`ws` 库 → Durable Object（`VoiceDO`）
- 实时：`eventBus.js` 进程内事件 → D1 `device_events` 表 + SSE 轮询（`/events`）
- 路由前缀：Worker 自动归一化 `/api`、`/pet-garden/api` 前缀，前端无论是否带前缀都能命中
