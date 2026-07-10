#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ESP32Audio.cpp"
#include "ESP32Audio.h"
#include "ES8311.h"
#include "Board.h"
#include "LcdDisplay.h"
#include "ClassPetUI.h"

// 录音参数
#define I2S_PORT_REC I2S_NUM_1
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT
#define CHANNELS 1

// 录音实时电平估算 (最近 64 个采样点的峰值)
static int16_t last_samples[64] = {0};
static uint8_t last_sample_idx = 0;
static int last_peak_db = 0;

ESP32Audio::ESP32Audio()
  : is_playing(false), is_recording(false), record_data_size(0),
    record_start_time(0), session_peak(0), last_diag_log_ms(0) {}

ESP32Audio::~ESP32Audio() {
  stopAudio();
}

void ESP32Audio::init() {
  // 1. 初始化播放音频 (I2S_NUM_0 由内部使用)
  pinMode(AUDIO_EN_PIN, OUTPUT);
  // 注意：AUDIO_EN_PIN 取决于功放芯片型号。
  // 当前硬件 (SC8002B / FM8002E) 是高电平使能 (SD 引脚拉高打开功放输出)。
  // 若实测发现仍无声音，可临时改 LOW 验证。
  digitalWrite(AUDIO_EN_PIN, HIGH); // 高电平使能功放 (SC8002B / FM8002E)
  
  bool codec_ok = ES8311::init(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
  
  LcdDisplay::getInstance().lock();
  if (codec_ok) {
    ClassPetUI::getInstance().showToast("音频芯片 ES8311 初始化成功！", 6000);
  } else {
    ClassPetUI::getInstance().showToast("音频芯片 ES8311 初始化失败！请检查硬件", 15000);
  }
  LcdDisplay::getInstance().unlock();

  // ES8311 默认是 DAC 通路（播放）打开，ADC 通路（录音）关闭
  ES8311::enableDAC(true);
  ES8311::enableADC(false);
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  audioStream.setVolume(12); // 设置适度音量，防止小喇叭爆音
  is_playing = false;
  Serial.printf("🔊 [音频] ESP32-S3 I2S 音频输出(BCLK:%d, LRC:%d, DOUT:%d)初始化完成。\n", I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);

  // 2. 初始化录音麦克风驱动 (I2S_NUM_1)
  // 关键修复：麦克风数据实际由 ES8311 的 ADC (SDOUT) 提供。
  // ES8311 的 SDOUT 与 SDIN 是同一根物理线 (BGA 引脚复用)，所以必须与播放端共享 BCLK/LRC，
  // 录音时通过 ES8311 内部切换 ADC 通路，I2S_NUM_1 只负责读取数字信号。
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = BITS_PER_SAMPLE,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,        // 与播放端共享 BCLK
    .ws_io_num = I2S_LRC_PIN,          // 与播放端共享 WS/LRC
    .data_out_num = I2S_PIN_NO_CHANGE, // 录音端不输出
    .data_in_num = I2S_DOUT_PIN        // ES8311 的 SDOUT 走 DOUT 引脚 (与 DAC SDIN 同一根)
  };
  
  if (i2s_driver_install(I2S_PORT_REC, &i2s_config, 0, NULL) == ESP_OK) {
    if (i2s_set_pin(I2S_PORT_REC, &pin_config) == ESP_OK) {
      Serial.printf("🎙️ [音频] I2S 麦克风录音驱动(复用 BCLK:%d, LRC:%d, SDIN/DOUT:%d)初始化成功。\n", I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    }
  } else {
    Serial.println("❌ [音频] I2S 麦克风录音驱动初始化失败。");
  }
}

void ESP32Audio::writeWavHeader() {
  if (!record_file) return;
  uint8_t header[44] = {0};
  memcpy(header, "RIFF", 4);
  uint32_t fileSize = 36; // 暂无数据
  memcpy(header + 4, &fileSize, 4);
  memcpy(header + 8, "WAVEfmt ", 8);
  uint32_t fmtSize = 16;
  memcpy(header + 16, &fmtSize, 4);
  uint16_t fmtCode = 1; // PCM
  memcpy(header + 20, &fmtCode, 2);
  uint16_t channels = CHANNELS;
  memcpy(header + 22, &channels, 2);
  uint32_t sampleRate = SAMPLE_RATE;
  memcpy(header + 24, &sampleRate, 4);
  uint32_t byteRate = SAMPLE_RATE * CHANNELS * 2;
  memcpy(header + 28, &byteRate, 4);
  uint16_t blockAlign = CHANNELS * 2;
  memcpy(header + 32, &blockAlign, 2);
  uint16_t bitsPerSample = 16;
  memcpy(header + 34, &bitsPerSample, 2);
  memcpy(header + 36, "data", 4);
  uint32_t dataSize = 0; // 暂无数据
  memcpy(header + 40, &dataSize, 4);
  
  record_file.seek(0);
  record_file.write(header, 44);
}

void ESP32Audio::updateWavHeader() {
  if (!record_file) return;
  uint32_t fileSize = 36 + record_data_size;
  record_file.seek(4);
  record_file.write((uint8_t*)&fileSize, 4);
  record_file.seek(40);
  record_file.write((uint8_t*)&record_data_size, 4);
}

void ESP32Audio::startRecording() {
  Serial.println("🔊 [音频] 准备进行 I2S 麦克风录音...");

  // 如果正在播放，先停止，避免 ES8311 的 DAC/ADC 数据冲突
  if (is_playing) {
    stopAudio();
  }

  // 切换 ES8311 到 ADC 录音模式 (关闭 DAC、打开 ADC 模拟通路)
  ES8311::enableDAC(false);
  ES8311::enableADC(true);

  // 重新配置 I2S_NUM_1 共享播放端的 BCLK/LRC，data_in 指向 ES8311 SDOUT
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DOUT_PIN
  };
  i2s_set_pin(I2S_PORT_REC, &pin_config);

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("❌ 录音失败，SD 卡不可用！");
    // 切回 DAC 模式
    ES8311::enableADC(false);
    ES8311::enableDAC(true);
    return;
  }

  record_file = SD_MMC.open("/record.wav", FILE_WRITE);
  if (!record_file) {
    Serial.println("❌ 录音失败，无法创建文件！");
    ES8311::enableADC(false);
    ES8311::enableDAC(true);
    return;
  }

  record_data_size = 0;
  // 清空电平采样缓冲
  for (uint8_t i = 0; i < 64; i++) last_samples[i] = 0;
  last_sample_idx = 0;
  last_peak_db = 0;
  session_peak = 0;
  last_diag_log_ms = 0;
  writeWavHeader();
  is_recording = true;
  record_start_time = millis();
  Serial.println("🎙️ 录音已正式开始，数据直接写入 TF 卡 /record.wav");
  Serial.println("🎙️ 诊断: 每 200ms 打印一次累计字节数 + 瞬时/会话峰值，用于排查硬件无声问题");
}

