#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "config.h"
#include "nvs_storage.h"

namespace WebConfig {

    // 以下加 static，限定为单编译单元内部链接，防止多重定义链接错误 (S1 修复)
    static WebServer server(80);
    static DNSServer dnsServer;

    // 标志是否成功保存了参数
    static bool save_success = false;

    /**
     * @brief 渲染 HTML 配置页面 (响应式、毛玻璃微光现代设计风格)
     */
    String get_html_page() {
        String html = "<!DOCTYPE html>";
        html += "<html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>设备配网配置 - 温湿度监控系统</title>";
        html += "<style>";
        html += "body { font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif; ";
        html += "background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%); color: #f8fafc; ";
        html += "margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }";
        html += ".card { background: rgba(30, 41, 59, 0.7); backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); ";
        html += "border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 20px; padding: 30px; width: 90%; max-width: 440px; ";
        html += "box-shadow: 0 15px 35px rgba(0, 0, 0, 0.4); box-sizing: border-box; }";
        html += "h2 { margin-top: 0; text-align: center; color: #38bdf8; font-size: 24px; font-weight: 700; margin-bottom: 25px; }";
        html += ".form-group { margin-bottom: 18px; }";
        html += "label { display: block; margin-bottom: 8px; font-size: 13px; font-weight: 500; color: #94a3b8; text-transform: uppercase; letter-spacing: 0.5px; }";
        html += "input { width: 100%; padding: 12px 14px; border: 1px solid #334155; background: rgba(15, 23, 42, 0.6); ";
        html += "color: #f8fafc; border-radius: 10px; box-sizing: border-box; font-size: 15px; transition: all 0.25s ease; }";
        html += "input:focus { outline: none; border-color: #38bdf8; box-shadow: 0 0 0 3px rgba(56, 189, 248, 0.25); background: rgba(15, 23, 42, 0.9); }";
        html += "button { width: 100%; padding: 14px; background: linear-gradient(135deg, #0ea5e9 0%, #0284c7 100%); ";
        html += "color: #ffffff; border: none; border-radius: 10px; font-size: 16px; font-weight: 600; cursor: pointer; ";
        html += "box-shadow: 0 4px 15px rgba(14, 165, 233, 0.35); transition: all 0.2s ease; margin-top: 10px; }";
        html += "button:hover { opacity: 0.95; transform: translateY(-1px); box-shadow: 0 6px 20px rgba(14, 165, 233, 0.45); }";
        html += "button:active { transform: translateY(0); }";
        html += ".footer { text-align: center; margin-top: 25px; font-size: 12px; color: #64748b; }";
        html += ".sensor-container { display: flex; justify-content: space-around; align-items: center; background: rgba(15, 23, 42, 0.45); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 14px; padding: 18px 10px; margin-bottom: 20px; }";
        html += ".sensor-item { text-align: center; flex: 1; }";
        html += ".sensor-separator { width: 1px; height: 36px; background: rgba(255, 255, 255, 0.08); }";
        html += ".sensor-label { font-size: 11px; color: #94a3b8; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 6px; }";
        html += ".sensor-value { font-size: 26px; font-weight: 700; line-height: 1.1; }";
        html += ".temp-color { color: #f43f5e; }";
        html += ".humi-color { color: #38bdf8; }";
        html += ".unit { font-size: 13px; font-weight: 500; margin-left: 2px; color: #94a3b8; }";
        html += ".sensor-time { text-align: center; font-size: 11px; color: #64748b; margin-top: -12px; margin-bottom: 22px; }";
        html += "</style></head><body>";
        
        html += "<div class='card'>";
        html += "<h2>THS Monitor 配网与配置</h2>";

        // 插入温湿度实时监视看板
        if (global_sensor_ready) {
            unsigned long ago_sec = (millis() - global_last_read_time) / 1000;
            html += "<div class='sensor-container'>";
            html += "  <div class='sensor-item'>";
            html += "    <div class='sensor-label'>当前温度</div>";
            html += "    <div class='sensor-value temp-color'>" + String(global_last_temp, 1) + "<span class='unit'>°C</span></div>";
            html += "  </div>";
            html += "  <div class='sensor-separator'></div>";
            html += "  <div class='sensor-item'>";
            html += "    <div class='sensor-label'>当前湿度</div>";
            html += "    <div class='sensor-value humi-color'>" + String(global_last_humi, 1) + "<span class='unit'>%</span></div>";
            html += "  </div>";
            html += "</div>";
            html += "<div class='sensor-time'>上次数据更新于：" + String(ago_sec) + " 秒前 (手动刷新网页可同步)</div>";
        } else {
            html += "<div class='sensor-container'>";
            html += "  <div class='sensor-item' style='width: 100%;'>";
            html += "    <div class='sensor-label'>当前温湿度</div>";
            html += "    <div class='sensor-value' style='font-size: 15px; color: #94a3b8; font-weight: 500;'>⏳ 传感器准备中...</div>";
            html += "  </div>";
            html += "</div>";
        }

        html += "<form method='POST' action='/save'>";
        
        // Wi-Fi SSID
        html += "<div class='form-group'>";
        html += "<label>Wi-Fi 名称 (SSID)</label>";
        html += "<input type='text' name='ssid' value='" + global_wifi_ssid + "' required autocomplete='off'>";
        html += "</div>";

        // Wi-Fi Password
        html += "<div class='form-group'>";
        html += "<label>Wi-Fi 密码</label>";
        html += "<input type='password' name='pass' value='" + global_wifi_password + "' autocomplete='off'>";
        html += "</div>";

        // Server URL
        html += "<div class='form-group'>";
        html += "<label>后端数据接收接口 URL</label>";
        html += "<input type='url' name='url' value='" + global_server_url + "' required autocomplete='off'>";
        html += "</div>";

        // API Key
        html += "<div class='form-group'>";
        html += "<label>安全密钥 (X-API-Key)</label>";
        html += "<input type='text' name='key' value='" + global_api_key + "' required autocomplete='off'>";
        html += "</div>";

        // Device ID
        html += "<div class='form-group'>";
        html += "<label>设备标识 (Device ID)</label>";
        html += "<input type='text' name='device_id' value='" + global_device_id + "' required autocomplete='off'>";
        html += "</div>";

        // Device Name
        html += "<div class='form-group'>";
        html += "<label>设备别名 (Device Name)</label>";
        html += "<input type='text' name='device_name' value='" + global_device_name + "' autocomplete='off'>";
        html += "</div>";

        // Report Interval
        html += "<div class='form-group'>";
        html += "<label>上报周期 (秒)</label>";
        html += "<input type='number' name='interval_sec' min='5' max='86400' value='" + String(global_report_interval_ms / 1000) + "' required>";
        html += "</div>";

        // Sensor Alert Option
        html += "<div class='form-group'>";
        html += "<label>传感器断连报警 (飞书推送)</label>";
        html += "<select name='sensor_alert' style='width: 100%; padding: 12px 14px; border: 1px solid #334155; background: rgba(15, 23, 42, 0.6); color: #f8fafc; border-radius: 10px; font-size: 15px;'>";
        html += "  <option value='1'" + String(global_sensor_alert_enabled ? " selected" : "") + ">开启</option>";
        html += "  <option value='0'" + String(!global_sensor_alert_enabled ? " selected" : "") + ">关闭</option>";
        html += "</select>";
        html += "</div>";

        html += "<button type='submit'>保存配置并重启设备</button>";
        html += "</form>";
        html += "<div class='footer'>设备芯片: ESP32-C3 | I2C 传感器: SHT40</div>";
        html += "</div>";
        html += "</body></html>";
        return html;
    }

