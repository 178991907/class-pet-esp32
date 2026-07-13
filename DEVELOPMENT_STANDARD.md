# 班级宠物园 (ClassPetGarden) 硬件开发标准

> **本文件是本项目 ESP32-S3 固件开发的强制标准。**
> 所有涉及主板 / 屏幕 / 麦克风 / 喇叭 / 触摸 / SD / RGB 的代码改动，
> **必须**遵循本文件规定的引脚、信号链路、编译配置与反模式清单。
> 目的：终结“开发了很久功能仍不可用，尤其是麦克风与喇叭”的问题。

---

## 0. 依据与背景

本标准的引脚与信号链路，来自两个**在本开发板上验证过**的来源，二者交叉印证：

1. **LCDwiki 官方 Wiki**（开发板原厂资料）
   https://www.lcdwiki.com/zh/2.8inch_ESP32-S3_Display
2. **小智 xiaozhi-esp32 2.1.0 参考实现**
   `/Users/Terry/Downloads/小智例程源码/xiaozhi-esp32-main_2.1.0`
   （该项目在“同款/兼容 ESP32-S3 开发板”上完整跑通 ES8311 双工音频、ILI9341 显示、FT 触摸、esp-sr 唤醒词；其 `main/audio/codecs/es8311_audio_codec.cc` 与 `main/audio/wake_words/afe_wake_word.cc` 是被引用的权威实现。）

**开发板型号**：LCDWiki 2.8inch ESP32-S3 Display（丝印 **ES3C28P**）
**主控**：ESP32-S3 **N16R8**（16MB Flash + 8MB OPI PSRAM）

> ⚠️ 注意：xiaozhi 2.1.0 自带的全部 `main/boards/*` 配置中**没有**与本板完全一致的板型
> （上游无 ES3C28P 配置）。因此本板使用的“完美运行的小智”是用户自行 fork 的定制构建，
> 本文件取其**与板无关的、已验证的通用实现模式**（ES8311 双工 / PA 静音逻辑 / AFE 唤醒词），
> 引脚则以 LCDwiki Wiki 为唯一权威来源。

---

## 1. 权威引脚表（AUTHORITATIVE — 禁止随意改动）

以下引脚经 Wiki 与 xiaozhi 通用实现双重确认。**改动前必须先说明理由并验证。**

| 功能 | 信号 | GPIO | 备注 |
|---|---|---|---|
| **LCD** | SPI CS | 10 | ILI9341V 240×320 4 线 SPI |
| | SPI DC | 46 | 数据/命令选择 |
| | SPI BL | 45 | 背光，高电平点亮 |
| | SPI MOSI | 11 | |
| | SPI SCLK | 12 | |
| | SPI MISO | 13 | |
| | SPI RST | -1 | 接 EN，与上电共复位，不单独控制 |
| **触摸** | I2C SDA | 16 | FT6336G，与 ES8311 **共用** I2C 总线 |
| | I2C SCL | 15 | |
| | INT | 17 | 触摸中断 |
| | RST | 18 | 触摸复位 |
| **音频** | MCLK | 4 | ES8311 主时钟 = 采样率 × 256 |
| | BCLK | 5 | 位时钟 |
| | WS / LRCK | 7 | 左右声道时钟 |
| | I2S **DOUT** | 8 | **ESP32 → ES8311**（喇叭 / DAC 输入） |
| | I2S **DIN** | 6 | **ES8311 → ESP32**（麦克风 / ADC 输出） |
| | 功放使能 | 1 | FM8002E，**低电平使能**（高电平=静音） |
| **ES8311** | I2C ADDR | 0x18 | CE 脚接地；共用 SDA=16/SCL=15 |
| **SD 卡** | CLK | 38 | SDIO 4 线 |
| | CMD | 40 | |
| | D0 | 39 | |
| | D1 | 41 | |
| | D2 | 48 | |
| | D3 | 47 | |
| **其他** | BOOT 键 | 0 | 物理录音键 |
| | RGB LED | 42 | WS2812B |
| | 电池 ADC | 9 | 电压检测 |

