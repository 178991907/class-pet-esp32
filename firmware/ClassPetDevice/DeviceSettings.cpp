/**
 * @file DeviceSettings.cpp
 * @brief 设备本地设置持久化实现
 */

#include "DeviceSettings.h"

const char* DeviceSettings::NS = "classpet";
Preferences DeviceSettings::_prefs;

void DeviceSettings::begin() {
  // 只读打开以确保命名空间存在；写入时再以读写模式打开
  _prefs.begin(NS, true);
  _prefs.end();
}

int DeviceSettings::getVolume() {
  _prefs.begin(NS, true);
  int v = _prefs.getInt("volume", 85);
  _prefs.end();
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  return v;
}
void DeviceSettings::setVolume(int v) {
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  _prefs.begin(NS, false);
  _prefs.putInt("volume", v);
  _prefs.end();
}

int DeviceSettings::getBrightness() {
  _prefs.begin(NS, true);
  int v = _prefs.getInt("brightness", 80);
  _prefs.end();
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  return v;
}
void DeviceSettings::setBrightness(int v) {
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  _prefs.begin(NS, false);
  _prefs.putInt("brightness", v);
  _prefs.end();
}

int DeviceSettings::getStandbySeconds() {
  _prefs.begin(NS, true);
  int v = _prefs.getInt("standby_sec", 30);
  _prefs.end();
  return v;
}
void DeviceSettings::setStandbySeconds(int s) {
  if (s < 0) s = 0;
  _prefs.begin(NS, false);
  _prefs.putInt("standby_sec", s);
  _prefs.end();
}

int DeviceSettings::getScreenOffSeconds() {
  _prefs.begin(NS, true);
  int v = _prefs.getInt("screenoff_sec", 60);
  _prefs.end();
  return v;
}
void DeviceSettings::setScreenOffSeconds(int s) {
  if (s < 0) s = 0;
  _prefs.begin(NS, false);
  _prefs.putInt("screenoff_sec", s);
  _prefs.end();
}
