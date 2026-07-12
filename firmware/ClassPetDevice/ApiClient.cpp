/**
 * @file ApiClient.cpp
 * @brief 班级宠物园通用固件 - 通用云端 API 交互客户端实现
 */

#include "ApiClient.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>
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
String ApiClient::api_prefix = "/api";
String ApiClient::device_secret = "";
String ApiClient::proxy_ip = "";
String ApiClient::web_url = "";
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

void ApiClient::init(const String& serverUrl, const String& secret, const String& proxyIp, const String& webUrl) {
  server_url = serverUrl;
  device_secret = secret;
  proxy_ip = proxyIp;
  web_url = webUrl;
  
  #if !defined(ESP8266)
    secureClient.setInsecure();
    secureClient.setTimeout(10000); // 10秒超时，防握手死锁
  #endif
  plainClient.setTimeout(10000);
  clientsInitialized = true;

  // 移除尾部斜杠
  if (server_url.endsWith("/")) {
    server_url = server_url.substring(0, server_url.length() - 1);
  }

  // 兼容多种填写方式（默认走 /api，适配 Cloudflare Worker 根路径 API）：
  // - https://xxx.workers.dev           -> /api
  // - https://xxx.workers.dev/api       -> /api (剥除 /api 后缀)
  // - https://xxx.vercel.app/pet-garden -> /api (Vercel 兼容, 保留 /pet-garden 前缀)
  api_prefix = "/api";
  if (server_url.endsWith("/pet-garden")) {
    api_prefix = "/api";
  } else if (server_url.endsWith("/api")) {
    server_url = server_url.substring(0, server_url.length() - 4);
    api_prefix = "/api";
  }

  // 静态资源站点（宠物 GIF 等）默认与 API 域名不同：
  // 若未显式提供 web_url，则按 api.xxx -> pet.xxx 的约定自动推导；
  // 仍无法推导则回退到 API 域名本身。
  if (web_url.length() == 0) {
    if (server_url.startsWith("https://api.")) {
      web_url = "https://pet." + server_url.substring(12);
    } else if (server_url.startsWith("http://api.")) {
      web_url = "http://pet." + server_url.substring(11);
    } else {
      web_url = server_url;
    }
  }
  if (web_url.endsWith("/")) {
    web_url = web_url.substring(0, web_url.length() - 1);
  }
  // 去除可能误填的 /api 或 /pet-garden 后缀，避免静态资源 404
  if (web_url.endsWith("/api")) {
    web_url = web_url.substring(0, web_url.length() - 4);
  }
  if (web_url.endsWith("/pet-garden")) {
    web_url = web_url.substring(0, web_url.length() - 11);
  }
  
  DEBUG_PRINTF("🚀 API 客户端就绪。API URL: %s%s, 静态资源: %s\n", server_url.c_str(), api_prefix.c_str(), web_url.c_str());
}

String ApiClient::buildApiUrl(const String& path) {
  String p = path;
  if (!p.startsWith("/")) p = "/" + p;
  return server_url + api_prefix + p;
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

#include <SD_MMC.h>

bool ApiClient::downloadAsset(const String& petType, int petLevel) {
  if (petType.isEmpty()) return false;
  
  // 版本号与 DeviceStateMachine::loadPetGif 保持一致
  String filename = "/" + petType + "_" + String(petLevel) + "_v4.gif";
  if (SD_MMC.exists(filename)) {
    // 已经存在，无需下载
    return true;
  }
  
  if (WiFi.status() != WL_CONNECTED) return false;
  
  // 使用静态资源站点 web_url（通常是 Pages 前端域名）下载 GIF 素材，
  // 而非 API Worker 域名，因为 Worker 不托管 /pets/ 下的静态文件。
  String baseUrl = web_url;
  // 确保 baseUrl 不以 / 结尾
  if (baseUrl.endsWith("/")) baseUrl = baseUrl.substring(0, baseUrl.length() - 1);
  // 兼容旧 Vercel 配置中可能存在的 /pet-garden 后缀（静态文件不走此前缀）
  if (baseUrl.endsWith("/pet-garden")) {
    baseUrl = baseUrl.substring(0, baseUrl.length() - 11);
  }
  String targetUrl = baseUrl + "/pets/" + petType + "/lv" + String(petLevel) + ".gif";
  
  DEBUG_PRINTF("🌐 开始下载宠物素材: %s\n", targetUrl.c_str());
  
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000); // 15秒超时，给 TLS 握手留足时间
  
  // 已经移至 ApiClient::init 进行全局初始化
  
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
    
    int size = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    
    fs::File f = SD_MMC.open(filename, "w");
    if (!f) {
      DEBUG_PRINTLN("❌ 无法创建本地文件保存素材");
      http.end();
      return false;
    }
    
    uint8_t buff[512];
    int written = 0;
    
    while (http.connected() && (size > 0 || size == -1)) {
      int toRead = (size > 0 && size < (int)sizeof(buff)) ? size : (int)sizeof(buff);
      int c = stream->readBytes(buff, toRead);
      if (c > 0) {
        f.write(buff, c);
        written += c;
        if (size > 0) {
          size -= c;
        }
      } else {
        // readBytes 返回 0 代表数据读取完毕或连接断开超时
        break;
      }
    }
    
    f.close();
    http.end();
    
    if (written > 0) {
      DEBUG_PRINTF("✅ 素材下载并保存成功，大小: %d 字节\n", written);
      return true;
    } else {
      DEBUG_PRINTLN("❌ 素材写入文件失败");
      return false;
    }
  } else {
    DEBUG_PRINTF("❌ 素材下载失败，HTTP 状态码: %d\n", httpCode);
    http.end();
    return false;
  }
}
bool ApiClient::downloadCjkFont() {
  const char* fontPath = "/cjk16.bin";

  // 已存在则跳过 (下次启动会直接加载)
  if (SD_MMC.exists(fontPath)) {
    DEBUG_PRINTLN("🌐 [字体] TF 卡已存在 cjk16.bin，跳过下载");
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("🌐 [字体] 网络未连接，暂缓下载字库");
    return false;
  }
  if (server_url.isEmpty()) {
    DEBUG_PRINTLN("🌐 [字体] 服务器地址未配置，暂缓下载字库");
    return false;
  }

  // 使用与 TTS/状态接口相同的路径拼接方式 (server_url + api_prefix + endpoint)
  String targetUrl = server_url + api_prefix + "/device/font/cjk16.bin";
  DEBUG_PRINTF("🌐 [字体] 首次启动从服务器下载中文字库: %s\n", targetUrl.c_str());

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(20000);  // 给 TLS 握手和 440KB 传输留足时间

  #if defined(ESP8266)
    WiFiClient client;
    http.begin(client, targetUrl);
  #else
    if (targetUrl.startsWith("https")) {
      http.begin(secureClient, targetUrl);
    } else {
      http.begin(plainClient, targetUrl);
    }
  #endif

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int size = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    fs::File f = SD_MMC.open(fontPath, "w");
    if (!f) {
      DEBUG_PRINTLN("❌ [字体] 无法在 TF 卡创建 cjk16.bin");
      http.end();
      return false;
    }

    uint8_t buff[512];
    int written = 0;
    while (http.connected() && (size > 0 || size == -1)) {
      int toRead = (size > 0 && size < (int)sizeof(buff)) ? size : (int)sizeof(buff);
      int c = stream->readBytes(buff, toRead);
      if (c > 0) {
        f.write(buff, c);
        written += c;
        if (size > 0) size -= c;
      } else {
        break;
      }
    }
    f.close();
    http.end();

    if (written > 0) {
      DEBUG_PRINTF("✅ [字体] 已从服务器下载并保存到 TF 卡 (%d 字节)\n", written);
      return true;
    }
    DEBUG_PRINTLN("❌ [字体] 下载内容为空");
    return false;
  } else {
    DEBUG_PRINTF("❌ [字体] 下载失败，HTTP 状态码: %d\n", httpCode);
    http.end();
    return false;
  }
}

