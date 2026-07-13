/**
 * @file WakeWordEngine.h
 * @brief 离线唤醒词引擎 (乐鑫 esp-sr 1.0, 单麦 AFE 接口)
 *
 * 设计要点:
 *  - 使用 esp-sr 1.0 的 esp_afe_sr_1mic 接口 (注意: 与 xiaozhi-esp32 2.x 的
 *    esp_afe_handle_from_config API 不同! 本仓库捆绑的是 esp-sr 1.0 修订版)。
 *  - 设备处于空闲态 (STATE_NORMAL_ONLINE / STATE_NORMAL_OFFLINE) 时, 本引擎
 *    常驻监听麦克风: 自己安装 I2S_NUM_0 的 RX 驱动, 把 16k/16bit/单声道 PCM
 *    喂给 AFE, 检测唤醒词。
 *  - 检测到唤醒词 (AFE_FETCH_WWE_DETECTED) 时调用 onWake 回调, 由状态机
 *    postEvent(EVENT_VOICE_START) 进入录音流程; 同时本引擎 pause() 卸载 I2S,
 *    把音频硬件交还给录音/播放流程, 避免争夺 I2S_NUM_0。
 *  - 默认唤醒词: "你好小智" (nihaoxiaozhi_wn5, wakenet5 量化模型)。
 *    若要切换为官方 "嗨乐鑫" (hilexin), 改下面两处并重新链接对应 .a 即可。
 */

#ifndef WAKE_WORD_ENGINE_H
#define WAKE_WORD_ENGINE_H

#include <Arduino.h>
#include <functional>
#include <driver/i2s.h>
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"   // 声明 esp_afe_sr_1mic / _2mic (esp-sr 1.0 单/双麦 AFE 接口)
#include "esp_wn_models.h"
// 默认唤醒词模型系数 (头文件声明 get_coeff_*; 权重在 lib/esp32/lib*.a 中)
#include "nihaoxiaozhi_wn5.h"
// 备选: #include "hilexin_wn5.h"  ->  get_coeff_hilexin_wn5

#include "Board.h"

class WakeWordEngine {
public:
  static WakeWordEngine& getInstance() {
    static WakeWordEngine instance;
    return instance;
  }

  // 设置唤醒回调 (由 DeviceStateMachine 注册: postEvent(EVENT_VOICE_START))
  void setWakeCallback(std::function<void()> cb) { _onWake = cb; }

  // 启动 AFE + 常驻监听任务。任务初始为暂停态, 由状态机 resume() 开始真正监听。
  // 返回 false 表示 AFE 创建失败 (esp-sr 链接/模型缺失), 此时仅语音按钮可用。
  bool begin();

  // 进入常驻监听: 安装 I2S RX + ES8311 麦克风模式 + 静音功放
  void resume();
  // 暂停监听: 立即卸载 I2S RX, 交还音频硬件给录音/播放流程
  void pause();

  bool isReady()     const { return _afeData != nullptr; }
  bool isListening() const { return _running && !_paused && _i2sInstalled; }

  // 当前唤醒词文本 (用于 UI 提示)
  const char* wakeWord() const { return "你好小智"; }

private:
  WakeWordEngine() {}
  WakeWordEngine(const WakeWordEngine&) = delete;
  WakeWordEngine& operator=(const WakeWordEngine&) = delete;

  static void taskEntry(void* arg);
  void run();

  void installI2s();
  void uninstallI2s();

  const esp_afe_sr_iface_t* _afeHandle = nullptr;
  esp_afe_sr_data_t*  _afeData   = nullptr;
  TaskHandle_t        _taskHandle = nullptr;

  bool _running = false;
  bool _paused  = true;     // 默认暂停, 由状态机 resume() 启动
  bool _i2sInstalled = false;

  int16_t* _feedBuf  = nullptr;
  int16_t* _fetchBuf = nullptr;
  int _feedChunksize  = 0;
  int _fetchChunksize = 0;
  int _channelNum = 1;

  uint32_t _suppressUntil = 0;   // 唤醒后防抖静默期 (ms 时间戳)
  std::function<void()> _onWake = nullptr;
};

#endif // WAKE_WORD_ENGINE_H
