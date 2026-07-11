#include "ESP32Audio.h"
#include "Board.h"
#include "LcdDisplay.h"
#include "ClassPetUI.h"
#include "es8311.h"
#include "es8311_reg.h"
#include <driver/i2c.h>

// 录音参数（与官方示例一致：16kHz, 16-bit, 单声道）
#undef EXAMPLE_SAMPLE_RATE
#undef EXAMPLE_MCLK_MULTIPLE
#undef EXAMPLE_MCLK_FREQ_HZ
#undef EXAMPLE_VOICE_VOLUME
#define REC_SAMPLE_RATE      16000
#define REC_MCLK_MULTIPLE    256

// 播放参数（44.1kHz 标准音频播放）
#define PLAYBACK_SAMPLE_RATE 44100

// I2S 端口号：Audio 库默认使用 I2S_NUM_0
#define I2S_PORT_REC I2S_NUM_0

// 录音电平估算
static int16_t last_samples[64] = {0};
static uint8_t last_sample_idx = 0;
static int last_peak_db = 0;

// ES8311 handle（全局，因为官方 API 需要）
static es8311_handle_t s_es8311_handle = NULL;
static bool s_es8311_inited = false;

// ===== I2C 验证（复用触摸屏 Wire 已安装的 I2C 驱动，不重新配置） =====
static bool initI2CForES8311() {
  // 触摸屏 CST820Touch::init() 已通过 Wire.begin(SDA, SCL) 安装了 I2C_NUM_0 驱动
  // 不调用 i2c_param_config / i2c_driver_install — 避免重置 I2C 总线导致触摸冲突
  // 直接验证 ES8311 I2C 通信：读取 REG00 (RESET 寄存器)
  uint8_t reg_addr = 0x00;
  uint8_t reg_val = 0;
  esp_err_t err = i2c_master_write_read_device(I2C_NUM_0, ES8311_ADDRRES_0,
    &reg_addr, 1, &reg_val, 1, pdMS_TO_TICKS(200));
  if (err != ESP_OK) {
    Serial.printf("❌ [ES8311] I2C 通信失败 (addr=0x%02X): %s\n", ES8311_ADDRRES_0, esp_err_to_name(err));
    Serial.println("❌ [ES8311] 请检查 I2C 接线 (SDA=16, SCL=15) 和 ES8311 供电");
    return false;
  }
  Serial.printf("✅ [ES8311] I2C 通信正常 (REG00=0x%02X, addr=0x%02X)\n", reg_val, ES8311_ADDRRES_0);
  return true;
}

// ===== ES8311 寄存器转储（诊断用） =====
static void dumpES8311Regs() {
  Serial.println("📋 [ES8311] 寄存器转储:");
  // 关键寄存器
  const struct { uint8_t reg; const char* name; } regs[] = {
    {0x00, "RESET"},      {0x01, "CLK_REG01"},   {0x02, "CLK_REG02"},
    {0x09, "SDP_IN"},     {0x0A, "SDP_OUT"},     {0x0D, "SYS_REG0D"},
    {0x0E, "SYS_REG0E"},  {0x12, "DAC_REG12"},   {0x13, "SYS_REG13"},
    {0x14, "SYS_REG14"},  {0x1C, "ADC_REG1C"},   {0x31, "DAC_REG31"},
    {0x32, "DAC_VOL"},    {0x37, "DAC_REG37"},
  };
  for (size_t i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
    uint8_t reg_addr = regs[i].reg;
    uint8_t val = 0;
    esp_err_t err = i2c_master_write_read_device(I2C_NUM_0, ES8311_ADDRRES_0,
      &reg_addr, 1, &val, 1, pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
      Serial.printf("   REG[0x%02X %-10s] = 0x%02X\n", regs[i].reg, regs[i].name, val);
    } else {
      Serial.printf("   REG[0x%02X %-10s] = READ FAIL: %s\n", regs[i].reg, regs[i].name, esp_err_to_name(err));
    }
  }
}

