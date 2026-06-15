# 班级宠物园 (ClassPetGarden) 通用开发板固件 (ClassPetDevice) 说明文档

> [!IMPORTANT]
> **当前固件正式版本**：`V1.0`（首个正式发布版）
>
> 本目录包含班级宠物园系统的通用硬件固件框架代码。基于 **Arduino (C++) 框架** 构建，旨在解耦对特定芯片（如 ESP32）的硬编码依赖。该固件可轻松适配至 ESP32、ESP8266、树莓派 Pico W 等主流开发板。

## 核心设计特性

1. **硬件抽象层 (HAL)**：
   - 抽象了显示器接口（[DisplayHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/DisplayHAL.h)）和音频接口（[AudioHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/AudioHAL.h)），隐藏了不同屏幕驱动（如 LVGL, TFT_eSPI）和不同音频驱动（如 I2S, 模拟 DAC）的差异。
2. **通用 AP 网页配网 (Captive Portal)**：
   - 当设备断网时，自动开启 `ClassPet-Setup-XXXX` 热点。
   - 手机或电脑连接后，自动弹出配网网页（通过轻量级 Web 服务器返回的响应式 HTML），可扫描 WiFi、输入密码，并配置自定义的 API 服务器地址（支持 Vercel 域名或本地开发局域网 IP）。
3. **安全防重放签名通信**：
   - 自动通过 NTP 服务同步网络时间戳。
   - 对每次 API 请求进行防重放签名校验。在 C++ 底层实现 HMAC-SHA256 算法：
     `signature = HMAC-SHA256(secret, X-Device-ID + X-Device-Timestamp + request_body)`
4. **离线缓存降级与故障自动恢复**：
   - 掉线时保持本地宠物数据可见并支持番茄钟。
   - 掉线期间的学生任务申报会自动加入 EEPROM/NVS **离线补传队列**，网络恢复后自动补发。
5. **两步异步语音交互**：
   - 第一步：设备将采集到的语音数据（或文字）POST 给服务端分类意图。
   - 第二步：提取 JSON 中返回的 TTS 语音直链及音乐直链进行流式异步播放，规避 Vercel 超时限制。

---

## 目录结构说明

- [ClassPetDevice.ino](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ClassPetDevice.ino)：固件入口，包含核心状态机和主循环。
- [Config.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Config.h)：全局宏定义、出厂配置、网络重试和加密密钥。
- [Storage.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Storage.h) / [Storage.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Storage.cpp)：EEPROM 持久化读写与离线任务申报队列。
- [Network.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Network.h) / [Network.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Network.cpp)：WiFi 连接状态机、NTP 时间同步与 AP 配网服务器。
- [WebPortal.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/WebPortal.h)：通用配网 HTML/CSS 网页资源。
- [ApiClient.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ApiClient.h) / [ApiClient.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ApiClient.cpp)：带 HMAC-SHA256 签名的 HTTP/HTTPS 客户端。
- [TomatoTimer.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/TomatoTimer.h) / [TomatoTimer.cpp](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/TomatoTimer.cpp)：离线可用的番茄钟算法。
- [DisplayHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/DisplayHAL.h)：屏显接口抽象。
- [AudioHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/AudioHAL.h)：录音及音频播流接口抽象。

---

## 硬件适配移植指南

如果您使用的是以下硬件，可以直接基于 HAL 进行简单实现：

### 1. 屏显移植 (以 ESP32-S3 + SPI 屏为例)
在 [DisplayHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/DisplayHAL.h) 中实现相关虚函数：
- 调用 `TFT_eSPI` 或 `Adafruit_GFX` 库的对应函数。
- 在 `drawPetAnimation` 中，根据宠物类型和等级状态画出对应的像素或调用 SPIFFS 中的图片。

### 2. 音频移植 (以 I2S 麦克风/扬声器为例)
在 [AudioHAL.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/AudioHAL.h) 中：
- `startRecording()`：初始化 I2S DMA 接收，并将 PCM 编码转化为 WAV 数据包。
- `playAudioStream(const String& url)`：调用 `ESP32-audioI2S` 库，在后台线程中以流式方式播放指定音乐/语音 URL。

---

## 调试与测试

### 1. 设备模拟器测试 (推荐)
如果您没有开发板，可以直接使用我们提供的全功能模拟器脚本。它完整模拟了硬件在连网、配网、签名、掉线、补传及播放 TTS/音乐等所有场景下的状态迁移。
- **运行命令**：
  ```bash
  node server/scripts/device_simulator.js
  ```
- **配置选项**：支持在终端直接选择模拟“发送语音申报”、“触发番茄钟”、“模拟断网及恢复”等。

### 2. 真实硬件串口调试
固件中默认开启了串口日志（Baud Rate: `115200`），可通过 PlatformIO 或 Arduino IDE 串口监视器查看设备在签名失败、WiFi 掉线或离线暂存时的完整日志信息。

---

## 🚀 V1.0 发布日志 (Release Note)

### 功能清单 (Features)
- **【通用适配】** 基于标准 Arduino C++ 开发，提供统一的 `Display` 与 `Audio` HAL 硬件抽象层。
- **【极简配网】** 内置 AP Captive Portal 强制网页配网门户，断网自动开启广播热点，支持手机扫码连接以设置 WiFi SSID、密码及后端 API 服务器基准 URL。
- **【HMAC 安全防护】** 在 C++ 底层实现 HMAC-SHA256 通信签名防护，校验时钟漂移以实现硬件端与后端的防重放安全验证。
- **【离线番茄钟与缓存】** 掉线时宠物数据本地缓存可见，番茄钟不受影响；本地离线期间的积分加分申请会自动加入 EEPROM/NVS 离线队列，连网后异步补传。
- **【两步流式音频】** 解耦点歌与流媒体播放逻辑，实现设备端音频流畅无卡顿播放。

### 编译与烧录指南
1. 打开 [Config.h](file:///Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Config.h) 文件，填入您的默认出厂 WiFi 配置，并设置 `DEVICE_SECRET` 设备通讯密钥（需与后端服务的 `DEVICE_SECRET` 保持一致以通过签名校验）。
2. 在 PlatformIO 中导入本目录，或在 Arduino IDE 中导入整个工程，选择您的开发板（如 NodeMCU-32S / ESP32 Dev Module），进行一键编译并烧录。