void ApiClient::postVoiceAudio(const String& filePath, VoiceActionResponse& res) {
  res.success = false;
  res.action = "none";
  res.reply_text = "";
  res.audio_url = "";
  res.error_msg = "";

  File file = SD_MMC.open(filePath, FILE_READ);
  if (!file) {
    res.error_msg = "SD卡无法打开音频文件";
    return;
  }

  size_t fileSize = file.size();
  if (fileSize <= 44) {
    // 只有 WAV 头但没有录音数据
    res.error_msg = "录音文件为空，请检查麦克风硬件";
    file.close();
    return;
  }
  Serial.printf("🎙️ [上传] 准备流式上传录音: %s, 大小: %u 字节\n", filePath.c_str(), (unsigned)fileSize);

  // 复用与 status 相同的 WiFiClient 通道（HTTPClient 会通过 addHeader 携带签名）
  HTTPClient http;
  String url = server_url + api_prefix + "/device/voice";

  #if defined(ESP8266)
    WiFiClient client;
    client.setTimeout(35000);
    http.begin(client, url);
  #else
    if (url.startsWith("https")) {
      secureClient.setTimeout(35000); // 覆盖底层socket超时限制
      http.begin(secureClient, url);
    } else {
      plainClient.setTimeout(35000);
      http.begin(plainClient, url);
    }
  #endif
  http.setTimeout(35000); // 语音识别可能较慢

  String ts = String(Network::getUnixTimestamp());
  http.addHeader("X-Device-ID", Network::getMacAddress());
  http.addHeader("X-Device-Timestamp", ts);
  http.addHeader("X-Device-Signature", calculateSignature(ts, ""));
  // 直接以原始 WAV 二进制流方式上传 (后端用 express.raw 接收)
  http.addHeader("Content-Type", "audio/wav");

  // 关键：使用 sendRequest 重载直接发送原始字节流，避免分块上传的复杂性
  // 由于 ESP32 的 HTTPClient 不直接支持流式分块上传，我们采用一次性发送。
  // 为兼容无 PSRAM 板卡，先尝试 ps_malloc，失败后降级到普通 malloc。
  uint8_t* payload = (uint8_t*)ps_malloc(fileSize);
  if (!payload) {
    Serial.println("⚠️ [上传] PSRAM 不可用或不足，降级到普通 malloc 申请大块内存");
    payload = (uint8_t*)malloc(fileSize);
  }
  if (!payload) {
    res.error_msg = "内存不足，无法打包音频 (PSRAM 与普通堆均无可用空间)";
    file.close();
    http.end();
    return;
  }
  file.read(payload, fileSize);
  file.close();

  int code = http.sendRequest("POST", payload, fileSize);
  free(payload);

  // 恢复底层socket超时时间
  #if !defined(ESP8266)
    secureClient.setTimeout(10000);
    plainClient.setTimeout(10000);
  #endif

  if (code != 200) {
    res.error_msg = "语音上传失败，错误代码: " + String(code);
    return;
  }

  String responseStr = http.getString();
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, responseStr);

  if (error) {
    res.error_msg = "响应 JSON 解析失败";
    return;
  }

  res.success = true;
  res.action = doc["action"].as<String>();
  res.reply_text = doc["reply_text"].as<String>();
  if (doc.containsKey("audio_url")) res.audio_url = doc["audio_url"].as<String>();
}
