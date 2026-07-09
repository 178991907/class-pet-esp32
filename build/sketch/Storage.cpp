#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Storage.cpp"
/**
 * @file Storage.cpp
 * @brief 班级宠物园通用固件 - 本地持久化存储模块实现
 */

#include "Storage.h"
#include <EEPROM.h>
#include <FFat.h>
#include <SD_MMC.h>
#include <SPI.h>
#include "Config.h"
#include "Board.h"


#include "ff.h"
#include <esp_idf_version.h>

void Storage::init() {
  DEBUG_PRINTLN("💾 正在初始化 EEPROM 存储模块...");
  EEPROM.begin(EEPROM_SIZE);
  
  DEBUG_PRINTLN("💾 正在初始化 FFat 文件系统...");
  if (!FFat.begin(true)) {
    DEBUG_PRINTLN("❌ FFat 挂载失败");
  } else {
    DEBUG_PRINTLN("✅ FFat 挂载成功");
  }

  DEBUG_PRINTLN("💾 正在初始化 SD 卡系统...");
  // 使用 1-bit 模式 SDIO (避免占用 D1/D2/D3，留给 I2S 麦克风)
  SD_MMC.setPins(38, 40, 39); // CLK=38, CMD=40, D0=39
  
  // 限制时钟频率为 10MHz (10000 kHz)，提高抗干扰能力，防读写卡死
  if (!SD_MMC.begin("/sdcard", true, false, 10000)) {
    DEBUG_PRINTLN("⚠️ 未检测到 SD 卡，或挂载失败！");
  } else {
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
      DEBUG_PRINTLN("⚠️ 警告：检测到 SD 卡模块，但无有效卡片。");
    } else {
      DEBUG_PRINTF("✅ SD 卡挂载成功！存储空间: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
      
      // =======================================================
      // 强制格式化 TF 卡调试宏 (若要格式化卡，请取消注释下一行，烧录运行一次后，再重新注释并烧录)
      // #define FORCE_FORMAT_SD
      // =======================================================
      #ifdef FORCE_FORMAT_SD
        formatSDCard();
      #endif
    }
  }
}

bool Storage::formatSDCard() {
  DEBUG_PRINTLN("💾 开始进行 TF 卡 (FAT32) 底层格式化...");
  
  char drv[3] = {'0', ':', 0};
  const size_t workbuf_size = 4096;
  void* workbuf = malloc(workbuf_size);
  if (workbuf == NULL) {
    DEBUG_PRINTLN("❌ 内存不足，无法分配格式化缓冲区");
    return false;
  }

  FRESULT res;
  #if (ESP_IDF_VERSION_MAJOR < 5)
    // ESP-IDF v4 (对应 Arduino ESP32 v2.x)
    res = f_mkfs(drv, FM_ANY, 0, workbuf, workbuf_size);
  #else
    // ESP-IDF v5 (对应 Arduino ESP32 v3.x)
    const MKFS_PARM opt = {(BYTE)FM_ANY, 0, 0, 0, 0};
    res = f_mkfs(drv, &opt, workbuf, workbuf_size);
  #endif

  free(workbuf);

  if (res != FR_OK) {
    DEBUG_PRINTF("❌ TF 卡格式化失败，FatFs 错误码: %d\n", res);
    return false;
  }
  
  DEBUG_PRINTLN("⚠️ TF 卡格式化成功！请及时在 Storage.cpp 中注释掉 #define FORCE_FORMAT_SD 并重新烧录，防止每次启动都进行格式化！");
  return true;
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