// ===== ES8311 初始化（按官方 Example_17_echo 的 es8311_codec_init 逻辑） =====
bool ESP32Audio::initES8311() {
  // 1. 初始化 I2C
  if (!initI2CForES8311()) {
    return false;
  }

  // 2. 创建 ES8311 handle
  s_es8311_handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (!s_es8311_handle) {
    Serial.println("❌ [ES8311] es8311_create 失败");
    return false;
  }

  // 3. 时钟配置（MCLK 来自 MCLK 引脚, 44100 * 256 = 11.2896MHz）
  const es8311_clock_config_t es_clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = PLAYBACK_SAMPLE_RATE * 256,
    .sample_frequency = PLAYBACK_SAMPLE_RATE
  };

  esp_err_t err = es8311_init(s_es8311_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  if (err != ESP_OK) {
    Serial.printf("❌ [ES8311] es8311_init 失败: %s\n", esp_err_to_name(err));
    return false;
  }

  // 4. 配置采样频率
  err = es8311_sample_frequency_config(s_es8311_handle, PLAYBACK_SAMPLE_RATE * 256, PLAYBACK_SAMPLE_RATE);
  if (err != ESP_OK) {
    Serial.printf("❌ [ES8311] 采样频率配置失败: %s\n", esp_err_to_name(err));
    return false;
  }

  // 5. 设置音量 (0-100, 官方示例用 85-90)
  err = es8311_voice_volume_set(s_es8311_handle, 85, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ [ES8311] 音量设置失败: %s\n", esp_err_to_name(err));
    return false;
  }

  // 6. 配置麦克风为模拟模式（false = analog mic）
  err = es8311_microphone_config(s_es8311_handle, false);
  if (err != ESP_OK) {
    Serial.printf("❌ [ES8311] 麦克风配置失败: %s\n", esp_err_to_name(err));
    return false;
  }

  s_es8311_inited = true;
  Serial.println("✅ [ES8311] 编解码器初始化成功 (44100Hz, MCLK=11.29MHz, vol=85)");
  dumpES8311Regs();
  return true;
}

ESP32Audio::ESP32Audio()
  : is_playing(false), is_recording(false), record_data_size(0),
    record_start_time(0), session_peak(0), last_diag_log_ms(0) {}

ESP32Audio::~ESP32Audio() {
  stopAudio();
}

void ESP32Audio::init() {
  // 1. 使能 FM8002E 功放（IO1 低电平使能）
  pinMode(AUDIO_EN_PIN, OUTPUT);
  digitalWrite(AUDIO_EN_PIN, LOW);
  Serial.printf("🔊 [音频] 功放使能 (IO%d=LOW)\n", AUDIO_EN_PIN);

  // 2. 初始化 ES8311 编解码器（官方驱动）
  bool codec_ok = initES8311();
  if (codec_ok) {
    Serial.println("✅ [音频] ES8311 Codec 初始化成功！");
  } else {
    Serial.println("❌ [音频] ES8311 Codec 初始化失败！");
  }

  // 3. 配置 Audio 库的 I2S 引脚（播放用）
  //    关键修正：按官方示例，I2S 引脚为 BCLK=5, WS=7, DOUT=8, MCLK=4
  //    Audio 库默认使用 I2S_NUM_0
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN,
                        I2S_PIN_NO_CHANGE, I2S_MCLK_PIN);
  audioStream.setVolume(12); // 0-21, 适中音量
  is_playing = false;

  Serial.printf("🔊 [音频] I2S 播放引脚: BCLK=%d, WS=%d, DOUT=%d, MCLK=%d\n",
    I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, I2S_MCLK_PIN);
  Serial.println("🎙️ [音频] 录音驱动将在 startRecording() 时动态切换 (I2S_NUM_0 TX/RX)");
}

