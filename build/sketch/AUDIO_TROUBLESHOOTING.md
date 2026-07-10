#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/AUDIO_TROUBLESHOOTING.md"
# 班级宠物园 - 喇叭无声 & 麦克风无法识别 排查与修复指南

> 本指南适用于 `firmware/ClassPetDevice` 下的 ESP32-S3 固件。
> 适用于使用 **ES8311 音频编解码器** + **SC8002B / FM8002E 功放** 的硬件方案。

---

## 一、问题症状（用户视角）

- 点击"语音对话"或按住 BOOT 键后，屏幕一直停留在「识别中...」或弹窗提示"无法识别声音"。
- 设备从未播放过提示音、番茄钟结束铃声、宠物升级提示音。
- 串口日志中没有报错，也没有"ES8311 初始化成功"。

---

## 二、根因分析（已发现 3 个 Bug）

### Bug 1：功放使能引脚电平错误（导致喇叭无声）

- **位置**：`ESP32Audio.cpp::init()`
- **问题**：`digitalWrite(AUDIO_EN_PIN, LOW)` 把功放 (SC8002B/FM8002E) 关掉了。
- **修复**：改为 `HIGH`，功放才能正常放大信号推动喇叭。

### Bug 2：麦克风 I2S 通道未接 ES8311 ADC（导致无法识别声音）

- **位置**：`ESP32Audio.cpp::init()` 中 `i2s_set_pin` 的 `data_in_num = I2S_MIC_SD_PIN` (GPIO 6)。
- **问题**：
  - 麦克风实际是经过 ES8311 编码器再上 I2S 总线，而不是独立直连 I2S 外设。
  - ES8311 的 SDOUT 与 SDIN 是同一根物理线（芯片 BGA 引脚复用）。
  - 原代码独立开了一个 I2S 外设读 GPIO 6，但 GPIO 6 上根本没有麦克风信号源 → 录到的是 0 / 全是底噪。
- **修复**：
  - 让 `I2S_NUM_1`（录音）与播放端 **共享 BCLK/LRC**。
  - `data_in_num` 改为 `I2S_DOUT_PIN`（即 ES8311 SDOUT）。
  - 录音时切换 ES8311 寄存器：关闭 DAC、打开 ADC。
  - 播放时反向切换：关闭 ADC、打开 DAC。

### Bug 3：ES8311 始终未切换 ADC 通路（与 Bug 2 关联）

- **位置**：`ES8311.h`
- **问题**：原 ES8311 初始化后只配置了 DAC（播放），没有提供 ADC/DAC 切换函数，录音时只能默默读到静音。
- **修复**：新增 `ES8311::enableDAC(bool on)` 和 `ES8311::enableADC(bool on)` 静态方法。

### 附加修复

- `i2s_read` 之前使用 `portMAX_DELAY` 永久阻塞，会让主循环在录音时卡死、触摸失灵 → 改为 50ms 超时。
- 真实电平计算替代假随机数 → 录音时 UI 声波条才会动。
- 上传时若 `fileSize <= 44`（只有 WAV 头）直接拒绝并提示"录音文件为空，请检查麦克风硬件"。
- 修复了 `postVoiceAudio` 中 `WiFi.macAddress()` → `Network::getMacAddress()`，保持与其它接口一致。
- PSRAM 申请失败时降级到普通 `malloc`，避免无 PSRAM 板子（如 N8R2 缺货用 N4R4 替代时）直接失败。

---

## 三、修改文件清单

| 文件 | 主要修改 |
|------|---------|
| `firmware/ClassPetDevice/ESP32Audio.cpp` | 功放使能电平改为 HIGH；I2S_NUM_1 共享 BCLK/LRC；录音前后切换 ES8311 ADC/DAC；50ms 超时；真实电平计算 |
| `firmware/ClassPetDevice/ES8311.h` | 新增 `enableDAC` / `enableADC` 切换函数 |
| `firmware/ClassPetDevice/ApiClient.cpp::postVoiceAudio` | 空文件检测、内存申请降级、MAC 地址统一 |

---

## 四、烧录后串口观察要点

打开 Arduino IDE 串口监视器 (115200 波特率)，重新烧录固件后应当看到：

### 4.1 正常启动日志（应出现的关键行）

```
🔊 [音频] ESP32-S3 I2S 音频输出(BCLK:5, LRC:7, DOUT:8)初始化完成。
🎙️ [音频] I2S 麦克风录音驱动(复用 BCLK:5, LRC:7, SDIN/DOUT:8)初始化成功。
```

