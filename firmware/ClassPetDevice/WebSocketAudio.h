/**
 * @file WebSocketAudio.h
 * @brief 轻量 WebSocket 客户端 (RFC6455), 用于设备 <-> 服务器 的流式语音通道
 *        抄小智架构: 一条长连接既上行 PCM 音频, 又下行 TTS PCM 音频与控制 JSON
 *        自带互斥锁, 支持在录音核(Core0)上行 + 状态机核(Core1)下行 的跨核并发访问
 */
#ifndef WEBSOCKET_AUDIO_H
#define WEBSOCKET_AUDIO_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class WebSocketAudio {
public:
  WebSocketAudio();
  // host 不含协议; path 如 "/ws/voice"; useTls=true 走 wss(WiFiClientSecure)
  bool begin(const char* host, uint16_t port, const char* path, bool useTls, const char* deviceId);
  bool connected();
  void close();
  bool sendText(const String& text);
  bool sendBinary(const uint8_t* data, size_t len);
  void poll();   // 轮询接收, 解析帧后触发 onText/onBinary 回调

  void onText(std::function<void(const String&)> cb) { _onText = cb; }
  void onBinary(std::function<void(const uint8_t*, size_t)> cb) { _onBinary = cb; }
  void onClose(std::function<void()> cb) { _onClose = cb; }
  void onConnect(std::function<void()> cb) { _onConnect = cb; }

private:
  WiFiClientSecure _secure;
  WiFiClient _plain;
  Client* _client = nullptr;
  bool _useTls = true;
  SemaphoreHandle_t _mutex = nullptr;
  bool _connected = false;

  uint8_t* _rxBuf = nullptr;
  size_t _rxLen = 0;
  size_t _rxCap = 0;

  uint8_t* _fragBuf = nullptr;
  size_t _fragLen = 0;
  size_t _fragCap = 0;
  bool _fragIsBinary = false;

  std::function<void(const String&)> _onText;
  std::function<void(const uint8_t*, size_t)> _onBinary;
  std::function<void()> _onClose;
  std::function<void()> _onConnect;

  void ensureRxCap(size_t need);
  void ensureFragCap(size_t need);
  void parseFrames();
  void handleFrame(uint8_t opcode, bool fin, const uint8_t* payload, size_t len);
  void deliverFragment();
  void sendFrame(uint8_t opcode, const uint8_t* data, size_t len);
  static void base64_16(const uint8_t* in, char* out); // 16 字节 -> 24 字符 base64
};

#endif // WEBSOCKET_AUDIO_H
