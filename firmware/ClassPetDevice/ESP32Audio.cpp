#include "ESP32Audio.h"

ESP32Audio::ESP32Audio() 
  : is_playing(false), fake_wav_buffer(nullptr), fake_wav_size(0) {}

ESP32Audio::~ESP32Audio() {
  if (fake_wav_buffer) {
    delete[] fake_wav_buffer;
  }
}

void ESP32Audio::init() {
  // S3 没有内置 DAC，必须传入实际的 I2S 物理引脚 (5, 7, 6) 防止底层驱动崩溃 Panic。
  audioStream.setPinout(5, 7, 6);
  audioStream.setVolume(12); // 设置适度音量，防止小喇叭爆音
  is_playing = false;
  Serial.println("🔊 [音频] ESP32-S3 I2S 音频输出(BCLK:5, LRC:7, DOUT:6)初始化完成。");
}

void ESP32Audio::startRecording() {
  Serial.println("🔊 [音频] 模拟麦克风录音开始...");
}

bool ESP32Audio::stopRecording(uint8_t*& wavBuffer, size_t& wavSize) {
  // 黄板默认不带板载麦克风接口（需要自行外接 I2S INMP441 麦克风），此处提供模拟 Wav 信号占位，由大模型自动匹配指令
  Serial.println("🔊 [音频] 模拟录音结束，封装 WAV...");
  
  if (fake_wav_buffer) {
    delete[] fake_wav_buffer;
  }
  
  // 生成一段带 WAV 头的空声音片段 (约 1 秒，16KHz 单声道，16Bit PCM)
  fake_wav_size = 44 + 1000;
  fake_wav_buffer = new uint8_t[fake_wav_size];
  memset(fake_wav_buffer, 0, fake_wav_size);
  
  // 填充标准 44 字节 WAV 头部信息
  memcpy(fake_wav_buffer, "RIFF", 4);
  uint32_t fileSize = fake_wav_size - 8;
  memcpy(fake_wav_buffer + 4, &fileSize, 4);
  memcpy(fake_wav_buffer + 8, "WAVEfmt ", 8);
  uint32_t fmtSize = 16;
  memcpy(fake_wav_buffer + 16, &fmtSize, 4);
  uint16_t fmtCode = 1; // PCM
  memcpy(fake_wav_buffer + 20, &fmtCode, 2);
  uint16_t channels = 1; // 单声道
  memcpy(fake_wav_buffer + 22, &channels, 2);
  uint32_t sampleRate = 16000; // 16KHz
  memcpy(fake_wav_buffer + 24, &sampleRate, 4);
  uint32_t byteRate = 16000 * 2;
  memcpy(fake_wav_buffer + 28, &byteRate, 4);
  uint16_t blockAlign = 2;
  memcpy(fake_wav_buffer + 32, &blockAlign, 2);
  uint16_t bitsPerSample = 16;
  memcpy(fake_wav_buffer + 34, &bitsPerSample, 2);
  memcpy(fake_wav_buffer + 36, "data", 4);
  uint32_t dataSize = 1000;
  memcpy(fake_wav_buffer + 40, &dataSize, 4);
  
  wavBuffer = fake_wav_buffer;
  wavSize = fake_wav_size;
  return true;
}

int ESP32Audio::getRecordVolumeDb() {
  return random(10, 30); // 离线测试模拟音量分贝
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
  // 必须配合库的 isPlaying 判定
  return audioStream.isRunning() || is_playing;
}

void ESP32Audio::setVolume(int volume) {
  int mappedVol = map(volume, 0, 100, 0, 21); // 库的音量范围 0 - 21
  audioStream.setVolume(mappedVol);
  Serial.printf("🔊 [音频] 调整系统音量为: %d (映射范围 0-21)\n", mappedVol);
}

void ESP32Audio::update() {
  if (is_playing) {
    audioStream.loop();
    
    // 检查播放器是否正在工作，如果没有检测到音频流在工作则更新播放状态
    if (!audioStream.isRunning()) {
      is_playing = false;
      Serial.println("🔊 [音频] 音频流流式播放完毕。");
    }
  }
}
