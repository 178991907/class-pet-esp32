# 班级宠物园 (ClassPetGarden) 🐾

> 游戏化班级管理与虚拟宠物养成系统，专为教师设计，通过正向激励激发学生的学习与行为动力。

本项目是一个精美的 Web 应用程序，将积分追踪与虚拟宠物成长机制深度结合。现在已适配 **一键部署**，并支持**云端服务器模式**与**纯前端本地存储模式**双模自适应切换！同时支持**通用物联网开发板 (IoT Hardware) 接入**，实现软硬件多模态联动。

---

## 🚀 最新版本：v1.4.2 (硬件重生与会说话的宠物)

- **🎙️ 零延迟流媒体 TTS 合成**：后端新增 `ttsService.js`，无缝对接百度“度丫丫”童声与免费保底接口。单片机直接拉取音频流，实现秒级发声，打通智能语音对话最后一块拼图！
- **🛠️ 修复底层音频通信**：彻底重构 `ESP32Audio.cpp`，铲除错误的音频编解码依赖，重映射麦克风 (`IO8`) 与功放使能 (`IO1`)，硬件 100% 满血复活！

---

## 🚀 历史版本：v1.4.1 (Groq 语音识别版)

- **🎙️ 新增 Groq Whisper ASR 提供商**：完全免费、速度极快的语音识别服务，作为 OpenRouter 余额不足问题的解决方案。
  - `asrService.js` 新增 `recognizeGroq()` 函数，使用 Groq 免费 Whisper Large v3 API。
  - 前端系统管理弹窗新增 Groq 选项（推荐），支持在线配置 Groq API Key。
  - 默认 ASR provider 从 `openrouter` 改为 `groq`。
  - Groq API 兼容 OpenAI 接口格式，支持中文语言识别，注册地址：console.groq.com。

---

## 🚀 历史版本：v1.4.0 (架构重构版)

- **🏗️ 全面架构重构**：对前后端进行系统性拆分，大幅提升代码可维护性和可读性。
- **📦 前端 Home.vue 拆分**：将 3092 行的巨型组件拆分为 349 行的精简入口 + 17 个独立组件 + 3 个 Pinia Store + 2 个 Composable，代码量减少 89%。
  - **Pinia Store**：`useClassStore`、`useStudentStore`、`useSystemStore`，状态管理从组件内部迁移到集中式 Store。
  - **组件化**：12 个模态框组件（班级、学生、评价、宠物选择、排行榜、记录、规则、Logo、聊天审计、日程、学生详情、系统管理）+ 4 个布局组件（AppHeader、StudentCard、LevelUpAnimation、LoadingScreen）。
  - **Composable**：`useLevelUp`（升级动画）、`useConfirm`（确认对话框）。
- **🔧 后端服务层抽取**：将 984 行的 `device.js` 拆分为 620 行路由 + 4 个独立服务模块。
  - `asrService.js`：ASR 语音识别服务（百度/OpenAI/OpenRouter 三 provider）。
  - `nlpService.js`：NLP 意图分类服务（大模型优先 + 正则降级）。
  - `taskService.js`：自动审批懒加载机制。
  - `deviceAuth.js`：设备认证中间件（HMAC 签名 + 防重放）。
  - `settings.js`：通用 Settings 表读写工具。
- **🧹 垃圾文件清理**：删除 25+ 个临时测试文件、补丁文件、`.orig` 备份文件。
- **✅ 构建验证通过**：TypeScript 严格模式检查 + Vite 生产构建零错误，后端所有模块加载验证通过。

---

## 🚀 历史版本：v1.3.5 (紧急修复版)

- **🩹 修复云端 Vercel 崩溃**：修复了 v1.3.1 引入 `multer` 和 `node-fetch` 但遗漏在 `package.json` 依赖中的致命疏忽，这导致了云端部署拉取代码后缺失模块，从而引发全站 API 瘫痪并持续抛出 500 (`FUNCTION_INVOCATION_FAILED`)。现已完全补充依赖。

---

## 🚀 历史版本：v1.3.4 (云端数据库自愈版)

- **☁️ 云端数据库自动升级修复**：修复了在 Vercel 等平台部署的 PostgreSQL 云端数据库中，对老旧版本数据表未能自动追加 `device_id` 硬件标识字段的致命漏洞。该漏洞会导致较早部署过本项目的服务器在接收硬件连网诊断（`/status` 接口）时引发 SQL 执行错误并抛出 `HTTP 500`。现已加入云端建表自适应补齐机制，保障硬件新老版本数据向上兼容无缝接入。

---

## 🚀 历史版本：v1.3.3 (硬件全面修复版)

- **🎙️ 彻底修复语音申报 400 与 500 报错**：修复代理或反向代理篡改 `Content-Type` 导致后端 `express.raw` 抛弃请求的 Bug（放宽匹配至 `*/*`）；同时增加了基于设备的未绑定学生非法请求阻断，消除了 500 空指针异常崩溃。
- **🍅 番茄时钟彻底重构修复**：移除了心跳刷新状态同步导致番茄时钟状态异常闪退主屏的严重 Bug，消除了倒计时期间界面的按键延迟与失灵；后端增加提供生成的 1000Hz `.wav` 音源，解决了番茄钟结束后没有提示音的报错现象。