对应代码常量（`firmware/ClassPetDevice/Board.h`）：

```
TFT_MOSI_PIN=11  TFT_SCLK_PIN=12  TFT_CS_PIN=10  TFT_DC_PIN=46  TFT_BL_PIN=45  TFT_RST_PIN=-1
TOUCH_SDA_PIN=16 TOUCH_SCL_PIN=15 TOUCH_INT_PIN=17 TOUCH_RST_PIN=18
AUDIO_EN_PIN=1   I2S_MCLK_PIN=4 I2S_BCLK_PIN=5 I2S_LRC_PIN=7 I2S_DOUT_PIN=8 I2S_DIN_PIN=6
ES8311_I2C_ADDR=0x18
SD_SCK_PIN=38 SD_CMD_PIN=40 SD_D0_PIN=39 SD_D1_PIN=41 SD_D2_PIN=48 SD_D3_PIN=47
PHYSICAL_KEY_PIN=0 RGB_LED_PIN=42 BATTERY_ADC_PIN=9
```

---

## 2. 音频子系统标准（最重要的部分）

### 2.1 信号链路（必须理解，否则必定出错）

```
[ MEMS 麦克风 ] → ES8311(ADC) → I2S DIN(GPIO6) → ESP32
                                              ↕ 同一根 I2S 总线 (MCLK=4 BCLK=5 WS=7)
ESP32 → I2S DOUT(GPIO8) → ES8311(DAC) → FM8002E 功放(IO1 低使能) → [ 喇叭 ]
```

- **ES8311** 是唯一的音频编解码器，同时负责 ADC（麦克风入）和 DAC（喇叭出）。
- **麦克风数据线只能是 GPIO6（I2S_DIN_PIN）**；GPIO8 是 DAC 输出脚，接它只会读到数字垃圾。
- **喇叭数据线只能是 GPIO8（I2S_DOUT_PIN）**。
- MCLK / BCLK / WS 三者收发共用。

### 2.2 强制规则（RULES — 违反任意一条都会导致“没声 / 录音是 you / 啸叫”）

- **R1 — 麦克风脚锁定 GPIO6。** 录音 `i2s_set_pin` 的 `data_in_num` 必须 = `I2S_DIN_PIN`(6)。曾误用 GPIO8 读取，结果是 DAC 数字垃圾 → Whisper 幻觉成 “you”。
- **R2 — 喇叭脚锁定 GPIO8。** 播放 `data_out_num` 必须 = `I2S_DOUT_PIN`(8)。
- **R3 — 录音时功放必须静音。** `AUDIO_EN_PIN` 在录音期间 = **HIGH**（静音）。否则喇叭把环境噪音/回授放出来，淹没麦克风真实信号 → “you” + 啸叫。仅在**播放/输出**时拉 **LOW** 使能。（与 xiaozhi `es8311_audio_codec.cc::UpdateDeviceState` 的 PA 逻辑一致：output 使能才拉高，否则拉低静音。）
- **R4 — 采样率。** 录音与 ASR / 唤醒词链路**必须 16000 Hz**（Whisper 与 esp-sr AFE 的硬要求）。音乐 MP3 播放可用 44.1kHz，但**语音 TTS / 识别一律 16k**。禁止把录音设成 44.1k 再指望后端能正常识别。
- **R5 — MCLK 倍频 = 256**（MCLK = 采样率 × 256）。
- **R6 — 本板 ES8311 ADC 经验寄存器值（mic_sweep 实测，禁止改动）：**
  - `ES8311_SYSTEM_REG14 = 0x0A`（麦克风输入通道，信号最强；驱动默认 0x1A / 误试 0x10 在本板几乎无信号）
  - `ES8311_ADC_REG17 = 0xF0`（ADC 音量，峰值 ~16250 不削波；0xC8 太轻，0xFF 易削波）
  - `es8311_microphone_gain_set(..., ES8311_MIC_GAIN_30DB)`
