#include "CST820Touch.h"

void CST820Touch::init(int sda, int scl, int rst, int int_pin) {
  _sda = sda; _scl = scl; _rst = rst; _int = int_pin;
  Wire.begin(_sda, _scl);
  Wire.setTimeOut(100);
  
  if (_rst != -1) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(50);
    digitalWrite(_rst, HIGH);
    delay(50);
  }
  
  // 动态扫描 I2C 触控地址 (FT6336G = 0x38, CST820 = 0x15)
  _addr = 0x38; // 默认设为 FT6336G
  Wire.beginTransmission(0x38);
  if (Wire.endTransmission() != 0) {
    Serial.println("🔍 [触控] 0x38 (FT6336G) 未响应，正在尝试扫描 0x15 (CST820)...");
    Wire.beginTransmission(0x15);
    if (Wire.endTransmission() == 0) {
      _addr = 0x15;
      Serial.println("🎯 [触控] 成功发现 CST820 触控芯片，地址设置为 0x15");
    } else {
      Serial.println("⚠️ [触控] 未扫描到任何已知触控芯片，默认保持 0x38");
    }
  } else {
    Serial.println("🎯 [触控] 成功发现 FT6336G 触控芯片，地址设置为 0x38");
  }
}

bool CST820Touch::getTouch(uint16_t &x, uint16_t &y) {
  Wire.beginTransmission(_addr);
  Wire.write(0x02); // 读取手指数量
  if (Wire.endTransmission(false) != 0) return false;
  
  Wire.requestFrom((uint16_t)_addr, (uint8_t)5, true);
  if (Wire.available() >= 5) {
    uint8_t fingers = Wire.read();
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();
    
    // 过滤合理的触控点数量
    uint8_t touch_points = fingers & 0x0F;
    if (touch_points > 0 && touch_points <= 2) {
      uint16_t raw_x = ((xh & 0x0F) << 8) | xl;
      uint16_t raw_y = ((yh & 0x0F) << 8) | yl;
      
      // 映射到横屏 (320x240)
      x = raw_y;
      y = 240 - raw_x;
      return true;
    }
  }
  return false;
}
