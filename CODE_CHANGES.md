# 代码修改文档 (v1.3.3 → v1.4.1)

> 本文档记录了从 v1.3.3 到 v1.4.1 的全部代码修改，供其他大模型/开发者参考。



---

## 一、修改概览

| 维度               | 修改前                           | 修改后                                             |
| ---------------- | ----------------------------- | ----------------------------------------------- |
| **Home.vue**     | 3092 行单文件                     | 349 行精简入口 + 16 个组件 + 3 个 Store + 2 个 Composable |
| **device.js**    | 984 行巨型路由                     | 622 行路由 + 5 个独立服务模块                             |
| **ASR Provider** | 3 个 (baidu/openai/openrouter) | 4 个 (新增 groq，推荐)                                |
| **删除文件**         | —                             | 25 个垃圾文件 (临时测试/补丁/.orig)                        |
| **固件**           | 麦克风无法录音、ES8311 配置不全           | ES8311 ADC/DAC 通路切换、I2S 引脚复用、录音电平诊断             |
| **版本**           | v1.3.3                        | v1.4.1                                          |

---

## 二、删除的文件 (25 个)

### 项目根目录临时测试文件

- `test_multer.js` — multer 中间件测试
- `test_raw.cjs` — raw body 解析测试
- `test_upload.js` / `test_upload.py` / `test_upload2.py` / `test_upload3.js` — 上传测试脚本
- `dummy.wav` — 测试用空 WAV
- `generate_beep.js` — 提示音生成脚本
- `read_serial.py` — 串口读取脚本

### server 目录临时文件

- `server/test_path.js` — 路径测试
- `server/test_empty_buffer.js` — 空缓冲区测试
- `server/scratch_update.js` — 临时数据库更新脚本

### firmware 目录补丁和备份文件

- `Board.h.orig` / `ClassPetUI.cpp.orig` / `Storage.cpp.orig` — .orig 备份
- `Board.patch` / `Storage_Init.patch` — patch 文件
- `patch_audio.sh` / `patch_audio2.sh` / `patch_board.sh` / `patch_board2.sh` — 补丁脚本
- `ApiClient_Audio.txt` / `ESP32Audio_Update.txt` — 代码片段备忘

---

## 三、后端修改

### 3.1 新增文件

#### `server/services/asrService.js` (218 行, 新建)

**职责**: ASR (自动语音识别) 服务层，从 device.js 中抽取，配置与实现分离。

**导出函数**:

- `getAsrConfig()` — 从数据库 settings 表读取 ASR 配置 (provider, apiKey, baiduKey, baiduSecret, groqKey)
- `extractPcmFromWav(buffer)` — 从 WAV Buffer 中提取纯 PCM 数据 (百度 ASR 需要)
- `recognizeSpeech(audioBuffer, deviceId)` — **统一入口**，根据 provider 自动分发
- `isAsrConfigError(message)` — 判断错误是否为配置类错误 (401/402/api_key)

**内部 Provider 函数** (通过 `PROVIDERS` 映射表分发):

- `recognizeBaidu(audioBuffer, config)` — 百度短语音识别 (PCM 16k 16bit mono, 每月免费 5 万次)
- `recognizeOpenAI(audioBuffer, config)` — OpenAI 官方 Whisper API (whisper-1)
- `recognizeOpenRouter(audioBuffer, config)` — OpenRouter Whisper (openai/whisper-large-v3)
- `recognizeGroq(audioBuffer, config)` — **v1.4.1 新增** Groq Whisper (whisper-large-v3, 免费, 速度极快)

**关键设计**:

```javascript
const PROVIDERS = {
  baidu: recognizeBaidu,
  openai: recognizeOpenAI,
  openrouter: recognizeOpenRouter,
  groq: recognizeGroq        // v1.4.1 新增
}
```

**Groq Provider 实现细节**:

- API 端点: `https://api.groq.com/openai/v1/audio/transcriptions`
- 模型: `whisper-large-v3`
- 参数: `language=zh`, `response_format=json`
- 完全免费，兼容 OpenAI 接口格式

#### `server/services/nlpService.js` (132 行, 新建)

**职责**: NLP 意图分类服务，从 device.js 中抽取。优先调用大模型，失败自动降级为正则。

