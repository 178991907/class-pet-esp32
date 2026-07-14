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

// 本地辅助：直接通过 I2C 写 ES8311 寄存器（绕过驱动内 static 函数）
static esp_err_t es8311_write_reg_local(uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = {reg, val};
  return i2c_master_write_to_device(I2C_NUM_0, ES8311_ADDRRES_0, buf, 2, pdMS_TO_TICKS(100));
}
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

  // 7. 使能 MICBIAS 麦克风偏置电压(~2.7V, 寄存器0x0F)。
  //    驱动 es8311_init 默认不动 0x0F; 模拟 MEMS 麦克风无偏置则信号极弱(差~27dB),
  //    导致录音/唤醒词收不到人声。必须在 init 一次性打开。
  es8311_write_reg_local(ES8311_SYSTEM_REG10, 0x41);  // 关键: 使能 VMID/麦克风偏置(寄存器0x10)

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

  // 3. I2S 引脚由 I2S 模式管理器 (ensureRxMic/installTxPlay) 统一设置.
  //    启动时不预装 TX 驱动 —— 空闲基线为 RX(麦克风)模式, 由唤醒词引擎经 ensureRxMic() 装入.
  audioStream.setVolume(12); // 0-21, 适中音量 (仅设置 AudioStream 音量参数, 不安装驱动)
  is_playing = false;
  _i2sMode = I2S_MODE_NONE;

  Serial.printf("🔊 [音频] I2S 播放引脚 (播放时): BCLK=%d, WS=%d, DOUT=%d, MCLK=%d\n",
    I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, I2S_MCLK_PIN);
  Serial.println("🎙️ [音频] 空闲基线 = RX(麦克风)模式, 由唤醒词引擎 ensureRxMic() 装入 (I2S_NUM_0)");
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

  // 如果正在播放，先停止 (stopAudio 内会 stopSong, 后续 ensureRxMic 负责卸载 TX)
  if (is_playing) {
    stopAudio();
  }

  // 确保 I2S_NUM_0 处于 RX(麦克风)模式.
  // 空闲态已由唤醒词引擎装入 RX 基线, ensureRxMic 幂等复用; 若处于 TX(播放)则自动切换.
  // ES8311 麦克风模式 + 静音功放由 ensureRxMic 内部统一处理 (与唤醒词监听一致).
  if (!ensureRxMic()) {
    Serial.println("❌ [音频] 无法进入 RX 麦克风模式, 录音中止");
    restoreIdleRxMic();
    return;
  }
  Serial.printf("✅ [音频] I2S_NUM_0 已就绪 MASTER RX (DIN=GPIO%d)\n", I2S_DIN_PIN);

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("❌ 录音失败，SD 卡不可用！");
    restoreIdleRxMic();
    return;
  }

  record_file = SD_MMC.open("/record.wav", FILE_WRITE);
  if (!record_file) {
    Serial.println("❌ 录音失败，无法创建文件！");
    restoreIdleRxMic();
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

  // ===== 录音结束: 恢复 RX(麦克风)空闲基线 (ES8311 mic 模式 + 静音功放) =====
  // 空闲态唤醒词监听依赖此基线; 若接下来要播放, playAudioStream/startPcmPlayback 会自行切 TX.
  restoreIdleRxMic();

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

// ================= I2S_NUM_0 单一所有者: 模式管理器 =================
// 唤醒词监听 / 录音 共用 RX(麦克风) 基线; 播放时临时切 TX, 结束恢复 RX。
// 所有 i2s_driver_install / uninstall 只发生在这里, 杜绝多子系统争夺同一外设。

bool ESP32Audio::ensureRxMic() {
  if (_i2sMode == I2S_MODE_RX_MIC) return true;   // 幂等: 已是 RX 麦克风基线
  // 卸载可能存在的 TX / 旧驱动 (无驱动时 uninstall 返回错误可忽略)
  i2s_driver_uninstall(I2S_NUM_0);
  vTaskDelay(pdMS_TO_TICKS(20));

  // ES8311 切到麦克风模式 + 静音功放。
  // 关键: 必须完全交给驱动 API 配置, 不得手动写 REG14/REG17 —— 驱动
  // es8311_microphone_config(false) 已正确写 REG14=0x1A(使能模拟MIC+最大PGA增益)
  // 与 REG17=0xC8(ADC增益); 曾经误写 REG14=0x0A 把"模拟MIC使能"位清掉, 导致 ADC
  // 读不到麦克风(恒定噪声), 唤醒词/录音永远收不到人声。
  digitalWrite(AUDIO_EN_PIN, HIGH);
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle,
      REC_SAMPLE_RATE * REC_MCLK_MULTIPLE, REC_SAMPLE_RATE);
    es8311_microphone_config(s_es8311_handle, false);
    es8311_microphone_gain_set(s_es8311_handle, ES8311_MIC_GAIN_30DB);
    // 关键: 使能 ES8311 MICBIAS 麦克风偏置(~2.7V, 寄存器0x0F)。
    // 驱动 es8311_init 默认不动 0x0F, 模拟 MEMS 麦克风无偏置则信号弱~27dB, 唤醒词收不到人声。
    es8311_write_reg_local(ES8311_SYSTEM_REG10, 0x41);  // 关键: 使能 VMID/麦克风偏置(寄存器0x10), 否则模拟麦克风信号被压低~30dB
  }

  i2s_config_t rx_cfg = {
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
  i2s_pin_config_t rx_pins = {
    .mck_io_num = I2S_MCLK_PIN,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN_PIN
  };
  esp_err_t e = i2s_driver_install(I2S_NUM_0, &rx_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &rx_pins);
  if (e != ESP_OK) {
    Serial.printf("⚠️ [音频] ensureRxMic 安装返回 %s (已尽力继续)\n", esp_err_to_name(e));
  }
  _i2sMode = I2S_MODE_RX_MIC;
  Serial.printf("✅ [音频] I2S_NUM_0 已置 RX(麦克风)基线 (DIN=GPIO%d, 16kHz)\n", I2S_DIN_PIN);
  return true;
}

