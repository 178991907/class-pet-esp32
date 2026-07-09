#line 1 "/Users/Terry/Downloads/class-pet-main/firmware/ClassPetDevice/Network.cpp"
/**
 * @file Network.cpp
 * @brief 班级宠物园通用固件 - 网络管理实现
 */

#include "Network.h"
#include "Config.h"
#include "Storage.h"
#include "WebPortal.h"
#include <time.h>

// 静态成员变量实例化
WebServerType* Network::server = nullptr;
DNSServer* Network::dnsServer = nullptr;
bool Network::is_ap_active = false;
String Network::cached_mac = "";

void Network::init() {
  DEBUG_PRINTLN("🌐 正在初始化网络模块...");
  
  // 仅在未连接时关闭 AP 模式以节省功耗
  WiFi.mode(WIFI_STA);
  
  // 缓存设备 MAC 地址
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  cached_mac = String(macStr);
  
  DEBUG_PRINTF("🌐 设备唯一硬件 MAC 识别码: %s\n", cached_mac.c_str());
}

bool Network::connectWiFi(const char* ssid, const char* password) {
  if (!ssid || strlen(ssid) == 0) {
    DEBUG_PRINTLN("❌ 错误: WiFi SSID 为空！");
    return false;
  }

  DEBUG_PRINTF("🌐 正在连接目标 WiFi: %s ...\n", ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  
  // 循环等待连接结果
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    DEBUG_PRINT(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("\n✅ WiFi 连接成功！");
    DEBUG_PRINTF("🌐 本地分配 IP: %s\n", WiFi.localIP().toString().c_str());
    
    // 设置干净的 DNS，解决 Vercel 域名污染导致的 connection refused 故障
    IPAddress local_IP = WiFi.localIP();
    IPAddress gateway = WiFi.gatewayIP();
    IPAddress subnet = WiFi.subnetMask();
    IPAddress primaryDNS(223, 5, 5, 5);      // 阿里 DNS
    IPAddress secondaryDNS(119, 29, 29, 29); // 腾讯 DNS
    WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
    DEBUG_PRINTLN("📶 [DNS] 已成功覆盖为国内公共 DNS：223.5.5.5 / 119.29.29.29");
    
    // 连网成功后，立即尝试同步网络时间
    syncNTP();
    return true;
  } else {
    DEBUG_PRINTLN("\n❌ WiFi 连接超时/失败。");
    return false;
  }
}

bool Network::isConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

void Network::startAP() {
  if (is_ap_active) return;

  // 混合当前 MAC 地址后四位以防冲突
  String apSSID = String(AP_SSID_PREFIX) + cached_mac.substring(12, 14) + cached_mac.substring(15, 17);
  DEBUG_PRINTF("🌐 正在启动通用 AP 配网热点: %s ...\n", apSSID.c_str());

  WiFi.mode(WIFI_AP);
  
  // 设置热点 IP
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
  // 开启无密码开放式热点（手机极易连接）
  WiFi.softAP(apSSID.c_str());

  // 启动 Web 服务器和 DNS 服务器
  if (server == nullptr) {
    server = new WebServerType(80);
  }
  if (dnsServer == nullptr) {
    dnsServer = new DNSServer();
  }

  // 捕获所有 DNS 请求并重定向到本地 IP (Captive Portal 核心)
  dnsServer->start(53, "*", local_IP);

  server->on("/", HTTP_GET, handleRoot);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/api/mac", HTTP_GET, handleMacApi);
  server->on("/api/scan", HTTP_GET, handleScanApi);
  server->onNotFound(handleNotFound);

  server->begin();
  is_ap_active = true;
  DEBUG_PRINTLN("✅ AP 配网 Web 服务已在端口 80 启动。访问地址: http://192.168.4.1");
}

void Network::stopAP() {
  if (!is_ap_active) return;
  
  DEBUG_PRINTLN("🌐 正在关闭 AP 配网热点...");
  
  if (server != nullptr) {
    server->stop();
    delete server;
    server = nullptr;
  }
  
  if (dnsServer != nullptr) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
  
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  is_ap_active = false;
  DEBUG_PRINTLN("✅ AP 模式已完全关闭。");
}

void Network::handleAPClient() {
  if (is_ap_active) {
    if (dnsServer != nullptr) {
      dnsServer->processNextRequest();
    }
    if (server != nullptr) {
      server->handleClient();
    }
  }
}

String Network::getMacAddress() {
  return cached_mac;
}

uint32_t Network::getUnixTimestamp() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // 也可以直接读取 time(nullptr)
    now = time(nullptr);
  } else {
    now = mktime(&timeinfo);
  }

  // 1970年之前的视为未同步（或者小于 2026年，这里宽松校验 1700000000 即可）
  if (now < 1700000000) {
    return 0;
  }
  return (uint32_t)now;
}