- **R7 — I2S 端口统一用 `I2S_NUM_0`**（xiaozhi 也用 0）。当前 `Board.h` 的 `I2S_NUM_USE` 宏误标为 `I2S_NUM_1`，但**实际代码全用 `I2S_NUM_0`**——以代码为准。新增代码不得改用 `I2S_NUM_1`。
- **R8 — I2C 不重复安装。** ES8311 复用触摸屏的 `I2C_NUM_0`（SDA=16/SCL=15）。**不要**再调 `i2c_param_config` / `i2c_driver_install`，否则破坏触摸。直接 `i2c_master_write_to_device` 读写 ES8311 寄存器即可。

### 2.3 半双工 vs 全双工（本项目的取舍）

xiaozhi 用**全双工**（同一 `I2S_NUM_0` 上 TX/RX 同时开，`WORK_MODE_BOTH`）。
本项目固件采用**半双工**：录音时卸载 TX 驱动、装 RX 驱动；播放时反之。
**两种都可行**，关键在于：
- 录音与播放**不得同时**占用 `I2S_NUM_0`（RX/TX 冲突会崩溃）；
- 半双工切换时必须遵守 R3（录音静音功放）。

### 2.4 参考实现（来自 xiaozhi，已验证）

`es8311_audio_codec.cc`：
- `CreateDuplexChannels()`：`i2s_channel_init_std_mode` 同参数初始化 TX 与 RX；
  `I2S_SLOT_MODE_STEREO` / `I2S_DATA_BIT_WIDTH_16BIT` / `bit_shift=true` / `I2S_MCLK_MULTIPLE_256`。
- PA：`pa_pin` 在 `output_enabled` 时拉高、否则拉低（静音）；支持 `pa_inverted` 适配不同功放极性。
- `input_gain_ = 30`（对应 `MIC_GAIN_30DB`）。

`afe_wake_word.cc`（唤醒词，见 §6）：
- `afe_config_init(input_format, models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF)`
- `afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM`（**依赖 PSRAM**）
- `WAKENET_DETECTED` → 触发；`get_feed_chunksize()` 决定喂数据块大小。

---

## 3. 显示 / 触摸标准

- **LCD**：ILI9341V，240×320，4 线 SPI。引脚见 §1。背光 `BL=45` 高电平点亮。
- **中文显示**：完整字库 `cjk16.bin`（16px / bpp=2 / GB2312 全量）放 **TF 卡根目录**，
  固件用 LVGL `lv_binfont_create_from_buffer()` 从 PSRAM 加载。**字库不入固件**（会超 huge_app 分区）。
  无 TF 卡时回退 LVGL 内置思源黑体简体子集（`lv_font_source_han_sans_sc_16_cjk`）。
- **触摸**：FT6336G，I2C SDA=16/SCL=15/INT=17/RST=18。手势返回 = 任意方向滑动。
- **UI 文案禁用 emoji**（🎙️💭🔊 等）：CJK 字库不含 emoji，会显示成方块（tofu）。

---

## 4. 其他外设

- **SD 卡**：SDIO 4 线，CLK=38/CMD=40/D0=39/D1=41/D2=48/D3=47。录音 WAV、字库、宠物素材均存于此。
- **BOOT 键**：GPIO0，长按录音，松手停止，最长 15s。
- **RGB LED**：GPIO42（WS2812B），状态指示。
- **电池 ADC**：GPIO9。

---

## 5. 编译 / 构建配置标准（MANDATORY）

arduino-cli 编译 FQBN（**四个参数缺一不可**）：

```
esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc
```

- **`PSRAM=opi` 必须**：N16R8 的 8MB PSRAM 是 OPI 接口。禁用时 `max_alloc` 仅 ~4KB，
  SSL 握手 / 音频缓冲 / LVGL 全部失败。
