/**
 * @file WebSocketAudio.cpp
 * @brief 轻量 WebSocket 客户端实现
 */
#include "WebSocketAudio.h"

WebSocketAudio::WebSocketAudio() {
  _mutex = xSemaphoreCreateMutex();
}

// ================= 握手与连接 =================

bool WebSocketAudio::begin(const char* host, uint16_t port, const char* path, bool useTls, const char* deviceId) {
  if (_mutex == nullptr) _mutex = xSemaphoreCreateMutex();

  _useTls = useTls;
  if (useTls) {
    _secure.setInsecure();   // 开发阶段不校验证书; 生产可改为 setCACert
    _client = &_secure;
  } else {
    _client = &_plain;
  }

  Serial.printf("🔌 [WS] 连接 %s://%s:%u%s (设备 %s)\n",
    useTls ? "wss" : "ws", host, port, path, deviceId);

  if (!_client->connect(host, port)) {
    Serial.println("❌ [WS] TCP 连接失败");
    _connected = false;
    return false;
  }

  // 生成 Sec-WebSocket-Key (16 随机字节 base64)
  uint8_t key[16];
  for (int i = 0; i < 16; i++) key[i] = (uint8_t)random(0, 256);
  char keyB64[32];
  base64_16(key, keyB64);

  String req = String("GET ") + path + " HTTP/1.1\r\n"
    + "Host: " + host + "\r\n"
    + "Upgrade: websocket\r\n"
    + "Connection: Upgrade\r\n"
    + "Sec-WebSocket-Key: " + keyB64 + "\r\n"
    + "Sec-WebSocket-Version: 13\r\n"
    + "X-Device-ID: " + deviceId + "\r\n"
    + "\r\n";

  _client->print(req);

  // 读取握手响应直到 \r\n\r\n
  unsigned long t0 = millis();
  String header;
  while (millis() - t0 < 5000) {
    if (_client->available()) {
      char c = _client->read();
      header += c;
      if (header.endsWith("\r\n\r\n")) break;
    }
    delay(2);
  }

  if (!header.startsWith("HTTP/1.1 101")) {
    Serial.printf("❌ [WS] 握手失败: %.40s\n", header.c_str());
    _client->stop();
    _connected = false;
    return false;
  }

  _connected = true;
  _rxLen = 0;
  _fragLen = 0;
  Serial.println("✅ [WS] 握手成功, 语音流已建立");

  if (_onConnect) _onConnect();

  // 发送 hello
  String hello = String("{\"type\":\"hello\",\"device_id\":\"") + deviceId
    + "\",\"audio_params\":{\"format\":\"pcm\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":20}}";
  sendText(hello);
  return true;
}

bool WebSocketAudio::connected() {
  return _connected && _client && _client->connected();
}

void WebSocketAudio::close() {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
  if (_connected && _client) {
    uint8_t closeFrame[2] = { 0x88, 0x00 }; // FIN + close, len 0
    _client->write(closeFrame, 2);
    _client->stop();
  }
  _connected = false;
  if (_mutex) xSemaphoreGive(_mutex);
}

// ================= 发送 =================

bool WebSocketAudio::sendText(const String& text) {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
  bool ok = false;
  if (_connected && _client) {
    sendFrame(0x1, (const uint8_t*)text.c_str(), text.length());
    ok = true;
  }
  if (_mutex) xSemaphoreGive(_mutex);
  return ok;
}

bool WebSocketAudio::sendBinary(const uint8_t* data, size_t len) {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
  bool ok = false;
  if (_connected && _client) {
    sendFrame(0x2, data, len);
    ok = true;
  }
  if (_mutex) xSemaphoreGive(_mutex);
  return ok;
}

void WebSocketAudio::sendFrame(uint8_t opcode, const uint8_t* data, size_t len) {
  uint8_t header[10];
  size_t hlen = 0;
  header[0] = 0x80 | (opcode & 0x0F); // FIN=1
  if (len < 126) {
    header[1] = (uint8_t)(0x80 | len); // MASK=1
    hlen = 2;
  } else if (len < 65536) {
    header[1] = 0x80 | 126;
    header[2] = (len >> 8) & 0xFF;
    header[3] = len & 0xFF;
    hlen = 4;
  } else {
    header[1] = 0x80 | 127;
    for (int i = 0; i < 8; i++) header[2 + i] = (uint8_t)((len >> (8 * (7 - i))) & 0xFF);
    hlen = 10;
  }

  uint8_t mask[4];
  for (int i = 0; i < 4; i++) mask[i] = (uint8_t)random(0, 256);

  _client->write(header, hlen);
  _client->write(mask, 4);
  for (size_t i = 0; i < len; i++) {
    _client->write((uint8_t)(data[i] ^ mask[i & 3]));
  }
}

