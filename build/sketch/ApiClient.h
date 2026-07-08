#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/ApiClient.h"
/**
 * @file ApiClient.h
 * @brief 班级宠物园通用固件 - 通用云端 API 交互客户端
 * @note 带有标准的防重放 HMAC-SHA256 签名校验 Header 模块
 */

#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <Arduino.h>

#if defined(ESP8266)
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
#else
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#endif

// 解析状态返回的实体结构
struct DeviceStatusResponse {
  bool success;
  String status;
  String student_name;
  String class_name;
  int total_points;
  String pet_type;
  int pet_level;
  int pet_exp;
  int exp_progress;
  int exp_required;
  bool is_max_level;
  String error_msg;
};

// 语音/指令核销返回实体结构
struct VoiceActionResponse {
  bool success;
  String action;
  String reply_text;
  String audio_url; // 异步合成的 TTS 地址
  String error_msg;
};

class ApiClient {
public:
  static void init(const String& serverUrl, const String& secret);

  // 1. 获取设备绑定的学生宠物状态
  static DeviceStatusResponse getStatus();

  // 2. 模拟语音文本指令交互（调试后门）
  static VoiceActionResponse postVoiceText(const String& text);

  // 3. 补传一条离线任务申报
  static bool reportOfflineTask(const String& taskName, int points, uint32_t timestamp);

  // 4. 上报电量与充电状态心跳
  static bool sendHeartbeat(int batteryLevel, bool isCharging);

  // 5. 辅助函数：计算带时间戳的 HMAC-SHA256 签名，生成 Hex 字符串
  static String calculateSignature(const String& timestamp, const String& requestBody);

private:
  static String server_url;
  static String device_secret;

  // 通用的 HTTP 请求发送并注入 Headers 签名
  static int sendRequest(HTTPClient& http, const String& method, const String& path, const String& body, String& response);
};

#endif // API_CLIENT_H
