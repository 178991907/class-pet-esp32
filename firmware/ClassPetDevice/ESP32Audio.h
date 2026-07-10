#ifndef ESP32_AUDIO_H
#define ESP32_AUDIO_H

#include "AudioHAL.h"
#include <Audio.h> // ESP32-audioI2S 头文件
#include <driver/i2s.h>
#include <SD.h>

class ESP32Audio : public AudioHAL {
private:
  Audio audioStream;
  bool is_playing;
  bool is_recording;
  File record_file;
  uint32_t record_data_size;
  uint32_t record_start_time;
  int16_t session_peak;       // 本次录音会话中的最大峰值 (0..32767)
  uint32_t last_diag_log_ms;  // 上一次打印录音诊断日志的时间

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
  void setVolume(int volume) override;
  void update() override;
};

#endif // ESP32_AUDIO_H