void ESP32Audio::releaseI2s() {
  i2s_driver_uninstall(I2S_NUM_0);
  _i2sMode = I2S_MODE_NONE;
}

void ESP32Audio::installTxPlay(uint32_t sampleRate) {
  i2s_driver_uninstall(I2S_NUM_0);
  vTaskDelay(pdMS_TO_TICKS(20));
  digitalWrite(AUDIO_EN_PIN, LOW);  // 功放使能 (播放需出声)
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle, sampleRate * 256, sampleRate);
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
  }
  i2s_config_t tx_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sampleRate,
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
  i2s_pin_config_t tx_pins = {
    .mck_io_num = I2S_MCLK_PIN,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  esp_err_t e = i2s_driver_install(I2S_NUM_0, &tx_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &tx_pins);
  if (e != ESP_OK) {
    Serial.printf("⚠️ [音频] installTxPlay(%u) 安装返回 %s\n", sampleRate, esp_err_to_name(e));
  }
  _i2sMode = I2S_MODE_TX_PLAY;
  Serial.printf("🔊 [音频] I2S_NUM_0 已置 TX(播放)驱动 (DOUT=GPIO%d, %uHz)\n", I2S_DOUT_PIN, sampleRate);
}

void ESP32Audio::restoreIdleRxMic() {
  _i2sMode = I2S_MODE_NONE;   // 强制 ensureRxMic 重新安装, 避免状态机 shortcut 误判
  ensureRxMic();
}

// ===== 离线唤醒词 (esp-sr) (向后兼容, 新代码走 ensureRxMic) =====
// 切换 ES8311 到麦克风输入并静音功放。I2S RX 的安装/卸载统一由上面的模式管理器负责。
void ESP32Audio::enterWakeMicMode() {
  // 录音时必须关闭功放 (FM8002E IO1 低电平使能), 否则扬声器播放环境噪音/回授,
  // 淹没麦克风真实信号并产生吱啦声。
  digitalWrite(AUDIO_EN_PIN, HIGH);
  Serial.printf("🔇 [音频] 唤醒词监听: 静音功放 (IO%d=HIGH)\n", AUDIO_EN_PIN);

  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle,
      REC_SAMPLE_RATE * REC_MCLK_MULTIPLE, REC_SAMPLE_RATE);
    es8311_microphone_config(s_es8311_handle, false);
    es8311_microphone_gain_set(s_es8311_handle, ES8311_MIC_GAIN_30DB);
    es8311_write_reg_local(ES8311_SYSTEM_REG10, 0x41);  // 关键: 使能 VMID/麦克风偏置(寄存器0x10), 否则模拟麦克风信号被压低~30dB  // 使能 MICBIAS 偏置(~2.7V)
    Serial.println("✅ [音频] 唤醒词监听: ES8311 已切到麦克风模式 (16kHz, MICBIAS=0x34, GAIN_30DB)");
  }
}

void ESP32Audio::exitWakeMicMode() {
  // 恢复功放使能 (低电平), 让后续播放/录音流程能正常出声 (它们会自行重设 ES8311 模式)
  digitalWrite(AUDIO_EN_PIN, LOW);
  Serial.printf("🔊 [音频] 退出唤醒词监听: 恢复功放 (IO%d=LOW)\n", AUDIO_EN_PIN);
}