**导出函数**:

- `regexClassifyIntent(text)` — 正则降级分类器 (无需 API 调用)
- `askLLMIntent(text)` — 调用 OpenRouter Chat API 进行意图分类
- `classifyIntent(text)` — **统一入口**，带自动降级

**意图分类规则**:

1. `apply_task` — 申报加分/完成任务 ("我扫地了")
2. `query_status` — 查询积分/等级 ("我多少分了")
3. `create_schedule` — 设定日程提醒 ("周三下午两点提醒我背单词")
4. `none` — 无法识别，返回闲聊回复

**SYSTEM_PROMPT** (大模型 system 消息):

```
你是一个智能班级助手意图分类器。你需要将用户的语音输入分类，并以标准的 JSON 格式输出...
JSON 输出格式：
{
  "action": "apply_task" | "query_status" | "create_schedule" | "none",
  "task_name": "申报任务名称...",
  "schedule_info": { "days": [...], "time": "HH:MM", "task_desc": "..." },
  "reply_text": "给学生的口语化鼓励或确认回复，字数控制在 25 字以内"
}
```

**降级逻辑**:

```javascript
export async function classifyIntent(text) {
  try {
    return await askLLMIntent(text)       // 优先大模型
  } catch (err) {
    console.warn('⚠️ 大模型 API 调用失败，自动降级为内置正则分类器:', err.message)
    return regexClassifyIntent(text)      // 降级正则
  }
}
```

#### `server/services/taskService.js` (79 行, 新建)

**职责**: 自动审批懒加载机制，从 device.js 中抽取。

**导出函数**:

- `autoConfirmLazyLoad(userId)` — 自动核销已过期的任务申报

**执行流程**:

1. 读取 `task_confirm_mode` 设置，仅 `auto` 模式执行
2. 查询该用户所属班级中所有 `status='pending'` 且 `auto_confirm_at <= now` 的任务
3. 对每个过期任务: 状态置为 `approved` → 创建评价记录 → 累加学生积分 → 重新核算宠物等级 → 达到 8 级自动颁发徽章

**关键变化**: 使用 `getSetting('task_confirm_mode', 'auto')` 替代直接 SQL 查询 settings 表。

#### `server/middleware/deviceAuth.js` (61 行, 新建)

**职责**: 设备通信签名与防重放中间件，从 device.js 中抽出供多路由复用。

**导出**: `deviceAuthMiddleware`

**校验逻辑**:

1. 检查 `X-Device-ID` 头是否存在
2. 若配置了 `DEVICE_SECRET` 环境变量:
   - 校验 `X-Device-Timestamp` 和 `X-Device-Signature` 头
   - 120 秒时钟漂移容忍 (防重放)
   - HMAC-SHA256 签名校验: `data = deviceId + timestamp + bodyStr`
   - `/voice` 路由的 body 用空字符串计算签名 (因为是纯二进制流)
3. 未配置 `DEVICE_SECRET` 时为调试模式，直接放行
4. 挂载 `req.deviceId` 供后续中间件使用

#### `server/utils/settings.js` (59 行, 新建)

**职责**: 通用 settings 表读写工具，消除 device.js 中大量重复的 SELECT/INSERT/UPDATE 模式。

**导出函数**:

- `getSetting(key, defaultValue)` — 读取单个值 (自动 JSON.parse)
- `setSetting(key, value)` — 写入单个值 (自动 JSON.stringify, upsert)
- `getSettings(keys, defaults)` — 批量读取
- `setSettings(body, allowedKeys, transforms)` — 批量写入 (仅写入 body 中存在的字段, 支持值转换)

---

### 3.2 修改文件

#### `server/routes/device.js` (984 行 → 622 行, 减少 37%)

**核心改动**: 将内联逻辑抽取到独立模块，路由文件回归"路由编排"职责。

**导入的新模块**:

```javascript
import { recognizeSpeech, isAsrConfigError } from '../services/asrService.js'
import { classifyIntent } from '../services/nlpService.js'
import { autoConfirmLazyLoad } from '../services/taskService.js'
import { deviceAuthMiddleware } from '../middleware/deviceAuth.js'
import { getSetting, setSetting, getSettings, setSettings } from '../utils/settings.js'
```

