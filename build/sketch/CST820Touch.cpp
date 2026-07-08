#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/CST820Touch.cpp"
#include "CST820Touch.h"

void CST820Touch::init(int sda, int scl, int rst, int int_pin) {
  _sda = sda; _scl = scl; _rst = rst; _int = int_pin;
  Wire.begin(_sda, _scl);
  
  if (_rst != -1) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(50);
    digitalWrite(_rst, HIGH);
    delay(50);
  }
}

bool CST820Touch::getTouch(uint16_t &x, uint16_t &y) {
  Wire.beginTransmission(CST820_ADDR);
  Wire.write(0x02); // 读取手指数量
  if (Wire.endTransmission(false) != 0) return false;
  
  Wire.requestFrom((uint16_t)CST820_ADDR, (uint8_t)5, true);
  if (Wire.available() >= 5) {
    uint8_t fingers = Wire.read();
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();
    
    if (fingers > 0 && fingers < 5) {
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
