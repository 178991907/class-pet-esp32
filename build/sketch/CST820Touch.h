#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/CST820Touch.h"
#ifndef CST820_TOUCH_H
#define CST820_TOUCH_H

#include <Arduino.h>
#include <Wire.h>

class CST820Touch {
public:
  void init(int sda = 16, int scl = 15, int rst = -1, int int_pin = 21);
  bool getTouch(uint16_t &x, uint16_t &y);
private:
  int _sda, _scl, _rst, _int;
  uint8_t _addr; // 动态保存扫描到的触控芯片 I2C 地址 (FT6336G=0x38, CST820=0x15)
};

#endif