bool Network::syncNTP() {
  DEBUG_PRINTLN("🕒 正在配置网络时间 (NTP) 同步...");
  
  // 配置时间服务，北京时间 GMT+8
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  // 等待同步（最多等待 5 秒）
  int retries = 10;
  while (getUnixTimestamp() == 0 && retries > 0) {
    delay(500);
    DEBUG_PRINT(".");
    retries--;
  }

  if (getUnixTimestamp() > 0) {
    DEBUG_PRINTF("\n🕒 时间同步成功！当前 Unix 时间戳: %d\n", getUnixTimestamp());
    return true;
  } else {
    DEBUG_PRINTLN("\n⚠️ 警告: NTP 时间同步失败。签名防重放将处于降级或受限状态。");
    return false;
  }
}

// ================= Web 路由事件回调 =================

void Network::handleRoot() {
  server->send_P(200, "text/html", HTTP_PORTAL_HTML);
}

void Network::handleSave() {
  if (!server->hasArg("ssid") || !server->hasArg("password")) {
    server->send(400, "text/plain", "Bad Request: Missing SSID or Password");
    return;
  }

  DeviceConfig config;
  memset(&config, 0, sizeof(DeviceConfig));

  // 从表单读取参数
  strncpy(config.wifi_ssid, server->arg("ssid").c_str(), sizeof(config.wifi_ssid) - 1);
  strncpy(config.wifi_password, server->arg("password").c_str(), sizeof(config.wifi_password) - 1);
  strncpy(config.server_url, server->arg("server").c_str(), sizeof(config.server_url) - 1);
  strncpy(config.device_secret, server->arg("secret").c_str(), sizeof(config.device_secret) - 1);
  if (server->hasArg("proxy")) {
    strncpy(config.proxy_ip, server->arg("proxy").c_str(), sizeof(config.proxy_ip) - 1);
  }
  config.is_configured = true;

  // 写入本地存储
  bool saved = Storage::saveConfig(config);

  if (saved) {
    // 渲染美观的提交成功网页，提示设备即将重启
    String html = "<html><head><meta charset='UTF-8'><style>body{background:#764ba2;color:#fff;font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}div{background:rgba(255,255,255,0.1);padding:30px;border-radius:15px;text-align:center;box-shadow:0 4px 15px rgba(0,0,0,0.2);}</style></head><body><div><h2>⚙️ 配置已保存！</h2><p>设备正在自动关闭热点并尝试连接 WiFi 网络，请检查设备屏显或指示灯。</p></div></body></html>";
    server->send(200, "text/html", html);
    
    delay(1000);
    // 触发软件复位/重启以开始连接
    #if defined(ESP8266) || defined(ESP32)
      ESP.restart();
    #endif
  } else {
    server->send(500, "text/plain", "写入配置失败");
  }
}

void Network::handleMacApi() {
  server->send(200, "application/json", "{\"mac\":\"" + cached_mac + "\"}");
}

void Network::handleScanApi() {
  DEBUG_PRINTLN("📡 Web 网页请求扫描周围 WiFi...");
  int n = WiFi.scanNetworks();
  
  String json = "{\"networks\":[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    #if defined(ESP8266)
      json += "\"secure\":" + String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false");
    #else
      json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    #endif
    json += "}";
  }
  json += "]}";
  
  server->send(200, "application/json", json);
}

void Network::handleNotFound() {
  // 网页配网的核心：Captive Portal 强行重定向所有未知路由到配网首页
  String host = server->hostHeader();
  if (host != "192.168.4.1") {
    DEBUG_PRINTF("🔄 捕获门户劫持重定向: %s -> http://192.168.4.1/\n", server->uri().c_str());
    server->sendHeader("Location", "http://192.168.4.1/", true);
    server->send(302, "text/plain", ""); // 302 重定向
  } else {
    server->send(404, "text/plain", "File Not Found");
  }
}
