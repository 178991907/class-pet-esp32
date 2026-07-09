#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/LcdDisplay.cpp"
/**
 * @file LcdDisplay.cpp
 * @brief LVGL 9.5 + TFT_eSPI + CST820 屏幕与触控绑定子系统实现
 */

#include "LcdDisplay.h"
#include "Board.h"
#include "Config.h"

// 提供给 LVGL 9.5.0 的高精度 tick 定时器回调
static uint32_t get_sys_millis(void) {
  return millis();
}

void LcdDisplay::init() {
  // 1. 初始化背光引脚并点亮屏幕
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);
  
  // 2. 初始化 TFT_eSPI 并配置方向
  _tft.init();
  _tft.setRotation(1); // 横屏 320x240 模式
  _tft.invertDisplay(true); // 修复颜色反转问题 (修复了橙色变蓝，绿色变粉的问题)
  _tft.fontLoaded = false; // 修正 fontLoaded 未初始化崩溃bug
  
  // 3. 初始化 CST820 I2C 电容触摸屏
  _touch.init(TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN);
  
  // 4. 初始化 LVGL 图形库并绑定系统 Tick
  lv_init();
  lv_tick_set_cb(get_sys_millis);
  
  // 创建 LVGL 互斥锁
  _lvgl_mutex = xSemaphoreCreateMutex();
  
  // 5. 创建 LVGL 9.5.0 独立显示对象
  _disp = lv_display_create(320, 240);
  
  // 申请双缓冲区，大小为 320x24 像素 (16-bit RGB565) = 15360 字节
  const uint32_t buf_size = 320 * 24 * sizeof(uint16_t);
  _buf1 = (uint8_t*)malloc(buf_size);
  _buf2 = (uint8_t*)malloc(buf_size);
  
  if (_buf1 && _buf2) {
    memset(_buf1, 0, buf_size);
    memset(_buf2, 0, buf_size);
    lv_display_set_buffers(_disp, _buf1, _buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  }
  
  lv_display_set_flush_cb(_disp, dispFlushCb);
  
  // 6. 注册 LVGL 输入设备 (触摸屏)
  _indev = lv_indev_create();
  lv_indev_set_type(_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(_indev, touchReadCb);
  
  DEBUG_PRINTLN("🖥️ [显示屏] LVGL 9.5.0 显卡与触控层初始化完成！");
}

void LcdDisplay::resetActivityTime() {
  _last_activity_time = millis();
  if (!_is_screen_on) {
    _is_screen_on = true;
    digitalWrite(TFT_BL_PIN, HIGH);
  }
}

void LcdDisplay::update() {
  // 息屏检查 (15秒无操作则直接息屏省电)
  if (_is_screen_on && (millis() - _last_activity_time > 15000)) {
    _is_screen_on = false;
    digitalWrite(TFT_BL_PIN, LOW);
  }

  // 加锁保护 LVGL 渲染循环，防止与状态机跨核冲突
  if (xSemaphoreTake(_lvgl_mutex, portMAX_DELAY) == pdTRUE) {
    lv_timer_handler();
    xSemaphoreGive(_lvgl_mutex);
  }
}

LcdDisplay::~LcdDisplay() {
  if (_buf1) free(_buf1);
  if (_buf2) free(_buf2);
}

// 屏幕刷新刷入 LCD 缓存回调
void LcdDisplay::dispFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  
  TFT_eSPI& tft = LcdDisplay::getInstance().getTft();
  
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)px_map, w * h, true);
  tft.endWrite();
  
  // 通知 LVGL 本次冲刷完成
  lv_display_flush_ready(disp);
}

void LcdDisplay::touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
  uint16_t tx = 0, ty = 0;
  if (LcdDisplay::getInstance().getTouch().getTouch(tx, ty)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = tx;
    data->point.y = ty;
    
    // 有触摸，唤醒屏幕并重置定时器
    LcdDisplay::getInstance().resetActivityTime();
    
    Serial.printf("🎯 [触摸] x: %d, y: %d\n", tx, ty);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}
