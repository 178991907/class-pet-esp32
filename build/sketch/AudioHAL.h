#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/AudioHAL.h"
/**
 * @file AudioHAL.h
 * @brief 班级宠物园通用固件 - 音频输入输出硬件抽象层 (Audio HAL)
 * @note 封装麦克风录音（WAV 封包）、扬声器 HTTP 音频流流式拉取及播放控制，解耦具体音频编解码硬件和底层驱动
 */

#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include <Arduino.h>

class AudioHAL {
public:
  virtual ~AudioHAL() {}

  // 1. 初始化音频硬件 (如配置 I2S 录音和播放引脚)
  virtual void init() = 0;

  // ================= 音频输入 (麦克风录制) =================
  
  // 2. 开始捕获录制麦克风音频
  virtual void startRecording() = 0;

  // 3. 停止录制，并通过引用返回保存着录音数据的缓冲区指针及文件大小
  // @note 返回的数据需为标准的 16KHz, 16Bit, 单声道 WAV 编码格式
  virtual bool stopRecording(uint8_t*& wavBuffer, size_t& wavSize) = 0;

  // 4. 获取当前麦克风输入的音量分贝大小 (0-100)，用于绘制跳动声波
  virtual int getRecordVolumeDb() = 0;

  // ================= 音频输出 (语音及音乐播放) =================

  // 5. 异步播放指定的 HTTP 远程音频直链（如 TTS 音频、音乐 MP3 等）
  // @note 需开启内部工作线程/非阻塞轮询，避免卡死主循环
  virtual bool playAudioStream(const String& url) = 0;

  // 6. 停止播放当前音频流
  virtual void stopAudio() = 0;

  // 7. 检查当前是否仍在播放中
  virtual bool isPlaying() = 0;

  // 7.5 检查当前是否正在录音
  virtual bool isRecording() { return false; }

  // 8. 调整播放音量 (0 - 100)
  virtual void setVolume(int volume) = 0;

  // 9. 核心非阻塞刷新方法，若底层不支持后台多线程，则需在主 loop 中不断调用此接口
  virtual void update() = 0;

  // 10. 直接 I2S 音调测试（诊断用，绕过 Audio 库验证硬件链路）
  virtual void playTestTone(int frequency = 1000, int duration_ms = 2000) {}
};

/* 
 * =========================================================================
 * 【移植参考】基于 ESP32 (使用 ESP32-audioI2S 库) 异步拉取音频流的实现伪代码:
 * =========================================================================
 * #include <Audio.h> // ESP32-audioI2S 库头文件
 * 
 * class ESP32Audio : public AudioHAL {
 * private:
 *   Audio audio;
 *   bool playing;
 * public:
 *   void init() override {
 *     audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT); // 设定 I2S 引脚
 *     audio.setVolume(15); // 0-21 级音量
 *     playing = false;
 *   }
 *   
 *   bool playAudioStream(const String& url) override {
 *     playing = audio.connecttohost(url.c_str());
 *     return playing;
 *   }
 *   
 *   void stopAudio() override {
 *     audio.stopSong();
 *     playing = false;
 *   }
 *   
 *   bool isPlaying() override {
 *     return playing;
 *   }
 *   
 *   void update() override {
 *     audio.loop(); // 必须在主 loop 中不断调用以驱动流式拉取及播放
 *   }
 *   // ... 录音部分使用 ESP-IDF 中的 i2s_read 封装 WAV 封包即可
 * };
 */

#endif // AUDIO_HAL_H
