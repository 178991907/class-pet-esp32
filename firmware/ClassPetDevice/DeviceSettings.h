/**
 * @file DeviceSettings.h
 * @brief 班级宠物园设备本地设置 (音量/亮度/待机/息屏)，基于 Preferences(NVS) 持久化
 */

#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "2.1.0"
#endif

class DeviceSettings {
public:
  static void begin();

  // 音量 0-100 (TTS/喇叭播放音量)，默认 85
  static int getVolume();
  static void setVolume(int v);

  // 屏幕亮度 0-100，默认 80
  static int getBrightness();
  static void setBrightness(int v);

  // 待机(进入暗屏)秒数，0=永不，默认 30
  static int getStandbySeconds();
  static void setStandbySeconds(int s);

  // 息屏(关闭背光)秒数，0=永不，默认 60
  static int getScreenOffSeconds();
  static void setScreenOffSeconds(int s);

  // 固件版本号 (编译期注入)
  static String getFirmwareVersion() { return String(FIRMWARE_VERSION); }

private:
  static Preferences _prefs;
  static const char* NS;
};

#endif // DEVICE_SETTINGS_H
