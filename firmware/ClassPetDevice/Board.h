/**
 * @file Board.h
 * @brief 班级宠物园设备硬件引脚与配置文件
 */

#ifndef BOARD_H
#define BOARD_H

// ==========================================
// 1. 液晶显示屏引脚配置 (ST7789 SPI)
// ==========================================
#define TFT_BL_PIN             46    // 屏幕背光引脚

// ==========================================
// 2. 电容触摸屏引脚配置 (CST820 / FT6336 I2C)
// ==========================================
#define TOUCH_SDA_PIN          16   // I2C 数据引脚
#define TOUCH_SCL_PIN          15   // I2C 时钟引脚
#define TOUCH_INT_PIN          21   // 中断引脚
#define TOUCH_RST_PIN          -1   // 复位引脚

// ==========================================
// 3. 音频外设引脚配置 (直连 I2S 麦克风与功放)
// ==========================================
#define AUDIO_EN_PIN           1    // 功放使能引脚 (低电平有效)
#define I2S_BCLK_PIN           4    // I2S 比特时钟 (MCLK/BCLK)
#define I2S_LRC_PIN            5    // I2S 左右声道时钟 (WS/LRC)
#define I2S_DOUT_PIN           6    // I2S 数据输出 (喇叭)
#define I2S_MIC_SD_PIN         8    // I2S 数据输入 (麦克风)

// ==========================================
// 4. 物理按键与指示配置 (IO0)
// ==========================================
#define PHYSICAL_KEY_PIN       0    // 物理按键 BOOT 引脚

#endif // BOARD_H
