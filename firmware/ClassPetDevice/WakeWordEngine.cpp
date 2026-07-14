/**
 * @file WakeWordEngine.cpp
 * @brief 离线唤醒词引擎实现 (esp-sr 1.0, 单麦 AFE)
 * @see WakeWordEngine.h
 */

#include "WakeWordEngine.h"
#include "AudioHAL.h"
#include "Board.h"
#include <math.h>

// 麦克风 I2S 参数 (与 ESP32Audio::startRecording 一致: 16k / 16bit / 单声道)
#define WAKE_I2S_PORT      I2S_NUM_0
#define WAKE_SAMPLE_RATE   16000
#define WAKE_MCLK_MULTIPLE 256

// 全局音频句柄 (在 ClassPetDevice.ino 中 new ESP32Audio())
extern AudioHAL* audio;
// 串口诊断模式: 占用音频硬件时, 唤醒词引擎必须让出 I2S
extern volatile bool serial_diag_active;

// ---------------------------------------------------------------------------
// 构造 / 启动
// ---------------------------------------------------------------------------
bool WakeWordEngine::begin() {
  // 1. 取得单麦 AFE 接口
  _afeHandle = &esp_afe_sr_1mic;

  // 2. 显式构造 afe_config_t (不依赖 AFE_CONFIG_DEFAULT 宏, 因为它需要 menuconfig
  //    定义的 WAKENET_MODEL / WAKENET_COEFF; Arduino 无 menuconfig, 故手动赋值)
  afe_config_t cfg;
  cfg.aec_init        = false;  // 单麦无参考声道, 关闭 AEC
  cfg.se_init         = true;   // 噪声抑制 (NS) 开
  cfg.vad_init        = true;   // VAD 开
  cfg.wakenet_init    = true;   // 唤醒词检测开
  cfg.vad_mode        = 3;
  cfg.wakenet_model   = &esp_sr_wakenet5_quantized;   // wakenet5 量化引擎
  cfg.wakenet_coeff   = &get_coeff_nihaoxiaozhi_wn5;  // 你好小智 模型系数
  //            (备选: &get_coeff_hilexin_wn5  -> 嗨乐鑫)
  cfg.wakenet_mode    = DET_MODE_90;     // 单麦用 DET_MODE_90 (2CH 用于双麦)
  cfg.afe_mode        = SR_MODE_LOW_COST;
  cfg.afe_perferred_core    = 0;
  cfg.afe_perferred_priority = 5;
  cfg.afe_ringbuf_size = 50;
  cfg.alloc_from_psram = AFE_PSRAM_MEDIA_COST;  // 模型/中间数据放 PSRAM (本板 8MB OPI)
  cfg.agc_mode        = 2;

  _afeData = _afeHandle->create_from_config(&cfg);
  if (!_afeData) {
    Serial.println("❌ [唤醒词] AFE 创建失败! 请检查 esp-sr 链接与模型 .a 是否已加入 ldflags");
    return false;
  }

  // 部分 esp-sr 1.0 版本 create_from_config 后 wakenet 默认 DISABLED,
  // 必须显式 enable, 否则 AFE 只跑 VAD 永不检测唤醒词 (fetch 永远 NOISE/不返回 WWE)
  if (_afeHandle->enable_wakenet) {
    int en = _afeHandle->enable_wakenet(_afeData);
    Serial.printf("🔊 [唤醒词] enable_wakenet 返回 %d (1=成功启用)\n", en);
  }

  _feedChunksize  = _afeHandle->get_feed_chunksize(_afeData);
  _fetchChunksize = _afeHandle->get_fetch_chunksize(_afeData);
  _channelNum     = _afeHandle->get_channel_num(_afeData);

  _feedBuf  = (int16_t*)heap_caps_malloc(_feedChunksize * _channelNum * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  _fetchBuf = (int16_t*)heap_caps_malloc(_fetchChunksize * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!_feedBuf || !_fetchBuf) {
    Serial.println("❌ [唤醒词] 缓冲区分配失败 (PSRAM 不足?)");
    return false;
  }

  Serial.printf("✅ [唤醒词] AFE 创建成功: feed=%d fetch=%d ch=%d, 模型=你好小智\n",
    _feedChunksize, _fetchChunksize, _channelNum);

  _running = true;
  _paused  = true;   // 默认暂停, 由状态机 resume() 启动常驻监听
  BaseType_t ret = xTaskCreatePinnedToCore(taskEntry, "WakeWordTask", 8192, this, 4, &_taskHandle, 0);
  if (ret != pdPASS) {
    Serial.println("❌ [唤醒词] FreeRTOS 任务创建失败!");
    _running = false;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 任务入口 / 主循环
// ---------------------------------------------------------------------------
void WakeWordEngine::taskEntry(void* arg) {
  ((WakeWordEngine*)arg)->run();
  vTaskDelete(NULL);
}

void WakeWordEngine::run() {
  uint32_t lastDiag = 0;
  int64_t sumSqAccum = 0;
  int32_t peakMax = 0;
  int     diagCount = 0;
  int     speechFrames = 0;   // 过去 2 秒内 fetch==SPEECH 的帧数
  int     wweFrames = 0;      // 过去 2 秒内 fetch==WWE 的帧数
  int     feedRet = 0;
  afe_fetch_mode_t lastFetch = AFE_FETCH_NOISE;

  while (_running) {
    // 暂停 / 串口诊断占用音频硬件时, 不监听, 确保让出 I2S
    if (_paused || serial_diag_active) {
      if (_i2sInstalled) uninstallI2s();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (!_i2sInstalled) installI2s();
    if (!_i2sInstalled) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

    // 读取一帧麦克风 PCM 喂给 AFE
    size_t wantBytes = (size_t)_feedChunksize * _channelNum * sizeof(int16_t);
    size_t bytesRead = 0;
    esp_err_t r = i2s_read(WAKE_I2S_PORT, _feedBuf, wantBytes, &bytesRead, pdMS_TO_TICKS(100));
    if (r != ESP_OK || bytesRead == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    // 数字域预放大: 本板模拟麦克风硬件灵敏度天花板约 pk~2500(-22dBFS),
    // ES8311 寄存器层面(REG14/REG17/偏置)均无法突破; 在喂 AFE 前对样本饱和放大,
    // 使 VAD 能将其识别为语音而非噪声(否则 wakenet 永不触发).
    const int16_t WAKE_MIC_GAIN = 10;
    {
      int nGain = (int)(bytesRead / 2);
      for (int i = 0; i < nGain; i++) {
        int32_t v = (int32_t)_feedBuf[i] * WAKE_MIC_GAIN;
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        _feedBuf[i] = (int16_t)v;
      }
    }

    // 诊断: 统计本帧麦克风能量 (用于判断信号是否真正进入 AFE)
    int n = (int)(bytesRead / 2);
    int32_t framePeak = 0;
    for (int i = 0; i < n; i++) {
      int16_t s = _feedBuf[i];
      int16_t a = (int16_t)(s < 0 ? -s : s);
      if (a > framePeak) framePeak = a;
      sumSqAccum += (int64_t)s * s;
    }
    if (framePeak > peakMax) peakMax = framePeak;
    diagCount++;

    // 喂入 AFE 并取回处理结果 (fetch 内部含 VAD/NS/唤醒词检测状态机)
    feedRet = _afeHandle->feed(_afeData, _feedBuf);
    lastFetch = _afeHandle->fetch(_afeData, _fetchBuf);
    if (lastFetch == AFE_FETCH_SPEECH) speechFrames++;
    else if (lastFetch == AFE_FETCH_WWE_DETECTED) { speechFrames++; wweFrames++; }

    // 限频诊断 (每 2 秒打印一次, 不刷屏)
    uint32_t now = millis();
    if (now - lastDiag >= 2000) {
      lastDiag = now;
      int rms = 0;
      if (diagCount > 0 && n > 0) {
        double meanSq = (double)sumSqAccum / (double)(diagCount * n);
        rms = (int)sqrt(meanSq);
      }
      const char* fs = (lastFetch == AFE_FETCH_WWE_DETECTED) ? "WWE"
                    : (lastFetch == AFE_FETCH_SPEECH)         ? "SPEECH"
                    : (lastFetch == AFE_FETCH_NOISE)          ? "NOISE"
                    : "VERIFIED";
      int speechPct = diagCount ? (speechFrames * 100 / diagCount) : 0;
      Serial.printf("[唤醒词诊断] rms≈%d peakMax=%d feed=%d speech%%=%d wweHits=%d fetch=%s\n",
        rms, peakMax, feedRet, speechPct, wweFrames, fs);
      sumSqAccum = 0; peakMax = 0; diagCount = 0; speechFrames = 0; wweFrames = 0;
    }

    if (lastFetch == AFE_FETCH_WWE_DETECTED) {
      if (now > _suppressUntil) {
        _suppressUntil = now + 3000;   // 3 秒防抖, 避免单次唤醒反复触发
        Serial.println("🔔 [唤醒词] 检测到 '你好小智' ! 触发语音流程");
        if (_onWake) _onWake();
      }
    }
  }

  if (_i2sInstalled) uninstallI2s();
}

// ---------------------------------------------------------------------------
// I2S 安装 / 卸载
// ---------------------------------------------------------------------------
void WakeWordEngine::installI2s() {
  // I2S_NUM_0 由 AudioHAL 统一管理 (单一所有者). 空闲监听与录音共用 RX(麦克风)基线,
  // ensureRxMic 幂等: 若已是 RX 模式直接返回 true, 不会重复安装导致 INVALID_STATE 死循环.
  // 若当前是 TX(播放)模式, 则自动卸载并切回 RX.
  if (audio && audio->ensureRxMic()) {
    _i2sInstalled = true;
    Serial.println("✅ [唤醒词] I2S RX(麦克风)基线就绪, 开始常驻监听");
  } else {
    Serial.println("❌ [唤醒词] I2S RX 就绪失败, 无法监听");
  }
}

void WakeWordEngine::uninstallI2s() {
  // 唤醒词引擎不卸载 I2S: 单一所有者是 AudioHAL, RX 基线是空闲/录音共用资源.
  // 暂停监听时仅停止读取, 保留 RX 驱动供录音/播放流程使用; 恢复时由 ensureRxMic 重新校验.
  _i2sInstalled = false;
}

// ---------------------------------------------------------------------------
// 暂停 / 恢复 (由状态机驱动)
// ---------------------------------------------------------------------------
void WakeWordEngine::resume() {
  _paused = false;
  _suppressUntil = millis() + 500;   // 刚恢复时先静默 0.5s, 丢弃残留帧
}

void WakeWordEngine::pause() {
  _paused = true;
  // 停止读取麦克风 (让 I2S 驱动保留供录音/播放使用); 标记 _i2sInstalled=false,
  // 使恢复监听时由 ensureRxMic 重新校验当前 I2S 模式 (可能已被切到 TX 播放).
  if (_i2sInstalled) uninstallI2s();
}
