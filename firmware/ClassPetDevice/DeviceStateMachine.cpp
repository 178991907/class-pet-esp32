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
#include "WebSocketAudio.h"
#include "WakeWordEngine.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// 电池 ADC 引脚 (适用于 Caturda / Sunton 2.8寸 S3 屏)
#define PIN_BATTERY_ADC 9

// P1: 轻量 VAD (静音自动断句) 参数 —— 检测到语音后, 若静音持续超过阈值则自动停止录音
#define VAD_SILENCE_DB      -35    // 低于此 dB 视为静音 (依麦克风增益调整)
#define VAD_SILENCE_MS      600    // 静音持续超过此时长则断句 (从 1000 调短, 说完即停)
#define VAD_MIN_SPEECH_MS   300    // 至少说过这么久才允许 VAD 断句 (从 800 调短, 儿童短句也能触发)

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

// 串口诊断模式标志 (定义在 ClassPetDevice.ino 中)
// 当为 true 时，状态机不应操作 I2S 驱动，避免与串口诊断命令并发冲突
extern volatile bool serial_diag_active;

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

  // ===== 配置流式语音 WebSocket 回调 (Route B) =====
  audio->setPcmUploadCallback([this](const uint8_t* d, size_t l) { this->uploadVoiceFrame(d, l); });
  voiceWs.onBinary([this](const uint8_t* d, size_t l) { audio->feedPcm(d, l); });
  voiceWs.onText([this](const String& t) { this->handleWsText(t); });
  voiceWs.onClose([this]() { Serial.println("🔌 [WS] 语音流已断开"); _wsClosed = true; _voiceWsOk = false; });

  // ===== 离线唤醒词引擎 (esp-sr) =====
  // 唤醒时回调 -> 触发与语音按钮相同的 EVENT_VOICE_START, 进入录音流程。
  WakeWordEngine::getInstance().setWakeCallback([this]() { this->postEvent(EVENT_VOICE_START); });
  if (WakeWordEngine::getInstance().begin()) {
    Serial.println("✅ [唤醒词] 引擎已启动, 空闲态将常驻监听 '" +
      String(WakeWordEngine::getInstance().wakeWord()) + "'");
  } else {
    Serial.println("⚠️ [唤醒词] 引擎启动失败 (esp-sr 链接/模型缺失), 仅语音按钮可用");
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
    case EVENT_POMODORO_SETTINGS:
      if (_state == STATE_NORMAL_ONLINE || _state == STATE_NORMAL_OFFLINE) {
        DEBUG_PRINTLN("🍅 [状态机] 收到事件: 进入番茄钟设置");
        _state = STATE_POMODORO_SETTINGS;
        LVGL_LOCK();
        ClassPetUI::getInstance().showTomatoSettings();
        LVGL_UNLOCK();
      }
      break;
      
    case EVENT_POMODORO_START:
      if (_state == STATE_NORMAL_ONLINE || _state == STATE_NORMAL_OFFLINE || _state == STATE_POMODORO_SETTINGS) {
        DEBUG_PRINTLN("🍅 [状态机] 收到事件: 启动番茄工作钟");
        uint32_t mins = _voiceTomatoMinutes > 0 ? (uint32_t)_voiceTomatoMinutes : ClassPetUI::getInstance().getSelectedTomatoTime();
        _voiceTomatoMinutes = 0;
        tomatoTimer.start(mins);
        _state = STATE_POMODORO;
      }
      break;
      
    case EVENT_POMODORO_STOP:
      if (_state == STATE_POMODORO || _state == STATE_POMODORO_SETTINGS) {
        DEBUG_PRINTLN("🍅 [状态机] 收到事件: 退出番茄钟");
        tomatoTimer.stop();
        _state = STATE_NORMAL_ONLINE; // 强行退出回在线态 (如果有网的话，依靠 loop 自行纠正离线)
        _last_sync_time = millis(); // 退出时重置同步时间，避免立刻被网络同步阻塞导致 UI 卡顿
        LVGL_LOCK();
        ClassPetUI::getInstance().forceSwitchToNormal();
        LVGL_UNLOCK();
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
      if (_state == STATE_NORMAL_ONLINE || _state == STATE_NORMAL_OFFLINE) {
        // 3 秒录音冷却期, 防止上一次语音流程结束后立即重复进入
        if (millis() - _voiceDoneTime < 3000) {
          DEBUG_PRINTLN("🎙️ [状态机] 录音冷却期内, 忽略重复语音启动请求");
          break;
        }
        // 如果串口诊断模式正在使用 I2S，拒绝进入录音状态
        if (serial_diag_active) {
          DEBUG_PRINTLN("⚠️ [状态机] 串口诊断模式正在使用音频硬件，跳过语音录制请求");
          break;
        }
        DEBUG_PRINTLN("🎙️ [状态机] 收到事件: 启动语音模拟对话");
        _state = STATE_RECORDING;
      } else {
        DEBUG_PRINTLN("🎙️ [状态机] 非待机状态, 忽略语音启动请求");
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
  static DeviceState last_state = (DeviceState)255;
  if (_state != last_state) {
    _state_start_time = millis();
    last_state = _state;
  }

  // ===== 离线唤醒词引擎: 空闲态常驻监听, 离开空闲态立即让出 I2S =====
  // 空闲态 = 在线/离线待机; 进入录音/识别/播放时暂停, 把 I2S_NUM_0 交还录音/播放流程。
  {
    bool idle = (_state == STATE_NORMAL_ONLINE || _state == STATE_NORMAL_OFFLINE);
    static bool wkeRunning = false;
    bool canListen = idle && !serial_diag_active && !audio->isPlaying() && !audio->isRecording();
    if (canListen && !wkeRunning) {
      WakeWordEngine::getInstance().resume();
      wkeRunning = true;
    } else if (!idle && wkeRunning) {
      WakeWordEngine::getInstance().pause();
      wkeRunning = false;
    } else if (serial_diag_active && wkeRunning) {
      // 串口诊断占用音频硬件, 唤醒词引擎必须让出 I2S
      WakeWordEngine::getInstance().pause();
      wkeRunning = false;
    }
  }

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
        // 未显式配置代理 IP/域名时直连 (Cloudflare Worker 可直接访问, 无需优选代理)
        // 如需 SNI 优选代理, 可在配网页面填写代理 IP/域名
        ApiClient::init(deviceConfig.server_url, deviceConfig.device_secret, proxy);

        // 中文字库已编译进固件 (cjk16_bin.c), 在 ClassPetUI::init() 时已通过
        // lv_binfont_create_from_buffer 从内置缓冲区加载, 无需再从服务器下载。
        // 因此这里不再调用 ApiClient::downloadCjkFont(), 避免无谓的 450KB 下载
        // 以及之前出现的下载污染导致中文显示为方块的问题。
        // (保留 ApiClient::downloadCjkFont() 函数以便将来如需恢复 TF 卡方案。)

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
      if (millis() - _last_sync_time > 20000) {
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
        
        _last_sync_time = millis();
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

        static uint32_t lastUiUpdate = 0;
        if (millis() - lastUiUpdate > 1000) {
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
          
          lastUiUpdate = millis();
        }
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
      static uint32_t lastOfflineUiUpdate = 0;
      if (millis() - lastOfflineUiUpdate > 1000) {
        LVGL_LOCK();
        ClassPetUI::getInstance().showNormalScreen(
          studentName, totalPoints, petLevel, 0, 0, false, false
        );
        
        int batPct = 100;
        bool isCharging = false;
        getBatteryStatus(batPct, isCharging);
        
        ClassPetUI::getInstance().updateStatusBar(false, "", batPct, isCharging);
        LVGL_UNLOCK();
        lastOfflineUiUpdate = millis();
      }
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
      // ---- 进入录音: 建立语音 WS 长连接并开录 (仅首次进入执行一次) ----
      if (!_recEntryDone) {
        _recEntryDone = true;
        _routeB = false;
        _voiceWsOk = false;
        _wsClosed = false;
        _tts_started = false;
        _tts_done = false;
        _pcmMode = false;
        _aborted = false;
        _ttsDoneTime = 0;
        _sttText = "";
        _lastReplyText = "";
        _ttsFallbackUrl = "";

        // 清空队列中录音前积压的语音事件, 避免它们之后被误触发
        drainVoiceEvents();

        audio->startRecording();

        // 解析 server_url 并直连语音 WS (/ws/voice)
        String host; uint16_t port; bool tls;
        parseServerUrl(deviceConfig.server_url, host, port, tls);
        if (WiFi.status() == WL_CONNECTED && host.length() > 0) {
          _voiceWsOk = voiceWs.begin(host.c_str(), port, "/ws/voice", tls,
                                     Network::getMacAddress().c_str());
        }
        if (_voiceWsOk) {
          _routeB = true;
          audio->enableStreamUp(true);  // 录音即分帧上行
          Serial.println("🚀 [WS] 语音流通道已建立, 开始上行 PCM");
        } else {
          Serial.println("⚠️ [WS] 连接失败, 本次退回原 HTTP 流程");
        }

        // P0: 进入语音覆盖层 (宠物常驻 + 实时字幕), 不再切到转圈页
        LVGL_LOCK();
        ClassPetUI::getInstance().enterVoiceOverlay("我在听... 说完松手");
        ClassPetUI::getInstance().setVoiceOverlayCountdown(8);
        LVGL_UNLOCK();
      }

      uint32_t recordStart = millis();
      bool is_btn_start = (digitalRead(PHYSICAL_KEY_PIN) == LOW);

      // 根据触发方式给出不同的操作提示, 并在标题更新倒计时
      LVGL_LOCK();
      ClassPetUI::getInstance().setVoiceOverlayTitle(is_btn_start ? "我在听… 松开按键结束" : "我在听… 点击结束录音");
      ClassPetUI::getInstance().setVoiceOverlayCountdown(is_btn_start ? 15 : 8);
      LVGL_UNLOCK();

      // 录音上限: 实体键最长 15s, 触屏按钮最长 8s (给儿童用, 句子短)
      uint32_t timeout = is_btn_start ? 15000 : 8000;
      uint32_t lastSpeechMs = recordStart;
      uint32_t speechAccumMs = 0;
      bool vadStop = false;
      uint32_t lastCountdownUpdate = 0;

      while (millis() - recordStart < timeout) {
        // 状态机任务自己驱动 I2S 读取，避免 loop() 被阻塞时录音数据丢失
        if (audio) audio->update();

        int db = audio->getRecordVolumeDb();
        int lvl = constrain(map(db, VAD_SILENCE_DB, -5, 0, 100), 0, 100);
        LVGL_LOCK();
        ClassPetUI::getInstance().setVoiceOverlayLevel(lvl);
        LVGL_UNLOCK();

        // 每 500ms 刷新一次倒计时, 让孩子/老师知道还能说多久
        if (millis() - lastCountdownUpdate >= 500) {
          int remaining = (int)((timeout - (millis() - recordStart) + 999) / 1000);
          LVGL_LOCK();
          ClassPetUI::getInstance().setVoiceOverlayCountdown(remaining);
          LVGL_UNLOCK();
          lastCountdownUpdate = millis();
        }

        // P1: 轻量 VAD —— 有语音则累计, 静音持续超阈值且已说满最短时长 -> 自动断句
        if (lvl > 10) {
          speechAccumMs += 50;
          lastSpeechMs = millis();
        }
        if (speechAccumMs > VAD_MIN_SPEECH_MS && (millis() - lastSpeechMs) > VAD_SILENCE_MS) {
          vadStop = true;
          break;
        }

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
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      if (vadStop) Serial.println("🎙️ [VAD] 检测到静音, 自动停止录音");

      if (!is_btn_start) {
         DeviceEvent ev;
         // 消费掉队列中的停止事件
         if (xQueuePeek(_queue, &ev, 0) == pdTRUE && ev == EVENT_VOICE_RECORD_DONE) {
             xQueueReceive(_queue, &ev, 0);
         }
      }

      // ---- 录音结束: 关闭上行, 发 end (仅 Route B), 释放 RX 驱动 ----
      audio->enableStreamUp(false);
      uint8_t* wavBuf = nullptr; size_t wavSize = 0;
      audio->stopRecording(wavBuf, wavSize);
      // 清空录音期间可能积压的语音事件, 防止退出后残留事件立即触发重复倒计时
      drainVoiceEvents();
      _voiceDoneTime = millis(); // 标记本次语音流程结束, 用于后续冷却去抖
      if (_routeB && _voiceWsOk) {
        voiceWs.sendText("{\"type\":\"end\"}");
        Serial.println("📤 [WS] 已发送 end, 等待服务端 ASR/LLM/TTS");
      }
      _recEntryDone = false;
      postEvent(EVENT_VOICE_RECORD_DONE);
      break;
    }

    case STATE_PROCESSING: {
      LVGL_LOCK();
      ClassPetUI::getInstance().setVoiceOverlayTitle("识别中...");
      ClassPetUI::getInstance().setVoiceOverlayProcessing(true);
      LVGL_UNLOCK();

      // ---- Route A (原 HTTP 流程): 整段 WAV 上传 ----
      if (!_routeB) {
        VoiceActionResponse res;
        ApiClient::postVoiceAudio("/record.wav", res);
        if (res.success) {
          LVGL_LOCK();
          ClassPetUI::getInstance().exitVoiceOverlay();
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
          ClassPetUI::getInstance().exitVoiceOverlay();
          ClassPetUI::getInstance().showToast(res.error_msg.c_str(), 3000);
          LVGL_UNLOCK();
          _state = STATE_NORMAL_ONLINE;
        }
        break;
      }

      // ---- Route B (流式): 等待 WS 下发的 stt/llm/tts ----
      // tts start 已在 handleWsText 中触发 startPcmPlayback 并置 _tts_started
      if (_tts_started) {
        if (_ttsFallbackUrl.length() > 0) {
          audio->playAudioStream(_ttsFallbackUrl); // MP3 降级拉流
          _pcmMode = false;
        } else {
          _pcmMode = true; // PCM 直推已在播
        }
        _state = STATE_PLAYING_AUDIO;
        break;
      }

      // 服务端已关闭连接且未下发任何音频 (仅 llm 文本, 如未绑定/限频/识别失败)
      if (!voiceWs.connected()) {
        if (_lastReplyText.length() > 0) {
          LVGL_LOCK();
          ClassPetUI::getInstance().exitVoiceOverlay();
          ClassPetUI::getInstance().showToast(_lastReplyText, 4000);
          LVGL_UNLOCK();
        } else {
          LVGL_LOCK();
          ClassPetUI::getInstance().exitVoiceOverlay();
          LVGL_UNLOCK();
        }
        voiceWs.close();
        _state = STATE_NORMAL_ONLINE;
        break;
      }

      // 超时保护: 20 秒未收到任何音频则退出
      if (millis() - _state_start_time > 20000) {
        Serial.println("⚠️ [WS] 处理超时, 强制退出");
        LVGL_LOCK();
        ClassPetUI::getInstance().exitVoiceOverlay();
        LVGL_UNLOCK();
        voiceWs.close();
        _state = STATE_NORMAL_ONLINE;
      }
      break;
    }

    case STATE_PLAYING_AUDIO: {
      LVGL_LOCK();
      ClassPetUI::getInstance().setVoiceOverlayTitle("宠物正在说...");
      LVGL_UNLOCK();

      if (_pcmMode) {
        // ---- PCM 流式播放: feedPcm 由 WS poll() 在 loop 中驱动 ----
        // 用户打断: 播放 0.5s 后按实体键即发 abort 并停止
        if (!_aborted && digitalRead(PHYSICAL_KEY_PIN) == LOW &&
            (millis() - _state_start_time > 500)) {
          _aborted = true;
          voiceWs.sendText("{\"type\":\"abort\"}");
          audio->stopPcmPlayback();
          voiceWs.close();
          Serial.println("🛑 [WS] 用户打断, 已 abort");
          LVGL_LOCK();
          ClassPetUI::getInstance().exitVoiceOverlay();
          LVGL_UNLOCK();
          _state = STATE_NORMAL_ONLINE;
          break;
        }

        // TTS 下发结束且 (缓冲放完 或 服务端已关) -> 收尾
        if (_tts_done) {
          // feedPcm 受 I2S DMA 背压, 播放约为实时, 留 800ms 余量等 DMA 排空
          if (millis() - _ttsDoneTime > 800 || !voiceWs.connected()) {
            audio->stopPcmPlayback();
            voiceWs.close();
            if (_lastReplyText.length() > 0) {
              LVGL_LOCK();
              ClassPetUI::getInstance().exitVoiceOverlay();
              ClassPetUI::getInstance().showToast(_lastReplyText, 3000);
              LVGL_UNLOCK();
            } else {
              LVGL_LOCK();
              ClassPetUI::getInstance().exitVoiceOverlay();
              LVGL_UNLOCK();
            }
            Serial.println("✅ [WS] PCM 播放完成, 回到待机");
            _state = STATE_NORMAL_ONLINE;
            break;
          }
        } else if (millis() - _state_start_time > 30000) {
          // 收到 tts start 但迟迟没有 stop (异常), 超时强退
          Serial.println("⚠️ [WS] PCM 播放超时, 强制退出");
          audio->stopPcmPlayback();
          voiceWs.close();
          LVGL_LOCK();
          ClassPetUI::getInstance().exitVoiceOverlay();
          LVGL_UNLOCK();
          _state = STATE_NORMAL_ONLINE;
        }
      } else {
        // ---- MP3 降级拉流播放 ----
        // playAudioStream 为异步连接, 刚调用时 isPlaying() 可能为 false, 故加 1.5s 时间门槛
        if (!audio->isPlaying() && millis() - _state_start_time > 1500) {
          if (_lastReplyText.length() > 0) {
            LVGL_LOCK();
            ClassPetUI::getInstance().exitVoiceOverlay();
            ClassPetUI::getInstance().showToast(_lastReplyText, 3000);
            LVGL_UNLOCK();
          } else {
            LVGL_LOCK();
            ClassPetUI::getInstance().exitVoiceOverlay();
            LVGL_UNLOCK();
          }
          voiceWs.close();
          _state = STATE_NORMAL_ONLINE;
        } else if (millis() - _state_start_time > 30000) {
          audio->stopAudio();
          voiceWs.close();
          LVGL_LOCK();
          ClassPetUI::getInstance().exitVoiceOverlay();
          LVGL_UNLOCK();
          _state = STATE_NORMAL_ONLINE;
        }
      }
      break;
    }
    
    default:
      break;
  }
}

#include <SD_MMC.h>

// ==========================================
// 流式语音辅助方法 (Route B)
// ==========================================

// 解析配置中的 server_url -> host / port / tls
// 兼容 https://host, https://host:port, http://host:port, 以及带路径后缀
void DeviceStateMachine::drainVoiceEvents() {
  if (!_queue) return;
  DeviceEvent ev;
  // 只丢弃排在最前面的语音类事件; 遇到其他事件立即停止, 不破坏其顺序
  while (xQueuePeek(_queue, &ev, 0) == pdTRUE) {
    if (ev == EVENT_VOICE_START || ev == EVENT_VOICE_RECORD_DONE) {
      xQueueReceive(_queue, &ev, 0);
    } else {
      break;
    }
  }
}

void DeviceStateMachine::parseServerUrl(const String& url, String& host, uint16_t& port, bool& useTls) {
  host = "";
  port = 0;
  useTls = false;

  String s = url;
  int idx = s.indexOf("://");
  if (idx >= 0) {
    String scheme = s.substring(0, idx);
    useTls = (scheme == "https" || scheme == "wss");
    s = s.substring(idx + 3);
  }
  // 去掉路径部分, 只保留 host[:port]
  int slash = s.indexOf('/');
  if (slash >= 0) s = s.substring(0, slash);
  // 拆分 host 与 port
  int colon = s.indexOf(':');
  if (colon >= 0) {
    host = s.substring(0, colon);
    port = (uint16_t)s.substring(colon + 1).toInt();
  } else {
    host = s;
  }
  if (port == 0) port = useTls ? 443 : 80;
  Serial.printf("🔌 [WS] 解析服务器地址: %s -> host=%s port=%u tls=%d\n",
    url.c_str(), host.c_str(), port, useTls);
}

// 把录音读到的 I2S PCM 块上行到 WS (由 AudioHAL 的 upload 回调触发)
void DeviceStateMachine::uploadVoiceFrame(const uint8_t* d, size_t l) {
  if (_voiceWsOk && voiceWs.connected()) {
    voiceWs.sendBinary(d, l);
  }
}

// 解析服务端下发的文本控制消息 (stt/llm/tts)
void DeviceStateMachine::handleWsText(const String& text) {
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, text);
  if (err) {
    Serial.println("⚠️ [WS] 控制消息 JSON 解析失败");
    return;
  }
  const char* type = doc["type"] | "";

  if (strcmp(type, "hello") == 0) {
    Serial.println("🤝 [WS] 收到 hello, 语音会话已建立");
  } else if (strcmp(type, "stt") == 0) {
    const char* t = doc["text"] | "";
    _sttText = t;
    Serial.printf("📝 [WS] ASR 识别: %s\n", t);
    // P0: 实时字幕 —— 你说的内容直接浮现在宠物屏上, 不再用一闪而过的 Toast
    LVGL_LOCK();
    ClassPetUI::getInstance().setVoiceOverlayCaption(true, t);
    LVGL_UNLOCK();
  } else if (strcmp(type, "llm") == 0) {
    const char* t = doc["text"] | "";
    _lastReplyText = t;
    Serial.printf("💬 [WS] 大模型回复: %s\n", t);
    // P1: 流式字幕 —— llm 消息为累计全文, 每收到一段就刷新"宠物说"气泡
    LVGL_LOCK();
    ClassPetUI::getInstance().setVoiceOverlayCaption(false, t);
    LVGL_UNLOCK();
    // 语音控制: 解析 action 并路由到对应设备动作(无需大模型参与)
    const char* action = doc["action"] | "";
    if (strcmp(action, "start_pomodoro") == 0) {
      if (doc.containsKey("params") && doc["params"].containsKey("minutes")) {
        _voiceTomatoMinutes = doc["params"]["minutes"] | 0;
      }
      Serial.printf("🍅 [WS] 语音启动番茄钟 (%d 分钟)\n", _voiceTomatoMinutes);
      postEvent(EVENT_POMODORO_START);
    } else if (strcmp(action, "change_pet") == 0) {
      // 后端已更新 pet_type, 由 20s 在线同步自动重载宠物动画
      Serial.println("🐾 [WS] 语音更换宠物, 等待下次同步刷新");
    }
  } else if (strcmp(type, "tts") == 0) {
    if (doc.containsKey("state")) {
      const char* st = doc["state"] | "";
      if (strcmp(st, "start") == 0) {
        _tts_started = true;
        _pcmMode = true;
        // 立即安装 16k TX 驱动, 准备接收下行的 TTS PCM
        // (先于状态机切入 PLAYING_AUDIO, 避免首帧 PCM 被丢弃)
        audio->startPcmPlayback();
        Serial.println("🔊 [WS] TTS start, 开始 PCM 直推播放");
      } else if (strcmp(st, "stop") == 0) {
        _tts_done = true;
        _ttsDoneTime = millis();
        Serial.println("🔊 [WS] TTS stop, PCM 下发结束");
      }
    } else if (doc.containsKey("url")) {
      // 服务端 PCM 解码失败, 降级为 MP3 直链, 由 Audio 库拉流播放
      _ttsFallbackUrl = String((const char*)doc["url"]);
      _tts_started = true;
      _tts_done = true;
      Serial.printf("🔊 [WS] TTS 降级为 MP3: %s\n", _ttsFallbackUrl.c_str());
    }
  }
}

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
