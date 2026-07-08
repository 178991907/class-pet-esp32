/**
 * @file WebPortal.h
 * @brief 班级宠物园通用固件 - 设备 AP 配网静态网页内容
 * @note 采用现代毛玻璃美学 (Glassmorphism) 与自适应渐变设计
 */

#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>

const char HTTP_PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>班级宠物园 - 通用设备配网中心</title>
    <style>
        :root {
            --primary: #FF9F43;
            --primary-hover: #ff8f23;
            --bg-gradient: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            --glass-bg: rgba(255, 255, 255, 0.12);
            --glass-border: rgba(255, 255, 255, 0.2);
            --text-color: #ffffff;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
        }

        body {
            background: var(--bg-gradient);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            color: var(--text-color);
            padding: 20px;
        }

        .container {
            background: var(--glass-bg);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--glass-border);
            border-radius: 20px;
            padding: 40px 30px;
            width: 100%;
            max-width: 450px;
            box-shadow: 0 8px 32px 0 rgba(31, 38, 135, 0.37);
            text-align: center;
            animation: fadeIn 0.6s ease-out;
        }

        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        h1 {
            font-size: 1.8rem;
            margin-bottom: 10px;
            font-weight: 700;
            letter-spacing: 1px;
            text-shadow: 0 2px 4px rgba(0,0,0,0.2);
        }

        .subtitle {
            font-size: 0.9rem;
            color: rgba(255, 255, 255, 0.7);
            margin-bottom: 30px;
        }

        .form-group {
            margin-bottom: 20px;
            text-align: left;
        }

        label {
            display: block;
            font-size: 0.85rem;
            margin-bottom: 8px;
            font-weight: 600;
            color: rgba(255, 255, 255, 0.9);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        input[type="text"], input[type="password"], select {
            width: 100%;
            padding: 12px 16px;
            background: rgba(255, 255, 255, 0.08);
            border: 1px solid rgba(255, 255, 255, 0.15);
            border-radius: 10px;
            color: #fff;
            font-size: 0.95rem;
            outline: none;
            transition: all 0.3s;
        }

        input:focus, select:focus {
            background: rgba(255, 255, 255, 0.15);
            border-color: var(--primary);
            box-shadow: 0 0 10px rgba(255, 159, 67, 0.3);
        }

        select option {
            background: #4a3b70;
            color: #fff;
        }

        .btn {
            width: 100%;
            padding: 14px;
            background: var(--primary);
            border: none;
            border-radius: 10px;
            color: #fff;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
            box-shadow: 0 4px 15px rgba(255, 159, 67, 0.4);
            margin-top: 10px;
        }

        .btn:hover {
            background: var(--primary-hover);
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(255, 159, 67, 0.6);
        }

        .btn:active {
            transform: translateY(0);
        }

        .footer {
            margin-top: 25px;
            font-size: 0.75rem;
            color: rgba(255, 255, 255, 0.5);
        }

        .scan-btn {
            background: rgba(255, 255, 255, 0.1);
            border: 1px solid rgba(255, 255, 255, 0.2);
            padding: 6px 12px;
            border-radius: 6px;
            color: #fff;
            font-size: 0.75rem;
            cursor: pointer;
            margin-left: 10px;
            transition: background 0.3s;
        }

        .scan-btn:hover {
            background: rgba(255, 255, 255, 0.2);
        }

        .wifi-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 8px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ClassPetGarden</h1>
        <p class="subtitle">通用嵌入式设备网管中心</p>
        
        <form action="/save" method="POST">
            <div class="form-group">
                <div class="wifi-header">
                    <label for="ssid">选择 WiFi 网络</label>
                    <button type="button" class="scan-btn" onclick="scanWifi()">🔄 刷新列表</button>
                </div>
                <!-- 既支持选择也支持手动输入 -->
                <select id="wifi_select" onchange="document.getElementById('ssid').value=this.value">
                    <option value="">-- 点击刷新加载周围 WiFi --</option>
                </select>
                <input type="text" id="ssid" name="ssid" placeholder="手动输入或选择 SSID" required style="margin-top: 8px;">
            </div>
            
            <div class="form-group">
                <label for="password">WiFi 密码</label>
                <input type="password" id="password" name="password" placeholder="请输入 WiFi 密码" required>
            </div>
            
            <div class="form-group">
                <label for="server">后端 API 服务器地址</label>
                <input type="text" id="server" name="server" value="https://pete.qqzy.de5.net" placeholder="Vercel 域名或本地局域网 IP" required>
            </div>
            
            <div class="form-group">
                <label for="secret">通信校验密钥 (Device Secret)</label>
                <input type="password" id="secret" name="secret" value="class-pet-device-secret" placeholder="请输入签名校验密钥" required>
            </div>

            <div class="form-group">
                <label for="proxy">Cloudflare 优选代理 IP (选填)</label>
                <input type="text" id="proxy" name="proxy" placeholder="例如: 104.16.82.15 (留空表示直连解析)" style="margin-top: 4px;">
            </div>
            
            <button type="submit" class="btn">💾 保存配置并使设备连网</button>
        </form>
        
        <div class="footer">
            设备物理 MAC 识别码: <span id="mac">加载中...</span>
        </div>
    </div>

    <script>
        // 动态获取当前设备的 MAC 地址并显示
        fetch('/api/mac')
            .then(res => res.json())
            .then(data => {
                document.getElementById('mac').innerText = data.mac;
            })
            .catch(() => {
                document.getElementById('mac').innerText = "FF:FF:FF:FF:FF:FF";
            });

        // 扫描周围 WiFi 列表
        function scanWifi() {
            const select = document.getElementById('wifi_select');
            select.innerHTML = '<option>📡 正在扫描周围 WiFi...</option>';
            
            fetch('/api/scan')
                .then(res => res.json())
                .then(data => {
                    select.innerHTML = '<option value="">-- 请选择 WiFi 网络 --</option>';
                    data.networks.forEach(net => {
                        const opt = document.createElement('option');
                        opt.value = net.ssid;
                        opt.innerText = `${net.ssid} (${net.rssi} dBm) ${net.secure ? '🔒' : '🔓'}`;
                        select.appendChild(opt);
                    });
                })
                .catch(() => {
                    select.innerHTML = '<option value="">❌ 扫描失败，请手动输入 SSID</option>';
                });
        }
        
        // 首次加载自动触发扫描
        window.onload = () => {
            setTimeout(scanWifi, 500);
        };
    </script>
</body>
</html>
)rawhtml";

#endif // WEB_PORTAL_H