    /**
     * @brief 渲染保存成功的确认和过渡页面
     */
    String get_success_page() {
        String html = "<!DOCTYPE html>";
        html += "<html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>配置已保存</title>";
        html += "<style>";
        html += "body { font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif; ";
        html += "background: #0f172a; color: #f8fafc; text-align: center; display: flex; justify-content: center; ";
        html += "align-items: center; min-height: 100vh; margin: 0; }";
        html += ".card { background: rgba(30, 41, 59, 0.8); border-radius: 16px; padding: 40px; max-width: 400px; border: 1px solid rgba(255,255,255,0.05); }";
        html += "h2 { color: #10b981; margin-top: 0; }";
        html += "p { color: #94a3b8; font-size: 15px; line-height: 1.6; }";
        html += ".redirect-tips { color: #64748b; font-size: 12px; margin-top: 15px; }";
        html += ".spinner { border: 4px solid rgba(255,255,255,0.1); width: 36px; height: 36px; border-radius: 50%; ";
        html += "border-left-color: #10b981; animation: spin 1s linear infinite; margin: 20px auto 0; }";
        html += "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }";
        html += "</style>";
        html += "</head><body>";
        html += "<div class='card'>";
        html += "<h2>配置保存成功！</h2>";
        html += "<p>设备参数已成功写入本地闪存。<br>ESP32-C3 正在重启以尝试连接您的新网络并上报数据。</p>";
        html += "<div class='spinner'></div>";
        html += "<div class='redirect-tips'>设备正在重启，6 秒后将自动尝试返回配置首页...</div>";
        html += "</div>";
        html += "<script>";
        html += "  setTimeout(function() { window.location.href = '/'; }, 6000);";
        html += "</script>";
        html += "</body></html>";
        return html;
    }

    /**
     * @brief 强制门户 (Captive Portal) 重定向处理。
     * 所有不符合定义的 HTTP 请求均重定向至 192.168.4.1 (Web 首页)
     */
    void handle_captive_redirect() {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", ""); // 302 重定向
    }