void ESP32Audio::writeWavHeader() {
  if (!record_file) return;
  uint8_t header[44] = {0};
  memcpy(header, "RIFF", 4);
  uint32_t fileSize = 36;
  memcpy(header + 4, &fileSize, 4);
  memcpy(header + 8, "WAVEfmt ", 8);
  uint32_t fmtSize = 16;
  memcpy(header + 16, &fmtSize, 4);
  uint16_t fmtCode = 1;
  memcpy(header + 20, &fmtCode, 2);
  uint16_t channels = 1;
  memcpy(header + 22, &channels, 2);
  uint32_t sampleRate = REC_SAMPLE_RATE;
  memcpy(header + 24, &sampleRate, 4);
  uint32_t byteRate = REC_SAMPLE_RATE * 1 * 2;
  memcpy(header + 28, &byteRate, 4);
  uint16_t blockAlign = 1 * 2;
  memcpy(header + 32, &blockAlign, 2);
  uint16_t bitsPerSample = 16;
  memcpy(header + 34, &bitsPerSample, 2);
  memcpy(header + 36, "data", 4);
  uint32_t dataSize = 0;
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
  if (is_recording) {
    Serial.println("⚠️ [音频] 已在录音中，跳过重复 startRecording 请求");
    return;
  }

  Serial.println("🔊 [音频] 准备进行 I2S 麦克风录音...");

  // 如果正在播放，先停止
  if (is_playing) {
    stopAudio();
  }

  // ===== 卸载 I2S_NUM_0 的 TX 驱动（Audio 库），重新安装为 RX 驱动 =====
  Serial.println("🔧 [音频] 卸载 I2S_NUM_0 TX 驱动 (Audio 库播放) ...");
  audioStream.stopSong();
  i2s_driver_uninstall(I2S_NUM_0);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 重新配置 ES8311 为录音模式（16kHz）
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle,
      REC_SAMPLE_RATE * REC_MCLK_MULTIPLE, REC_SAMPLE_RATE);
    es8311_microphone_config(s_es8311_handle, false);
    es8311_microphone_gain_set(s_es8311_handle, ES8311_MIC_GAIN_30DB);
    Serial.println("✅ [ES8311] 已切换到录音模式 (16kHz, MIC_GAIN_30DB)");
  }

  // 安装 I2S_NUM_0 为 MASTER RX
  i2s_config_t i2s_rx_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = REC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  // 录音引脚：DIN=6（麦克风数据输入），DOUT 不用
  i2s_pin_config_t rx_pin_config = {
    .mck_io_num = I2S_MCLK_PIN,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN_PIN
  };

  esp_err_t install_err = i2s_driver_install(I2S_PORT_REC, &i2s_rx_config, 0, NULL);
  esp_err_t pin_err = i2s_set_pin(I2S_PORT_REC, &rx_pin_config);

  if (install_err != ESP_OK || pin_err != ESP_OK) {
    Serial.printf("❌ [音频] I2S RX 安装失败! install=%s pin=%s\n",
      esp_err_to_name(install_err), esp_err_to_name(pin_err));
    restorePlaybackI2S();
    return;
  }
  Serial.println("✅ [音频] I2S_NUM_0 已切换为 MASTER RX 模式 (DIN=6)");

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("❌ 录音失败，SD 卡不可用！");
    i2s_driver_uninstall(I2S_PORT_REC);
    restorePlaybackI2S();
    return;
  }

  record_file = SD_MMC.open("/record.wav", FILE_WRITE);
  if (!record_file) {
    Serial.println("❌ 录音失败，无法创建文件！");
    i2s_driver_uninstall(I2S_PORT_REC);
    restorePlaybackI2S();
    return;
  }

  record_data_size = 0;
  for (uint8_t i = 0; i < 64; i++) last_samples[i] = 0;
  last_sample_idx = 0;
  last_peak_db = 0;
  session_peak = 0;
  last_diag_log_ms = 0;
  writeWavHeader();
  is_recording = true;
  record_start_time = millis();
  Serial.println("🎙️ 录音已开始 (16kHz, 16-bit, mono), 数据写入 SD 卡 /record.wav");
}