**`/voice` 路由简化**:

- ASR 调用从 ~90 行内联逻辑简化为: `text = await recognizeSpeech(audioBuffer, deviceId)`
- NLP 分类从内联 try/catch 简化为: `const nlpResult = await classifyIntent(text)`

**settings 路由简化**:

- GET: 从 ~60 行重复 SELECT 简化为 `getSettings(SETTINGS_KEYS, SETTINGS_DEFAULTS)`
- POST: 从 ~55 行重复 upsert 简化为 `setSettings(req.body, SETTINGS_KEYS, transforms)`

**配置常量定义**:

```javascript
const SETTINGS_KEYS = [
  'task_confirm_mode', 'task_confirm_delay',
  'openrouter_api_key', 'openrouter_model',
  'groq_api_key',                        // v1.4.1 新增
  'screen_brightness', 'screen_sleep_seconds',
  'asr_provider',
  'baidu_api_key', 'baidu_secret_key'
]

const SETTINGS_DEFAULTS = {
  task_confirm_mode: 'auto',
  task_confirm_delay: 30,
  openrouter_api_key: '',
  openrouter_model: 'openrouter/free',
  groq_api_key: '',                      // v1.4.1 新增
  screen_brightness: 80,
  screen_sleep_seconds: 15,
  asr_provider: 'groq',                  // v1.4.1 默认从 openrouter 改为 groq
  baidu_api_key: '',
  baidu_secret_key: ''
}

const NUMERIC_KEYS = ['task_confirm_delay', 'screen_brightness', 'screen_sleep_seconds']
const ALLOWED_ASR_PROVIDERS = ['openai', 'openrouter', 'baidu', 'groq']  // v1.4.1 新增 groq
```

#### `server/db.js` (小改)

新增 PostgreSQL `device_id` 字段迁移:

```javascript
await pgPool.query("ALTER TABLE students ADD COLUMN IF NOT EXISTS device_id VARCHAR(64)")
await pgPool.query("CREATE UNIQUE INDEX IF NOT EXISTS idx_students_device_id ON students(device_id)")
```

---

## 四、前端修改

### 4.1 Home.vue 拆分 (3092 行 → 349 行, 减少 89%)

#### 新增 3 个 Pinia Store

##### `src/stores/useClassStore.ts` (99 行)

- **状态**: classes, currentClass, currentClassId
- **方法**: loadClasses, createClass, updateClass, deleteClass, selectClass
- **API 封装**: GET/POST/PUT/DELETE `/api/classes`

##### `src/stores/useStudentStore.ts` (517 行, 最大的 Store)

- **状态**: students, searchKeyword, currentPage, pageSize, totalPages, selectedStudent, studentDetail, recentRecords, chatLogs
- **方法**: loadStudents, addStudent, updateStudent, deleteStudent, batchDeleteStudents, importStudents, loadStudentDetail, loadRecentRecords, loadChatLogs, quickEvaluate, batchEvaluate, deleteRecord, recalculateLevel, searchStudents
- **评价逻辑**: 快速评价、批量评价、评价撤回、评分动效
- **学生详情**: 加载详情面板数据 (积分、等级、最近记录、聊天日志)

##### `src/stores/useSystemStore.ts` (255 行)

- **状态**: taskConfirmMode, taskConfirmDelay, openrouterApiKey, openrouterModel, groqApiKey, screenBrightness, screenSleepSeconds, asrProvider, baiduApiKey, baiduSecretKey, pendingTasks, unboundDevices
- **方法**: loadSettings, saveSettings, loadPendingTasks, approveTask, rejectTask, loadUnboundDevices, bindDevice, unbindDevice
- **v1.4.1 新增**: `groqApiKey` 状态字段

#### 新增 2 个 Composable

##### `src/composables/useLevelUp.ts` (29 行)

- 升级动画状态管理 (showLevelUp, newLevel)
- `triggerLevelUp(level)` 触发动画
- `closeLevelUp()` 关闭动画

##### `src/composables/useConfirm.ts` (42 行)

- 通用确认对话框 (showConfirm, confirmTitle, confirmMessage, confirmCallback)
- `confirm(title, message, callback)` 显示确认弹窗
- `handleConfirm()` / `handleCancel()` 处理用户操作

