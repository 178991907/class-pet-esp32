#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ESP32TftDisplay.h"
#ifndef ESP32_TFT_DISPLAY_H
#define ESP32_TFT_DISPLAY_H

#include <TFT_eSPI.h>
#include "DisplayHAL.h"

class ESP32TftDisplay : public DisplayHAL {
private:
  TFT_eSPI tft;
  TFT_eSprite spr = TFT_eSprite(&tft);
  int current_battery;
  bool current_charging;
  unsigned long toast_start;
  unsigned long toast_duration;
  String toast_msg;

public:
  ESP32TftDisplay();
  void init() override;
  void clear() override;
  void showInitScreen() override;
  void showSetupAPScreen(const String& apSSID, const String& ip) override;
  void showConnectingScreen(const String& ssid) override;
  void showNormalScreen(const String& studentName, int points, int level, 
                        int expProgress, int expRequired, bool isMaxLevel) override;
  void showOfflineNormalScreen(const String& cachedName, int cachedPoints, int cachedLevel) override;
  void showProcessingScreen(const String& message) override;
  void showRecordingScreen(int volumeDb) override;
  void showTomatoTimerScreen(uint32_t remainingSec, int percent, bool isPaused) override;
  void showTextToast(const String& text, int durationMs = 3000) override;
  
  // 新增接口实现
  void updateBatteryStatus(int level, bool isCharging) override;
  void showChineseErrorScreen(const String& errorTitle, const String& errorDetail) override;

private:
  unsigned long last_refresh_ms;
  int last_state_id;
  bool checkRefresh(int stateId);
  void drawBatteryArea();
  void drawToastArea();
};

#endif // ESP32_TFT_DISPLAY_H
