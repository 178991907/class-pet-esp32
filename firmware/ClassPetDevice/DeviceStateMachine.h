/**
 * @file DeviceStateMachine.h
 * @brief 班级宠物园设备状态机系统 (基于 FreeRTOS 消息队列与状态模式)
 */

#ifndef DEVICE_STATE_MACHINE_H
#define DEVICE_STATE_MACHINE_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "Config.h"
#include "WebSocketAudio.h"

// 系统交互事件
enum DeviceEvent {
  EVENT_NONE,
  EVENT_POMODORO_SETTINGS,
  EVENT_POMODORO_START,
  EVENT_POMODORO_STOP,
  EVENT_POMODORO_PAUSE_RESUME,
  EVENT_POMODORO_ADJUST,
  EVENT_VOICE_START,
  EVENT_VOICE_RECORD_DONE,
  EVENT_VOICE_PLAY_DONE,
  EVENT_NET_RESTORE,
  EVENT_NET_LOST,
  EVENT_API_TICK
};

class DeviceStateMachine {
public:
  static DeviceStateMachine& getInstance() {
    static DeviceStateMachine instance;
    return instance;
  }
  
  void init();
  void postEvent(DeviceEvent ev);
  DeviceState getState() const { return _state; }

  // 在主 loop() 中驱动语音 WS 接收 (Core 1)
  void pollVoiceWs() { voiceWs.poll(); }
  
private:
  DeviceStateMachine() : _state(STATE_CONNECTING_WIFI), _queue(nullptr), 
    _pet_gif_buffer(nullptr), _pet_gif_size(0), _last_sync_time(0) {}
  
  DeviceState _state;
  QueueHandle_t _queue;
  
  // 宠物动图资源缓存
  String _current_pet_type;
  int _current_pet_level;
  uint8_t* _pet_gif_buffer;
  size_t _pet_gif_size;
  
  static void taskEntry(void* arg);
  void handleEvent(DeviceEvent ev);
  void loopState();
  void loadPetGif(const String& petType, int petLevel);
  
  uint32_t _state_start_time;
  uint32_t _last_sync_time;

  // ===== 流式语音 (Route B) =====
  WebSocketAudio voiceWs;          // 语音 WebSocket 客户端
  bool _voiceWsOk = false;         // 本次录音 WS 是否连接成功
  bool _routeB = false;            // 本次是否走流式通道 (false=原 HTTP 流程)
  bool _recEntryDone = false;      // STATE_RECORDING 进入初始化是否已执行
  bool _wsClosed = false;          // 服务端是否已关闭 WS
  bool _tts_started = false;       // 是否已收到 tts start / 降级 url
  bool _tts_done = false;          // 是否已收到 tts stop
  bool _pcmMode = false;           // true=PCM 直推播放, false=MP3 降级拉流
  bool _aborted = false;           // 用户是否已打断
  uint32_t _ttsDoneTime = 0;       // 收到 tts stop 的时间戳
  String _sttText;                 // ASR 识别文本
  String _lastReplyText;           // 最近一次 LLM 回复 (用于播放结束后吐司)
  String _ttsFallbackUrl;          // 服务端 PCM 解码失败时的降级 MP3 地址

  void parseServerUrl(const String& url, String& host, uint16_t& port, bool& useTls);
  void handleWsText(const String& text);
  void uploadVoiceFrame(const uint8_t* d, size_t l);
};

#endif // DEVICE_STATE_MACHINE_H
