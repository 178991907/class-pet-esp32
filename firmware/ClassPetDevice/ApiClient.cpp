/**
 * @file ApiClient.cpp
 * @brief 班级宠物园通用固件 - 通用云端 API 交互客户端实现
 */

#include "ApiClient.h"
#include "Network.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <mbedtls/md.h>

// 静态成员变量实例化
String ApiClient::server_url = "";
String ApiClient::device_secret = "";

void ApiClient::init(const String& serverUrl, const String& secret) {
  server_url = serverUrl;
  device_secret = secret;
  // 移除尾部斜杠
  if (server_url.endsWith("/")) {
    server_url = server_url.substring(0, server_url.length() - 1);
  }
  
  DEBUG_PRINTF("🚀 API 客户端就绪。指向 URL: %s\n", server_url.c_str());
}

String ApiClient::calculateSignature(const String& timestamp, const String& requestBody) {
  String deviceId = Network::getMacAddress();
  
  // 拼接签名主体：deviceId + timestamp + requestBody
  String rawData = deviceId + timestamp + requestBody;
  
  // 引入 mbedtls 计算 HMAC-SHA256
  unsigned char hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1); // 1 表示启用 HMAC 模式
  
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)device_secret.c_str(), device_secret.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)rawData.c_str(), rawData.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  
  // 将 byte 数组转为 16 进制小写 Hex 字符串
  char hexResult[65];
  for (int i = 0; i < 32; i++) {
    sprintf(&hexResult[i * 2], "%02x", hmacResult[i]);
  }
  hexResult[64] = '\0';
  
  return String(hexResult);
}

int ApiClient::sendRequest(HTTPClient& http, const String& method, const String& path, const String& body, String& response) {
  if (!Network::isConnected()) {
    DEBUG_PRINTLN("❌ API 客户端错误: 网络未连接！");
    return -1;
  }

  // 拼接完整 URL
  String fullUrl = server_url + path;
  
  // 决定使用安全 WiFi 还是普通 WiFi 客户端
  #if defined(ESP8266)
    WiFiClient client;
    http.begin(client, fullUrl);
  #else
    if (fullUrl.startsWith("https")) {
      WiFiClientSecure* secureClient = new WiFiClientSecure();
      secureClient->setInsecure(); // 忽略 SSL 证书签名校验，保障各种云部署域名直接跑通
      http.begin(*secureClient, fullUrl);
    } else {
      WiFiClient* client = new WiFiClient();
      http.begin(*client, fullUrl);
    }
  #endif

  // 生成毫秒精度时间戳
  uint32_t unixSec = Network::getUnixTimestamp();
  if (unixSec == 0) {
    // 强制回退为毫秒数偏移量以支持无网络时间下的基础通信，但在生产中可能会签名失效
    unixSec = millis() / 1000 + 1700000000;
  }
  String timestampStr = String((uint64_t)unixSec * 1000); // 必须是毫秒精度字符串

  // 计算安全通信签名
  String signature = calculateSignature(timestampStr, body);

  // 注入安全认证和通信状态 Header 头部
  http.addHeader("X-Device-ID", Network::getMacAddress());
  http.addHeader("X-Device-Timestamp", timestampStr);
  http.addHeader("X-Device-Signature", signature);
  http.addHeader("Content-Type", "application/json");

  DEBUG_PRINTF("📤 发送 %s -> %s\n", method.c_str(), path.c_str());
  
  int httpCode = -1;
  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(body);
  }

  if (httpCode > 0) {
    response = http.getString();
    DEBUG_PRINTF("📥 响应代码 [%d]\n", httpCode);
  } else {
    DEBUG_PRINTF("❌ 请求失败。错误码 / 原因: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  return httpCode;
}

DeviceStatusResponse ApiClient::getStatus() {
  DeviceStatusResponse res;
  res.success = false;
  
  HTTPClient http;
  String responseStr;
  
  // 查询路径加上 query 参数
  String path = "/api/device/status?device_id=" + Network::getMacAddress();
  int code = sendRequest(http, "GET", path, "", responseStr);
  
  if (code != 200) {
    res.error_msg = "服务器返回错误: " + String(code);
    return res;
  }

  // 使用 ArduinoJson 反序列化
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, responseStr);
  
  if (error) {
    res.error_msg = "JSON 解析失败: " + String(error.c_str());
    return res;
  }

  String status = doc["status"].as<String>();
  res.status = status;
  
  if (status == "unbound") {
    res.success = true;
    res.student_name = "未绑定";
    res.error_msg = doc["message"].as<String>();
    return res;
  }

  if (status == "ok") {
    res.success = true;
    JsonObject student = doc["student"];
    res.student_name = student["name"].as<String>();
    res.class_name = student["class_name"].as<String>();
    res.total_points = student["total_points"].as<int>();
    res.pet_type = student["pet_type"].as<String>();
    res.pet_level = student["pet_level"].as<int>();
    res.pet_exp = student["pet_exp"].as<int>();
    res.exp_progress = student["exp_progress"].as<int>();
    res.exp_required = student["exp_required"].as<int>();
    res.is_max_level = student["is_max_level"].as<bool>();
  } else {
    res.error_msg = "未知状态: " + status;
  }

  return res;
}

VoiceActionResponse ApiClient::postVoiceText(const String& text) {
  VoiceActionResponse res;
  res.success = false;
  
  HTTPClient http;
  String responseStr;
  
  // 封装为 JSON
  StaticJsonDocument<256> reqDoc;
  reqDoc["text"] = text;
  String reqBody;
  serializeJson(reqDoc, reqBody);
  
  int code = sendRequest(http, "POST", "/api/device/voice", reqBody, responseStr);
  
  if (code != 200) {
    res.error_msg = "语音提交失败，错误代码: " + String(code);
    return res;
  }

  // 反序列化结果
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, responseStr);
  
  if (error) {
    res.error_msg = "响应 JSON 解析失败";
    return res;
  }

  res.success = true;
  res.action = doc["action"].as<String>();
  res.reply_text = doc["reply_text"].as<String>();
  
  if (doc.containsKey("audio_url")) {
    res.audio_url = doc["audio_url"].as<String>();
  }
  
  return res;
}

bool ApiClient::reportOfflineTask(const String& taskName, int points, uint32_t timestamp) {
  HTTPClient http;
  String responseStr;
  
  StaticJsonDocument<256> reqDoc;
  reqDoc["text"] = "我完成了" + taskName; // 将离线积累包装为调试文本意图
  
  String reqBody;
  serializeJson(reqDoc, reqBody);
  
  int code = sendRequest(http, "POST", "/api/device/voice", reqBody, responseStr);
  
  if (code == 200) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, responseStr);
    if (doc["action"].as<String>() == "apply_task") {
      DEBUG_PRINTF("💾 离线记录补发成功: %s\n", taskName.c_str());
      return true;
    }
  }
  return false;
}
