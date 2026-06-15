/**
 * @file ClassPetDevice.ino
 * @brief 班级宠物园通用固件主程序入口
 * @note 串联系统配置、WiFi配网、NTP同步、防重放Api请求、本地番茄钟及离线补传状态机
 */

#include "Config.h"
#include "Storage.h"
#include "Network.h"
#include "ApiClient.h"
#include "TomatoTimer.h"
#include "DisplayHAL.h"
#include "AudioHAL.h"

// =========================================================================
// 1. 默认虚拟硬件抽象层实现 (防编译报错，供未接入真实外设时进行串口开发调试)
// =========================================================================

class DummyDisplay : public DisplayHAL {
private:
  DeviceState last_state = STATE_INIT;
public:
  void init() override {
    Serial.println("🖥️ [显示屏] 虚拟显示模块已启动。");
  }
  void clear() override {}
  
  void showInitScreen() override {
    Serial.println("🖥️ [显示屏] === 班级宠物园初始化中... ===");
  }
  
  void showSetupAPScreen(const String& apSSID, const String& ip) override {
    if (last_state != STATE_SETUP_AP) {
      Serial.println("\n-------------------------------------------");
      Serial.printf("🖥️ [显示屏] 请连接配网热点: %s\n", apSSID.c_str());
      Serial.printf("🖥️ [显示屏] 用手机浏览器访问: http://%s 进行WiFi配置\n", ip.c_str());
      Serial.println("-------------------------------------------\n");
      last_state = STATE_SETUP_AP;
    }
  }
  
  void showConnectingScreen(const String& ssid) override {
    Serial.printf("🖥️ [显示屏] 正在连接目标路由器: %s ...\n", ssid.c_str());
  }
  
  void showNormalScreen(const String& studentName, int points, int level, 
                         int expProgress, int expRequired, bool isMaxLevel) override {
    static unsigned long last_print = 0;
    if (millis() - last_print > 5000) { // 在线状态下，每 5 秒打印一次主待机屏
      Serial.println("\n🖥️ [显示屏] === 班级宠物园 (在线) ===");
      Serial.printf("   👤 学生绑定: %s\n", studentName.c_str());
      Serial.printf("   🏆 累计积分: %d 分 | 宠物等级: Lv.%d\n", points, level);
      if (isMaxLevel) {
        Serial.println("   ⭐ 宠物已毕业！");
      } else {
        Serial.printf("   📈 升级进度: [%d / %d]\n", expProgress, expRequired);
      }
      Serial.println("   [提示] 按 2号键 触发语音申报，按 1号键 启动番茄钟。");
      last_print = millis();
    }
  }
  
  void showOfflineNormalScreen(const String& cachedName, int cachedPoints, int cachedLevel) override {
    static unsigned long last_print = 0;
    if (millis() - last_print > 5000) {
      Serial.println("\n🖥️ [显示屏] === 班级宠物园 (⚠️ 离线状态) ===");
      Serial.printf("   👤 缓存学生: %s\n", cachedName.c_str());
      Serial.printf("   🏆 缓存积分: %d 分 | 等级: Lv.%d\n", cachedPoints, cachedLevel);
      Serial.println("   [网络] 网络连接已断开，本地倒计时及缓存任务正常可用。");
      last_print = millis();
    }
  }
  
  void showProcessingScreen(const String& message) override {
    Serial.printf("🖥️ [显示屏] ⌛ %s ...\n", message.c_str());
  }
  
  void showRecordingScreen(int volumeDb) override {
    Serial.print("🖥️ [显示屏] 🎙️ 录音中 (声波振幅): ");
    for (int i = 0; i < volumeDb / 5; i++) Serial.print("|");
    Serial.println();
  }
  
  void showTomatoTimerScreen(uint32_t remainingSec, int percent, bool isPaused) override {
    static uint32_t last_sec = 0;
    if (remainingSec != last_sec) {
      Serial.printf("🖥️ [显示屏] 🍅 番茄钟: %02d:%02d [%d%%] %s\n", 
                    remainingSec / 60, remainingSec % 60, percent, isPaused ? "(暂停)" : "");
      last_sec = remainingSec;
    }
  }
  
  void showTextToast(const String& text, int durationMs) override {
    Serial.printf("🖥️ [显示屏] 💬 弹出通知: %s (%d ms)\n", text.c_str(), durationMs);
  }
};