- **`CDCOnBoot=cdc` 必须**：本板用原生 USB，不加此参数在串口监视器看不到应用输出。
- **`PartitionScheme=huge_app` 必须**：固件较大（字库虽分离但 LVGL+WiFi+HTTP 仍占空间）。
- **字库不入固件**：见 §3，放 TF 卡。

烧录：本板支持 DTR/RTS 自动复位，`esptool --before default_reset` 即可进 download 模式，
无需手动 BOOT+RST。

---

## 6. 唤醒词（esp-sr）集成标准（已落地 ✅ 2026-07-13）

> 离线唤醒词已集成：`WakeWordEngine`（见固件 `WakeWordEngine.h/.cpp`）。
> 空闲态常驻监听麦克风，检测到唤醒词即 `postEvent(EVENT_VOICE_START)` 进入语音流程
> （与“长按/点击按钮触发”走同一条路径），并把 I2S_NUM_0 让给录音/播放。

### 6.1 版本与 API（关键：与 xiaozhi 2.x 不同）

- 用 **esp-sr 1.0**（分支 `release/v1.0`，对应 IDF 4.4）。预编译引擎库在 `lib/esp32s3/*.a`。
- **API 是 `esp_afe_sr_1mic` + `afe->create_from_config(&cfg)` + `afe->feed()` / `afe->fetch()`**，
  `fetch()` 返回 `afe_fetch_mode_t`，`AFE_FETCH_WWE_DETECTED == 1` 表示唤醒。
  **不是** xiaozhi 2.x 的 `esp_afe_handle_from_config` / `afe_config_init` / `fetch_with_delay` /
  `WAKENET_DETECTED` —— 这些是更新版 esp-sr 的 API，本仓库捆绑的 1.0 修订版没有这些符号。
- `afe_config_t` **不用** `AFE_CONFIG_DEFAULT()` 宏（它依赖 `menuconfig` 的
  `WAKENET_MODEL`/`WAKENET_COEFF` 宏，Arduino 无 menuconfig）。改为代码里逐字段赋值
  （参考 `WakeWordEngine.cpp`）：`wakenet_model = &esp_sr_wakenet5_quantized`、
  `wakenet_coeff = &get_coeff_nihaoxiaozhi_wn5`、`wakenet_mode = DET_MODE_90`（**单麦用 90，双麦才用 2CH**）、
  `alloc_from_psram = AFE_PSRAM_MEDIA_COST`（依赖 §5 的 `PSRAM=opi`）。

### 6.2 唤醒词模型（**嵌入 .a，无需 Flash 分区**）

- S3 可用 wakenet5 模型系数（纯只读数据，**架构无关**，esp32 版的 `.a` 在 S3 也能直接链接）：
  `嗨乐鑫`(hilexin) / `你好小智`(nihaoxiaozhi) / `你好小新`(nihaoxiaoxin) / `customized_word`。
  （`小爱同学`/`Hi ESP`/`Alexa` 是 wakenet7/8，需对应引擎，本仓库未链接。）
- 默认用 **`你好小智`(nihaoxiaozhi)** —— `src/esp32s3/libnihaoxiaozhi_wn5.a`（符号链接到
  `/tmp/esp-sr/lib/esp32/libnihaoxiaozhi_wn5.a`）提供 `get_coeff_nihaoxiaozhi_wn5`，
  引擎 `src/esp32s3/libwakenet.a` 提供 `esp_sr_wakenet5_quantized`。
  > ⚠️ 模型 `.a` 以**符号链接**形式存在，指向本地 `/tmp/esp-sr/lib/...`（esp-sr 1.0 仓库 clone）。
  > 换机/清 `/tmp` 后需重新 `git clone` 到 `/tmp/esp-sr` 或把 `.a` 实体拷入 `src/esp32s3/`，否则链接失败。
- **模型权重直接编进固件**（链接 `lib*.a`），**不**走 `model` SPIFFS 分区 / `esp_srmodel_init`
  （arduino-cli 没有 IDF 的分区烧录工具链，嵌入 .a 最省事）。
  `libwakenet.a` 仍会引用 `get_model_base_path()`，由 `lib/esp-sr/src/model_path_stub.c` 提供桩实现满足链接。

