#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Config.h"
/**
 * @file Config.h
 * @brief 班级宠物园通用固件 - 全局配置文件
 * @note 包含设备通信密钥、配网参数和硬件宏定义
 */

#ifndef CONFIG_H
#define CONFIG_H

// 启用串口调试输出
#define DEBUG_ENABLE 1

#if DEBUG_ENABLE
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(x, ...) Serial.printf(x, __VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(x, ...)
#endif

// 串口波特率
#define SERIAL_BAUD 115200

// 默认配网 AP 热点名称前缀（后接设备 MAC 地址后四位）
#define AP_SSID_PREFIX "ClassPet-Setup-"
#define AP_DEFAULT_IP "192.168.4.1"

// 默认 API 通信密钥（需与后端 DEVICE_SECRET 保持一致）
// 在实际生产部署中，应通过配网网页配置或烧录各自唯一的 Secret，此处提供默认开发值
#define DEFAULT_DEVICE_SECRET "class-pet-device-secret"

// 连接 WiFi 的超时时间（毫秒）
#define WIFI_CONNECT_TIMEOUT_MS 15000

// 离线任务申报队列的最大缓存条数
#define OFFLINE_QUEUE_MAX_SIZE 20

// NTP 服务器配置，用于同步签名所用时间戳
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "cn.ntp.org.cn"
#define GMT_OFFSET_SEC 28800 // 北京时间 (GMT+8)
#define DAYLIGHT_OFFSET_SEC 0

// 番茄工作法默认时长（分钟）
#define DEFAULT_POMODORO_MINS 25

// 设备底层状态枚举
enum DeviceState {
  STATE_INIT,          // 初始化
  STATE_SETUP_AP,      // 配网热点开启中
  STATE_CONNECTING_WIFI,// 正在连接路由器 WiFi
  STATE_NORMAL_ONLINE, // 在线正常待机状态
  STATE_NORMAL_OFFLINE,// 离线正常待机状态
  STATE_PROCESSING,    // 正在请求云端 API
  STATE_RECORDING,     // 正在录音
  STATE_PLAYING_AUDIO, // 正在播放音频（TTS 语音合成）
  STATE_POMODORO       // 本地番茄钟计时中
};

#endif // CONFIG_H
