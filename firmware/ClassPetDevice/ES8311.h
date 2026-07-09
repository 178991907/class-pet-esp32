#ifndef ES8311_H
#define ES8311_H

#include <Arduino.h>
#include <Wire.h>

class ES8311 {
public:
    static bool init(int sda, int scl) {
        Wire.begin(sda, scl);
        Wire.setClock(100000); // 100kHz
        
        // 检查设备是否在线 (ES8311 I2C 地址为 0x18)
        Wire.beginTransmission(0x18);
        if (Wire.endTransmission() != 0) {
            Serial.println("❌ ES8311 初始化失败，未找到 I2C 设备 0x18");
            return false;
        }

        // ES8311 初始化序列 (设置为 Slave 模式，16kHz/44.1kHz 自适应)
        writeReg(0x00, 0x1F); delay(10); // Reset
        writeReg(0x00, 0x00);
        writeReg(0x01, 0x30); // Clock management (MCLK从ESP32输入，或者无需MCLK如果支持BCLK PLL)
        // 关键：在没有外置 MCLK 的情况下，很多开发板要求 ES8311 从 BCLK 恢复时钟 (BCLK PLL)
        writeReg(0x02, 0x10); // BCLK PLL mode
        writeReg(0x0E, 0x02); // Power up Vref
        writeReg(0x12, 0x00); // Power up ADC
        writeReg(0x13, 0x10); // ADC volume
        writeReg(0x14, 0x1A); // MIC gain
        
        writeReg(0x09, 0x0C); // I2S format, 16 bit
        writeReg(0x31, 0x40); // Power up DAC
        writeReg(0x32, 0x00); // DAC digital volume
        writeReg(0x37, 0x08); // Route DAC to output
        
        Serial.println("✅ ES8311 (麦克风/喇叭编解码器) 初始化成功！");
        return true;
    }

private:
    static void writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(0x18);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }
};

#endif