    /**
     * @brief 初始化 Web 路由配置。此函数需在系统启动 (setup) 阶段调用一次，避免重复注册路由。
     */
    void init() {
        // 1. 配置表单页
        server.on("/", HTTP_GET, []() {
            server.send(200, "text/html", get_html_page());
        });

        // 2. 提交表单保存接口〔S3 修复：对所有输入增加长度、范围校验〕
        server.on("/save", HTTP_POST, []() {
            String ssid   = server.arg("ssid");
            String pass   = server.arg("pass");
            String url    = server.arg("url");
            String key    = server.arg("key");
            String dev_id = server.arg("device_id");
            String dev_name = server.arg("device_name");
            String interval_str = server.arg("interval_sec");
            String sensor_alert_str = server.arg("sensor_alert");

            // 必填字段非空校验
            if (ssid.isEmpty() || url.isEmpty() || key.isEmpty() || dev_id.isEmpty()) {
                server.send(400, "text/plain; charset=utf-8", "Error: SSID, URL, Key and Device ID are required.");
                return;
            }

            // 字段长度上限校验，防止 NVS 唯一性库写入超长字符串静默失败
            if (ssid.length() > 32) {
                server.send(400, "text/plain; charset=utf-8", "Error: SSID too long (max 32 chars).");
                return;
            }
            if (key.length() > 64) {
                server.send(400, "text/plain; charset=utf-8", "Error: API Key too long (max 64 chars).");
                return;
            }
            if (dev_id.length() > 32) {
                server.send(400, "text/plain; charset=utf-8", "Error: Device ID too long (max 32 chars).");
                return;
            }
            if (dev_name.length() > 64) {
                server.send(400, "text/plain; charset=utf-8", "Error: Device Name too long (max 64 chars).");
                return;
            }
            if (url.length() > 128) {
                server.send(400, "text/plain; charset=utf-8", "Error: Server URL too long (max 128 chars).");
                return;
            }

            // 上报周期范围校验：必须在 [5, 86400] 秒范围内，防止 0 导致无限循环上报
            uint32_t interval = interval_str.toInt();
            if (interval < 5 || interval > 86400) {
                Serial.printf("[WebConfig] 上报周期字段不合法 (%u)，已回退为默认 60 秒。\n", interval);
                interval = 60; // 回退安全默认值，不直接拒绝请求
            }

            bool sensor_alert = (sensor_alert_str == "1");

            // 校验通过，保存到 NVS
            NvsStorage::save_configs(ssid, pass, url, key, dev_id, dev_name, interval, sensor_alert);

            server.send(200, "text/html", get_success_page());
            save_success = true;
        });
    }

    /**
     * @brief 启动 AP 配网服务 (AP模式 + WebServer + DNSServer)
     */
    void start_ap_server() {
        Serial.println("[WebConfig] 启动 AP 配网模式...");
        
        // 1. 设置 AP 模式 IP 地址段 (192.168.4.1)
        IPAddress apIP(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apIP, subnet);
        
        // 2. 发射 Wi-Fi 热点 (无密码，方便用户直接接入)
        WiFi.softAP("THS-Monitor-Setup");
        Serial.printf("[WebConfig] 热点 \"THS-Monitor-Setup\" 已启用。请用手机连接该热点，在浏览器中配置设备。\n");
        Serial.print("[WebConfig] 本地配置 IP: ");
        Serial.println(WiFi.softAPIP());

        // 3. 启动 DNS 强制门户重定向服务 (重定向所有非 192.168.4.1 域名解析)
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", apIP);

        // 4. 绑定强制重定向
        server.onNotFound(handle_captive_redirect);

        // 5. 启动 Web 服务
        server.begin();
        save_success = false;
    }

    /**
     * @brief 启动 STA 局域网配置网页服务
     */
    void start_sta_server() {
        Serial.println("[WebConfig] 启动 STA 局域网网页配置服务...");
        Serial.print("[WebConfig] 局域网配置 URL: http://");
        Serial.print(WiFi.localIP());
        Serial.println("/");

        // 绑定普通的 404 响应，覆盖强制重定向
        server.onNotFound([]() {
            server.send(404, "text/plain", "Not Found");
        });

        server.begin();
        save_success = false;
    }

    /**
     * @brief 运行 AP 配置下的 DNS 服务与 Web 服务轮询（需在 loop 中运行）
     * 
     * @return true 用户已提交新配置，主逻辑可以执行重启
     * @return false 正常服务运行中，暂未提交新配置
     */
    bool handle() {
        dnsServer.processNextRequest();
        server.handleClient();
        
        if (save_success) {
            delay(2000); // 留出 2 秒展示成功确认页面
            return true;
        }
        return false;
    }

    /**
     * @brief 运行 STA 配置下的 Web 服务轮询（需在 loop 中运行）
     * 
     * @return true 用户已提交新配置，主逻辑可以执行重启
     * @return false 正常服务运行中，暂未提交新配置
     */
    bool handle_sta() {
        server.handleClient();
        
        if (save_success) {
            delay(2000); // 留出 2 秒展示成功确认页面
            return true;
        }
        return false;
    }

    /**
     * @brief 关闭 AP 配网服务，清空资源
     */
    void stop_ap_server() {
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        Serial.println("[WebConfig] AP 配网服务器已关闭。");
    }

    /**
     * @brief 关闭 STA 配置服务，释放资源
     */
    void stop_sta_server() {
        server.stop();
        Serial.println("[WebConfig] STA 配置服务器已关闭。");
    }
}

#endif // WEB_CONFIG_H
