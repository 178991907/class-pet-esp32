#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ESP32TftDisplay.cpp"
#include "ESP32TftDisplay.h"

#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_PRIMARY TFT_ORANGE // 橙色
#define COLOR_SUCCESS TFT_GREEN  // 绿色
#define COLOR_WARNING TFT_YELLOW // 黄色
#define COLOR_DANGER  TFT_RED    // 红色
#define COLOR_CARD_BG tft.color565(20, 20, 20)
#define COLOR_TOAST   tft.color565(0, 80, 160)

ESP32TftDisplay::ESP32TftDisplay() 
  : current_battery(100), current_charging(false), toast_start(0), toast_duration(0), toast_msg(""),
    last_refresh_ms(0), last_state_id(0) {}

bool ESP32TftDisplay::checkRefresh(int stateId) {
  unsigned long now = millis();
  if (stateId != last_state_id) {
    last_state_id = stateId;
    last_refresh_ms = now;
    return true;
  }
  
  unsigned long interval = 300;
  if (stateId == 7) interval = 100;
  
  if (now - last_refresh_ms > interval) {
    last_refresh_ms = now;
    return true;
  }
  return false;
}

void ESP32TftDisplay::init() {
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);
  
  spr.createSprite(320, 240);
  
  pinMode(45, OUTPUT);
  digitalWrite(45, HIGH);
}

