#ifndef ESP32_AUDIO_H
#define ESP32_AUDIO_H

#include "AudioHAL.h"
#include <Audio.h>       // ESP32-audioI2S 库（用于 MP3/流解码 + I2S 播放）
#include <driver/i2s.h>
#include <SD.h>
#include "es8311.h"      // 官方 ES8311 驱动（C 函数式 API, 来自 Example_17_echo）

class ESP32Audio : public AudioHAL {
private:
  // I2S_NUM_0 当前驱动模式 (单一所有者, 避免唤醒词/录音/播放争夺同一外设)
  enum I2sMode { I2S_MODE_NONE = 0, I2S_MODE_RX_MIC, I2S_MODE_TX_PLAY };
  I2sMode _i2sMode = I2S_MODE_NONE;

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

  // 麦克风寄存器扫描诊断: 遍历 REG14(ADC输入选择)/REG17(ADC音量) 多种组合,
  // 每种录 0.3s 输出纯 ASCII 的 RMS/峰值, 用于精确定位能采到真实语音的寄存器组合。
  void micSweepTest();

  // ===== 流式语音 (Route B) =====
  void setPcmUploadCallback(std::function<void(const uint8_t*, size_t)> cb) override { _pcmUploadCb = cb; }
  void enableStreamUp(bool en) override { _streamUpEnabled = en; }
  bool startPcmPlayback() override;
  void feedPcm(const uint8_t* data, size_t len) override;
  void stopPcmPlayback() override;
  bool isPcmPlaying() override { return _pcm_playing; }

  // ===== I2S_NUM_0 单一所有者 (唤醒词/录音/播放 共用) =====
  bool ensureRxMic() override;        // 确保 RX(麦克风)模式 (幂等)
  void releaseI2s() override;         // 卸载当前驱动, 标记 NONE
  void installTxPlay(uint32_t sampleRate = 44100) override;  // 装 TX(播放)驱动
  void restoreIdleRxMic() override;   // 播放结束恢复 RX(麦克风)基线

  // ===== 离线唤醒词 (esp-sr) (向后兼容) =====
  // 切换 ES8311 到麦克风输入并静音功放 (新代码应直接走 ensureRxMic)
  void enterWakeMicMode() override;
  void exitWakeMicMode() override;

private:
  bool _pcm_playing = false;
  bool _streamUpEnabled = false;
  std::function<void(const uint8_t*, size_t)> _pcmUploadCb = nullptr;
};

#endif // ESP32_AUDIO_H
