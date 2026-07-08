/**
 * @file Network.h
 * @brief 班级宠物园通用固件 - 网络管理与配网服务
 * @note 处理 WiFi 连接、基于 Captive Portal 的 AP 热点服务器与 NTP 网络时间同步
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <DNSServer.h>
  using WebServerType = ESP8266WebServer;
#else
  #include <WiFi.h>
  #include <WebServer.h>
  #include <DNSServer.h>
  using WebServerType = WebServer;
#endif

class Network {
public:
  static void init();
  
  // 连接 WiFi
  static bool connectWiFi(const char* ssid, const char* password);
  
  // 是否已连接 WiFi 且获取了有效 IP
  static bool isConnected();

  // 启动 AP 配网热点和本地 Web 服务
  static void startAP();
  
  // 停止 AP 并清理
  static void stopAP();

  // 刷新并处理 AP 状态下的客户端请求（需要在 loop 中不断轮询）
  static void handleAPClient();

  static bool isAPActive() { return is_ap_active; }

  // 获取设备 MAC 地址
  static String getMacAddress();

  // 获取当前的 Unix 时间戳（秒），若未同步则返回 0
  static uint32_t getUnixTimestamp();

  // 触发一次 NTP 时间强制同步
  static bool syncNTP();

private:
  static WebServerType* server;
  static DNSServer* dnsServer;
  static bool is_ap_active;
  static String cached_mac;

  // Web 路由回调函数
  static void handleRoot();
  static void handleSave();
  static void handleMacApi();
  static void handleScanApi();
  static void handleNotFound();
};

#endif // NETWORK_H
