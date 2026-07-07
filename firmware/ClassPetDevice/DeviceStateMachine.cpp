/**
 * @file DeviceStateMachine.cpp
 * @brief 班级宠物园设备状态机系统实现
 */

#include "DeviceStateMachine.h"
#include "ClassPetUI.h"
#include "LcdDisplay.h"
#include "ApiClient.h"
#include "Network.h"
#include "Config.h"
#include "Storage.h"
#include "AudioHAL.h"
#include "TomatoTimer.h"
#include <WiFi.h>

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
  
  // 2. 启动状态机子线程，堆栈 16KB，运行在 Core 1
  xTaskCreatePinnedToCore(taskEntry, "AppStateMachineTask", 16384, this, 5, nullptr, 1);
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
        // 静默重连
        WiFi.begin(deviceConfig.wifi_ssid, deviceConfig.wifi_password);
        vTaskDelay(pdMS_TO_TICKS(3000));
      }
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
        
        lastUpdate = millis();
      }
      
      // 更新在线屏幕显示（加锁保护 LVGL）
      {
        bool isOnline = (WiFi.status() == WL_CONNECTED);
        LVGL_LOCK();
        ClassPetUI::getInstance().showNormalScreen(
          studentName, totalPoints, petLevel, expProgress, expRequired, isMaxLevel, isOnline
        );
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
      
      // 渲染离线屏幕（加锁保护 LVGL）
      LVGL_LOCK();
      ClassPetUI::getInstance().showNormalScreen(
        studentName, totalPoints, petLevel, 0, 0, false, false
      );
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
      // 模拟录音 2 秒，绘制波形
      for (int i = 0; i < 4; i++) {
        LVGL_LOCK();
        ClassPetUI::getInstance().showRecordingScreen(random(30, 95));
        LVGL_UNLOCK();
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      postEvent(EVENT_VOICE_RECORD_DONE);
      break;
    }
    
    case STATE_PROCESSING: {
      LVGL_LOCK();
      ClassPetUI::getInstance().showProcessingScreen("Uploading points...");
      LVGL_UNLOCK();
      
      // 随机分配一个模拟语音指令进行测试
      const char* testTexts[] = {
        "我完成了认真打扫卫生", 
        "帮我申请平时测验满分",
        "我完成了早读认真专注",
        "查询我现在多少分"
      };
      String selectedText = testTexts[random(0, 4)];
      DEBUG_PRINTF("🎙️ ASR 模拟语音: \"%s\"\n", selectedText.c_str());
      
      VoiceActionResponse res;
      ApiClient::postVoiceText(selectedText, res);
      
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
        ClassPetUI::getInstance().showToast("Voice action failed", 3000);
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

#include <FFat.h>

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
  ApiClient::downloadAsset(petType, petLevel);
  
  // 2. 从本地读取
  String filename = "/" + petType + "_" + String(petLevel) + "_v4.gif";
  if (!FFat.exists(filename)) {
    DEBUG_PRINTLN("❌ 本地无宠物 GIF，且下载失败");
    return;
  }
  
  fs::File f = FFat.open(filename, "r");
  if (!f) return;
  
  size_t size = f.size();
  uint8_t* new_buf = (uint8_t*)ps_malloc(size); // ESP32-S3 有 PSRAM
  if (!new_buf) {
    new_buf = (uint8_t*)malloc(size);
  }
  
  if (new_buf) {
    f.read(new_buf, size);
    
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
    DEBUG_PRINTF("✅ 宠物动画 %s 加载完成并发送至 UI，大小: %u 字节\n", petType.c_str(), size);
  } else {
    DEBUG_PRINTLN("❌ 内存不足，无法加载 GIF");
  }
  
  f.close();
}
