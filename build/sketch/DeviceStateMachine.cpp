#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/DeviceStateMachine.cpp"
/**
 * @file DeviceStateMachine.cpp
 * @brief 班级宠物园设备状态机系统实现
 */

#include "DeviceStateMachine.h"
#include "Board.h"
#include "ClassPetUI.h"
#include "LcdDisplay.h"
#include "ApiClient.h"
#include "Network.h"
#include "Config.h"
#include "Storage.h"
#include "AudioHAL.h"
#include "TomatoTimer.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// 电池 ADC 引脚 (适用于 Caturda / Sunton 2.8寸 S3 屏)
#define PIN_BATTERY_ADC 9

// 电池平滑电压状态
static float smoothed_voltage = -1.0f;

// 锂电池放电曲线电压->百分比映射表
struct LiPoCurve { float v; int p; };
static const LiPoCurve lipo_curve[] = {
  {4.15f, 100},
  {4.05f, 90},
  {3.95f, 80},
  {3.85f, 60},
  {3.75f, 40},
  {3.65f, 20},
  {3.55f, 5},
  {3.20f, 0}
};

// 内部函数：读取并计算电池电量
static void getBatteryStatus(int& pct, bool& isCharging) {
  // 多次采样求平均以过滤高频噪声
  int raw_sum = 0;
  const int samples = 20;
  for (int i = 0; i < samples; i++) {
    raw_sum += analogRead(PIN_BATTERY_ADC);
  }
  float raw_avg = (float)raw_sum / samples;
  
  // 计算实际电池电压
  float current_voltage = (raw_avg / 4095.0f) * 3.3f * 2.0f;
  
  // 粗略校准：有时 ADC 读数偏小，可以加上偏移补偿
  current_voltage += 0.05f; 

  // IIR 低通滤波，极其平滑数据防止电量乱跳
  if (smoothed_voltage < 0) {
    smoothed_voltage = current_voltage; // 初始赋值
  } else {
    smoothed_voltage = smoothed_voltage * 0.95f + current_voltage * 0.05f;
  }

  // 查表法映射真实百分比
  pct = 0;
  if (smoothed_voltage >= lipo_curve[0].v) {
    pct = 100;
  } else if (smoothed_voltage <= lipo_curve[7].v) {
    pct = 0;
  } else {
    for (int i = 0; i < 7; i++) {
      if (smoothed_voltage <= lipo_curve[i].v && smoothed_voltage > lipo_curve[i+1].v) {
        float v_range = lipo_curve[i].v - lipo_curve[i+1].v;
        float p_range = lipo_curve[i].p - lipo_curve[i+1].p;
        float fraction = (smoothed_voltage - lipo_curve[i+1].v) / v_range;
        pct = lipo_curve[i+1].p + (int)(fraction * p_range);
        break;
      }
    }
  }

  if (pct > 100) pct = 100;
  if (pct < 0) pct = 0;
  
  // 由于这块主板没有专用的充电识别引脚（VBUS/CHRG），单靠电压阈值会发生频繁误判（闪烁）
  // 最佳方案是直接屏蔽充电判定，只显示真实的电池百分比
  isCharging = false;
}

// LVGL 多核互斥锁宏：状态机在 Core 1 操作 UI 前必须加锁
#define LVGL_LOCK()   LcdDisplay::getInstance().lock()
#define LVGL_UNLOCK() LcdDisplay::getInstance().unlock()

// ==========================================
// 外部全局变量引用
// ==========================================
extern DeviceConfig deviceConfig;
extern AudioHAL* audio;
extern TomatoTimer tomatoTimer;

extern String studentName;
extern String className;
extern int totalPoints;
extern String petType;
extern int petLevel;
extern int petExp;
extern int expProgress;
extern int expRequired;
extern bool isMaxLevel;

extern void updateStudentCache(const DeviceStatusResponse& res);

// 静态诊断状态
extern DiagnosticInfo lastDiagnostic;