// ================= 接收与解析 =================

void WebSocketAudio::poll() {
  if (!_connected || !_client) return;

  if (_client->available()) {
    int avail = _client->available();
    ensureRxCap(_rxLen + avail + 16);
    int got = _client->readBytes(_rxBuf + _rxLen, avail);
    if (got > 0) _rxLen += (size_t)got;
    parseFrames();
  }

  // 检测对端是否已关闭
  if (!_client->connected()) {
    if (_connected) {
      _connected = false;
      if (_onClose) _onClose();
    }
  }
}

void WebSocketAudio::ensureRxCap(size_t need) {
  if (_rxCap >= need) return;
  size_t ncap = _rxCap ? _rxCap : 1024;
  while (ncap < need) ncap *= 2;
  uint8_t* nb = (uint8_t*)realloc(_rxBuf, ncap);
  if (nb) { _rxBuf = nb; _rxCap = ncap; }
}

void WebSocketAudio::ensureFragCap(size_t need) {
  if (_fragCap >= need) return;
  size_t ncap = _fragCap ? _fragCap : 2048;
  while (ncap < need) ncap *= 2;
  uint8_t* nb = (uint8_t*)realloc(_fragBuf, ncap);
  if (nb) { _fragBuf = nb; _fragCap = ncap; }
}

void WebSocketAudio::parseFrames() {
  size_t off = 0;
  while (_rxLen - off >= 2) {
    uint8_t b0 = _rxBuf[off];
    uint8_t b1 = _rxBuf[off + 1];
    bool fin = (b0 & 0x80) != 0;
    uint8_t opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t p = off + 2;

    if (len == 126) {
      if (_rxLen - off < 4) break;
      len = ((uint64_t)_rxBuf[p] << 8) | _rxBuf[p + 1];
      p += 2;
    } else if (len == 127) {
      if (_rxLen - off < 10) break;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | _rxBuf[p + i];
      p += 8;
    }

    uint8_t mask[4] = {0};
    if (masked) {
      if (_rxLen - off < (p - off) + 4) break;
      memcpy(mask, _rxBuf + p, 4);
      p += 4;
    }

    if (_rxLen - p < len) break; // 帧未接收完整

    const uint8_t* payload = _rxBuf + p;
    if (masked) {
      // 服务器通常不加掩码, 但防御性处理
      uint8_t* m = (uint8_t*)payload;
      for (uint64_t i = 0; i < len; i++) m[i] ^= mask[i & 3];
    }

    if (opcode == 0x8) { // close
      _connected = false;
      if (_onClose) _onClose();
      off = p + len;
      break;
    } else if (opcode == 0x9) { // ping -> pong
      sendFrame(0xA, payload, (size_t)len);
    } else if (opcode == 0xA) {
      // pong, 忽略
    } else if (opcode == 0x0 || opcode == 0x1 || opcode == 0x2) {
      handleFrame(opcode, fin, payload, (size_t)len);
    }

    off = p + (size_t)len;
  }

  if (off > 0) {
    memmove(_rxBuf, _rxBuf + off, _rxLen - off);
    _rxLen -= off;
  }
}

void WebSocketAudio::handleFrame(uint8_t opcode, bool fin, const uint8_t* payload, size_t len) {
  if (opcode == 0x1 || opcode == 0x2) {
    _fragLen = 0;
    _fragIsBinary = (opcode == 0x2);
    ensureFragCap(len);
    memcpy(_fragBuf, payload, len);
    _fragLen = len;
    if (fin) deliverFragment();
  } else if (opcode == 0x0) { // 续帧
    ensureFragCap(_fragLen + len);
    memcpy(_fragBuf + _fragLen, payload, len);
    _fragLen += len;
    if (fin) deliverFragment();
  }
}

void WebSocketAudio::deliverFragment() {
  if (!_fragLen) return;
  if (_fragIsBinary) {
    if (_onBinary) _onBinary(_fragBuf, _fragLen);
  } else {
    if (_onText) _onText(String((const char*)_fragBuf, _fragLen));
  }
  _fragLen = 0;
}

// ================= base64 (16 字节 -> 24 字符) =================

void WebSocketAudio::base64_16(const uint8_t* in, char* out) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; i < 16; i += 3) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | (uint32_t)in[i + 2];
    out[0] = tbl[(v >> 18) & 0x3F];
    out[1] = tbl[(v >> 12) & 0x3F];
    out[2] = tbl[(v >> 6) & 0x3F];
    out[3] = tbl[v & 0x3F];
    out += 4;
  }
  out[0] = '='; out[1] = '='; out[2] = '\0'; // 16 字节 -> 24 字符(含2填充)
}
