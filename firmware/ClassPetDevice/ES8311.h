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

        uint8_t id1 = readReg(0xFD);
        uint8_t id2 = readReg(0xFE);
        Serial.printf("🔍 ES8311 芯片 ID: 0x%02X%02X\n", id1, id2);
        if (id1 != 0x83 || id2 != 0x11) {
            Serial.printf("❌ ES8311 芯片 ID 错误! 期望 0x8311，实际为 0x%02X%02X\n", id1, id2);
            return false;
        }

        // ES8311 初始化序列 (设置为 Slave 模式，16kHz/44.1kHz 自适应)
        writeReg(0x00, 0x1F); delay(10); // Reset
        writeReg(0x00, 0x00);
        writeReg(0x01, 0xBF); // Clock management (启用BCLK作为时钟源并开启所有ADC/DAC时钟)
        // 关键：在没有外置 MCLK 的情况下，很多开发板要求 ES8311 从 BCLK 恢复时钟 (BCLK PLL)
        writeReg(0x02, 0x10); // BCLK PLL mode
        writeReg(0x0D, 0x01); // Power up analog (VMID, MICBIAS) - 给麦克风偏置供电
        writeReg(0x0E, 0x02); // Power up Vref
        writeReg(0x12, 0x00); // Power up ADC
        writeReg(0x13, 0x10); // ADC volume (analog?)
        writeReg(0x14, 0x1A); // MIC gain
        writeReg(0x17, 0xC8); // ADC digital volume (非常关键，默认0x00是-95.5dB纯静音，0xBF是0dB)

        writeReg(0x09, 0x0C); // DAC I2S format, 16 bit
        writeReg(0x0A, 0x0C); // ADC I2S format, 16 bit
        writeReg(0x31, 0x40); // Power up DAC
        writeReg(0x32, 0x00); // DAC digital volume
        writeReg(0x37, 0x08); // Route DAC to output

        Serial.println("✅ ES8311 (麦克风/喇叭编解码器) 初始化成功！");
        return true;
    }

    // 切换到"播放"模式：打开 DAC，关闭 ADC（避免数据冲突，ES8311 的 SDOUT/SDIN 是同一根线）
    static void enableDAC(bool on) {
        if (on) {
            // 打开 DAC 电源与输出路由
            writeReg(0x31, 0x40); // Power up DAC
            writeReg(0x37, 0x08); // Route DAC to output
            // 关闭 ADC 模拟通路避免功耗与干扰
            writeReg(0x12, 0x00); // Power down ADC
            Serial.println("🔊 [ES8311] 切换到 DAC 播放模式 (ADC 已关闭)");
        } else {
            writeReg(0x31, 0x32); // Power down DAC
            writeReg(0x37, 0x00); // Mute DAC output
        }
    }

    // 切换到"录音"模式：打开 ADC，关闭 DAC
    static void enableADC(bool on) {
        if (on) {
            // 打开模拟偏置、ADC 电源
            writeReg(0x0D, 0x01); // Power up analog (VMID, MICBIAS)
            writeReg(0x0E, 0x02); // Power up Vref
            writeReg(0x12, 0x00); // Power up ADC
            // 确保 ADC 数字音量有输出 (0xC8 = 0dB 左右)
            writeReg(0x17, 0xC8);
            // 关闭 DAC 模拟通路避免冲突
            writeReg(0x31, 0x32); // Power down DAC
            writeReg(0x37, 0x00); // Mute DAC output
            Serial.println("🎙️ [ES8311] 切换到 ADC 录音模式 (DAC 已关闭)");
        } else {
            writeReg(0x12, 0x02); // Power down ADC
        }
    }

private:
    static void writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(0x18);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }

    static uint8_t readReg(uint8_t reg) {
        Wire.beginTransmission(0x18);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) {
            return 0xFF;
        }
        Wire.requestFrom((uint8_t)0x18, (uint8_t)1);
        if (Wire.available()) {
            return Wire.read();
        }
        return 0xFF;
    }
};

#endif