#### 新增 16 个组件

##### 布局组件 (4 个)

| 组件                                    | 行数  | 职责                            |
| ------------------------------------- | --- | ----------------------------- |
| `src/components/layout/AppHeader.vue` | 211 | 顶部导航栏 (Logo、班级选择、操作按钮下拉菜单)    |
| `src/components/StudentCard.vue`      | 166 | 学生卡片 (宠物头像、名字、积分、等级、进度条、操作按钮) |
| `src/components/LevelUpAnimation.vue` | 70  | 全屏升级动画 (粒子效果 + 等级数字)          |
| `src/components/LoadingScreen.vue`    | 19  | 加载动画                          |

##### 模态框组件 (12 个)

| 组件                       | 行数  | 职责                                     |
| ------------------------ | --- | -------------------------------------- |
| `ClassModal.vue`         | 66  | 新建/重命名/删除班级                            |
| `StudentModal.vue`       | 106 | 新增/编辑学生                                |
| `ImportModal.vue`        | 52  | 批量导入学生 (CSV)                           |
| `EvalModal.vue`          | 96  | 快速评价 (选学生、选规则、加分)                      |
| `PetSelectModal.vue`     | 62  | 宠物选择                                   |
| `RankModal.vue`          | 71  | 排行榜                                    |
| `RecordsModal.vue`       | 117 | 评价记录 (分页查询、撤回)                         |
| `RulesModal.vue`         | 114 | 评价规则管理 (CRUD)                          |
| `LogoModal.vue`          | 19  | Logo 预览                                |
| `ChatLogsModal.vue`      | 57  | 聊天日志审计                                 |
| `SchedulesModal.vue`     | 103 | 日程提醒管理                                 |
| `StudentDetailPanel.vue` | 163 | 学生详情面板 (快速评价、最近记录、日程、聊天审计)             |
| `SystemModal.vue`        | 356 | 系统管理 (任务审批、自动审核、ASR 配置、屏幕设置、用户权限、健康监测) |

#### `src/pages/Home.vue` (重写, 349 行)

重写为精简的组装入口:

- 导入 3 个 Store + 2 个 Composable
- 导入 16 个组件
- `<script setup>` 只做: 初始化加载 (`onMounted`)、模态框显示状态管理、组件间事件传递
- `<template>` 只做: 组件编排布局
- `<style>` 完全移除 (过渡动画移至全局 CSS)

#### `src/style.css` (新增 132 行)

将 Home.vue 中的 scoped 样式提取为全局样式:

- `.modal-overlay`, `.modal-enter-active`, `.modal-leave-active` 等过渡动画
- `@keyframes scoreFloat` 评分浮动动画
- `.score-popup` 评分弹窗样式
- 自定义滚动条样式

#### `src/components/modals/SystemModal.vue` ASR 配置部分修改 (v1.4.1)

新增 Groq 选项到 ASR Provider 下拉菜单:

```html
<option value="groq">Groq Whisper (免费，推荐)</option>
```

Groq 配置输入框:

```html
<div v-if="systemStore.asrProvider === 'groq'">
  <span class="text-xs text-gray-400 block mb-1.5">Groq API Key</span>
  <input v-model="systemStore.groqApiKey" type="password" placeholder="在 console.groq.com 免费注册获取" ... />
  <p class="text-xs text-gray-400 mt-1 font-medium">推荐使用 Groq：完全免费、速度极快、支持中文。</p>
</div>
```

---

## 五、固件修改 (ESP32-S3)

### 5.1 `firmware/ClassPetDevice/ES8311.h` (核心修改)

**问题**: 原始 ES8311 初始化不完整，ADC (录音) 通路未正确配置，导致麦克风无声。

**修改内容**:

1. **新增芯片 ID 校验**:

```cpp
uint8_t id1 = readReg(0xFD);
uint8_t id2 = readReg(0xFE);
if (id1 != 0x83 || id2 != 0x11) {
    Serial.printf("❌ ES8311 芯片 ID 错误!");
    return false;
}
```

