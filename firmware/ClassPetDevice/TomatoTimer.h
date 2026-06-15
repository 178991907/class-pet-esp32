/**
 * @file TomatoTimer.h
 * @brief 班级宠物园通用固件 - 本地离线番茄钟模块
 * @note 支持设定分钟数、暂停/继续、倒计时自减、以及计时结束触发硬件回调
 */

#ifndef TOMATO_TIMER_H
#define TOMATO_TIMER_H

#include <Arduino.h>

class TomatoTimer {
public:
  TomatoTimer();

  // 启动番茄钟，传入倒计时分钟数，默认为 25 分钟
  void start(int minutes = 25);
  
  // 暂停计时
  void pause();

  // 恢复计时
  void resume();

  // 终止并复位番茄钟
  void stop();

  // 核心轮询函数，需在主 loop() 中每周期调用
  void update();

  // 状态查询
  bool isActive() const { return is_active; }
  bool isPaused() const { return is_paused; }
  
  // 获取剩余时间（秒）
  uint32_t getRemainingSeconds() const;

  // 获取完成比例百分比 (0 到 100)
  int getPercent() const;

  // 绑定计时结束的回调函数指针
  void onFinished(void (*callback)()) { finished_callback = callback; }

private:
  bool is_active;
  bool is_paused;
  
  uint32_t duration_seconds;  // 总设定时长（秒）
  uint32_t remaining_seconds; // 剩余时长（秒）
  unsigned long last_millis;  // 上次扣减秒数时的系统毫秒计数

  void (*finished_callback)(); // 计时完成的回调指针
};

#endif // TOMATO_TIMER_H