bool ESP32Audio::playAudioStream(const String& url) {
  if (is_recording) {
    uint8_t* p = nullptr; size_t s = 0;
    stopRecording(p, s);
  }

  // ES8311 切到 44.1k 播放模式 + 功放使能 (播放需出声)
  digitalWrite(AUDIO_EN_PIN, LOW);
  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle,
      PLAYBACK_SAMPLE_RATE * 256, PLAYBACK_SAMPLE_RATE);
    es8311_voice_volume_set(s_es8311_handle, 85, NULL);
  }

  // 卸载当前 I2S 驱动 (RX 基线或上一首 TX), 让 AudioStream 在 connecttohost 时干净地安装 TX.
  releaseI2s();
  // 确保 I2S 引脚已配置 (AudioStream 内部 i2s_driver_install 用这些引脚)
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN,
                        I2S_PIN_NO_CHANGE, I2S_MCLK_PIN);

  Serial.printf("🔊 [音频] 正在播放网络音频: %s\n", url.c_str());
  is_playing = true;
  bool ret = audioStream.connecttohost(url.c_str());
  if (!ret) {
    is_playing = false;
    Serial.println("❌ [音频] 网络连接失败，停止播放。");
    restoreIdleRxMic();   // 失败也回到 RX 基线, 保持唤醒词可监听
  } else {
    _i2sMode = I2S_MODE_TX_PLAY;  // AudioStream 已装 TX 驱动
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
      // 播放结束: 恢复 RX(麦克风)空闲基线, 让唤醒词引擎可继续监听
      restoreIdleRxMic();
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

  // 2. 统一由模式管理器安装 TX(44100) 播放驱动 (ES8311 DAC + 功放使能)
  audioStream.stopSong();
  installTxPlay(PLAYBACK_SAMPLE_RATE);
  Serial.println("🔊 [音调测试] ES8311 已设为播放模式 (vol=85)");
  Serial.printf("🔊 [音调测试] 功放使能 IO%d=%s\n", AUDIO_EN_PIN, digitalRead(AUDIO_EN_PIN) ? "HIGH" : "LOW");

  // 3. 开始播放
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

  // 恢复 RX(麦克风)空闲基线, 让唤醒词引擎回到可监听状态
  restoreIdleRxMic();
}

