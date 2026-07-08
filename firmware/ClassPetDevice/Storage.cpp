/**
 * @file Storage.cpp
 * @brief 班级宠物园通用固件 - 本地持久化存储模块实现
 */

#include "Storage.h"
#include <EEPROM.h>
#include <FFat.h>
#include "Config.h"

void Storage::init() {
  DEBUG_PRINTLN("💾 正在初始化 EEPROM 存储模块...");
  EEPROM.begin(EEPROM_SIZE);
  
  DEBUG_PRINTLN("💾 正在初始化 FFat 文件系统...");
  if (!FFat.begin(true)) {
    DEBUG_PRINTLN("❌ FFat 挂载失败");
  } else {
    DEBUG_PRINTLN("✅ FFat 挂载成功");
  }
}

bool Storage::loadConfig(DeviceConfig& config) {
  EEPROM.get(ADDR_CONFIG, config);
  
  // 检查是否已有效配网，并且具备正确的魔数校验，以防读到全新 Flash 中的垃圾数据
  if (config.magic_number == 0x1A2B3C4D && config.is_configured) {
    DEBUG_PRINTLN("💾 成功加载本地存储配置:");
    DEBUG_PRINTF("   - SSID: %s\n", config.wifi_ssid);
    DEBUG_PRINTF("   - Server: %s\n", config.server_url);
    return true;
  }
  
  DEBUG_PRINTLN("💾 本地无有效配网配置或校验失败。");
  return false;
}

bool Storage::saveConfig(DeviceConfig config) {
  DEBUG_PRINTLN("💾 正在向本地写入配网配置...");
  config.magic_number = 0x1A2B3C4D; // 强制打上合法数据标签
  EEPROM.put(ADDR_CONFIG, config);
  bool success = EEPROM.commit();
  if (success) {
    DEBUG_PRINTLN("💾 配置写入成功。");
  } else {
    DEBUG_PRINTLN("❌ 错误: 本地配置写入提交失败！");
  }
  return success;
}

void Storage::clearConfig() {
  DEBUG_PRINTLN("💾 清除本地配网配置中...");
  DeviceConfig config;
  memset(&config, 0, sizeof(DeviceConfig));
  config.is_configured = false;
  saveConfig(config);
}

int Storage::getOfflineQueueSize() {
  uint16_t count = 0;
  EEPROM.get(ADDR_QUEUE_COUNT, count);
  
  // 防止垃圾数据溢出
  if (count > OFFLINE_QUEUE_MAX_SIZE) {
    count = 0;
    EEPROM.put(ADDR_QUEUE_COUNT, count);
    EEPROM.commit();
  }
  return count;
}

bool Storage::pushOfflineTask(const OfflineTask& task) {
  int count = getOfflineQueueSize();
  if (count >= OFFLINE_QUEUE_MAX_SIZE) {
    DEBUG_PRINTLN("⚠️ 警告: 离线任务队列已满，抛弃最老的数据！");
    
    // 队列满时，将后面的元素向前平移，腾出尾部空间
    OfflineTask temp;
    for (int i = 1; i < count; i++) {
      int srcAddr = ADDR_QUEUE_START + i * sizeof(OfflineTask);
      int destAddr = ADDR_QUEUE_START + (i - 1) * sizeof(OfflineTask);
      EEPROM.get(srcAddr, temp);
      EEPROM.put(destAddr, temp);
    }
    count--; // 队列实际有效计数减1
  }

  // 写入尾部
  int targetAddr = ADDR_QUEUE_START + count * sizeof(OfflineTask);
  EEPROM.put(targetAddr, task);
  
  // 更新计数
  count++;
  EEPROM.put(ADDR_QUEUE_COUNT, (uint16_t)count);
  
  bool success = EEPROM.commit();
  if (success) {
    DEBUG_PRINTF("💾 成功暂存离线申报: %s (分值: %d) 队列深度: %d\n", task.task_name, task.points, count);
  }
  return success;
}

bool Storage::popOfflineTask(OfflineTask& task) {
  int count = getOfflineQueueSize();
  if (count <= 0) {
    return false;
  }

  // 1. 读取头部元素（最早存入的）
  EEPROM.get(ADDR_QUEUE_START, task);

  // 2. 所有后续元素向前移动一位
  OfflineTask temp;
  for (int i = 1; i < count; i++) {
    int srcAddr = ADDR_QUEUE_START + i * sizeof(OfflineTask);
    int destAddr = ADDR_QUEUE_START + (i - 1) * sizeof(OfflineTask);
    EEPROM.get(srcAddr, temp);
    EEPROM.put(destAddr, temp);
  }

  // 3. 计数减 1 写入
  count--;
  EEPROM.put(ADDR_QUEUE_COUNT, (uint16_t)count);
  
  EEPROM.commit();
  return true;
}

bool Storage::peekOfflineTask(OfflineTask& task) {
  int count = getOfflineQueueSize();
  if (count <= 0) {
    return false;
  }
  EEPROM.get(ADDR_QUEUE_START, task);
  return true;
}

void Storage::clearOfflineQueue() {
  DEBUG_PRINTLN("💾 清空离线任务队列...");
  uint16_t count = 0;
  EEPROM.put(ADDR_QUEUE_COUNT, count);
  EEPROM.commit();
}
