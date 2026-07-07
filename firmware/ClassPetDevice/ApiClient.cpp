/**
 * @file ApiClient.cpp
 * @brief 班级宠物园通用固件 - 通用云端 API 交互客户端实现
 */

#include "ApiClient.h"
#include "Network.h"
#include "Config.h"
#if defined(ESP8266)
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
#else
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#endif
#include <ArduinoJson.h>
#include <mbedtls/md.h>

// 静态成员变量实例化
String ApiClient::server_url = "";
String ApiClient::api_prefix = "/pet-garden/api";
String ApiClient::device_secret = "";
String ApiClient::proxy_ip = "";
DiagnosticInfo lastDiagnostic = {false, "", "", "", 0, 0, ""};

#if !defined(ESP8266)
class ProxyWiFiClientSecure : public WiFiClientSecure {
public:
  int connect(const char *host, uint16_t port) override {
    String proxy = ApiClient::getProxyIp();
    if (proxy.length() > 0) {
      IPAddress ip;
      if (ip.fromString(proxy)) {
        DEBUG_PRINTF("🛡️ [Proxy] 拦截请求 %s -> 连接优选 IP: %s (SNI: %s)\n", host, proxy.c_str(), host);
        return WiFiClientSecure::connect(ip, port, host, NULL, NULL, NULL);
      } else {
        if (WiFi.hostByName(proxy.c_str(), ip)) {
          DEBUG_PRINTF("🛡️ [Proxy] 拦截请求 %s -> 解析优选域名 %s 为 IP: %s (SNI: %s)\n", host, proxy.c_str(), ip.toString().c_str(), host);
          return WiFiClientSecure::connect(ip, port, host, NULL, NULL, NULL);
        } else {
          DEBUG_PRINTF("🛡️ [Proxy] 警告: 优选域名 %s 解析失败，降级直连\n", proxy.c_str());
        }
      }
    }
    return WiFiClientSecure::connect(host, port);
  }

  int connect(const char *host, uint16_t port, int32_t timeout) override {
    String proxy = ApiClient::getProxyIp();
    if (proxy.length() > 0) {
      IPAddress ip;
      if (ip.fromString(proxy)) {
        DEBUG_PRINTF("🛡️ [Proxy] 拦截请求 %s -> 连接优选 IP: %s (SNI: %s)\n", host, proxy.c_str(), host);
        return WiFiClientSecure::connect(ip, port, host, NULL, NULL, NULL);
      } else {
        if (WiFi.hostByName(proxy.c_str(), ip)) {
          DEBUG_PRINTF("🛡️ [Proxy] 拦截请求 %s -> 解析优选域名 %s 为 IP: %s (SNI: %s)\n", host, proxy.c_str(), ip.toString().c_str(), host);
          return WiFiClientSecure::connect(ip, port, host, NULL, NULL, NULL);
        } else {
          DEBUG_PRINTF("🛡️ [Proxy] 警告: 优选域名 %s 解析失败，降级直连\n", proxy.c_str());
        }
      }
    }
    return WiFiClientSecure::connect(host, port, timeout);
  }
};

  static ProxyWiFiClientSecure secureClient;
  static WiFiClient plainClient;
#endif
static HTTPClient httpClient;
static bool clientsInitialized = false;

void ApiClient::init(const String& serverUrl, const String& secret, const String& proxyIp) {
  server_url = serverUrl;
  device_secret = secret;
  proxy_ip = proxyIp;
  // 移除尾部斜杠
  if (server_url.endsWith("/")) {
    server_url = server_url.substring(0, server_url.length() - 1);
  }

  // 兼容三种填写方式：
  // 1. https://xxx.vercel.app -> 自动走 /pet-garden/api
  // 2. https://xxx.vercel.app/pet-garden -> 走 /api
  // 3. https://xxx.vercel.app/api -> 直接走 /api
  api_prefix = "/pet-garden/api";
  if (server_url.endsWith("/pet-garden")) {
    api_prefix = "/api";
  } else if (server_url.endsWith("/api")) {
    server_url = server_url.substring(0, server_url.length() - 4);
    api_prefix = "/api";
  }
  
  DEBUG_PRINTF("🚀 API 客户端就绪。指向 URL: %s%s\n", server_url.c_str(), api_prefix.c_str());
}