### 6.3 arduino-cli 集成方式（已验证可行 ✅ 2026-07-13）

- **arduino-cli 不发现 `components/` 也不认 `EXTRA_COMPONENT_DIRS`**（已验证
  `#include "esp_afe_sr_iface.h"` 报 “No such file”）。
- **正确做法：把 esp-sr 当“预编译静态库 Arduino 库”（`precompiled=true`）** —— `firmware/lib/esp-sr/`：
  - `src/*.h`（头文件，来自 `esp-sr/include/esp32s3/`）
  - `src/esp32s3/*.a`（**按架构子目录放置**，arduino-cli 1.5.1 会自动把该目录下 `.a`
    以 `-Lsrc/esp32s3 -l<name>` 形式链接，且位于主 `--start-group` 内，可被 sketch 引用解析）
  - `src/model_path_stub.c`（提供 `get_model_base_path()` 桩，满足 `libwakenet.a` 的未定义引用）
  - `library.properties`（设 `precompiled=true`；**不要**用 `ldflags` —— arduino-cli 1.5.1
    **不读取** library.properties 的 `ldflags` 字段，写进去也不会进链接命令，已实测）
- 编译只需 `--libraries /Users/Terry/Downloads/class-pet-main/firmware/lib`，**无需任何额外
  `--build-property` 或手动 `-l`**。早期曾尝试用 `library.properties` 的 `ldflags` 写链接组，
  但实测链接命令里完全没出现那些 `-l`，导致 `esp_afe_sr_1mic`/`esp_sr_wakenet5_quantized`/
  `get_coeff_nihaoxiaozhi_wn5` 全部 undefined —— 改预编译库结构后解决。
- 引擎 `.a`（符号链接到 `/tmp/esp-sr/lib/esp32s3/*`）：`libwakenet` `libhufzip` `libdl_lib`
  `libc_speech_features` `libesp_audio_front_end` `libesp_audio_processor` `libmultinet`；
  模型 `.a`（符号链接到 `/tmp/esp-sr/lib/esp32/*`，纯只读数据，S3 可直接链接）：
  `libnihaoxiaozhi_wn5` `libhilexin_wn5`。
  **切换唤醒词只需改 `WakeWordEngine` 的 `include` 与 `wakenet_coeff`，重新编译即可**
  （两个模型 `.a` 都已链接，仅被引用的 `get_coeff_*` 符号进固件）。
- **编译结果（2026-07-13 实测通过）**：Sketch 占 2,922,089 / 3,145,728 字节（92% of huge_app），
  全局变量 59,604 / 327,680 字节（18%）。固件已能落进 huge_app 分区；esp-sr 体积偏大，
  后续加功能需注意分区余量（约 220KB 余量）。

### 6.4 运行时协调（避免 I2S 争夺）

- `WakeWordEngine` 在空闲态（`STATE_NORMAL_ONLINE`/`STATE_NORMAL_OFFLINE`）**自己安装 I2S_NUM_0
  的 RX 驱动**常驻监听；离开空闲态（录音/识别/播放）立即 `pause()` 卸载 I2S，交还音频硬件。
- 检测到唤醒词 → `onWake` 回调 → 状态机 `postEvent(EVENT_VOICE_START)` → 进入录音
  （`ESP32Audio::startRecording()` 重新安装 I2S RX）。**录音/播放期间唤醒词引擎暂停**，互不抢 I2S。
- `serial_diag_active` 为真时（串口诊断命令占用音频硬件），唤醒词引擎也暂停让出 I2S。
- ES8311 麦克风模式 + 功放静音由 `ESP32Audio::enterWakeMicMode()` 负责（不碰 I2S），
  与 `startRecording()` 的麦克风配置共用同一套 REG14=0x0A/REG17=0xF0/30dB 经验值（见 §2.4）。