class DummyAudio : public AudioHAL {
private:
  bool is_playing = false;
  unsigned long play_start = 0;
  unsigned long play_duration = 3000; // 模拟语音播放 3 秒
  uint8_t* fake_wav = nullptr;
public:
  void init() override {
    Serial.println("🔊 [音频] 虚拟音频模块已启动。");
  }
  void startRecording() override {
    Serial.println("🔊 [音频] 模拟麦克风输入已开启，录音开始...");
  }
  bool stopRecording(uint8_t*& wavBuffer, size_t& wavSize) override {
    Serial.println("🔊 [音频] 模拟麦克风录音结束。打包 16KHz 模拟单声道 WAV。");
    wavSize = 100;
    fake_wav = new uint8_t[100];
    memset(fake_wav, 0xAA, 100); // 填充测试垃圾数据
    wavBuffer = fake_wav;
    return true;
  }
  int getRecordVolumeDb() override {
    return random(20, 80); // 模拟噪音分贝跳动
  }
  bool playAudioStream(const String& url) override {
    Serial.printf("🔊 [音频] 🎵 正在开始异步拉取并播放音频直链: %s\n", url.c_str());
    is_playing = true;
    play_start = millis();
    // 模拟检测如果是 TTS (通常带 q= 或者是 tts 关键字) 播放 3 秒，如果是音乐则模拟长播放
    if (url.indexOf("translate_tts") > 0 || url.indexOf("/tts") > 0) {
      play_duration = 4000;
    } else {
      play_duration = 10000; // 模拟音乐播放 10 秒
    }
    return true;
  }
  void stopAudio() override {
    if (is_playing) {
      Serial.println("🔊 [音频] 🎵 停止播放当前音频。");
      is_playing = false;
    }
  }
  bool isPlaying() override {
    return is_playing;
  }
  void setVolume(int volume) override {
    Serial.printf("🔊 [音频] 调整系统音量为: %d%%\n", volume);
  }
  void update() override {
    // 模拟音频自动播完逻辑
    if (is_playing && (millis() - play_start > play_duration)) {
      Serial.println("🔊 [音频] 🎵 模拟音频拉取播放完毕。");
      is_playing = false;
    }
  }
};

// =========================================================================
// 2. 全局成员变量与控制器实例
// =========================================================================

DeviceState systemState = STATE_INIT;
DeviceConfig deviceConfig;

DisplayHAL* display = new DummyDisplay();
AudioHAL* audio = new DummyAudio();
TomatoTimer tomatoTimer;

// 本地缓存变量（用于离线状态显示）
String studentName = "离线用户";
int totalPoints = 0;
int petLevel = 1;
int petExp = 0;
int expProgress = 0;
int expRequired = 40;
bool isMaxLevel = false;

// 物理/模拟引脚定义（以 1号/2号按键模拟控制）
const int PIN_BTN_TOMATO = 0; // 本地番茄钟触发键
const int PIN_BTN_VOICE = 2;  // 语音唤醒/录音键

// 轮询和同步的定时标志
unsigned long lastStatusCheck = 0;
const unsigned long STATUS_CHECK_INTERVAL = 30000; // 正常在线 30 秒拉取一次数据
unsigned long lastOfflineSync = 0;
const unsigned long OFFLINE_SYNC_INTERVAL = 10000;  // 离线模式 10 秒尝试同步一次 WiFi 和队列

// =========================================================================
// 3. 番茄钟计时结束硬件回调与任务申报
// =========================================================================

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
  display->showTextToast("🍅 专注结束！+2积分已加入申报队列", 5000);

  // 模拟播放番茄钟结束轻快提示音
  audio->playAudioStream("http://local-spiffs/ringtone.mp3");
  systemState = STATE_PLAYING_AUDIO;
}

// =========================================================================
// 4. Arduino 初始化与主循环
// =========================================================================

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);
  
  DEBUG_PRINTLN("\n=== 班级宠物园 (ClassPetGarden) 通用固件启动 ===");

  // 设定引脚模式（使用内部上拉）
  pinMode(PIN_BTN_TOMATO, INPUT_PULLUP);
  pinMode(PIN_BTN_VOICE, INPUT_PULLUP);

  // 1. 初始化各驱动
  display->init();
  audio->init();
  Storage::init();
  Network::init();

  display->showInitScreen();
  delay(1500);

  // 2. 绑定番茄钟结束回调
  tomatoTimer.onFinished(onPomodoroFinished);

  // 3. 读取本地配网配置
  if (Storage::loadConfig(deviceConfig)) {
    systemState = STATE_CONNECTING_WIFI;
  } else {
    systemState = STATE_SETUP_AP;
  }
}

