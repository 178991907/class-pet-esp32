/**
 * @file WakeWordEngine.cpp
 * @brief 离线唤醒词引擎实现 (esp-sr 1.0, 单麦 AFE)
 * @see WakeWordEngine.h
 */

#include "WakeWordEngine.h"
#include "AudioHAL.h"
#include "Board.h"

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

    // 喂入 AFE 并取回处理结果 (fetch 内部含 VAD/NS/唤醒词检测状态机)
    _afeHandle->feed(_afeData, _feedBuf);
    afe_fetch_mode_t st = _afeHandle->fetch(_afeData, _fetchBuf);
    if (st == AFE_FETCH_WWE_DETECTED) {
      uint32_t now = millis();
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
  // 切换 ES8311 到麦克风输入并静音功放 (交给 ESP32Audio, 不碰 I2S)
  if (audio) audio->enterWakeMicMode();

  i2s_config_t i2s_cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = WAKE_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_cfg = {
    .mck_io_num = I2S_MCLK_PIN,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num  = I2S_LRC_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_DIN_PIN    // ESP32 麦克风脚 GPIO6
  };
  esp_err_t ierr = i2s_driver_install(WAKE_I2S_PORT, &i2s_cfg, 0, NULL);
  esp_err_t perr = i2s_set_pin(WAKE_I2S_PORT, &pin_cfg);
  if (ierr == ESP_OK && perr == ESP_OK) {
    _i2sInstalled = true;
    Serial.println("✅ [唤醒词] I2S RX 已安装 (麦克风常驻监听)");
  } else {
    Serial.printf("❌ [唤醒词] I2S RX 安装失败: install=%s pin=%s\n",
      esp_err_to_name(ierr), esp_err_to_name(perr));
  }
}

void WakeWordEngine::uninstallI2s() {
  i2s_driver_uninstall(WAKE_I2S_PORT);
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
  // 立即卸载 I2S, 让状态机下一步的录音/播放能立即安装自己的 I2S 驱动
  if (_i2sInstalled) uninstallI2s();
}
