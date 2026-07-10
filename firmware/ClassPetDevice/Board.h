/**
 * @file Board.h
 * @brief 班级宠物园设备硬件引脚与配置文件
 *
 * 引脚配置依据:
 *   1. LCDwiki 官方 wiki: https://www.lcdwiki.com/zh/2.8inch_ESP32-S3_Display
 *   2. 官方示例 Example_16_music / Example_17_echo / Example_30_ai_chat
 *   3. 开发板型号: ES3C28P (ESP32-S3 N16R8, 16MB Flash + 8MB PSRAM)
 *
 * 关键硬件:
 *   - LCD: ILI9341V, 240x320, 4线 SPI
 *   - 触摸: FT6336G, I2C
 *   - 音频编解码: ES8311 (I2C 0x18, I2S)
 *   - 音频功放: FM8002E (IO1 低电平使能)
 *   - 麦克风: MEMS LMA2718B381-OA7
 *   - RGB LED: WS2812B (IO42)
 *   - SD卡: SDIO 4线
 */

#ifndef BOARD_H
#define BOARD_H

// ==========================================
// 1. 液晶显示屏引脚配置 (ILI9341V 4线 SPI)
// ==========================================
#define TFT_MISO_PIN           13    // LCD SPI MISO
#define TFT_MOSI_PIN           11    // LCD SPI MOSI
#define TFT_SCLK_PIN           12    // LCD SPI SCK
#define TFT_CS_PIN             10    // LCD 片选
#define TFT_DC_PIN             46    // LCD 数据/命令选择
#define TFT_BL_PIN             45    // LCD 背光控制 (高电平点亮)
#define TFT_RST_PIN            -1    // LCD 复位 (接 EN 共复位)

// ==========================================
// 2. 电容触摸屏引脚配置 (FT6336G I2C)
// ==========================================
#define TOUCH_SDA_PIN          16    // I2C 数据引脚 (与 ES8311 共用)
#define TOUCH_SCL_PIN          15    // I2C 时钟引脚 (与 ES8311 共用)
#define TOUCH_INT_PIN          17    // 触摸中断引脚
#define TOUCH_RST_PIN          18    // 触摸复位引脚

// ==========================================
// 3. 音频外设引脚配置 (ES8311 + FM8002E)
// ==========================================
// 按官方 Example_16_music / Example_17_echo 引脚定义:
//   - ES8311 I2C 地址: 0x18 (CE pin low)
//   - ES8311 I2C 与触摸屏共用 I2C 总线 (SDA=16, SCL=15)
//   - FM8002E 功放使能: IO1, 低电平使能
//   - I2S 全双工: TX 和 RX 同时工作在 I2S_NUM_1
#define AUDIO_EN_PIN           1     // FM8002E 功放使能 (低电平使能)
#define I2S_MCLK_PIN           4     // I2S 主时钟 (MCLK)
#define I2S_BCLK_PIN           5     // I2S 比特时钟 (BCLK)
#define I2S_LRC_PIN            7     // I2S 左右声道时钟 (WS/LRCK)
#define I2S_DOUT_PIN           8     // I2S 数据输出 (ESP32 → ES8311 → DAC → 喇叭)
#define I2S_DIN_PIN            6     // I2S 数据输入 (ES8311 → ESP32, 麦克风)
#define I2S_NUM_USE            I2S_NUM_1  // 官方示例使用 I2S_NUM_1

// ES8311 I2C 配置
#define ES8311_I2C_ADDR        0x18  // ES8311 I2C 地址 (CE pin low)
#define ES8311_I2C_FREQ        400000 // I2C 频率 400kHz

// ==========================================
// 4. SD 卡引脚配置 (SDIO 4线)
// ==========================================
#define SD_SCK_PIN             38
#define SD_CMD_PIN             40
#define SD_D0_PIN              39
#define SD_D1_PIN              41
#define SD_D2_PIN              48
#define SD_D3_PIN              47

// ==========================================
// 5. 其他外设
// ==========================================
#define PHYSICAL_KEY_PIN       0     // BOOT 按键
#define RGB_LED_PIN            42    // WS2812B RGB LED
#define BATTERY_ADC_PIN        9     // 电池电压 ADC

// ==========================================
// 6. 编译配置
// ==========================================
// 开发板: ESP32-S3 N16R8 (16MB Flash + 8MB OPI PSRAM)
// 编译 FQBN:
//   esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc
#define AUDIO_SAMPLE_RATE      44100  // 官方示例使用 44100Hz
#define AUDIO_MCLK_MULTIPLE    256    // MCLK = 采样率 * 256

#endif // BOARD_H
