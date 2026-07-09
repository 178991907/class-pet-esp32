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
// 3. 音频外设引脚配置 (ES8311 I2S & 功放)
// ==========================================
#define I2S_BCLK_PIN           5    // I2S 比特时钟 (Bit Clock)
#define I2S_LRC_PIN            7    // I2S 左右声道选择 (L/R Clock / WS)
#define I2S_DOUT_PIN           8    // I2S 数据输出 (Data Out) 到喇叭
#define I2S_MIC_SCK_PIN        5    // 麦克风时钟复用 BCLK
#define I2S_MIC_WS_PIN         7    // 麦克风时钟复用 LRC
#define I2S_MIC_SD_PIN         6    // 麦克风数据输入 (Data In)
#define AUDIO_EN_PIN           2    // 功放使能引脚 (AUDIO_EN)

// ==========================================
// 4. 物理按键与指示配置 (IO0)
// ==========================================
#define PHYSICAL_KEY_PIN       0    // 物理按键 BOOT 引脚

#endif // BOARD_H