void loop() {
  // 驱动音频非阻塞流更新
  audio->update();
  // 驱动本地番茄钟时钟片
  tomatoTimer.update();

  // 检查按键输入触发器
  checkHardwareButtons();

  // 状态机核心轮询
  switch (systemState) {
    case STATE_SETUP_AP: {
      // 开启配网模式
      Network::startAP();
      display->showSetupAPScreen(
        String(AP_SSID_PREFIX) + Network::getMacAddress().substring(12, 14) + Network::getMacAddress().substring(15, 17), 
        AP_DEFAULT_IP
      );
      
      // 不断分发本地配网 HTTP 请求
      Network::handleAPClient();
      break;
    }

    case STATE_CONNECTING_WIFI: {
      display->showProcessingScreen("正在连接目标无线网...");
      bool connected = Network::connectWiFi(deviceConfig.wifi_ssid, deviceConfig.wifi_password);
      
      if (connected) {
        // 关闭热点并切回普通STA模式
        Network::stopAP();
        
        // 初始化 API 客户端并拉取初始状态
        ApiClient.init(deviceConfig.server_url, deviceConfig.device_secret);
        
        display->showProcessingScreen("正在拉取绑定学生档案");
        DeviceStatusResponse res = ApiClient::getStatus();
        
        if (res.success) {
          studentName = res.student_name;
          totalPoints = res.total_points;
          petLevel = res.pet_level;
          petExp = res.pet_exp;
          expProgress = res.exp_progress;
          expRequired = res.exp_required;
          isMaxLevel = res.is_max_level;
        }
        
        systemState = STATE_NORMAL_ONLINE;
      } else {
        // 首次连接不通过时，不强迫卡死，降级进入离线运行，保持本地番茄钟可用
        DEBUG_PRINTLN("⚠️ WiFi 首次连网失败。降级进入离线模式。");
        systemState = STATE_NORMAL_OFFLINE;
      }
      break;
    }

    case STATE_NORMAL_ONLINE: {
      // 渲染在线待机屏
      display->showNormalScreen(studentName, totalPoints, petLevel, expProgress, expRequired, isMaxLevel);
      
      // 在线模式：定期向服务器同步拉取数据以响应教师后台的加扣分
      if (millis() - lastStatusCheck > STATUS_CHECK_INTERVAL) {
        DEBUG_PRINTLN("🔄 定时拉取云端宠物状态...");
        DeviceStatusResponse res = ApiClient::getStatus();
        if (res.success && res.status == "ok") {
          studentName = res.student_name;
          totalPoints = res.total_points;
          petLevel = res.pet_level;
          petExp = res.pet_exp;
          expProgress = res.exp_progress;
          expRequired = res.exp_required;
          isMaxLevel = res.is_max_level;
        } else if (!Network::isConnected()) {
          // WiFi 掉线触发降级
          systemState = STATE_NORMAL_OFFLINE;
        }
        lastStatusCheck = millis();
      }

      // 在线模式下，持续检测是否有离线暂存的任务需补传
      syncOfflineQueue();
      break;
    }

    case STATE_NORMAL_OFFLINE: {
      // 渲染离线待机屏
      display->showOfflineNormalScreen(studentName, totalPoints, petLevel);

      // 离线模式：定时自检网络是否恢复，以及进行重连自愈
      if (millis() - lastOfflineSync > OFFLINE_SYNC_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
          DEBUG_PRINTLN("📶 检测到 WiFi 已经自动恢复！切回在线状态...");
          // 重新同步 NTP 并拉取接口
          Network::syncNTP();
          ApiClient::getStatus(); 
          systemState = STATE_NORMAL_ONLINE;
        } else {
          // 尝试进行静默连接重试以图自愈
          DEBUG_PRINTLN("🔄 离线模式静默重连 WiFi 中...");
          WiFi.begin(deviceConfig.wifi_ssid, deviceConfig.wifi_password);
        }
        lastOfflineSync = millis();
      }
      break;
    }

    case STATE_PLAYING_AUDIO: {
      // 当音频正在播放时，维持在播放状态，由 update() 自动流式运转并渲染正在播音提示
      display->showProcessingScreen("正在播放语音/音乐");
      if (!audio->isPlaying()) {
        DEBUG_PRINTLN("🔊 播音结束，切回在线待机");
        // 重新拉取一次最新的状态数据以反应积分累加
        DeviceStatusResponse res = ApiClient::getStatus();
        if (res.success) {
          studentName = res.student_name;
          totalPoints = res.total_points;
          petLevel = res.pet_level;
          petExp = res.pet_exp;
          expProgress = res.exp_progress;
          expRequired = res.exp_required;
          isMaxLevel = res.is_max_level;
        }
        systemState = STATE_NORMAL_ONLINE;
      }
      break;
    }

    case STATE_POMODORO: {
      // 渲染番茄钟倒计时屏
      display->showTomatoTimerScreen(tomatoTimer.getRemainingSeconds(), tomatoTimer.getPercent(), tomatoTimer.isPaused());
      break;
    }

    default:
      break;
  }
}

// =========================================================================
// 5. 离线缓存队列补传机制 (核心容灾恢复算法)
// =========================================================================

