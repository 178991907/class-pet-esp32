/**
 * @file ClassPetDevice.ino
 * @brief 班级宠物园设备端主入口程序
 * @note 采用 LVGL 9.5 图形库 + ESP-IDF 异步多任务多核调度架构重写
 */

#include "Config.h"
#include "Storage.h"
#include "Network.h"
#include "ApiClient.h"
#include "TomatoTimer.h"
#include "AudioHAL.h"
#include "ESP32Audio.h"
#include "Board.h"
#include "LcdDisplay.h"
#include "ClassPetUI.h"
#include "DeviceStateMachine.h"

#if defined(ESP32)
  #include <esp_system.h>
#endif

// ==========================================
// 1. 全局配置与实例定义
// ==========================================
DeviceConfig deviceConfig;
AudioHAL* audio = nullptr;
TomatoTimer tomatoTimer;

// 学生核心状态缓冲区 (供 UI 层和网络同步层共享)
String studentName = "未绑定";
String className = "";
int totalPoints = 0;
String petType = "";
int petLevel = 1;
int petExp = 0;
int expProgress = 0;
int expRequired = 40;
bool isMaxLevel = false;


// ==========================================
// 2. 外部调用的状态更新辅助方法
// ==========================================
void updateStudentCache(const DeviceStatusResponse& res) {
  if (!res.success) return;
  
  studentName = res.student_name;
  className = res.class_name;
  totalPoints = res.total_points;
  petType = res.pet_type;
  petLevel = res.pet_level;
  petExp = res.pet_exp;
  expProgress = res.exp_progress;
  expRequired = res.exp_required > 0 ? res.exp_required : 40;
  isMaxLevel = res.is_max_level;
}

// ==========================================
// 3. LVGL 核心渲染与触控扫描后台线程 (Core 0)
// ==========================================
void lvglTask(void* arg) {
  DEBUG_PRINTLN("🖥️ [显示屏] LVGL 渲染后台线程启动成功！");
  pinMode(PHYSICAL_KEY_PIN, INPUT_PULLUP);
  
  while (true) {
    // 驱动 LVGL 内部定时器与渲染轮询
    LcdDisplay::getInstance().update();
    
    // 监听物理按键 (IO0 / BOOT 按键)
    if (digitalRead(PHYSICAL_KEY_PIN) == LOW) {
      // 物理按键触发语音模拟对话
      DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_START);
      vTaskDelay(pdMS_TO_TICKS(500)); // 消抖防连击
    }
    
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms 频率刷新，保障流畅度
  }
}

// ==========================================
// 4. 番茄钟计时结束硬件回调与任务申报
// ==========================================
void onPomodoroFinished() {
  DEBUG_PRINTLN("🍅 番茄钟结束！触发本地蜂鸣器蜂鸣，并启动积分提交...");
  
  // 离线任务结构体
  OfflineTask task;
  strncpy(task.task_name, "认真专注学习25分钟", sizeof(task.task_name) - 1);
  task.points = 2; // 默认加 2 分
  task.timestamp = Network::getUnixTimestamp();

  // 1. 压入本地 Flash/EEPROM 暂存队列
  Storage::pushOfflineTask(task);
  // 2. 屏幕字幕闪烁提示
  ClassPetUI::getInstance().showToast("专注结束！+2积分已存入暂存队列", 5000);

  // 播放番茄钟结束提示音，并推送事件通知状态机
  audio->playAudioStream("http://local-spiffs/ringtone.mp3");
  DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_PLAY_DONE); // 用语音播完事件切回正常态
}

// ==========================================
// 5. Arduino setup & loop
// ==========================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  
  DEBUG_PRINTLN("\n=== 班级宠物园 (ClassPetGarden) 固件重构系统启动 ===");
  DEBUG_PRINTF("🔁 [启动] 重启原因为: %u\n", esp_reset_reason());
  
  // A. 加载 EEPROM 本地配置
  Storage::init();
  Network::init();
  if (Storage::loadConfig(deviceConfig)) {
    DEBUG_PRINTLN("💾 成功加载本地存储配置:");
    DEBUG_PRINTF("   - SSID: %s\n", deviceConfig.wifi_ssid);
    DEBUG_PRINTF("   - Server: %s\n", deviceConfig.server_url);
    DEBUG_PRINTF("   - Proxy IP: %s\n", deviceConfig.proxy_ip);
  } else {
    DEBUG_PRINTLN("⚠️ 未加载到有效配置，等待系统自适应...");
  }
  
  // B. 启动硬件显示面板 & LVGL 核心
  LcdDisplay::getInstance().init();
  ClassPetUI::getInstance().init();
  
  // 注册番茄钟计时结束回调
  tomatoTimer.onFinished(onPomodoroFinished);
  
  // 创建 LVGL 图形线程，分配 8KB 堆栈，运行于 Core 0，优先级 4
  xTaskCreatePinnedToCore(lvglTask, "LVGLTask", 8192, nullptr, 4, nullptr, 0);
  
  // C. 启动音频驱动
  audio = new ESP32Audio();
  audio->init();
  
  // D. 启动核心状态机
  DeviceStateMachine::getInstance().init();
}

void loop() {
  // 主线程进入休眠，系统运转完全由 LVGL 线程和状态机线程托管
  vTaskDelay(portMAX_DELAY);
}