void ESP32TftDisplay::clear() {
  spr.fillScreen(COLOR_BG);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showInitScreen() {
  if (!checkRefresh(1)) return;
  spr.fillScreen(COLOR_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_PRIMARY, COLOR_BG);
  spr.drawString("ClassPet Garden", 160, 90, 4);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString("Initializing device...", 160, 130, 2);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showSetupAPScreen(const String& apSSID, const String& ip) {
  if (!checkRefresh(2)) return;
  spr.fillScreen(COLOR_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_PRIMARY, COLOR_BG);
  spr.drawString("⚡ WiFi Setup Mode ⚡", 160, 40, 4);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString("1. Connect to hotspot (SSID):", 160, 85, 2);
  spr.setTextColor(COLOR_WARNING, COLOR_BG);
  spr.drawString(apSSID, 160, 110, 2);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString("2. Open web browser to configuration:", 160, 150, 2);
  spr.setTextColor(COLOR_SUCCESS, COLOR_BG);
  spr.drawString("http://" + ip, 160, 175, 2);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showConnectingScreen(const String& ssid) {
  if (!checkRefresh(3)) return;
  spr.fillScreen(COLOR_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString("Connecting to WiFi Router...", 160, 90, 2);
  spr.setTextColor(COLOR_PRIMARY, COLOR_BG);
  spr.drawString(ssid, 160, 125, 2);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showNormalScreen(const String& studentName, int points, int level, int expProgress, int expRequired, bool isMaxLevel) {
  if (!checkRefresh(4)) return;
  spr.fillScreen(COLOR_BG);
  drawBatteryArea();
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COLOR_SUCCESS, COLOR_BG);
  spr.drawString("Online", 10, 10, 2);
  spr.setTextDatum(MC_DATUM);
  spr.fillRoundRect(20, 40, 280, 140, 12, COLOR_CARD_BG);
  spr.drawRoundRect(20, 40, 280, 140, 12, COLOR_PRIMARY);
  spr.setTextColor(COLOR_TEXT, COLOR_CARD_BG);
  spr.drawString(studentName + "'s Pet", 160, 65, 4);
  spr.setTextColor(COLOR_WARNING, COLOR_CARD_BG);
  spr.drawString("Lv." + String(level) + " | Points: " + String(points), 160, 100, 2);
  
  if (isMaxLevel) {
    spr.setTextColor(COLOR_SUCCESS, COLOR_CARD_BG);
    spr.drawString("⭐ Graduate! ⭐", 160, 140, 2);
  } else {
    int barW = 200, barH = 10, barX = 60, barY = 125;
    spr.drawRect(barX, barY, barW, barH, COLOR_TEXT);
    int progressW = (expProgress * (barW - 4)) / expRequired;
    if (progressW < 0) progressW = 0;
    if (progressW > (barW - 4)) progressW = barW - 4;
    spr.fillRect(barX + 2, barY + 2, progressW, barH - 4, COLOR_PRIMARY);
    spr.setTextColor(COLOR_TEXT, COLOR_CARD_BG);
    spr.drawString(String(expProgress) + " / " + String(expRequired), 160, 150, 1);
  }
  spr.setTextColor(tft.color565(120, 120, 120), COLOR_BG);
  spr.drawString("[Tomato Timer]     |     [Voice Talk]", 160, 205, 1);
  drawToastArea();
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showOfflineNormalScreen(const String& cachedName, int cachedPoints, int cachedLevel) {
  if (!checkRefresh(5)) return;
  spr.fillScreen(COLOR_BG);
  drawBatteryArea();
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COLOR_DANGER, COLOR_BG);
  spr.drawString("Offline", 10, 10, 2);
  spr.setTextDatum(MC_DATUM);
  spr.fillRoundRect(20, 40, 280, 140, 12, COLOR_CARD_BG);
  spr.drawRoundRect(20, 40, 280, 140, 12, COLOR_DANGER);
  spr.setTextColor(COLOR_TEXT, COLOR_CARD_BG);
  spr.drawString(cachedName + " (Offline)", 160, 65, 4);
  spr.setTextColor(COLOR_WARNING, COLOR_CARD_BG);
  spr.drawString("Lv." + String(cachedLevel) + " | Points: " + String(cachedPoints), 160, 105, 2);
  spr.setTextColor(COLOR_DANGER, COLOR_CARD_BG);
  spr.drawString("Auto reconnecting...", 160, 145, 2);
  drawToastArea();
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showProcessingScreen(const String& message) {
  if (!checkRefresh(6)) return;
  spr.fillScreen(COLOR_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_PRIMARY, COLOR_BG);
  spr.drawString("Please wait", 160, 80, 4);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString(message + "...", 160, 130, 2);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showRecordingScreen(int volumeDb) {
  if (!checkRefresh(7)) return;
  spr.fillScreen(COLOR_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_DANGER, COLOR_BG);
  spr.drawString("🎙️ Voice Recording", 160, 70, 4);
  int maxBarW = 240;
  int barW = (volumeDb * maxBarW) / 100;
  if (barW > maxBarW) barW = maxBarW;
  spr.fillRect(160 - barW / 2, 120, barW, 16, COLOR_DANGER);
  spr.drawRect(40, 120, maxBarW, 16, COLOR_TEXT);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString("Release button to complete", 160, 170, 2);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showTomatoTimerScreen(uint32_t remainingSec, int percent, bool isPaused) {
  if (!checkRefresh(8)) return;
  spr.fillScreen(COLOR_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_DANGER, COLOR_BG);
  spr.drawString("🍅 Pomodoro Timer", 160, 40, 4);
  char buf[16];
  sprintf(buf, "%02d:%02d", remainingSec / 60, remainingSec % 60);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString(buf, 160, 100, 8);
  if (isPaused) {
    spr.setTextColor(COLOR_WARNING, COLOR_BG);
    spr.drawString("PAUSED", 160, 150, 2);
  }
  int barW = 240, barH = 10, barX = 40, barY = 180;
  spr.drawRect(barX, barY, barW, barH, COLOR_TEXT);
  spr.fillRect(barX + 2, barY + 2, ((barW - 4) * percent) / 100, barH - 4, COLOR_SUCCESS);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::showTextToast(const String& text, int durationMs) {
  toast_msg = text;
  toast_start = millis();
  toast_duration = durationMs;
  drawToastArea();
}

void ESP32TftDisplay::updateBatteryStatus(int level, bool isCharging) {
  current_battery = level;
  current_charging = isCharging;
}

void ESP32TftDisplay::showChineseErrorScreen(const String& errorTitle, const String& errorDetail) {
  if (!checkRefresh(9)) return;
  spr.fillScreen(COLOR_BG);
  spr.drawRoundRect(10, 10, 300, 220, 8, COLOR_DANGER);
  spr.drawRoundRect(12, 12, 296, 216, 8, COLOR_DANGER);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COLOR_DANGER, COLOR_BG);
  spr.drawString("⚠️ ERROR DETECTED", 160, 45, 4);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  spr.drawString(errorTitle, 160, 95, 2);
  spr.setTextColor(COLOR_WARNING, COLOR_BG);
  spr.drawString(errorDetail, 160, 135, 2);
  spr.setTextColor(tft.color565(120, 120, 120), COLOR_BG);
  spr.drawString("Please check settings on backend dashboard", 160, 185, 1);
  spr.pushSprite(0, 0);
}

void ESP32TftDisplay::drawBatteryArea() {
  int batX = 270, batY = 10, batW = 35, batH = 16;
  spr.drawRect(batX, batY, batW, batH, COLOR_TEXT);
  spr.fillRect(batX + batW, batY + 4, 3, batH - 8, COLOR_TEXT);
  int fillW = (current_battery * (batW - 4)) / 100;
  if (fillW < 0) fillW = 0;
  if (fillW > (batW - 4)) fillW = batW - 4;
  spr.fillRect(batX + 2, batY + 2, fillW, batH - 4, current_battery > 20 ? COLOR_SUCCESS : COLOR_DANGER);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(COLOR_TEXT, COLOR_BG);
  if (current_charging) {
    spr.drawString("CHG " + String(current_battery) + "%", batX - 5, batY + 1, 1);
  } else {
    spr.drawString(String(current_battery) + "%", batX - 5, batY + 1, 1);
  }
}

void ESP32TftDisplay::drawToastArea() {
  if (toast_duration > 0 && (millis() - toast_start < toast_duration)) {
    spr.fillRect(10, 175, 300, 40, COLOR_TOAST);
    spr.drawRect(10, 175, 300, 40, COLOR_TEXT);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COLOR_TEXT, COLOR_TOAST);
    spr.drawString(toast_msg, 160, 195, 2);
  }
}