### 6.5 集成参考

- 权威 API 形态参考 xiaozhi `main/audio/wake_words/afe_wake_word.cc`（注意其 API 是 2.x，仅作
  feed/fetch 任务结构参考，不要照搬 `esp_afe_handle_from_config`）。
- 本仓库实现：`firmware/ClassPetDevice/WakeWordEngine.h` / `WakeWordEngine.cpp`、
  `AudioHAL.h`（`enterWakeMicMode`）、`ESP32Audio.cpp`、`DeviceStateMachine.cpp`（init + loopState 协调）。

---

## 7. 反模式清单（DO NOT — 这些都是已经踩过的坑）

| 反模式 | 后果 |
|---|---|
| 把 GPIO8 当麦克风脚读 | 读到 DAC 数字垃圾 → Whisper 幻觉 “you” |
| 录音时不静音功放（IO1 不拉高） | 喇叭回授淹没麦克风 → “you” + 啸叫 |
| 录音与播放同时开 `I2S_NUM_0` | RX/TX 驱动冲突 → 固件崩溃 |
| 录音设成 44.1kHz 送 ASR | Whisper 识别异常 / 带宽浪费 |
| PSRAM=disabled | `max_alloc` 仅 4KB，SSL/音频/LVGL 全挂 |
| 重复 `i2c_driver_install`（ES8311） | 破坏触摸屏 I2C |
| 字库编进固件 | 超 huge_app 分区；中文变方块 |
| UI 用 emoji | 字库无 emoji → 方块 |
| 改动 §1 引脚未经验证 | 整块板功能错乱 |

---

## 8. 改动硬件相关代码后的验证清单

每次提交涉及音频 / 显示 / 触摸 / 引脚的改动前，确认：

- [ ] 编译 FQBN 含 `PSRAM=opi` 与 `CDCOnBoot=cdc`（见 §5）
- [ ] 录音：串口日志显示 **峰值 > 200/32767**（说明麦克风有真实信号，非静音/非 you）
- [ ] 播放：音调测试或 MP3 能正常出声（喇叭链路 OK）
- [ ] 录音期间 `AUDIO_EN_PIN` = **HIGH**（功放静音，见 R3）
- [ ] 中文 UI 显示正常，无方块（见 §3）
- [ ] 触摸返回手势可用
- [ ] 未改动 §1 引脚，或改动已在本文件登记并验证

---

## 9. 当前代码与标准的对齐状态（2026-07-13）

| 项 | 状态 | 说明 |
|---|---|---|
| 引脚（§1） | ✅ 一致 | `Board.h` 与 Wiki 完全一致 |
| 麦克风脚 GPIO6 | ✅ 正确 | `I2S_DIN_PIN=6`，`startRecording()` 用 GPIO6 |
| 喇叭脚 GPIO8 | ✅ 正确 | `I2S_DOUT_PIN=8` |
| 录音静音功放 | ✅ 正确 | `startRecording()` 拉 HIGH，`stopRecording`/`playAudio` 拉 LOW |
| ES8311 经验寄存器 | ✅ 正确 | REG14=0x0A / REG17=0xF0 / 30dB |
| 录音 16kHz | ✅ 正确 | `REC_SAMPLE_RATE=16000` |
| I2S 端口 | ⚠️ 宏误导 | `Board.h` 的 `I2S_NUM_USE` 标 `I2S_NUM_1`，实际代码用 `I2S_NUM_0`（已据实修正） |
| 字库分离 TF 卡 | ✅ 正确 | `cjk16.bin` + `lv_binfont_create_from_buffer` |
| 编译 FQBN | ✅ 正确 | `huge_app,PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc` |
| 离线唤醒词 | ✅ 已做 | `WakeWordEngine` + esp-sr 1.0（`esp_afe_sr_1mic`），默认唤醒词 `你好小智`，见 §6 |

---

*维护：本文件由固件负责人维护。任何引脚 / 音频链路变更必须先更新本节，再改代码。*