bool ESP32Audio::stopRecording(uint8_t*& wavBuffer, size_t& wavSize) {
  if (!is_recording) return false;
  is_recording = false;

  if (record_file) {
    updateWavHeader();
    record_file.close();
    Serial.printf("🎙️ [音频] 录音结束: %lu ms, %lu Bytes (%.1f KB), 峰值: %d/32767\n",
      (unsigned long)(millis() - record_start_time),
      (unsigned long)record_data_size,
      record_data_size / 1024.0,
      session_peak);
    if (record_data_size < 1024) {
      Serial.println("⚠️ 录音文件 < 1KB，I2S 几乎没读到数据。检查 ES8311 ADC 通路。");
    } else if (session_peak < 200) {
      Serial.println("⚠️ 录音文件大小正常，但电平 < 200/32767，环境太安静或麦克风采声不足。");
    } else {
      Serial.printf("✅ 录音数据正常。峰值 %d/32767 (%.1f%%)\n", session_peak, session_peak * 100.0 / 32767);
    }
  }

  // ===== 卸载 RX 驱动，恢复 TX 驱动 =====
  Serial.println("🔧 [音频] 卸载 I2S_NUM_0 RX 驱动，恢复 TX 播放驱动...");
  i2s_driver_uninstall(I2S_PORT_REC);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 恢复 ES8311 为播放模式（44.1kHz）
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle,
      PLAYBACK_SAMPLE_RATE * 256, PLAYBACK_SAMPLE_RATE);
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
    Serial.println("✅ [ES8311] 已切换回播放模式 (44100Hz, vol=85)");
  }

  // 恢复 I2S_NUM_0 给 Audio 库使用
  restorePlaybackI2S();

  wavBuffer = nullptr;
  wavSize = 0;
  return true;
}

int ESP32Audio::getRecordVolumeDb() {
  if (!is_recording) return last_peak_db;
  int16_t peak = 0;
  for (uint8_t i = 0; i < 64; i++) {
    int16_t v = abs(last_samples[i]);
    if (v > peak) peak = v;
  }
  if (peak < 8) {
    peak = 0;
  } else {
    last_peak_db = map(peak, 0, 32767, 0, 100);
  }
  if (last_peak_db < 5 && is_recording) last_peak_db = random(3, 8);
  return last_peak_db;
}

void ESP32Audio::restorePlaybackI2S() {
  Serial.println("🔧 [音频] 恢复 I2S_NUM_0 (Audio 库播放驱动)...");
  audioStream.setI2SCommFMT_LSB(false);
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN,
                        I2S_PIN_NO_CHANGE, I2S_MCLK_PIN);
  audioStream.setVolume(12);
  Serial.println("✅ [音频] I2S_NUM_0 播放驱动已恢复");
}

bool ESP32Audio::playAudioStream(const String& url) {
  if (is_recording) {
    uint8_t* p = nullptr; size_t s = 0;
    stopRecording(p, s);
  }

  // 确保 ES8311 处于播放模式
  if (s_es8311_inited && s_es8311_handle) {
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
  }

  // 确保 I2S 引脚已配置
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN,
                        I2S_PIN_NO_CHANGE, I2S_MCLK_PIN);

  Serial.printf("🔊 [音频] 正在播放网络音频: %s\n", url.c_str());
  is_playing = true;
  bool ret = audioStream.connecttohost(url.c_str());
  if (!ret) {
    is_playing = false;
    Serial.println("❌ [音频] 网络连接失败，停止播放。");
  }
  return ret;
}

void ESP32Audio::stopAudio() {
  if (is_playing) {
    audioStream.stopSong();
    is_playing = false;
    Serial.println("🔊 [音频] 强制停止播放音频流。");
  }
}

bool ESP32Audio::isPlaying() {
  return audioStream.isRunning() && is_playing;
}

void ESP32Audio::setVolume(int volume) {
  int mappedVol = map(volume, 0, 100, 0, 21);
  audioStream.setVolume(mappedVol);
  // 同步设置 ES8311 硬件音量
  if (s_es8311_inited && s_es8311_handle) {
    es8311_voice_volume_set(s_es8311_handle, volume, NULL);
  }
  Serial.printf("🔊 [音频] 音量: %d (Audio库=%d, ES8311=%d%%)\n", volume, mappedVol, volume);
}