bool ESP32Audio::stopRecording(uint8_t*& wavBuffer, size_t& wavSize) {
  if (!is_recording) return false;
  is_recording = false;

  if (record_file) {
    updateWavHeader();
    record_file.close();
    Serial.printf("🎙️ [音频] 录音结束并完成文件封装，总耗时: %lu ms，数据大小: %lu Bytes (%.1f KB), 会话最大峰值: %d / 32767\n",
      (unsigned long)(millis() - record_start_time),
      (unsigned long)record_data_size,
      record_data_size / 1024.0,
      session_peak);
    // 提示诊断结论
    if (record_data_size < 1024) {
      Serial.println("⚠️ 录音文件 < 1KB，说明 I2S 几乎没读到数据。检查 ES8311 ADC 通路、SD 卡连接。");
    } else if (session_peak < 200) {
      Serial.println("⚠️ 录音文件大小正常，但最大电平 < 200/32767（约 0.6%），环境太安静或麦克风信号被衰减。");
    } else {
      Serial.printf("✅ 录音数据正常，可继续上传 ASR。峰值 %d/32767 (%.1f%%)\n", session_peak, session_peak * 100.0 / 32767);
    }
  }

  // 关闭 ADC 通路，切回 DAC 播放模式以便后续 TTS 播放
  ES8311::enableADC(false);
  ES8311::enableDAC(true);

  // 对于 SD 卡模式，这里不返回实际的内存 buffer。网络层将直接读取 SD 文件。
  // 通过返回 nullptr 告知网络层走文件流上传。
  wavBuffer = nullptr;
  wavSize = 0;
  return true;
}