---

## 🚀 历史版本：v1.3.1 (语音修复与卡顿重构版)

- **🎙️ 语音申报底层重构**：修复由于 `express.raw` 缺少正确的内容类型挂载与设备端鉴权签名中间件执行时序错乱导致的云端 400 报错。现在设备端传送的原始 `.wav` 录音流将完美命中后端流解析。
- **🚀 零阻塞状态机重构**：彻底解耦了番茄钟倒计时结束时触发的死机长延时（移除了导致 DNS 解析堵塞的占位音频），重置了网络同步时钟以确保退出专注状态时能瞬间返回正常桌面而不再卡死 3~5 秒。

---

## 🚀 历史版本：v1.3.0 (UI 与功能全面进化版)

## 🚀 历史版本：v1.2.0 (Caturda ESP32 硬件专属优化版)

- **🔋 工业级电量监测与滤波**：内置真实 3.7V Li-Po 电池查表放电曲线，加入多重 IIR 低通滤波算法，彻底解决插拔电时电量百分比大幅跳变、颜色闪烁的问题。
- **🔌 智能电源管理 (息屏休眠)**：增加底层 30 秒触控防静置自动息屏机制。黑屏进入极低功耗模式，不仅大幅延长续航，更能让 USB 充电速度获得几何级提升。
- **📱 触控抗锯齿与 LVGL 修复**：完美修正 LVGL 9.5 图形库中字体加载引发的内存崩溃，修复屏幕渲染反色 (BGR/RGB) 与文字溢出排版错位问题。

---

## 🌟 核心特色

- **🐱 丰富宠物系统**：内置 25 种可爱宠物（18 种普通宠物 + 7 种珍稀神兽），分为 8 个成长等级（Lv.1 ~ Lv.8 毕业）。
- **🏆 荣誉徽章体系**：宠物成长到满级（Lv.8）后即可“毕业”，学生将荣获专属的宠物毕业徽章。
- **📈 智能积分系统**：积分按“学习”、“行为”、“健康”、“其他”四大维度划分。加分自动转化为宠物经验，扣分自动扣除相应经验，等级自适应升级与降级。
- **📊 实时排行榜**：一键查看班级学生的积分与宠物等级排行，支持徽章数量展示，激发良性竞争。
- **🔄 双模自适应架构**：
  - **本地存储模式（默认）**：无需任何后端服务器与数据库，所有数据均保存在浏览器本地（LocalStorage），支持一键备份与恢复。
  - **云端服务器模式**：配置环境变量后自动切换，与云端 Express + SQLite/PostgreSQL API 进行数据同步，支持多终端协作。
- **🔌 通用物联网硬件接入 (IoT)**：支持任意具备 WiFi 能力的开发板（如 ESP32-S3-2432S028R 黄板）安全接入，支持设备端语音交互申报、本地番茄钟倒计时，实现离线缓存降级。
- **🤖 OpenRouter 大模型与语音意图解析 (v1.1.0)**：后端 Fetch 完美对接 OpenRouter 大模型，智能解析加分申报、积分查询，并自动利用大模型智能提取日程任务。网页端可热拔插绑定 API Key，开发板无需重刷固件。
- **📅 智能定时日程表与 TTS 播报 (v1.1.0)**：支持设置周几、具体时间定时日程（在网页端设定或由设备端语音智能录入），到点后小喇叭先播放提示音，再由 TTS 语音大声朗读具体的日程任务。
- **💬 对话历史保存与滚动审计 (v1.1.0)**：每次学生使用大模型互动都会自动成对存入数据库，可在网页后台以精美对话气泡流审计，自动滑动清理 30 天前的聊天记录保护隐私。
- **🔋 锂电池充放电与在线心跳监控 (v1.1.0)**：支持外接锂电池电量采集，充电时网页和硬件屏幕自动展现 `⚡ 充电图标` 与百分比，放电时展现 `🔋 电池图标`，配合**绿色在线指示呼吸灯**实时监控设备状态。
- **🔆 屏幕亮度与无动作自动熄屏设置 (v1.1.0)**：可从网页配置背光亮度（PWM 控制）与熄屏秒数（5s, 10s, 30s 等），大幅降低设备发热并提升待机续航。
- **🇨🇳 简体中文屏幕报错反馈 (v1.1.0)**：设备在网络连接失败或 API 报 403 / 500 等错误时，TFT 屏幕将直接以红色显著的简体中文警告框弹出错误原因，省去连接串口线调试的烦恼。

---

## 🛠️ 技术栈

### 前端 (Frontend)
- **核心框架**：Vue 3 (SFC, `<script setup>`) + TypeScript (严格模式)
- **构建工具**：Vite
- **样式方案**：Tailwind CSS + CSS 变量自定义主题
- **路由管理**：Vue Router (HTML5 History 模式)

### 后端 (Backend)
- **运行环境**：Node.js + Express
- **数据库**：SQLite (`better-sqlite3`) / Serverless PostgreSQL (Neon)
- **执行沙箱**：`isolated-vm` / Node 原生 `vm` 超时沙箱隔离（安全解析教师上传的多音源脚本）