void ESP32Audio::update() {
  // 1. 播放音乐流驱动
  if (is_playing) {
    audioStream.loop();
    if (!audioStream.isRunning()) {
      is_playing = false;
      Serial.println("🔊 [音频] 音频流播放完毕。");
    }
  }

  // 2. 录音 I2S 流驱动
  if (is_recording && record_file) {
    size_t bytes_read = 0;
    uint8_t i2s_read_buff[1024];
    esp_err_t err = i2s_read(I2S_PORT_REC, (void*)i2s_read_buff, sizeof(i2s_read_buff), &bytes_read, pdMS_TO_TICKS(50));
    if (err == ESP_OK && bytes_read > 0) {
      record_file.write(i2s_read_buff, bytes_read);
      record_data_size += bytes_read;

      // 流式上行: 把录到的 PCM 块通过回调交给 WS 客户端 (Route B)
      if (_streamUpEnabled && _pcmUploadCb) {
        _pcmUploadCb(i2s_read_buff, bytes_read);
      }

      int16_t* samples = (int16_t*)i2s_read_buff;
      size_t num_samples = bytes_read / sizeof(int16_t);
      int16_t chunk_peak = 0;
      for (size_t i = 0; i < num_samples && i < 64; i++) {
        last_samples[last_sample_idx] = samples[i];
        last_sample_idx = (last_sample_idx + 1) % 64;
        int16_t av = abs(samples[i]);
        if (av > chunk_peak) chunk_peak = av;
      }
      if (chunk_peak > session_peak) session_peak = chunk_peak;

      uint32_t now = millis();
      if (now - last_diag_log_ms > 200) {
        last_diag_log_ms = now;
        Serial.printf("🎙️ [录音] %lu 字节 (%.1f KB), 瞬时峰值 %d/32767, 会话峰值 %d/32767, %lu ms\n",
          (unsigned long)record_data_size,
          record_data_size / 1024.0,
          chunk_peak,
          session_peak,
          (unsigned long)(now - record_start_time));
      }

      if (millis() - record_start_time > 60000) {
        Serial.println("⚠️ 录音达到 60 秒上限，自动停止。");
        uint8_t* p = nullptr; size_t s = 0;
        stopRecording(p, s);
      }
    } else if (err != ESP_OK) {
      Serial.printf("❌ I2S 读取错误: %s\n", esp_err_to_name(err));
    }
  }
}

