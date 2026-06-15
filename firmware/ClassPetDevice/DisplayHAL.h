/**
 * @file DisplayHAL.h
 * @brief 班级宠物园通用固件 - 显示屏硬件抽象层 (Display HAL)
 * @note 定义统一的显示交互接口，解耦具体屏幕（如 OLED、TFT、LCD）及驱动库（如 LVGL、U8g2、TFT_eSPI）
 */

#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <Arduino.h>

class DisplayHAL {
public:
  virtual ~DisplayHAL() {}

  // 1. 初始化显示屏
  virtual void init() = 0;

  // 2. 清屏或复位
  virtual void clear() = 0;

  // 3. 渲染初始化载入界面
  virtual void showInitScreen() = 0;

  // 4. 渲染 AP 网页配网界面，提示用户连接 SSID 并访问指定 IP 进行配网
  virtual void showSetupAPScreen(const String& apSSID, const String& ip) = 0;

  // 5. 渲染正在连接 WiFi 界面，显示目标 SSID
  virtual void showConnectingScreen(const String& ssid) = 0;

  // 6. 渲染正常在线运行待机界面（核心：宠物动画、等级、积分、经验条、姓名）
  virtual void showNormalScreen(const String& studentName, int points, int level, 
                                int expProgress, int expRequired, bool isMaxLevel) = 0;

  // 7. 渲染离线运行待机界面（提示 WiFi 断开，显示最后已知缓存状态）
  virtual void showOfflineNormalScreen(const String& cachedName, int cachedPoints, int cachedLevel) = 0;

  // 8. 渲染正在请求/解析中界面，如 NLP 解析、TTS 下载中
  virtual void showProcessingScreen(const String& message) = 0;

  // 9. 渲染录音状态界面，传入当前音量声波跳动分贝 (0 - 100) 以显示跳动声波
  virtual void showRecordingScreen(int volumeDb) = 0;

  // 10. 渲染本地番茄钟计时界面（大表盘、进度条、剩余秒数及暂停状态）
  virtual void showTomatoTimerScreen(uint32_t remainingSec, int percent, bool isPaused) = 0;

  // 11. 在屏幕下方/特定区域弹出消息通知（气泡弹窗或字幕）
  virtual void showTextToast(const String& text, int durationMs = 3000) = 0;
};

/* 
 * =========================================================================
 * 【移植移植参考】基于 ESP32 (使用 TFT_eSPI) 的具体实现伪代码:
 * =========================================================================
 * #include <TFT_eSPI.h>
 * 
 * class ESP32TftDisplay : public DisplayHAL {
 * private:
 *   TFT_eSPI tft;
 * public:
 *   void init() override {
 *     tft.init();
 *     tft.setRotation(1);
 *     tft.fillScreen(TFT_BLACK);
 *   }
 *   
 *   void showConnectingScreen(const String& ssid) override {
 *     tft.fillScreen(TFT_BLUE);
 *     tft.drawString("WiFi Connecting...", 10, 10, 2);
 *     tft.drawString("Target: " + ssid, 10, 30, 2);
 *   }
 *   
 *   void showTomatoTimerScreen(uint32_t remainingSec, int percent, bool isPaused) override {
 *     tft.fillScreen(TFT_BLACK);
 *     char buf[32];
 *     sprintf(buf, "%02d:%02d", remainingSec / 60, remainingSec % 60);
 *     tft.drawString(buf, 80, 60, 4);
 *     tft.drawCircle(120, 80, 50, TFT_RED);
 *     // 根据 percent 绘制弧度或进度条
 *     tft.fillRect(10, 150, (percent * 220)/100, 10, TFT_GREEN);
 *   }
 *   // ... 其余方法同理实现
 * };
 */

#endif // DISPLAY_HAL_H