如果看到 `❌ ES8311 初始化失败，未找到 I2C 设备 0x18`，说明：
- 触摸屏 I2C 总线 (SDA=16, SCL=15) 没接通到 ES8311；或
- ES8311 没焊好 / 没上电；或
- ES8311 的 I2C 地址不是 0x18（少数批次为 0x19，需要改 `ES8311.h`）。

### 4.2 触发语音对话时

长按 BOOT 键或点击屏幕「麦克风」按钮，应出现：

```
🔊 [音频] 准备进行 I2S 麦克风录音...
🎙️ [ES8311] 切换到 ADC 录音模式 (DAC 已关闭)
🎙️ 录音已正式开始，数据直接写入 TF 卡 /record.wav
```

如果**没看到 ADC 切换日志**：说明 ES8311 寄存器写入失败，请检查 I2C 总线是否被其他设备占用。

### 4.3 录音结束后应看到

```
🎙️ [音频] 录音结束并完成文件封装，总耗时: XXXX ms，数据大小: XXXXXX Bytes
🎙️ [ES8311] 切换到 DAC 播放模式 (ADC 已关闭)
🎙️ [上传] 准备流式上传录音: /record.wav, 大小: XXXXXX 字节
```

**如果"数据大小"小于 1000 字节**：麦克风实际没采到声音，请检查：
- ES8311 的 MIC 偏置是否正常供电（MICBIAS 引脚）
- 麦克风咪头是否虚焊
- 板子丝印上标的是不是 ES8311 芯片（若实际是其他 codec，本驱动不适用）

### 4.4 服务器端验证

在后端 `server/` 目录下查看 `server.log`，应出现：

```
🎙️ [ASR] 设备 XXXX 语音识别结果: <识别出的文字>
```

如果出现 `ASR 识别失败`，请检查：
- `settings` 表中的 `openrouter_api_key` 是否配置
- 服务器能否访问 `https://api.openai.com/v1/audio/transcriptions`（可能需要代理）
- 上传的 WAV 文件头是否正常（用 `file record.wav` 命令在 SD 卡本地验证）

---

## 五、硬件快速自检表

| 检查项 | 期望 |
|--------|------|
| 喇叭连接到功放 (SC8002B/FM8002E) 的 OUT+/OUT- | 是 |
| 功放 SD 引脚接到 ESP32-S3 GPIO 2 (`AUDIO_EN_PIN`) | 是 |
| 功放供电 (VDD 3.3V~5V) | 正常 |
| 功放 SD 引脚电平（启动后用万用表测） | **HIGH (3.3V)** |
| ES8311 的 SDA/SCL 是否与触摸屏共用总线 (GPIO 16/15) | 是 |
| ES8311 的 BCLK 接 GPIO 5，WS 接 GPIO 7，SDOUT 接 GPIO 8 | 是 |
| 咪头 MIC+/MIC- 接到 ES8311 的 MIC 引脚 (MIC1P/MIC1N 或 MIC2P/MIC2N) | 是 |
| ES8311 供电 (AVDD 3.3V, DVDD 1.8V 或 3.3V) | 正常 |

---

## 六、如果还是无声，进一步排查

### 情况 A：屏幕日志有"ES8311 初始化成功"但喇叭完全没声

1. 用万用表量 AUDIO_EN_PIN (GPIO 2) 启动后是否为 3.3V。
2. 用示波器或耳机探针（带 1µF 隔直电容）碰一下 ES8311 的 LOUT/ROUT 引脚：
   - 如果这里有声音波形 → 故障在功放电路或喇叭。
   - 如果这里没波形 → ES8311 的 I2S 通路有问题，验证 BCLK/WS 引脚上是否有 16kHz 级别的方波。

### 情况 B：串口日志看不到"切换到 ADC 录音模式"

- 可能是 I2C 写寄存器失败。在 `ES8311::enableADC(true)` 函数里加调试打印，看哪一行卡住。

### 情况 C：录音文件大小正常但 ASR 一直失败

- 在 PC 上 `adb pull` 或把 SD 卡取出，用 `ffplay record.wav` 播放，应该能听到自己的声音。
- 如果听不到 → 录音有数据但内容是静音或失真，MIC 偏置或模拟通路有问题。
- 如果能听到 → 排查网络（DNS、TLS、代理）到 OpenAI Whisper 的连通性。

### 情况 D：触摸时屏幕没反应（同时录音中）

- 旧代码 `portMAX_DELAY` 会卡死主循环，导致触摸无法响应。新代码已修复。

---

## 七、版本说明

修复对应的版本号：**v2.1.0** （音频子系统全面修复）
