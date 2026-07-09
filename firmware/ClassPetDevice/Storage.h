/**
 * @file Storage.h
 * @brief 班级宠物园通用固件 - 本地持久化存储模块
 * @note 负责 WiFi 凭据、服务器地址和离线待补传任务队列的读写
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>

// 存储配置结构体
struct DeviceConfig {
  uint32_t magic_number;   // 用于校验是否为有效数据 (0x1A2B3C4D)
  char wifi_ssid[33];      // WiFi SSID
  char wifi_password[65];  // WiFi 密码
  char server_url[128];    // 后端服务器地址 (如 https://class-pet-esp32-ten.vercel.app)
  char device_secret[65];  // 设备 HMAC 签名密钥
  char proxy_ip[16];       // 新增 Cloudflare 优选/代理 IP
  bool is_configured;      // 是否已配网
};

// 离线任务申报数据结构
struct OfflineTask {
  char task_name[64];      // 申报的任务名
  int points;              // 分值
  uint32_t timestamp;      // 产生时的时间戳
};

class Storage {
public:
  static void init();
  
  // 系统配置读写
  static bool loadConfig(DeviceConfig& config);
  static bool saveConfig(DeviceConfig config);
  static void clearConfig();

  // 离线队列读写
  static int getOfflineQueueSize();
  static bool pushOfflineTask(const OfflineTask& task);
  static bool popOfflineTask(OfflineTask& task); // 获取并移除队列头部元素
  static bool peekOfflineTask(OfflineTask& task); // 仅读取队列头部元素
  static void clearOfflineQueue();

  static bool formatSDCard();

private:
  static const int EEPROM_SIZE = 2048; // 分配 2048 字节存储空间（需容纳配置+20条离线任务队列）
  
  // 地址分配
  static const int ADDR_CONFIG = 0; // 配置结构体起始地址
  static const int ADDR_QUEUE_START = 512; // 队列数据起始地址
  static const int ADDR_QUEUE_COUNT = 510; // 记录当前队列大小的地址
};

#endif // STORAGE_H