// ==========================================
// 核心状态机生命周期
// ==========================================
void DeviceStateMachine::init() {
  // 1. 创建 FreeRTOS 队列（深度 16，传输 DeviceEvent）
  _queue = xQueueCreate(16, sizeof(DeviceEvent));
  
  // 2. 启动状态机子线程，堆栈增大为 16KB 防止栈溢出，运行在 Core 1
  BaseType_t ret = xTaskCreatePinnedToCore(taskEntry, "AppStateMachineTask", 16384, this, 5, nullptr, 1);
  if (ret != pdPASS) {
    Serial.printf("❌ [状态机] 创建 FreeRTOS 状态机任务失败! 错误码: %d\n", ret);
  } else {
    Serial.println("✅ [状态机] 创建 FreeRTOS 状态机任务成功！");
  }
}

void DeviceStateMachine::postEvent(DeviceEvent ev) {
  if (_queue) {
    xQueueSend(_queue, &ev, 0); // 非阻塞形式发送
  }
}

void DeviceStateMachine::taskEntry(void* arg) {
  DeviceStateMachine* sm = (DeviceStateMachine*)arg;
  DEBUG_PRINTLN("⚙️ [状态机] FreeRTOS 状态机任务启动成功！");
  
  while (true) {
    DeviceEvent ev = EVENT_NONE;
    // 每 100ms 检查一次队列
    if (xQueueReceive(sm->_queue, &ev, pdMS_TO_TICKS(100)) == pdTRUE) {
      sm->handleEvent(ev);
    }
    
    // 执行当前状态轮询
    sm->loopState();
    
    // 主循环避让以保障其他任务调度
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ==========================================
// 事件分发器 (Event Dispatcher)
// ==========================================
void DeviceStateMachine::handleEvent(DeviceEvent ev) {
  switch (ev) {
    case EVENT_POMODORO_START:
      if (_state == STATE_NORMAL_ONLINE || _state == STATE_NORMAL_OFFLINE) {
        DEBUG_PRINTLN("🍅 [状态机] 收到事件: 启动番茄工作钟");
        tomatoTimer.start(25); // 开启 25 分钟番茄钟
        _state = STATE_POMODORO;
      }
      break;
      
    case EVENT_POMODORO_STOP:
      if (_state == STATE_POMODORO) {
        DEBUG_PRINTLN("🍅 [状态机] 收到事件: 退出番茄工作钟");
        tomatoTimer.stop();
        _state = STATE_NORMAL_ONLINE;
      }
      break;
      
    case EVENT_POMODORO_PAUSE_RESUME:
      if (_state == STATE_POMODORO) {
        if (tomatoTimer.isPaused()) {
          DEBUG_PRINTLN("🍅 [状态机] 收到事件: 番茄钟恢复运行");
          tomatoTimer.resume();
        } else {
          DEBUG_PRINTLN("🍅 [状态机] 收到事件: 番茄钟暂停");
          tomatoTimer.pause();
        }
      }
      break;

    case EVENT_POMODORO_ADJUST:
      if (_state == STATE_POMODORO) {
        // 计算当前设置的时长，在 5, 15, 25, 45 之间循环
        uint32_t rem = tomatoTimer.getRemainingSeconds();
        int curMins = rem / 60;
        int nextMins = 25;
        if (curMins >= 25 && curMins < 45) nextMins = 45;
        else if (curMins >= 45) nextMins = 5;
        else if (curMins >= 5 && curMins < 15) nextMins = 15;
        else if (curMins >= 15 && curMins < 25) nextMins = 25;
        
        tomatoTimer.setDuration(nextMins);
      }
      break;
      
    case EVENT_VOICE_START:
      if (_state == STATE_NORMAL_ONLINE) {
        DEBUG_PRINTLN("🎙️ [状态机] 收到事件: 启动语音模拟对话");
        _state = STATE_RECORDING;
      }
      break;
      
    case EVENT_VOICE_RECORD_DONE:
      if (_state == STATE_RECORDING) {
        DEBUG_PRINTLN("🎙️ [状态机] 收到事件: 录音结束，进入云端分析");
        _state = STATE_PROCESSING;
      }
      break;
      
    case EVENT_VOICE_PLAY_DONE:
      if (_state == STATE_PLAYING_AUDIO) {
        DEBUG_PRINTLN("🔊 [状态机] 收到事件: 播音结束，切回在线待机");
        _state = STATE_NORMAL_ONLINE;
      }
      break;
      
    case EVENT_NET_LOST:
      if (_state == STATE_NORMAL_ONLINE) {
        DEBUG_PRINTLN("📶 [状态机] 检测到网络丢失，切换为离线模式");
        LVGL_LOCK();
        ClassPetUI::getInstance().showToast("Offline Mode Enabled", 4000);
        LVGL_UNLOCK();
        _state = STATE_NORMAL_OFFLINE;
      }
      break;
      
    case EVENT_NET_RESTORE:
      if (_state == STATE_NORMAL_OFFLINE) {
        DEBUG_PRINTLN("📶 [状态机] 网络自愈恢复，切回在线待机");
        LVGL_LOCK();
        ClassPetUI::getInstance().showToast("Online Mode Enabled", 4000);
        LVGL_UNLOCK();
        _state = STATE_CONNECTING_WIFI;
      }
      break;
      
    default:
      break;
  }
}

// ==========================================
// 状态周期轮询器 (State Poller)
// ==========================================
void DeviceStateMachine::loopState() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastOfflineCheck = 0;
  
  switch (_state) {
    case STATE_CONNECTING_WIFI: {
      LVGL_LOCK();
      ClassPetUI::getInstance().showProcessingScreen("Connecting to WiFi...");
      LVGL_UNLOCK();
      
      if (!deviceConfig.is_configured || strlen(deviceConfig.wifi_ssid) == 0) {
        DEBUG_PRINTLN("⚠️ 设备未配网，进入 AP 配置模式");
        _state = STATE_SETUP_AP;
        break;
      }
      
      // 执行网络连接尝试
      if (WiFi.status() == WL_CONNECTED) {
        Network::syncNTP();
        String proxy = String(deviceConfig.proxy_ip);
        proxy.trim();
        if (proxy.length() == 0) {
          proxy = "ProxyIP.CMLiussss.net"; // 默认使用优选代理域名
        }
        ApiClient::init(deviceConfig.server_url, deviceConfig.device_secret, proxy);
        
        LVGL_LOCK();
        ClassPetUI::getInstance().showProcessingScreen("WiFi Connected. Syncing...");
        LVGL_UNLOCK();
        
        DeviceStatusResponse res;
        ApiClient::getStatus(res);
        
        if (res.success) {
          updateStudentCache(res);
          loadPetGif(petType, petLevel); // 尝试加载宠物动画
          if (res.status == "unbound") {
            _state = STATE_NORMAL_ONLINE; // 未绑定也视作在线
          } else {
            LVGL_LOCK();
            ClassPetUI::getInstance().showToast("Sync Success!", 3000);
            LVGL_UNLOCK();
            _state = STATE_NORMAL_ONLINE;
          }
        } else {
          // 若获取 API 失败，进入智能诊断屏
          String resolvedIp = lastDiagnostic.host_resolved_ip;
          String suggestion = "自检提示: API 获取失败";
          if (lastDiagnostic.http_code == 403) {
            suggestion = "Vercel 403 封禁! 绑定自定义域名";
          } else if (lastDiagnostic.tls_error_code != 0) {
            suggestion = "TLS 失败: Cloudflare SSL 设为 Full";
          }
          
          LVGL_LOCK();
          ClassPetUI::getInstance().showDiagnosticScreen(
            "已连接",
            WiFi.localIP().toString(),
            lastDiagnostic.server_domain,
            resolvedIp,
            lastDiagnostic.http_code,
            lastDiagnostic.tls_error_code,
            suggestion,
            Network::getMacAddress()
          );
          LVGL_UNLOCK();
          vTaskDelay(pdMS_TO_TICKS(2000)); // 诊断屏留驻 2s 缓冲
        }
      } else {
        static int retryCount = 0;
        if (retryCount == 0) {
          WiFi.begin(deviceConfig.wifi_ssid, deviceConfig.wifi_password);
        }
        retryCount++;
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (retryCount > 15) { // 15秒超时
          DEBUG_PRINTLN("❌ WiFi 连接超时，开启 AP 配网热点");
          retryCount = 0;
          _state = STATE_SETUP_AP;
        }
      }
      break;
    }
    
    case STATE_SETUP_AP: {
      if (!Network::isAPActive()) {
        Network::startAP();
        
        LVGL_LOCK();
        // UI 上提示用户连接热点
        String apSSID = String(AP_SSID_PREFIX) + Network::getMacAddress().substring(12, 14) + Network::getMacAddress().substring(15, 17);
        ClassPetUI::getInstance().showProcessingScreen(String("请手机连接热点:\n" + apSSID + "\n浏览器打开 192.168.4.1").c_str());
        LVGL_UNLOCK();
      }
      
      Network::handleAPClient();
      vTaskDelay(pdMS_TO_TICKS(100)); // 适当让出 CPU 给 WebServer 处理请求
      break;
    }
    
    case STATE_NORMAL_ONLINE: {
      // 每 20 秒定时上报心跳并同步数据
      if (millis() - lastUpdate > 20000) {
        if (!Network::isConnected()) {
          postEvent(EVENT_NET_LOST);
          break;
        }
        
        DeviceStatusResponse res;
        ApiClient::getStatus(res);
        if (res.success && res.status == "ok") {
          updateStudentCache(res);
          loadPetGif(petType, petLevel); // 尝试加载宠物动画
        } else if (res.status == "unbound") {
          updateStudentCache(res);
          loadPetGif(petType, petLevel); // 尝试加载宠物动画
        }
        
        // 补传离线心跳
        ApiClient::sendHeartbeat(100, false);
        
        // 尝试清空离线任务队列
        int offlineCount = Storage::getOfflineQueueSize();
        if (offlineCount > 0) {
          DEBUG_PRINTF("💾 发现 %d 个离线任务，开始补传...\n", offlineCount);
          OfflineTask task;
          while (Storage::peekOfflineTask(task)) {
            if (ApiClient::reportOfflineTask(task.task_name, task.points, task.timestamp)) {
              Storage::popOfflineTask(task); // 上报成功，从队列中移除
              vTaskDelay(pdMS_TO_TICKS(500)); // 避免频繁请求
            } else {
              DEBUG_PRINTLN("❌ 离线任务补传失败，稍后再试。");
              break; // 网络错误或 API 错误，保留剩余队列
            }
          }
        }
        
        lastUpdate = millis();
      }
      
      // 更新在线屏幕显示（加锁保护 LVGL）
      {
        static uint32_t lastClockUpdate = 0;
        if (millis() - lastClockUpdate > 1000) {
          struct tm tinfo;
          if (getLocalTime(&tinfo)) {
            char timeBuf[16];
            char dateBuf[32];
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tinfo.tm_hour, tinfo.tm_min);
            const char* wdays[] = {"日","一","二","三","四","五","六"};
            snprintf(dateBuf, sizeof(dateBuf), "%d月%d日 星期%s", tinfo.tm_mon + 1, tinfo.tm_mday, wdays[tinfo.tm_wday]);
            
            LVGL_LOCK();
            ClassPetUI::getInstance().updateClock(timeBuf, dateBuf);
            LVGL_UNLOCK();
          }
          lastClockUpdate = millis();
        }

        bool isOnline = (WiFi.status() == WL_CONNECTED);
        LVGL_LOCK();
        ClassPetUI::getInstance().showNormalScreen(
          studentName, totalPoints, petLevel, expProgress, expRequired, isMaxLevel, isOnline
        );
        
        int batPct = 100;
        bool isCharging = false;
        getBatteryStatus(batPct, isCharging);
        
        ClassPetUI::getInstance().updateStatusBar(isOnline, WiFi.SSID(), batPct, isCharging);
        LVGL_UNLOCK();
      }
      break;
    }
    
    case STATE_NORMAL_OFFLINE: {
      // 离线状态下每 10 秒尝试重连自愈
      if (millis() - lastOfflineCheck > 10000) {
        if (WiFi.status() == WL_CONNECTED) {
          postEvent(EVENT_NET_RESTORE);
        } else {
          WiFi.begin(deviceConfig.wifi_ssid, deviceConfig.wifi_password);
        }
        lastOfflineCheck = millis();
      }
      
      // 更新时钟
      static uint32_t lastOfflineClock = 0;
      if (millis() - lastOfflineClock > 1000) {
        struct tm tinfo;
        if (getLocalTime(&tinfo)) {
          char timeBuf[16];
          char dateBuf[32];
          snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tinfo.tm_hour, tinfo.tm_min);
          const char* wdays[] = {"日","一","二","三","四","五","六"};
          snprintf(dateBuf, sizeof(dateBuf), "%d月%d日 星期%s", tinfo.tm_mon + 1, tinfo.tm_mday, wdays[tinfo.tm_wday]);
          
          LVGL_LOCK();
          ClassPetUI::getInstance().updateClock(timeBuf, dateBuf);
          LVGL_UNLOCK();
        }
        lastOfflineClock = millis();
      }

      // 渲染离线屏幕（加锁保护 LVGL）
      LVGL_LOCK();
      ClassPetUI::getInstance().showNormalScreen(
        studentName, totalPoints, petLevel, 0, 0, false, false
      );
      
      int batPct = 100;
      bool isCharging = false;
      getBatteryStatus(batPct, isCharging);
      
      ClassPetUI::getInstance().updateStatusBar(false, "", batPct, isCharging);
      LVGL_UNLOCK();
      break;
    }
    
    case STATE_POMODORO: {
      tomatoTimer.update();
      static uint32_t lastSec = 0;
      static bool lastPaused = false;
      uint32_t currSec = tomatoTimer.getRemainingSeconds();
      bool currPaused = tomatoTimer.isPaused();
      
      // 仅在秒数或暂停状态发生变化时刷新屏幕，防止频繁占用互斥锁导致触摸卡顿
      if (currSec != lastSec || currPaused != lastPaused) {
        LVGL_LOCK();
        ClassPetUI::getInstance().showTomatoScreen(
          currSec, tomatoTimer.getPercent(), currPaused
        );
        LVGL_UNLOCK();
        lastSec = currSec;
        lastPaused = currPaused;
      }
      break;
    }
    
    case STATE_RECORDING: {
      audio->startRecording();
      uint32_t recordStart = millis();
      bool is_btn_start = (digitalRead(PHYSICAL_KEY_PIN) == LOW);
      
      // 等待按键释放，或者超时 (10秒)，或者触摸屏收到停止事件
      while (millis() - recordStart < 10000) {
        LVGL_LOCK();
        ClassPetUI::getInstance().showRecordingScreen(audio->getRecordVolumeDb());
        LVGL_UNLOCK();
        
        if (is_btn_start) {
          if (digitalRead(PHYSICAL_KEY_PIN) == HIGH) break;
        } else {
          DeviceEvent ev;
          if (xQueuePeek(_queue, &ev, 0) == pdTRUE) {
            if (ev == EVENT_VOICE_RECORD_DONE) {
               break;
            }
          }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      
      if (!is_btn_start) {
         DeviceEvent ev;
         // 消费掉队列中的停止事件
         if (xQueuePeek(_queue, &ev, 0) == pdTRUE && ev == EVENT_VOICE_RECORD_DONE) {
             xQueueReceive(_queue, &ev, 0); 
         }
      }
      
      postEvent(EVENT_VOICE_RECORD_DONE);
      break;
    }
    
    case STATE_PROCESSING: {
      LVGL_LOCK();
      ClassPetUI::getInstance().showProcessingScreen("识别中...");
      LVGL_UNLOCK();
      
      uint8_t* wavBuf = nullptr;
      size_t wavSize = 0;
      audio->stopRecording(wavBuf, wavSize);
      
      VoiceActionResponse res;
      ApiClient::postVoiceAudio("/record.wav", res);
      
      if (res.success) {
        LVGL_LOCK();
        ClassPetUI::getInstance().showToast(res.reply_text, 4000);
        LVGL_UNLOCK();
        if (res.audio_url.length() > 0) {
          audio->playAudioStream(res.audio_url);
          _state = STATE_PLAYING_AUDIO;
        } else {
          _state = STATE_NORMAL_ONLINE;
        }
      } else {
        LVGL_LOCK();
        ClassPetUI::getInstance().showToast(res.error_msg.c_str(), 3000);
        LVGL_UNLOCK();
        _state = STATE_NORMAL_ONLINE;
      }
      break;
    }
    
    case STATE_PLAYING_AUDIO: {
      LVGL_LOCK();
      ClassPetUI::getInstance().showProcessingScreen("Playing voice response...");
      LVGL_UNLOCK();
      if (!audio->isPlaying()) {
        postEvent(EVENT_VOICE_PLAY_DONE);
      }
      break;
    }
    
    default:
      break;
  }
}

#include <SD_MMC.h>

void DeviceStateMachine::loadPetGif(const String& petType, int petLevel) {
  DEBUG_PRINTF("🎨 [GIF] loadPetGif 被调用: petType='%s', petLevel=%d, 已缓存='%s', buf=%s\n",
    petType.c_str(), petLevel, _current_pet_type.c_str(), _pet_gif_buffer ? "有" : "无");
  
  if (petType.isEmpty()) {
    DEBUG_PRINTLN("🎨 [GIF] petType 为空，跳过加载");
    return;
  }
  
  if (petType == _current_pet_type && petLevel == _current_pet_level && _pet_gif_buffer != nullptr) {
    DEBUG_PRINTLN("🎨 [GIF] 已缓存，跳过");
    return; // 已经加载过了
  }
  
  // 1. 尝试下载（如果本地没有）
  DEBUG_PRINTLN("🎨 [GIF] 开始尝试下载...");
  bool dl_res = ApiClient::downloadAsset(petType, petLevel);
  DEBUG_PRINTF("🎨 [GIF] 下载尝试完成，结果: %d\n", dl_res);
  
  // 2. 从本地读取
  String filename = "/" + petType + "_" + String(petLevel) + "_v4.gif";
  DEBUG_PRINTF("🎨 [GIF] 正在确认本地文件是否存在: %s\n", filename.c_str());
  bool file_exists = SD_MMC.exists(filename);
  DEBUG_PRINTF("🎨 [GIF] 本地文件存在状态: %d\n", file_exists);
  if (!file_exists) {
    DEBUG_PRINTLN("❌ 本地无宠物 GIF，且下载失败");
    return;
  }
  
  DEBUG_PRINTLN("🎨 [GIF] 正在打开文件...");
  fs::File f = SD_MMC.open(filename, "r");
  if (!f) {
    DEBUG_PRINTLN("❌ 无法打开本地 GIF 文件");
    return;
  }
  
  size_t size = f.size();
  DEBUG_PRINTF("🎨 [GIF] 文件大小: %u 字节，正在使用普通内存申请空间...\n", size);
  
  // 直接使用普通内存 (malloc)，避开 ps_malloc 以防板载无 PSRAM 时发生硬件挂起
  uint8_t* new_buf = (uint8_t*)malloc(size);
  DEBUG_PRINTF("🎨 [GIF] 内存申请完成，地址: %p\n", new_buf);
  
  if (new_buf) {
    DEBUG_PRINTLN("🎨 [GIF] 正在读取文件数据到内存...");
    f.read(new_buf, size);
    DEBUG_PRINTLN("🎨 [GIF] 文件数据读取成功，即将发送至 UI 层...");
    
    // 释放旧缓冲
    if (_pet_gif_buffer) {
      free(_pet_gif_buffer);
    }
    
    _pet_gif_buffer = new_buf;
    _pet_gif_size = size;
    _current_pet_type = petType;
    _current_pet_level = petLevel;
    
    // 通知 UI 层
    ClassPetUI::getInstance().setPetGif(_pet_gif_buffer, _pet_gif_size);
    DEBUG_PRINTLN("✅ [GIF] 成功向 UI 发送了 GIF 数据，加载操作完成！");
  } else {
    DEBUG_PRINTLN("❌ 内存不足，无法加载 GIF");
  }
  
  f.close();
}