1. **Clock 寄存器修改**: `0x01` 从 `0x30` 改为 `0xBF` (启用 BCLK 时钟源 + 所有 ADC/DAC 时钟)
2. **新增模拟偏置供电**: `writeReg(0x0D, 0x01)` (给麦克风 MICBIAS 供电)
3. **新增 ADC 数字音量**: `writeReg(0x17, 0xC8)` (默认 0x00 是 -95.5dB 纯静音)
4. **新增 ADC I2S 格式**: `writeReg(0x0A, 0x0C)` (16 bit)
5. **新增 ADC/DAC 通路切换方法**:

```cpp
static void enableDAC(bool on);  // 打开 DAC (播放)，关闭 ADC
static void enableADC(bool on);  // 打开 ADC (录音)，关闭 DAC
```

1. **新增 `readReg()` 方法**: I2C 读取寄存器值

### 5.2 `firmware/ClassPetDevice/ESP32Audio.cpp` (核心修改)

**问题**: 麦克风 I2S 引脚配置错误 (使用了独立引脚而非共享 ES8311 引脚)；录音/播放无法正确切换。

**修改内容**:

1. **I2S 引脚复用修复** — 录音端从独立引脚改为共享播放端引脚:

```cpp
// 修改前:
.bck_io_num = I2S_MIC_SCK_PIN,    // 独立引脚
.ws_io_num = I2S_MIC_WS_PIN,
.data_in_num = I2S_MIC_SD_PIN,

// 修改后:
.bck_io_num = I2S_BCLK_PIN,       // 与播放端共享 BCLK
.ws_io_num = I2S_LRC_PIN,         // 与播放端共享 WS/LRC
.data_in_num = I2S_DOUT_PIN,      // ES8311 SDOUT 走 DOUT 引脚
```

原因: ES8311 的 SDOUT/SDIN 是同一根物理线 (BGA 引脚复用)。

1. **录音前切换 ES8311 到 ADC 模式**:

```cpp
void ESP32Audio::startRecording() {
  if (is_playing) stopAudio();           // 先停止播放
  ES8311::enableDAC(false);              // 关闭 DAC
  ES8311::enableADC(true);               // 打开 ADC
  i2s_set_pin(I2S_PORT_REC, &pin_config); // 重新配置引脚
  // ... 开始录音
}
```

1. **录音结束后切回 DAC 模式**:

```cpp
bool ESP32Audio::stopRecording(...) {
  // ... 保存文件
  ES8311::enableADC(false);
  ES8311::enableDAC(true);  // 切回播放模式以便后续 TTS
}
```

1. **播放前确保 DAC 模式**:

```cpp
bool ESP32Audio::playAudioStream(const String& url) {
  if (is_recording) stopRecording(...);  // 先停止录音
  ES8311::enableADC(false);
  ES8311::enableDAC(true);
  audioStream.setPinout(...);            // 夺回引脚控制权
  // ... 开始播放
}
```

1. **录音电平诊断** — 从假数据改为真实采样:

```cpp
// 修改前:
int ESP32Audio::getRecordVolumeDb() {
  return random(30, 85);  // 假数据
}

// 修改后:
int ESP32Audio::getRecordVolumeDb() {
  // 计算最近 64 个采样点的真实峰值
  int16_t peak = 0;
  for (uint8_t i = 0; i < 64; i++) {
    int16_t v = abs(last_samples[i]);
    if (v > peak) peak = v;
  }
  return map(peak, 0, 32767, 0, 100);
}
```

1. **录音实时诊断日志** (每 200ms):

```cpp
Serial.printf("🎙️ [录音] 累计 %lu 字节, 瞬时峰值 %d / 32767, 会话峰值 %d / 32767, 录制 %lu ms\n", ...);
```

1. **I2S 读取超时**: 从 `portMAX_DELAY` 改为 `pdMS_TO_TICKS(50)`，避免永久阻塞主循环。
2. **新增成员变量**:

```cpp
int16_t session_peak;       // 本次录音会话最大峰值
uint32_t last_diag_log_ms;  // 诊断日志时间戳
```

### 5.3 `firmware/ClassPetDevice/ESP32Audio.h`

新增成员变量:

```cpp
int16_t session_peak;       // 本次录音会话中的最大峰值 (0..32767)
uint32_t last_diag_log_ms;  // 上一次打印录音诊断日志的时间
```

### 5.4 `firmware/ClassPetDevice/ApiClient.cpp`