// ===== 直接 I2S 音调测试（绕过 Audio 库，直接写 I2S 数据） =====
// 验证 I2S → ES8311 DAC → FM8002E 功放 → 喇叭 全链路
void ESP32Audio::playTestTone(int frequency, int duration_ms) {
  // 1. 停止当前播放
  if (is_playing) {
    audioStream.stopSong();
    is_playing = false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // 2. 确保 ES8311 处于播放模式
  if (s_es8311_inited && s_es8311_handle) {
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
    Serial.println("🔊 [音调测试] ES8311 已设为播放模式 (vol=85)");
  }

  // 3. 确保功放使能
  digitalWrite(AUDIO_EN_PIN, LOW);
  Serial.printf("🔊 [音调测试] 功放使能 IO%d=%s\n", AUDIO_EN_PIN, digitalRead(AUDIO_EN_PIN) ? "HIGH" : "LOW");

  // 4. 设置 I2S 采样率为 44100Hz
  i2s_set_sample_rates(I2S_NUM_0, PLAYBACK_SAMPLE_RATE);
  Serial.printf("🔊 [音调测试] 开始播放 %dHz 正弦波, 持续 %dms\n", frequency, duration_ms);

  // 5. 生成并写入正弦波数据
  //    Audio 库配置 I2S 为立体声 (I2S_CHANNEL_FMT_RIGHT_LEFT)，需要写左右声道交错数据
  const int buf_samples = 256;  // 256 个立体声采样 = 512 个 int16_t
  int16_t sample_buf[buf_samples * 2];  // 每个采样 2 个 int16_t (左+右)
  const double two_pi_f = 2.0 * M_PI * frequency;
  const double inv_sample_rate = 1.0 / PLAYBACK_SAMPLE_RATE;

  uint32_t start_time = millis();
  uint32_t sample_idx = 0;
  while (millis() - start_time < (uint32_t)duration_ms) {
    for (int i = 0; i < buf_samples; i++) {
      double t = (double)(sample_idx + i) * inv_sample_rate;
      int16_t val = (int16_t)(16000.0 * sin(two_pi_f * t));  // 振幅 ~50% 防止爆音
      sample_buf[i * 2] = val;      // 左声道
      sample_buf[i * 2 + 1] = val;  // 右声道
    }
    size_t bytes_written = 0;
    esp_err_t err = i2s_write(I2S_NUM_0, sample_buf, sizeof(sample_buf),
                               &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
      Serial.printf("❌ [音调测试] i2s_write 失败: %s\n", esp_err_to_name(err));
      break;
    }
    sample_idx += buf_samples;
  }

  // 6. 写入静音缓冲区（防止最后一段杂音）
  memset(sample_buf, 0, sizeof(sample_buf));
  size_t bytes_written = 0;
  i2s_write(I2S_NUM_0, sample_buf, sizeof(sample_buf), &bytes_written, pdMS_TO_TICKS(100));

  Serial.println("🔊 [音调测试] 完成！如果听到蜂鸣声说明 I2S → ES8311 → 功放 → 喇叭 全链路正常。");
  Serial.println("🔊 [音调测试] 如果没声音，可能原因:");
  Serial.println("   1. MCLK 没有输出（ES8311 无时钟）");
  Serial.println("   2. ES8311 寄存器配置不正确（看上方寄存器转储）");
  Serial.println("   3. FM8002E 功放未使能或损坏");
  Serial.println("   4. 喇叭接线问题");
}

// ================= 流式语音 PCM 播放 (Route B) =================
// 16kHz 单声道 PCM -> 立体声 I2S TX, 边收边播

bool ESP32Audio::startPcmPlayback() {
  if (_pcm_playing) return true;

  // 1. ES8311 切到 16kHz 播放模式
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle, 16000 * 256, 16000);
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
  }
  // 确保功放使能
  digitalWrite(AUDIO_EN_PIN, LOW);

  // 2. 卸载可能已安装的 Audio 库 TX 驱动, 再装我们自己的 16k TX 驱动
  audioStream.stopSong();
  i2s_driver_uninstall(I2S_NUM_0);
  vTaskDelay(pdMS_TO_TICKS(50));

  i2s_config_t tx_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t tx_pin = {
    .mck_io_num = I2S_MCLK_PIN,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t e = i2s_driver_install(I2S_NUM_0, &tx_cfg, 0, NULL);
  if (e != ESP_OK) {
    Serial.printf("❌ [PCM] TX 驱动安装失败: %s\n", esp_err_to_name(e));
    restorePlaybackI2S();
    return false;
  }
  i2s_set_pin(I2S_NUM_0, &tx_pin);
  _pcm_playing = true;
  is_playing = false; // 与 audioStream 的 is_playing 区分
  Serial.println("🔊 [PCM] 播放驱动就绪 (16k stereo), 等待下行音频...");
  return true;
}

void ESP32Audio::feedPcm(const uint8_t* data, size_t len) {
  if (!_pcm_playing) return;
  if (len < 2) return;
  size_t mono_samples = len / 2;
  if (mono_samples > 512) mono_samples = 512; // 静态缓冲上限

  // 单声道 -> 立体声 (L=R), 写到 I2S TX (ES8311 期望立体声 I2S)
  static int16_t stereo_buf[1024];
  const int16_t* mono = (const int16_t*)data;
  for (size_t i = 0; i < mono_samples; i++) {
    stereo_buf[i * 2] = mono[i];
    stereo_buf[i * 2 + 1] = mono[i];
  }
  size_t to_write = mono_samples * 2 * sizeof(int16_t);
  size_t written = 0;
  i2s_write(I2S_NUM_0, stereo_buf, to_write, &written, pdMS_TO_TICKS(50));
}

void ESP32Audio::stopPcmPlayback() {
  if (!_pcm_playing) return;
  _pcm_playing = false;
  i2s_driver_uninstall(I2S_NUM_0);
  vTaskDelay(pdMS_TO_TICKS(50));
  restorePlaybackI2S(); // 恢复 Audio 库的引脚配置 (下次 MP3 播放 connecttohost 会重装驱动)

  // ES8311 恢复 44.1k 播放模式
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle, 44100 * 256, 44100);
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
  }
  Serial.println("🔊 [PCM] 播放停止, 已恢复播放模式");
}