static void initDefaultStatusResponse(DeviceStatusResponse& res) {
  res.success = false;
  res.status = "error";
  res.student_name = "未绑定";
  res.class_name = "";
  res.total_points = 0;
  res.pet_type = "";
  res.pet_level = 1;
  res.pet_exp = 0;
  res.exp_progress = 0;
  res.exp_required = 40;
  res.is_max_level = false;
  res.error_msg = "";
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

int ApiClient::sendRequest(const String& method, const String& path, const String& body, String& response) {
  if (!Network::isConnected()) {
    DEBUG_PRINTLN("❌ API 客户端错误: 网络未连接！");
    return -1;
  }

  // 拼接完整 URL。上层历史传入 /api/...，这里统一剥离为 endpoint。
  String endpoint = path;
  if (endpoint.startsWith("/api/")) {
    endpoint = endpoint.substring(4);
  }
  String fullUrl = server_url + api_prefix + endpoint;
  
  if (!clientsInitialized) {
    #if !defined(ESP8266)
      secureClient.setInsecure(); // 忽略 SSL 证书签名校验，保障各种云部署域名直接跑通
    #endif
    clientsInitialized = true;
  }

  // 决定使用安全 WiFi 还是普通 WiFi 客户端
  #if defined(ESP8266)
    WiFiClient client;
    httpClient.begin(client, fullUrl);
  #else
    if (fullUrl.startsWith("https")) {
      httpClient.begin(secureClient, fullUrl);
    } else {
      httpClient.begin(plainClient, fullUrl);
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
  httpClient.setTimeout(8000);
  httpClient.addHeader("X-Device-ID", Network::getMacAddress());
  httpClient.addHeader("X-Device-Timestamp", timestampStr);
  httpClient.addHeader("X-Device-Signature", signature);
  httpClient.addHeader("Content-Type", "application/json");

  DEBUG_PRINTF("📤 发送 %s -> %s\n", method.c_str(), path.c_str());
  
  // 记录基础诊断状态
  lastDiagnostic.wifi_connected = Network::isConnected();
  lastDiagnostic.local_ip = WiFi.localIP().toString();

  int httpCode = -1;
  #if !defined(ESP8266)
    String host = "";
    int startIdx = fullUrl.indexOf("://");
    if (startIdx >= 0) {
      int endIdx = fullUrl.indexOf("/", startIdx + 3);
      host = (endIdx >= 0) ? fullUrl.substring(startIdx + 3, endIdx) : fullUrl.substring(startIdx + 3);
    }
    int colonIdx = host.indexOf(":");
    if (colonIdx >= 0) {
      host = host.substring(0, colonIdx);
    }
    lastDiagnostic.server_domain = host;

    IPAddress remote_ip;
    if (WiFi.hostByName(host.c_str(), remote_ip)) {
      DEBUG_PRINTF("🔍 [DNS] 域名 %s 解析为 IP: %s\n", host.c_str(), remote_ip.toString().c_str());
      lastDiagnostic.host_resolved_ip = remote_ip.toString();
    } else {
      DEBUG_PRINTF("🔍 [DNS] 域名 %s 解析失败！\n", host.c_str());
      lastDiagnostic.host_resolved_ip = "DNS Failed";
    }
    DEBUG_PRINTF("🧠 [内存] 准备请求时内存: free=%u max_alloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  #endif

  if (method == "GET") {
    httpCode = httpClient.GET();
  } else if (method == "POST") {
    httpCode = httpClient.POST(body);
  }

  lastDiagnostic.http_code = httpCode;
  if (httpCode > 0) {
    response = httpClient.getString();
    DEBUG_PRINTF("📥 响应代码 [%d]\n", httpCode);
    lastDiagnostic.tls_error_code = 0;
    lastDiagnostic.tls_error_msg = "OK";
  } else {
    DEBUG_PRINTF("❌ 请求失败。错误码 / 原因: %s\n", httpClient.errorToString(httpCode).c_str());
    #if !defined(ESP8266)
      if (fullUrl.startsWith("https")) {
        char err_buf[128];
        int err = secureClient.lastError(err_buf, sizeof(err_buf));
        lastDiagnostic.tls_error_code = err;
        lastDiagnostic.tls_error_msg = String(err_buf);
        DEBUG_PRINTF("🔒 [TLS 错误详情] %s\n", err_buf);
      } else {
        lastDiagnostic.tls_error_code = 0;
        lastDiagnostic.tls_error_msg = "No SecureClient";
      }
    #else
      lastDiagnostic.tls_error_code = 0;
      lastDiagnostic.tls_error_msg = "ESP8266 TLS";
    #endif
  }
  
  httpClient.end();
  return httpCode;
}

void ApiClient::getStatus(DeviceStatusResponse& res) {
  initDefaultStatusResponse(res);
  
  String responseStr;
  
  // 查询路径加上 query 参数
  String path = "/api/device/status?device_id=" + Network::getMacAddress();
  int code = sendRequest("GET", path, "", responseStr);
  
  if (code != 200) {
    res.error_msg = "服务器返回错误: " + String(code);
    return;
  }

  // 使用 ArduinoJson 反序列化
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, responseStr);
  
  if (error) {
    res.error_msg = "JSON 解析失败: " + String(error.c_str());
    return;
  }

  String status = doc["status"].as<String>();
  res.status = status;
  
  if (status == "unbound") {
    res.success = true;
    res.student_name = "未绑定";
    res.total_points = 0;
    res.pet_level = 1;
    res.pet_exp = 0;
    res.exp_progress = 0;
    res.exp_required = 40;
    res.is_max_level = false;
    res.error_msg = doc["message"].as<String>();
    return;
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
}

void ApiClient::postVoiceText(const String& text, VoiceActionResponse& res) {
  res.success = false;
  res.action = "none";
  res.reply_text = "";
  res.audio_url = "";
  res.error_msg = "";
  
  String responseStr;
  
  // 封装为 JSON
  StaticJsonDocument<256> reqDoc;
  reqDoc["text"] = text;
  String reqBody;
  serializeJson(reqDoc, reqBody);
  
  int code = sendRequest("POST", "/api/device/voice", reqBody, responseStr);
  
  if (code != 200) {
    res.error_msg = "语音提交失败，错误代码: " + String(code);
    return;
  }

  // 反序列化结果
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, responseStr);
  
  if (error) {
    res.error_msg = "响应 JSON 解析失败";
    return;
  }

  res.success = true;
  res.action = doc["action"].as<String>();
  res.reply_text = doc["reply_text"].as<String>();
  
  if (doc.containsKey("audio_url")) {
    res.audio_url = doc["audio_url"].as<String>();
  }
}

bool ApiClient::reportOfflineTask(const String& taskName, int points, uint32_t timestamp) {
  String responseStr;
  
  StaticJsonDocument<256> reqDoc;
  reqDoc["text"] = "我完成了" + taskName; // 将离线积累包装为调试文本意图
  
  String reqBody;
  serializeJson(reqDoc, reqBody);
  
  int code = sendRequest("POST", "/api/device/voice", reqBody, responseStr);
  
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

bool ApiClient::sendHeartbeat(int batteryLevel, bool isCharging) {
  String responseStr;
  
  StaticJsonDocument<128> reqDoc;
  reqDoc["battery_level"] = batteryLevel;
  reqDoc["is_charging"] = isCharging ? 1 : 0;
  
  String reqBody;
  serializeJson(reqDoc, reqBody);
  
  int code = sendRequest("POST", "/api/device/heartbeat", reqBody, responseStr);
  return (code == 200);
}

#include <FFat.h>

bool ApiClient::downloadAsset(const String& petType, int petLevel) {
  if (petType.isEmpty()) return false;
  
  // 版本号与 DeviceStateMachine::loadPetGif 保持一致
  String filename = "/" + petType + "_" + String(petLevel) + "_v4.gif";
  if (FFat.exists(filename)) {
    // 已经存在，无需下载
    return true;
  }
  
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // 使用配置的 server_url（域名）下载 GIF 素材
  // Vercel 部署后，public/pets/ 下的文件会被映射到根路径 /pets/
  // 本地 Node 服务器也配置了 app.use('/pets', ...) 路由
  String baseUrl = server_url;
  // 确保 baseUrl 不以 / 结尾
  if (baseUrl.endsWith("/")) baseUrl = baseUrl.substring(0, baseUrl.length() - 1);
  // 如果 server_url 包含 /pet-garden，则剥除该后缀（因为静态文件不走 /pet-garden 前缀）
  if (baseUrl.endsWith("/pet-garden")) {
    baseUrl = baseUrl.substring(0, baseUrl.length() - 11);
  }
  String targetUrl = baseUrl + "/pets/" + petType + "/lv" + String(petLevel) + ".gif";
  
  DEBUG_PRINTF("🌐 开始下载宠物素材: %s\n", targetUrl.c_str());
  
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000); // 15秒超时，给 TLS 握手留足时间
  
  // 复用已初始化的 secureClient / plainClient，确保 HTTPS 能正常握手
  if (!clientsInitialized) {
    #if !defined(ESP8266)
      secureClient.setInsecure(); // 忽略 SSL 证书校验
    #endif
    clientsInitialized = true;
  }
  
  if (targetUrl.startsWith("https")) {
    http.begin(secureClient, targetUrl); // HTTPS 必须使用 secureClient
  } else {
    http.begin(plainClient, targetUrl);
  }
  
  // 收集响应头（ESP32 默认不收集，不调用这个则 http.header() 永远返回空）
  const char* headerKeys[] = {"Content-Type"};
  http.collectHeaders(headerKeys, 1);
  
  int httpCode = http.GET();
  DEBUG_PRINTF("📦 素材下载 HTTP 响应码: %d\n", httpCode);
  if (httpCode == HTTP_CODE_OK) {
    // 校验 content-type，防止 SPA 路由返回 HTML 而非真正的图片
    String contentType = http.header("Content-Type");
    DEBUG_PRINTF("📦 素材 Content-Type: %s\n", contentType.c_str());
    if (contentType.length() > 0 && contentType.indexOf("image") < 0 && contentType.indexOf("octet") < 0) {
      DEBUG_PRINTF("❌ 素材下载返回了错误的类型: %s（不是图片）\n", contentType.c_str());
      http.end();
      return false;
    }
    
    int len = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    
    fs::File f = FFat.open(filename, "w");
    if (!f) {
      DEBUG_PRINTLN("❌ 无法创建本地文件保存素材");
      http.end();
      return false;
    }
    
    uint8_t buff[512];
    while(http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if(size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        f.write(buff, c);
        if(len > 0) {
          len -= c;
        }
      }
      delay(1);
    }
    
    f.close();
    http.end();
    DEBUG_PRINTLN("✅ 素材下载并保存成功!");
    return true;
  } else {
    DEBUG_PRINTF("❌ 素材下载失败，HTTP 状态码: %d\n", httpCode);
    http.end();
    return false;
  }
}
