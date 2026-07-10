#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/DeviceStateMachine.h"
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
};

#endif // DEVICE_STATE_MACHINE_H
