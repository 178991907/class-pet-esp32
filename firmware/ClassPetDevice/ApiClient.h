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

struct DiagnosticInfo {
  bool wifi_connected;
  String local_ip;
  String server_domain;
  String host_resolved_ip;
  int http_code;
  int tls_error_code;
  String tls_error_msg;
};
extern DiagnosticInfo lastDiagnostic;

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
  static void init(const String& serverUrl, const String& secret, const String& proxyIp = "", const String& webUrl = "");

  static String getProxyIp() { return proxy_ip; }
  static String getServerUrl() { return server_url; }
  static String getWebUrl() { return web_url; }
  // 返回 server_url + api_prefix + path (path 可带或不带前导 /)，适配 Cloudflare Worker 根路径 /api
  static String buildApiUrl(const String& path);

  // 1. 获取设备绑定的学生宠物状态
  static void getStatus(DeviceStatusResponse& res);

  // 2. 模拟语音文本指令交互（调试后门）
  static void postVoiceText(const String& text, VoiceActionResponse& res);

  // 2.5 真实上传录音文件 (WAV格式) 并接收指令反馈
  static void postVoiceAudio(const String& filePath, VoiceActionResponse& res);


  // 3. 补传一条离线任务申报
  static bool reportOfflineTask(const String& taskName, int points, uint32_t timestamp);

  // 4. 下载资产 (如宠物 GIF) 到本地 LittleFS  // 素材同步功能
  static bool downloadAsset(const String& petType, int petLevel);

  // 4b. 下载中文字库 cjk16.bin 到 TF 卡 (首次启动自动从服务器拉取，无需手动拷贝)
  static bool downloadCjkFont();

  // 5. 上报电量与充电状态心跳
  static bool sendHeartbeat(int batteryLevel, bool isCharging);

  // 5. 辅助函数：计算带时间戳的 HMAC-SHA256 签名，生成 Hex 字符串
  static String calculateSignature(const String& timestamp, const String& requestBody);

private:
  static String server_url;
  static String api_prefix;
  static String device_secret;
  static String proxy_ip;
  static String web_url;      // 静态资源站点（如宠物 GIF），通常与 API 域名不同

  // 通用的 HTTP 请求发送并注入 Headers 签名
  static int sendRequest(const String& method, const String& path, const String& body, String& response);
};

#endif // API_CLIENT_H