// 录音实时电平估算 - 真实读取 last_samples 中的最近 64 个采样点
int ESP32Audio::getRecordVolumeDb() {
  if (!is_recording) return last_peak_db;
  // 计算最近采样的峰值 (绝对值最大)，转换为 0-100 dB 近似
  int16_t peak = 0;
  for (uint8_t i = 0; i < 64; i++) {
    int16_t v = abs(last_samples[i]);
    if (v > peak) peak = v;
  }
  // 16-bit 采样最大 32767，转换为 dB 标度
  if (peak < 8) {
    peak = 0;
  } else {
    last_peak_db = map(peak, 0, 32767, 0, 100);
  }
  if (last_peak_db < 5 && is_recording) last_peak_db = random(3, 8); // 加点底噪避免完全静止
  return last_peak_db;
}

bool ESP32Audio::playAudioStream(const String& url) {
  // 如果正在录音，先关闭 (录音与播放不能并发)
  if (is_recording) {
    uint8_t* p = nullptr; size_t s = 0;
    stopRecording(p, s);
  }

  // 确保 ES8311 处于 DAC 播放模式
  ES8311::enableADC(false);
  ES8311::enableDAC(true);

  // 重新夺回引脚控制权给播放端 (I2S0)
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);

  Serial.printf("🔊 [音频] 正在拉取并流式播放网络音频: %s\n", url.c_str());
  is_playing = true;
  return audioStream.connecttohost(url.c_str());
}

void ESP32Audio::stopAudio() {
  if (is_playing) {
    audioStream.stopSong();
    is_playing = false;
    Serial.println("🔊 [音频] 强制停止播放音频流。");
  }
}

bool ESP32Audio::isPlaying() {
  return audioStream.isRunning() || is_playing;
}

void ESP32Audio::setVolume(int volume) {
  int mappedVol = map(volume, 0, 100, 0, 21); // 库的音量范围 0 - 21
  audioStream.setVolume(mappedVol);
  Serial.printf("🔊 [音频] 调整系统音量为: %d (映射范围 0-21)\n", mappedVol);
}

void ESP32Audio::update() {
  // 1. 播放音乐流驱动
  if (is_playing) {
    audioStream.loop();
    if (!audioStream.isRunning()) {
      is_playing = false;
      Serial.println("🔊 [音频] 音频流流式播放完毕。");
    }
  }

  // 2. 录音 I2S 流驱动 (避免一次性读取过多导致阻塞，每次抽取部分)
  if (is_recording && record_file) {
    size_t bytes_read = 0;
    uint8_t i2s_read_buff[1024];
    // 50ms 超时，避免 portMAX_DELAY 永久阻塞主循环
    esp_err_t err = i2s_read(I2S_PORT_REC, (void*)i2s_read_buff, sizeof(i2s_read_buff), &bytes_read, pdMS_TO_TICKS(50));
    if (err == ESP_OK && bytes_read > 0) {
      record_file.write(i2s_read_buff, bytes_read);
      record_data_size += bytes_read;

      // 缓存最近几个采样点供 getRecordVolumeDb 计算真实电平
      int16_t* samples = (int16_t*)i2s_read_buff;
      size_t num_samples = bytes_read / sizeof(int16_t);
      int16_t chunk_peak = 0;
      for (size_t i = 0; i < num_samples; i++) {
        last_samples[last_sample_idx] = samples[i];
        last_sample_idx = (last_sample_idx + 1) % 64;
        int16_t av = abs(samples[i]);
        if (av > chunk_peak) chunk_peak = av;
      }
      if (chunk_peak > session_peak) session_peak = chunk_peak;

      // 录音诊断日志：每 200ms 打一次累计字节数 + 当前峰值 + 录制时长
      uint32_t now = millis();
      if (now - last_diag_log_ms > 200) {
        last_diag_log_ms = now;
        Serial.printf("🎙️ [录音] 累计 %lu 字节 (%.1f KB), 瞬时峰值 %d / 32767, 会话峰值 %d / 32767, 录制 %lu ms\n",
          (unsigned long)record_data_size,
          record_data_size / 1024.0,
          chunk_peak,
          session_peak,
          (unsigned long)(now - record_start_time));
      }

      // 保护机制：如果录音超过 1 分钟 (约 2MB)，强制停止避免空间撑爆
      if (millis() - record_start_time > 60000) {
        Serial.println("⚠️ 录音达到 60 秒上限，自动停止。");
        uint8_t* p = nullptr; size_t s = 0;
        stopRecording(p, s);
      }
    } else if (err != ESP_OK) {
      Serial.printf("❌ I2S 读取错误: %d\n", err);
    }
  }
}