void syncOfflineQueue() {
  int queueSize = Storage::getOfflineQueueSize();
  if (queueSize <= 0) return;

  // 限制上报频率，避免集中爆发导致服务器拥堵
  static unsigned long lastUploadTime = 0;
  if (millis() - lastUploadTime < 3000) return;

  DEBUG_PRINTF("📶 在线检测: 检测到本地有 %d 条离线积压申报，开始自动补传...\n", queueSize);
  
  OfflineTask task;
  if (Storage::peekOfflineTask(task)) {
    // 调用 Api 客户端进行补发
    bool success = ApiClient::reportOfflineTask(String(task.task_name), task.points, task.timestamp);
    if (success) {
      // 发送成功，则将其移出队列
      Storage::popOfflineTask(task);
      DEBUG_PRINTLN("✅ 成功同步一条离线积分！已从队列移出。");
    } else {
      DEBUG_PRINTLN("❌ 补传通信失败，稍后尝试。");
    }
  }
  lastUploadTime = millis();
}

// =========================================================================
// 6. 物理/模拟按键触发中断与输入事件处理
// =========================================================================

void checkHardwareButtons() {
  // A. 1号按键：番茄钟启动/停止控制键 (引脚 0 拉低)
  static bool lastBtnTomato = HIGH;
  bool currentBtnTomato = digitalRead(PIN_BTN_TOMATO);
  if (lastBtnTomato == HIGH && currentBtnTomato == LOW) { // 下降沿触发
    delay(50); // 消抖
    if (digitalRead(PIN_BTN_TOMATO) == LOW) {
      if (tomatoTimer.isActive()) {
        tomatoTimer.stop();
        systemState = Network::isConnected() ? STATE_NORMAL_ONLINE : STATE_NORMAL_OFFLINE;
        display->showTextToast("🍅 番茄钟已终止并返回待机");
      } else {
        tomatoTimer.start(DEFAULT_POMODORO_MINS);
        systemState = STATE_POMODORO;
      }
    }
  }
  lastBtnTomato = currentBtnTomato;

  // B. 2号按键：语音/指令交互模拟 (引脚 2 拉低，仅在在线且未占空时可用)
  static bool lastBtnVoice = HIGH;
  bool currentBtnVoice = digitalRead(PIN_BTN_VOICE);
  if (lastBtnVoice == HIGH && currentBtnVoice == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_VOICE) == LOW) {
      if (systemState == STATE_NORMAL_ONLINE) {
        triggerSimulatedVoiceDeclaration();
      } else if (systemState == STATE_NORMAL_OFFLINE) {
        display->showTextToast("⚠️ 离线状态，语音交互不可用");
      }
    }
  }
  lastBtnVoice = currentBtnVoice;
}

// 串口端/按键模拟发送调试指令，进行意图核销自测
void triggerSimulatedVoiceDeclaration() {
  DEBUG_PRINTLN("🎙️ 按键触发: 启动语音录制...");
  systemState = STATE_RECORDING;
  
  // 1. 模拟录音 2 秒并绘制跳动波纹
  for (int i = 0; i < 4; i++) {
    display->showRecordingScreen(random(30, 90));
    delay(500);
  }
  
  uint8_t* wavBuf = nullptr;
  size_t wavSize = 0;
  audio->stopRecording(wavBuf, wavSize);
  
  // 2. 切为云端核算解析状态
  display->showProcessingScreen("正在提交语音意图到云端");
  
  // 3. 随机选择测试词进行模拟 ASR 意图核销（免去硬件上 Whisper ASR 的依赖）
  const char* testTexts[] = {
    "我完成了认真打扫卫生", 
    "帮我申请平时测验满分",
    "帮我播放一首经典童话",
    "查询我现在多少分"
  };
  String selectedText = testTexts[random(0, 4)];
  DEBUG_PRINTF("🎙️ ASR 模拟识别结果: \"%s\"\n", selectedText.c_str());
  
  // 4. 调用 API 接口
  VoiceActionResponse res = ApiClient::postVoiceText(selectedText);
  
  if (res.success) {
    DEBUG_PRINTF("✅ 云端意图匹配动作: %s\n", res.action.c_str());
    display->showTextToast(res.reply_text, 4000);
    
    if (res.audio_url.length() > 0) {
      // 5. 播报返回的 TTS
      audio->playAudioStream(res.audio_url);
      systemState = STATE_PLAYING_AUDIO;
    } else {
      systemState = STATE_NORMAL_ONLINE;
    }
  } else {
    DEBUG_PRINTF("❌ 云端处理失败: %s\n", res.error_msg.c_str());
    display->showTextToast("❌ 连接服务器或解析失败", 3000);
    systemState = STATE_NORMAL_ONLINE;
  }
}
