#include "ESP32Audio.h"
#include "ES8311.h"

#include "Board.h"

// 录音参数
#define I2S_PORT_REC I2S_NUM_1
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT
#define CHANNELS 1

ESP32Audio::ESP32Audio() 
  : is_playing(false), is_recording(false), record_data_size(0) {}

ESP32Audio::~ESP32Audio() {
  stopAudio();
}

void ESP32Audio::init() {
  // 1. 初始化播放音频 (I2S_NUM_0 由内部使用)
  pinMode(AUDIO_EN_PIN, OUTPUT);
  digitalWrite(AUDIO_EN_PIN, HIGH);
  ES8311::init(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  audioStream.setVolume(12); // 设置适度音量，防止小喇叭爆音
  is_playing = false;
  Serial.printf("🔊 [音频] ESP32-S3 I2S 音频输出(BCLK:%d, LRC:%d, DOUT:%d)初始化完成。\n", I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);

  // 2. 初始化录音麦克风驱动 (I2S_NUM_1)
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
    .bck_io_num = I2S_MIC_SCK_PIN,
    .ws_io_num = I2S_MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD_PIN
  };
  
  if (i2s_driver_install(I2S_PORT_REC, &i2s_config, 0, NULL) == ESP_OK) {
    if (i2s_set_pin(I2S_PORT_REC, &pin_config) == ESP_OK) {
      Serial.printf("🎙️ [音频] I2S 麦克风录音驱动(SCK:%d, WS:%d, SD:%d)初始化成功。\n", I2S_MIC_SCK_PIN, I2S_MIC_WS_PIN, I2S_MIC_SD_PIN);
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
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("❌ 录音失败，SD 卡不可用！");
    return;
  }
  
  record_file = SD_MMC.open("/record.wav", FILE_WRITE);
  if (!record_file) {
    Serial.println("❌ 录音失败，无法创建文件！");
    return;
  }
  
  record_data_size = 0;
  writeWavHeader();
  is_recording = true;
  record_start_time = millis();
  Serial.println("🎙️ 录音已正式开始，数据直接写入 TF 卡 /record.wav");
}

bool ESP32Audio::stopRecording(uint8_t*& wavBuffer, size_t& wavSize) {
  if (!is_recording) return false;
  is_recording = false;
  
  if (record_file) {
    updateWavHeader();
    record_file.close();
    Serial.printf("🎙️ [音频] 录音结束并完成文件封装，总耗时: %lu ms，数据大小: %lu Bytes\n", (unsigned long)(millis() - record_start_time), (unsigned long)record_data_size);
  }
  
  // 对于 SD 卡模式，这里不返回实际的内存 buffer。网络层将直接读取 SD 文件。
  // 通过返回 nullptr 告知网络层走文件流上传。
  wavBuffer = nullptr;
  wavSize = 0;
  return true;
}

int ESP32Audio::getRecordVolumeDb() {
  if (!is_recording) return 0;
  // 这里可以做真正的电平计算，为了不阻塞，先放一个动态模拟值让 UI 有波形跳动
  return random(30, 85); 
}

bool ESP32Audio::playAudioStream(const String& url) {
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
    size_t bytes_read;
    uint8_t i2s_read_buff[1024];
    esp_err_t err = i2s_read(I2S_PORT_REC, (void*)i2s_read_buff, sizeof(i2s_read_buff), &bytes_read, portMAX_DELAY);
    if (err == ESP_OK && bytes_read > 0) {
      record_file.write(i2s_read_buff, bytes_read);
      record_data_size += bytes_read;
      
      // 保护机制：如果录音超过 1 分钟 (约 2MB)，强制停止避免空间撑爆
      if (millis() - record_start_time > 60000) {
        Serial.println("⚠️ 录音达到 60 秒上限，自动停止。");
        uint8_t* p = nullptr; size_t s = 0;
        stopRecording(p, s);
      }
    }
  }
}
