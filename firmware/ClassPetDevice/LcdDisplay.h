/**
 * @file LcdDisplay.h
 * @brief LVGL 9.5 + TFT_eSPI + CST820 屏幕与触控绑定子系统
 */

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <lvgl.h>
#include <TFT_eSPI.h>
#include "CST820Touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class LcdDisplay {
public:
  static LcdDisplay& getInstance() {
    static LcdDisplay instance;
    return instance;
  }
  
  void init();
  void update();
  
  TFT_eSPI& getTft() { return _tft; }
  CST820Touch& getTouch() { return _touch; }
  
  // 通知有新活动，唤醒屏幕
  void resetActivityTime();
  unsigned long getLastActivityTime() const { return _last_activity_time; }

  // 设置屏幕背光亮度 (0-100)，0=关闭
  void setBrightness(int pct);

  // LVGL 互斥锁：多核访问保护
  void lock()   { if (_lvgl_mutex) xSemaphoreTake(_lvgl_mutex, portMAX_DELAY); }
  void unlock() { if (_lvgl_mutex) xSemaphoreGive(_lvgl_mutex); }
  
private:
  LcdDisplay() : _disp(nullptr), _indev(nullptr), _buf1(nullptr), _buf2(nullptr), _lvgl_mutex(nullptr), _last_activity_time(0), _is_screen_on(true) {}
  ~LcdDisplay();
  
  TFT_eSPI _tft;
  CST820Touch _touch;
  
  lv_display_t* _disp;
  lv_indev_t* _indev;
  
  uint8_t* _buf1;
  uint8_t* _buf2;
  SemaphoreHandle_t _lvgl_mutex;

  unsigned long _last_activity_time;
  bool _is_screen_on;
  int _ledc_channel = -1;
  
  
  // LVGL 回调
  static void dispFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
  static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data);
};

#endif // LCD_DISPLAY_H