### 硬件设备端 (Firmware)
- **开发框架**：Arduino (C++) 通用嵌入式框架
- **加密基础**：基于 mbedtls 硬件加密引擎的 HMAC-SHA256 签名
- **网络协议**：Captive Portal 门户劫持网页配网 + NTP 网络授时服务

---

## 💻 本地运行与开发

### 1. 克隆项目
```bash
git clone https://github.com/178991907/class-pet.git
cd class-pet
```

### 2. 纯前端模式运行
1. 安装依赖：
   ```bash
   npm install
   ```
2. 启动前端开发服务器：
   ```bash
   npm run dev
   ```
3. 打开浏览器访问 `http://localhost:3001`，开始体验本地存储版。

### 3. 前后端联调运行
1. 启动前后端服务（后端端口 3002，使用本地 SQLite `server/pet-garden.db`）：
   ```bash
   npm run start
   ```
2. 在浏览器中打开 `http://localhost:3001`，数据会保存在本地 SQLite 数据库中。

---

## 🔌 通用物联网开发板烧录与调试

### 1. 设备端固件位置
设备端烧录所需的全部 C++ 源码文件均存放于本项目的 **[firmware/ClassPetDevice/](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/)** 目录下。
- **核心文件说明**：
  - [ClassPetDevice.ino](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ClassPetDevice.ino)：主入口，包含核心状态机和主循环。
  - [Config.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Config.h)：全局配置文件（包含重试时间、时间服务器及默认密钥）。
  - [Storage.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Storage.cpp)：EEPROM 持久配置存储与离线重发队列。
  - [Network.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Network.cpp)：WiFi 自动连接、NTP 授时时间戳同步与 AP 配网服务器。
  - [WebPortal.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/WebPortal.h)：毛玻璃风 AP 配网静态网页。
  - [ApiClient.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ApiClient.cpp)：带 HMAC-SHA256 毫秒时间戳防重放签名的 HTTP/HTTPS 通信客户端。
  - [TomatoTimer.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/TomatoTimer.cpp)：本地离线番茄计时器。
  - [DisplayHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/DisplayHAL.h) / [AudioHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/AudioHAL.h)：显示及音频驱动的移植接口契约（默认开启虚拟串口模拟器）。
- **烧录操作**：您只需使用 **Arduino IDE** 或 **PlatformIO** 打开 `ClassPetDevice.ino`。在 `DisplayHAL.h` 和 `AudioHAL.h` 的子类中实现具体引脚的绘图及播放逻辑，然后连接开发板点击上传烧录即可。

### 2. 硬件网络模拟器调试 (无物理硬件时推荐)
我们提供了一个全功能命令行模拟器，可模拟设备在连接、配网、签名通信、断网、番茄钟、熔断降级等各种极端环境下的状态：
```bash
node server/scripts/device_simulator.js
```
启动后可在终端根据彩色菜单提示，模拟向本地 API 发送带签名的测试数据。

### 3. 后端 API 自动化集成测试
我们编写了自动化集成测试脚本，可对后端的防重放时间戳签名、自动审核懒加载结算、以及音乐源熔断做完整校验：
```bash
node server/scripts/auto_test_device.js
```

---

## 📦 项目结构说明

```
class-pet/
├── src/                      # 前端源码
│   ├── main.ts              # 前端入口
│   ├── pages/Home.vue       # 系统主页（业务 UI 与设备管理审批控制台）
├── server/                   # 后端源码
│   ├── index.js             # 后端 API 入口
│   ├── db.js                # 数据库结构与初始化 (支持 SQLite & PostgreSQL)
│   ├── routes/
│   │   └── device.js        # 通用硬件专属控制、高可用音源沙箱与防重放 API
│   └── scripts/
│       ├── auto_test_device.js # 自动化硬件通信集成测试脚本
│       ├── device_simulator.js # 高交互式硬件串口及网络模拟器
│       ├── pg_init.sql      # Neon PostgreSQL 建表与索引优化脚本
│       └── sqlite_to_pg.js  # 一键本地数据平滑迁移上云 (PostgreSQL) 脚本
├── firmware/                 # 物联网开发板通用 C++/Arduino 固件工程
│   ├── README.md            # 固件硬件移植与对时加密机制说明文档
│   └── ClassPetDevice/      # 固件项目目录 (含 .ino 主程序及 HAL 外设抽象层)
├── DEPLOY_GUIDE.md          # 详细的 Neon PostgreSQL 迁移与 Vercel 部署配置指南
├── vercel.json              # Vercel Serverless Functions 路由分发配置文件
└── package.json             # 项目依赖配置
```

---

## 📄 开源协议与版权致谢

1. 本项目基于 [MIT License](LICENSE) 协议开源。
2. 特此向原开源项目 [ku-class-pet-garden](https://github.com/178991907/ku-class-pet-garden) 及其贡献者致以最诚挚的谢意。本仓库所做出的全部二次优化和重构功能，同样遵循开源精神与社区共享。
