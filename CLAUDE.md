# CLAUDE.md — 班级宠物园 (ClassPetGarden) 项目指引

本文件供 AI 编码助手（Claude Code / WorkBuddy / Cursor 等）与本仓库的人类开发者共同遵循。

## 0. 最重要的硬规则（任何硬件相关改动前必读）

**`DEVELOPMENT_STANDARD.md`（项目根目录）是强制硬件开发标准，改动以下任一内容前必须先读它：**
- 主板引脚 / 屏幕 / 麦克风 / 喇叭 / 触摸 / SD / RGB
- 音频链路（I2S、ES8311、功放）
- 编译配置 / 分区 / PSRAM
- 唤醒词（esp-sr）

违反标准会导致“功能开发完却不可用（尤其麦克风/喇叭）”。标准里含：权威引脚表、音频强制规则 R1–R8、反模式清单、验证清单、当前对齐状态表。

## 1. 音频子系统不可违反的红线（摘要，详见标准 §2）

- **麦克风数据线锁定 GPIO6**（`I2S_DIN_PIN`）。绝不可把 GPIO8（`I2S_DOUT_PIN`，喇叭脚）当麦克风读，否则读到 DAC 数字垃圾 → Whisper 幻觉 “you”。
- **喇叭数据线锁定 GPIO8**（`I2S_DOUT_PIN`）。
- **录音时功放必须静音**：`AUDIO_EN_PIN`(GPIO1) 在 `startRecording()` 期间 = **HIGH**（静音）；仅在播放/输出时拉 **LOW** 使能。否则回授啸叫淹没麦克风。
- **采样率**：录音 / ASR / 唤醒词链路**必须 16000 Hz**；音乐 MP3 播放可用 44.1kHz。
- **MCLK 倍频 = 256**。
- **ES8311 本板 ADC 经验值（mic_sweep 实测，禁止改动）**：`ES8311_SYSTEM_REG14=0x0A`、`ES8311_ADC_REG17=0xF0`、`MIC_GAIN_30DB`。
- **I2S 端口统一 `I2S_NUM_0`**（`Board.h` 的 `I2S_NUM_USE` 已据实定义为 `I2S_NUM_0`）。
- **ES8311 复用触摸屏的 `I2C_NUM_0`（SDA=16/SCL=15），不要重复安装 I2C 驱动。**

## 2. 显示 / 字库红线

- 中文显示用 TF 卡根目录 `cjk16.bin` + LVGL `lv_binfont_create_from_buffer()`。**字库不入固件**（会超 huge_app 分区）。
- **UI 文案禁用 emoji**（🎙️💭🔊 等）：CJK 字库不含 emoji，会显示成方块。

## 3. 编译 / 构建（MANDATORY）

```
esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc
```
- `PSRAM=opi` 必须（N16R8 的 8MB PSRAM 是 OPI 接口；禁用会导致 SSL/音频/LVGL 全挂）。
- `CDCOnBoot=cdc` 必须（本板原生 USB，否则看不到串口输出）。
- 烧录：本板支持 DTR/RTS 自动复位，`--before default_reset` 即可，无需手动 BOOT+RST。

## 4. LVGL 双核互斥（运行时不死机的关键）

- `lvglTask`(Core 0) 持 `_lvgl_mutex` 时调用 `lv_timer_handler()` 并派发输入/手势回调；`loop()`(Core 1) 驱动音频/网络/电源策略。
- 从 `loop()`(Core 1) 修改 LVGL 对象（切屏 `loadScreen`/`lv_screen_load`、改文字、增删对象）**必须**包在 `LcdDisplay::getInstance().lock()`/`.unlock()` 中。
- 在 LVGL 事件回调内部已持锁，**不要**再 lock（会死锁，mutex 非递归）。
- 违反症状：设备卡在某屏（尤其待机屏）、触摸无响应、无崩溃转储。

## 5. 唤醒词（esp-sr）集成（进行中，详见标准 §6）

- 用 esp-sr 1.0（IDF 4.4 兼容，与 Arduino 2.0.17 捆绑的 IDF 匹配）。S3 预编译库在 `/tmp/esp-sr/lib/esp32s3/*.a`。
- arduino-cli 不自动发现 `components/`：集成时需通过 `EXTRA_COMPONENT_DIRS` 环境变量指向组件目录。
- 唤醒词：`嗨乐鑫`/`小爱同学`/`Hi ESP`/`Alexa`（S3 可用）；`你好小智` 需 esp-sr 2.x，暂不可用。
- AFE：`memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM`（依赖 §3 的 PSRAM=opi）。

## 6. 提交前检查

本仓库已配置 pre-commit 钩子（`.githooks/pre-commit`，由 `git config core.hooksPath .githooks` 启用），会自动拦截违反红线的改动（如麦克风误接 GPIO8、I2S 端口回退 NUM_1）。人工提交前另请对照 `docs/PRE_COMMIT_CHECKLIST.md`。