// ================= 麦克风寄存器扫描诊断 =================
void ESP32Audio::micSweepTest() {
  Serial.println("[MICSWEEP] 开始扫描 ES8311 ADC 输入/音量寄存器");
  Serial.println("[MICSWEEP] 请持续对着麦克风说话 (约 15 秒), 直到扫描结束");
  Serial.flush();

  // 关闭功放, 避免回授
  digitalWrite(AUDIO_EN_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(20));

  // 卸载当前驱动 (统一由模式管理器), 标记 NONE
  releaseI2s();
  vTaskDelay(pdMS_TO_TICKS(30));

  // 安装 I2S_NUM_0 为 MASTER RX (与 startRecording 一致)
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
  i2s_pin_config_t rx_pin_config = {
    .mck_io_num = I2S_MCLK_PIN,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN_PIN
  };
  esp_err_t ierr = i2s_driver_install(I2S_NUM_0, &i2s_rx_config, 0, NULL);
  if (ierr != ESP_OK) {
    Serial.printf("[MICSWEEP] RX 驱动安装失败: %s\n", esp_err_to_name(ierr));
    restoreIdleRxMic();
    return;
  }
  i2s_set_pin(I2S_NUM_0, &rx_pin_config);
  _i2sMode = I2S_MODE_RX_MIC;

  if (s_es8311_inited && s_es8311_handle) {
    es8311_sample_frequency_config(s_es8311_handle, REC_SAMPLE_RATE * REC_MCLK_MULTIPLE, REC_SAMPLE_RATE);
  }

  // ---- 偏置电压扫描: 找出能让麦克风信号增强的 MICBIAS 寄存器/值 ----
  // (不同版本 ES8311 偏置寄存器不同: 0x0F / 0x16 / 0x10 均有说法, 实测 0x0F=0x34 无效)
  struct {uint8_t reg; uint8_t val;} biases[] = {
    {0x0F,0x34},{0x0F,0x3C},{0x0F,0x44},{0x16,0x02},{0x10,0x41},{0xFF,0x00}
  };
  int Nb = sizeof(biases)/sizeof(biases[0]);
  for (int k = 0; k < Nb; k++) {
    uint8_t br = biases[k].reg, bv = biases[k].val;
    if (br != 0xFF && s_es8311_inited && s_es8311_handle) {
      es8311_microphone_config(s_es8311_handle, false);
      es8311_write_reg_local(ES8311_SYSTEM_REG14, 0x1A);
      es8311_write_reg_local(ES8311_ADC_REG17, 0xF0);
      es8311_write_reg_local(br, bv);
      es8311_microphone_gain_set(s_es8311_handle, ES8311_MIC_GAIN_30DB);
    }
    vTaskDelay(pdMS_TO_TICKS(60));
    int64_t sumSq=0; int32_t n=0; int16_t pk=0; uint32_t t0=millis();
    uint8_t buf[512];
    while (millis()-t0 < 300) {
      size_t rd=0;
      if (i2s_read(I2S_NUM_0, buf, sizeof(buf), &rd, pdMS_TO_TICKS(40))==ESP_OK && rd>0) {
        int16_t* s=(int16_t*)buf; int cnt=rd/2;
        for(int i=0;i<cnt;i++){int32_t v=s[i];sumSq+=(int64_t)v*v;int16_t av=abs(v);if(av>pk)pk=av;n++;}
      }
    }
    int rms = n? (int)sqrt((double)sumSq/n):0;
    Serial.printf("[MICSWEEP] BIAS R%02X=%02X -> rms=%4d pk=%5d\n", br, bv, rms, pk);
  }
  Serial.println("[MICSWEEP] === 偏置扫描结束, 以下继续 REG14/REG17 扫描 ===");

  // 仅扫 bit4=1 的组合 (bit4=0 会关闭模拟麦克风输入, 见 ES8311 REG14 定义)
  const uint8_t reg14s[] = {0x10,0x12,0x14,0x16,0x18,0x1A,0x1C,0x1E};
  const uint8_t reg17s[] = {0xC8, 0xE0, 0xF0};
  const int N14 = sizeof(reg14s)/sizeof(reg14s[0]);
  const int N17 = sizeof(reg17s)/sizeof(reg17s[0]);

  uint8_t i2s_read_buff[512];
  for (int a = 0; a < N14; a++) {
    uint8_t r14 = reg14s[a];
    for (int b = 0; b < N17; b++) {
      uint8_t r17 = reg17s[b];
      // 配置 ES8311: 默认 mic 配置 + 覆盖 REG14/REG17
      if (s_es8311_inited && s_es8311_handle) {
        es8311_microphone_config(s_es8311_handle, false);
        es8311_write_reg_local(ES8311_SYSTEM_REG14, r14);
        es8311_write_reg_local(ES8311_ADC_REG17, r17);
        es8311_microphone_gain_set(s_es8311_handle, ES8311_MIC_GAIN_30DB);
      }
      vTaskDelay(pdMS_TO_TICKS(60));

      int64_t sumSq = 0;
      int32_t n = 0;
      int16_t pk = 0;
      uint32_t t0 = millis();
      while (millis() - t0 < 300) {
        size_t rd = 0;
        esp_err_t e = i2s_read(I2S_NUM_0, i2s_read_buff, sizeof(i2s_read_buff), &rd, pdMS_TO_TICKS(40));
        if (e == ESP_OK && rd > 0) {
          int16_t* s = (int16_t*)i2s_read_buff;
          int cnt = rd / 2;
          for (int i = 0; i < cnt; i++) {
            int32_t v = s[i];
            sumSq += (int64_t)v * v;
            int16_t av = abs(v);
            if (av > pk) pk = av;
            n++;
          }
        }
      }
      int rms = (n > 0) ? (int)sqrt((double)sumSq / n) : 0;
      Serial.printf("[MICSWEEP] R14=0x%02X R17=0x%02X rms=%5d pk=%5d\n", r14, r17, rms, pk);
      Serial.flush();
    }
  }

  Serial.println("[MICSWEEP] 扫描完成。rms/pk 显著大于 0 且不过载(<=20000)的组合即为可用配置。");
  // 恢复 RX(麦克风)空闲基线, 让唤醒词引擎回到可监听状态
  restoreIdleRxMic();
}

// ================= 流式语音 PCM 播放 (Route B) =================
// 16kHz 单声道 PCM -> 立体声 I2S TX, 边收边播

bool ESP32Audio::startPcmPlayback() {
  // 防御: 录音进行中绝对不能切播放, 否则 I2S_NUM_0 RX 驱动被卸载/重写, 录音 update() 崩溃
  if (is_recording) {
    Serial.println("⚠️ [PCM] 录音进行中, 拒绝切换播放, 避免 I2S RX/TX 冲突");
    return false;
  }
  if (_pcm_playing) return true;

  // 停止可能进行的 MP3 播放, 然后统一由模式管理器安装 TX(16k) 驱动
  audioStream.stopSong();
  installTxPlay(16000);   // 卸载现有 + 装 TX 16k + ES8311 DAC 16k + 功放使能
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
  // 恢复 RX(麦克风)空闲基线 (ES8311 mic + 静音功放), 让唤醒词引擎继续监听
  restoreIdleRxMic();
  Serial.println("🔊 [PCM] 播放停止, 已恢复 RX(麦克风)基线");
}
