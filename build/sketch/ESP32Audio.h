#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ESP32Audio.h"
#ifndef ESP32_AUDIO_H
#define ESP32_AUDIO_H

#include "AudioHAL.h"
#include <Audio.h> // ESP32-audioI2S 头文件

class ESP32Audio : public AudioHAL {
private:
  Audio audioStream;
  bool is_playing;
  uint8_t* fake_wav_buffer;
  size_t fake_wav_size;

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
