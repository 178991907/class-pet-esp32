/**
 * @file TomatoTimer.cpp
 * @brief 班级宠物园通用固件 - 本地离线番茄钟实现
 */

#include "TomatoTimer.h"
#include "Config.h"

TomatoTimer::TomatoTimer() 
  : is_active(false)
  , is_paused(false)
  , duration_seconds(0)
  , remaining_seconds(0)
  , last_millis(0)
  , finished_callback(nullptr) {
}

void TomatoTimer::start(int minutes) {
  duration_seconds = minutes * 60;
  remaining_seconds = duration_seconds;
  is_active = true;
  is_paused = false;
  last_millis = millis();
  
  DEBUG_PRINTF("🍅 番茄钟已启动！时长: %d 分钟 (%d 秒)\n", minutes, duration_seconds);
}

void TomatoTimer::setDuration(int minutes) {
  uint32_t new_duration = minutes * 60;
  
  if (remaining_seconds > new_duration) {
    remaining_seconds = new_duration;
  } else if (duration_seconds > 0) {
    // 保持已流逝的时间比例或绝对值，这里采用增加/减少对应剩余时间
    int32_t diff = new_duration - duration_seconds;
    remaining_seconds += diff;
  } else {
    remaining_seconds = new_duration;
  }
  
  duration_seconds = new_duration;
  DEBUG_PRINTF("🍅 番茄钟时长已修改！新时长: %d 分钟\n", minutes);
}

void TomatoTimer::pause() {
  if (is_active && !is_paused) {
    is_paused = true;
    DEBUG_PRINTLN("🍅 番茄钟已暂停。");
  }
}

void TomatoTimer::resume() {
  if (is_active && is_paused) {
    is_paused = false;
    last_millis = millis(); // 刷新毫秒基准，避免暂停期间的时间突变
    DEBUG_PRINTLN("🍅 番茄钟已恢复。");
  }
}

void TomatoTimer::stop() {
  is_active = false;
  is_paused = false;
  remaining_seconds = 0;
  DEBUG_PRINTLN("🍅 番茄钟已终止。");
}

void TomatoTimer::update() {
  if (!is_active || is_paused) {
    return;
  }

  // 经典的无阻塞溢出安全延时累减
  unsigned long currentMillis = millis();
  if (currentMillis - last_millis >= 1000) {
    // 扣减秒数
    if (remaining_seconds > 0) {
      remaining_seconds--;
      last_millis += 1000; // 累加基准时间戳，消除调用延迟的累积误差
      
      // 每 30 秒在串口打印一次进度
      if (remaining_seconds % 30 == 0 && remaining_seconds > 0) {
        DEBUG_PRINTF("🍅 番茄钟运行中: 剩余 %d 秒...\n", remaining_seconds);
      }
    }

    // 倒计时结束
    if (remaining_seconds == 0) {
      is_active = false;
      DEBUG_PRINTLN("🍅 番茄钟时间到！触发回调事件...");
      if (finished_callback != nullptr) {
        finished_callback();
      }
    }
  }
}

uint32_t TomatoTimer::getRemainingSeconds() const {
  return remaining_seconds;
}

int TomatoTimer::getPercent() const {
  if (duration_seconds == 0) return 0;
  
  // 计算已经流逝的时间比率
  uint32_t elapsed = duration_seconds - remaining_seconds;
  return (int)((elapsed * 100) / duration_seconds);
}
