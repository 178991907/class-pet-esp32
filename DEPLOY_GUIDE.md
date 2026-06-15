# 班级宠物园 (ClassPetGarden) 部署与云端迁移指南

本指南详细说明如何将已经完成了优化和通用硬件升级的班级宠物园系统，从本地部署迁移至云端（**Vercel 托管前端与 API + Neon PostgreSQL 存储数据**）。

---

## 第一部分: Neon PostgreSQL 数据库配置与迁移

由于本地默认使用 SQLite (`pet-garden.db`)，在部署到 Serverless 环境 (Vercel Functions) 时，由于函数是无状态且只读的，必须将数据持久化迁移至云端 PostgreSQL 数据库（本系统选用 **Neon Serverless PostgreSQL** 实例）。

### 1. 创建 Neon PostgreSQL 实例
1. 访问 [Neon 官网 (https://neon.tech/)](https://neon.tech/) 并注册/登录账号。
2. 点击 **Create Project**，输入项目名称（如 `class-pet-garden`），数据库版本选择默认的 PostgreSQL 即可。
3. 创建成功后，控制台会弹出一个 **Connection String**。其格式类似于：
   ```
   postgresql://alex:AbCdEf123456@ep-cool-butterfly-123456.us-east-2.aws.neon.tech/neondb?sslmode=require
   ```
   **复制此连接串，这就是您的 `DATABASE_URL` 环境变量值。**

### 2. 执行一键数据迁移与表结构初始化
我们已经为您编写好了全自动迁移工具。它会自动读取本地 SQLite 表中的数据并同步结构。
1. 进入 `server/` 目录，安装 PostgreSQL 数据库 Node 驱动依赖：
   ```bash
   cd server
   npm install pg
   ```
2. 在您的终端配置上一步复制好的 `DATABASE_URL` 环境变量：
   - **macOS / Linux**：
     ```bash
     export DATABASE_URL="您的Neon连接串"
     ```
   - **Windows (CMD)**：
     ```cmd
     set DATABASE_URL="您的Neon连接串"
     ```
   - **Windows (PowerShell)**：
     ```powershell
     $env:DATABASE_URL="您的Neon连接串"
     ```
3. 执行迁移脚本。它会自动读取 [pg_init.sql](file:///Users/Terry/Downloads/class-pet-main/server/scripts/pg_init.sql) 在远程建表、建立优化的索引，并将您本地的所有班级、学生、积分和音乐源数据全自动安全上云：
   ```bash
   node scripts/sqlite_to_pg.js
   ```
4. 控制台打印 `🎉 恭喜！本地 SQLite 数据已全自动安全导入远程 Neon PostgreSQL！` 即表示数据库准备完成。

---

## 第二部分: Vercel 部署与环境变量配置

### 1. 准备项目
1. 确保将本地的所有代码更新并提交推送到您的个人 GitHub 仓库中。

### 2. 在 Vercel 关联部署
1. 登录 [Vercel 官网 (https://vercel.com/)](https://vercel.com/)。
2. 点击 **Add New** -> **Project**，导入您刚才推送的 `class-pet-garden` 仓库。
3. 在配置面板中：
   - **Framework Preset** 选择 **Vite**（Vercel 会自动识别并应用设置）。
   - **Root Directory** 保持默认的根目录 `./` 即可。

### 3. 配置环境变量 (关键步)
在 **Environment Variables** 展开栏中，配置以下 3 项环境变量：

| 变量名 | 值说明 | 示例值 |
| :--- | :--- | :--- |
| `DATABASE_URL` | 上一步取得的 Neon PostgreSQL 连接串 | `postgresql://alex:...` |
| `TOKEN_SECRET` | 教师管理后台登录 Token 的签名私钥 (防伪造) | `请自己随便输入一串复杂的随机字符串` |
| `DEVICE_SECRET` | 硬件设备与云端防重放 HMAC 通信签名密钥 | `class-pet-device-secret` (需与设备配置一致) |
| `OPENROUTER_API_KEY` | (可选) OpenRouter 大模型 API Key。不配时自动降级正则解析 | `sk-or-v1-...` |

4. 点击 **Deploy** 开始部署。Vercel 会根据我们预设的 [vercel.json](file:///Users/Terry/Downloads/class-pet-main/vercel.json) 分流静态前端页面和 `/api/...` 后端云函数。
5. 部署完成后，Vercel 会为您分配一个域名（如 `https://class-pet-garden.vercel.app`）。

---

## 第三部分: 硬件设备接入与验证

1. **配网指定云端地址**：
   - 当设备开机无法联网并启动配网 AP 热点网页 ([WebPortal.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/WebPortal.h)) 时，在 **API 服务器地址** 框内，填入您上一步部署取得的 Vercel 域名（如 `https://class-pet-garden.vercel.app`）。
   - 在 **通信校验密钥** 框内，输入您在 Vercel 环境变量中配置的 `DEVICE_SECRET`（开发默认是 `class-pet-device-secret`）。
2. **安全与高可用联动验证**：
   - 连接成功后，硬件固件会自动通过 NTP 对时，并使用 `DEVICE_SECRET` 生成带毫秒时间戳的防重放 HMAC-SHA256 签名发送给 Vercel，您可以在 Vercel 控制台的 **Functions Log** 中实时查阅签名解密和安全拦截日志，至此全系统完整打通！