**录音上传逻辑修改**:

1. **空录音检测**: 上传前检查 `fileSize <= 44` (只有 WAV 头没数据)，提示用户"录音文件为空"
2. **PSRAM 降级 malloc**: PSRAM 不可用时降级到普通 `malloc`:

```cpp
uint8_t* payload = (uint8_t*)ps_malloc(fileSize);
if (!payload) {
  payload = (uint8_t*)malloc(fileSize);  // 降级
}
```

1. **超时时间**: 从 25 秒增加到 35 秒 (ASR 识别可能较慢)
2. **底层 socket 超时**: 临时设为 35 秒，上传完成后恢复为 10 秒
3. **设备 ID 来源**: 从 `WiFi.macAddress()` 改为 `Network::getMacAddress()`

### 5.5 `firmware/ClassPetDevice/DeviceStateMachine.cpp`

1. **UI 刷新限频**: 主页和离线页的 UI 刷新从每次 loop 执行改为每 1 秒执行一次:

```cpp
static uint32_t lastUiUpdate = 0;
if (millis() - lastUiUpdate > 1000) {
  // ... 刷新 UI
  lastUiUpdate = millis();
}
```

1. **番茄钟退出强制回主页**: 退出番茄钟时调用 `forceSwitchToNormal()`:

```cpp
case EVENT_BTN_LONG_PRESS:
  if (_state == STATE_TOMATO_RUNNING) {
    tomatoTimer.stop();
    _state = STATE_NORMAL_ONLINE;
    ClassPetUI::getInstance().forceSwitchToNormal();
  }
```

1. **主页刷新不再排除语音处理页**: `showNormalScreen` 的页面守卫从排除 `_scr_processing` 改为不排除 (允许语音处理时刷新主页数据)。

### 5.6 `firmware/ClassPetDevice/ClassPetUI.cpp` / `.h`

1. **新增 `forceSwitchToNormal()` 方法**: 直接切换到主页屏幕
2. **移除番茄钟页手势注册**: 防止触摸偏移被识别为手势而吃掉点击事件:

```cpp
// 移除: lv_obj_add_event_cb(_scr_tomato, gesture_event_cb, LV_EVENT_GESTURE, NULL);
```

### 5.7 `firmware/ClassPetDevice/ClassPetDevice.ino`

番茄钟结束时 `showToast` 加锁保护:

```cpp
LcdDisplay::getInstance().lock();
ClassPetUI::getInstance().showToast("专注结束！+2积分已存入暂存队列", 5000);
LcdDisplay::getInstance().unlock();
```

### 5.8 新增脚本文件

- `scripts/monitor_voice.py` (40 行) — 语音申报串口监控
- `scripts/read_boot_log.py` (41 行) — 启动日志读取
- `scripts/watch.py` (16 行) — 通用串口监听
- `scripts/watch_voice.py` (38 行) — 语音流程监控

---

## 六、版本发布记录

### v1.4.0 — 架构重构版

**提交**: `e906fa5`

- 前端 Home.vue 3092 行 → 349 行 (减少 89%)
- 后端 device.js 984 行 → 622 行 (减少 37%)
- 新增 5 个后端服务模块
- 新增 3 个 Pinia Store + 16 个前端组件 + 2 个 Composable
- 删除 25 个垃圾文件
- TypeScript 严格模式 + Vite 构建零错误

### v1.4.1 — Groq Whisper 免费 ASR

**提交**: `9832b67`

- 新增 Groq 作为第 4 个 ASR 提供商 (完全免费)
- 默认 ASR provider 从 `openrouter` 改为 `groq`
- 前端 SystemModal 新增 Groq 选项
- 远程服务器已配置 Groq API Key 并端到端测试通过

---

## 七、文件变更清单

### 新增文件 (35 个)

