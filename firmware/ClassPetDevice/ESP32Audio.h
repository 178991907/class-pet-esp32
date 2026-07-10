#ifndef ESP32_AUDIO_H
#define ESP32_AUDIO_H

#include "AudioHAL.h"
#include <Audio.h>       // ESP32-audioI2S 库（用于 MP3/流解码 + I2S 播放）
#include <driver/i2s.h>
#include <SD.h>
#include "es8311.h"      // 官方 ES8311 驱动（C 函数式 API, 来自 Example_17_echo）

class ESP32Audio : public AudioHAL {
private:
  Audio audioStream;           // ESP32-audioI2S 音频流（MP3 解码 + I2S TX）
  bool is_playing;
  bool is_recording;
  File record_file;
  uint32_t record_data_size;
  uint32_t record_start_time;
  int16_t session_peak;
  uint32_t last_diag_log_ms;

  // ES8311 I2C 初始化 + codec 初始化（按官方 Example_17_echo）
  bool initES8311();

  // 录音用的 I2S RX 驱动（旧 API, 与 Audio 库的 TX 共享 I2S_NUM_0 的替代方案）
  // 官方示例使用 I2S_NUM_1 的全双工模式，但 Audio 库仅支持单通道
  // 这里沿用 "录音时卸载 TX, 安装 RX; 录音后恢复" 的方式
  void restorePlaybackI2S();
  void writeWavHeader();
  void updateWavHeader();

public:
  ESP32Audio();
  ~ESP32Audio();
  void init() override;
  void startRecording() override;
  bool stopRecording(uint8_t*& wavBuffer, size_t& wavSize) override;
  int getRecordVolumeDb() override;
  bool playAudioStream(const String& url) override;
  void stopAudio() override;
  bool isPlaying() override;
  bool isRecording() override { return is_recording; }
  void setVolume(int volume) override;
  void update() override;
  // 直接 I2S 音调测试（绕过 Audio 库，直接写 I2S 数据验证硬件链路）
  void playTestTone(int frequency = 1000, int duration_ms = 2000);
};

#endif // ESP32_AUDIO_H