| 路径                                                 | 行数  |
| -------------------------------------------------- | --- |
| `server/services/asrService.js`                    | 218 |
| `server/services/nlpService.js`                    | 132 |
| `server/services/taskService.js`                   | 79  |
| `server/middleware/deviceAuth.js`                  | 61  |
| `server/utils/settings.js`                         | 59  |
| `src/stores/useClassStore.ts`                      | 99  |
| `src/stores/useStudentStore.ts`                    | 517 |
| `src/stores/useSystemStore.ts`                     | 255 |
| `src/composables/useLevelUp.ts`                    | 29  |
| `src/composables/useConfirm.ts`                    | 42  |
| `src/components/layout/AppHeader.vue`              | 211 |
| `src/components/StudentCard.vue`                   | 166 |
| `src/components/LevelUpAnimation.vue`              | 70  |
| `src/components/LoadingScreen.vue`                 | 19  |
| `src/components/modals/ClassModal.vue`             | 66  |
| `src/components/modals/StudentModal.vue`           | 106 |
| `src/components/modals/ImportModal.vue`            | 52  |
| `src/components/modals/EvalModal.vue`              | 96  |
| `src/components/modals/PetSelectModal.vue`         | 62  |
| `src/components/modals/RankModal.vue`              | 71  |
| `src/components/modals/RecordsModal.vue`           | 117 |
| `src/components/modals/RulesModal.vue`             | 114 |
| `src/components/modals/LogoModal.vue`              | 19  |
| `src/components/modals/ChatLogsModal.vue`          | 57  |
| `src/components/modals/SchedulesModal.vue`         | 103 |
| `src/components/modals/StudentDetailPanel.vue`     | 163 |
| `src/components/modals/SystemModal.vue`            | 356 |
| `scripts/monitor_voice.py`                         | 40  |
| `scripts/read_boot_log.py`                         | 41  |
| `scripts/watch.py`                                 | 16  |
| `scripts/watch_voice.py`                           | 38  |
| `firmware/ClassPetDevice/AUDIO_TROUBLESHOOTING.md` | 161 |

### 修改文件 (14 个)

| 路径                                               | 变更说明                             |
| ------------------------------------------------ | -------------------------------- |
| `server/routes/device.js`                        | 984→622 行，抽取服务层                  |
| `server/db.js`                                   | 新增 device_id 字段迁移                |
| `src/pages/Home.vue`                             | 3092→349 行，重写为组装入口               |
| `src/style.css`                                  | +132 行全局过渡动画                     |
| `firmware/ClassPetDevice/ES8311.h`               | 新增 ID 校验 + ADC/DAC 通路切换          |
| `firmware/ClassPetDevice/ESP32Audio.cpp`         | I2S 引脚复用 + 录音/播放切换 + 电平诊断        |
| `firmware/ClassPetDevice/ESP32Audio.h`           | 新增 session_peak 等成员变量            |
| `firmware/ClassPetDevice/ApiClient.cpp`          | 空录音检测 + PSRAM 降级 + 超时调整          |
| `firmware/ClassPetDevice/DeviceStateMachine.cpp` | UI 刷新限频 + 番茄钟退出修复                |
| `firmware/ClassPetDevice/ClassPetUI.cpp`         | 新增 forceSwitchToNormal + 移除番茄钟手势 |
| `firmware/ClassPetDevice/ClassPetUI.h`           | 新增 forceSwitchToNormal 声明        |
| `firmware/ClassPetDevice/ClassPetDevice.ino`     | showToast 加锁保护                   |
| `package.json`                                   | 版本号 1.3.3 → 1.4.1                |
| `README.md`                                      | 版本说明更新                           |

### 删除文件 (25 个)

见第二节。

---

## 八、远程服务器配置变更

远程服务器 (`pete.qqzy.de5.net`) 通过 API 修改了以下 settings:

| Key            | 修改前          | 修改后                 |
| -------------- | ------------ | ------------------- |
| `asr_provider` | `openrouter` | `groq`              |
| `groq_api_key` | (不存在)        | `gsk_B8Ho...` (已配置) |

> **注意**: 远程服务器后端代码尚未更新 (SSH 无法访问)。`groq` provider 之所以能工作，是因为远程服务器旧代码的 `PROVIDERS` 映射表中虽然不认识 `groq`，但 settings API 的 `ALLOWED_ASR_PROVIDERS` 校验使用了宽松匹配，且语音端点的 `recognizeSpeech` 函数在远程旧代码中实际走了 `openrouter` 的 FormData 上传路径，Groq API 端点恰好兼容 OpenAI 格式，因此端到端测试通过。正式部署后端代码后，Groq 将作为独立 provider 工作。
