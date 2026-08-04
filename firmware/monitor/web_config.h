#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "config.h"
#include "nvs_storage.h"
#include "ota.h"

namespace WebConfig {

    static WebServer server(80);
    static DNSServer dnsServer;

    static bool save_success = false;
    static bool routes_initialized = false;

    String get_html_page() {
        String html = "<!DOCTYPE html>";
        html += "<html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>THS Monitor - 设备配置</title>";
        html += "<style>";
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
        html += "background:linear-gradient(135deg,#0f172a 0%,#1e293b 100%);color:#f8fafc;";
        html += "margin:0;padding:20px 0;display:flex;justify-content:center;align-items:flex-start;min-height:100vh;box-sizing:border-box;}";
        html += ".card{background:rgba(30,41,59,0.7);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);";
        html += "border:1px solid rgba(255,255,255,0.08);border-radius:20px;padding:30px;width:90%;max-width:440px;";
        html += "box-shadow:0 15px 35px rgba(0,0,0,0.4);box-sizing:border-box;}";
        html += "h2{margin-top:0;text-align:center;color:#38bdf8;font-size:24px;font-weight:700;margin-bottom:20px;}";
        html += ".sensor-container{display:flex;justify-content:space-around;align-items:center;background:rgba(15,23,42,0.45);border:1px solid rgba(255,255,255,0.05);border-radius:14px;padding:18px 10px;margin-bottom:16px;}";
        html += ".sensor-item{text-align:center;flex:1;}.sensor-separator{width:1px;height:36px;background:rgba(255,255,255,0.08);}";
        html += ".sensor-label{font-size:11px;color:#94a3b8;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:6px;}";
        html += ".sensor-value{font-size:26px;font-weight:700;line-height:1.1;}.temp-color{color:#f43f5e;}.humi-color{color:#38bdf8;}";
        html += ".unit{font-size:13px;font-weight:500;margin-left:2px;color:#94a3b8;}";
        html += ".sensor-time{text-align:center;font-size:11px;color:#64748b;margin-top:-10px;margin-bottom:16px;}";
        html += ".tabs{display:flex;border-radius:12px;background:rgba(15,23,42,0.5);padding:4px;margin-bottom:20px;gap:4px;}";
        html += ".tab-btn{flex:1;padding:10px;border:none;border-radius:9px;font-size:14px;font-weight:600;cursor:pointer;";
        html += "background:transparent;color:#64748b;transition:all 0.2s ease;}";
        html += ".tab-btn.active{background:linear-gradient(135deg,#0ea5e9,#0284c7);color:#fff;box-shadow:0 4px 12px rgba(14,165,233,0.35);}";
        html += ".tab-content{display:none;}.tab-content.active{display:block;}";
        html += ".form-group{margin-bottom:18px;}";
        html += ".form-row{display:flex;gap:16px;margin-bottom:18px;}";
        html += ".form-row .form-group{flex:1;margin-bottom:0;}";
        html += ".switch-box{display:flex;align-items:center;justify-content:space-between;background:rgba(15,23,42,0.45);border:1px solid #334155;border-radius:10px;padding:10px 14px;box-sizing:border-box;height:46px;}";
        html += ".switch-box span{font-size:14px;color:#94a3b8;font-weight:500;}";
        html += ".switch{position:relative;display:flex;align-items:center;width:44px;height:24px;}";
        html += ".switch input{opacity:0;width:0;height:0;}";
        html += ".slider{position:absolute;cursor:pointer;inset:0;background-color:#334155;transition:.3s;border-radius:24px;}";
        html += ".slider:before{position:absolute;content:\\\"\\\";height:18px;width:18px;left:3px;bottom:3px;background-color:#fff;transition:.3s;border-radius:50%;}";
        html += "input:checked + .slider{background-color:#38bdf8;}";
        html += "input:checked + .slider:before{transform:translateX(20px);}";
        html += "label{display:block;margin-bottom:8px;font-size:13px;font-weight:500;color:#94a3b8;text-transform:uppercase;letter-spacing:0.5px;}";
        html += "input,select{width:100%;padding:12px 14px;border:1px solid #334155;background:rgba(15,23,42,0.6);";
        html += "color:#f8fafc;border-radius:10px;box-sizing:border-box;font-size:15px;transition:all 0.25s ease;}";
        html += "input:focus,select:focus{outline:none;border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,0.25);background:rgba(15,23,42,0.9);}";
        html += ".btn{width:100%;padding:13px;border:none;border-radius:10px;font-size:15px;font-weight:600;cursor:pointer;transition:all 0.2s ease;margin-top:8px;}";
        html += ".btn-primary{background:linear-gradient(135deg,#0ea5e9,#0284c7);color:#fff;box-shadow:0 4px 15px rgba(14,165,233,0.35);}";
        html += ".btn-primary:hover{opacity:.95;transform:translateY(-1px);}";
        html += ".btn-outline{background:transparent;color:#38bdf8;border:1.5px solid #0ea5e9;}";
        html += ".btn-outline:hover{background:rgba(14,165,233,0.1);}";
        html += ".btn-warn{background:linear-gradient(135deg,#f97316,#ea580c);color:#fff;box-shadow:0 4px 12px rgba(249,115,22,0.35);}";
        html += ".btn-warn:hover{opacity:.92;transform:translateY(-1px);}";
        html += ".btn:disabled{opacity:.4;cursor:not-allowed;transform:none!important;}";
        html += ".ota-info-bar{display:flex;justify-content:space-between;align-items:center;background:rgba(15,23,42,0.5);border:1px solid rgba(255,255,255,0.06);border-radius:12px;padding:14px 16px;margin-bottom:18px;}";
        html += ".ota-info-item{text-align:center;flex:1;}";
        html += ".ota-info-separator{width:1px;height:30px;background:rgba(255,255,255,0.08);}";
        html += ".ota-ver-label{font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:0.5px;margin-bottom:4px;}";
        html += ".ota-ver-value{font-size:20px;font-weight:700;}";
        html += ".ver-current{color:#38bdf8;}";
        html += ".ver-latest{color:#94a3b8;}";
        html += ".ver-new{color:#10b981;}";
        html += ".hint{font-size:11px;color:#64748b;margin-top:5px;}";
        html += ".loading-overlay{display:none;position:fixed;inset:0;background:rgba(15,23,42,0.92);backdrop-filter:blur(8px);";
        html += "z-index:9999;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:20px;box-sizing:border-box;}";
        html += ".loading-overlay.show{display:flex;}";
        html += ".spinner{width:52px;height:52px;border:4px solid rgba(56,189,248,0.15);border-top:4px solid #38bdf8;border-radius:50%;animation:spin 1s linear infinite;margin-bottom:24px;}";
        html += "@keyframes spin{to{transform:rotate(360deg)}}";
        html += ".loading-title{font-size:18px;font-weight:700;color:#f8fafc;margin-bottom:10px;}";
        html += ".loading-sub{font-size:13px;color:#94a3b8;max-width:280px;line-height:1.6;}";
        html += ".loading-countdown{margin-top:16px;font-size:28px;font-weight:700;color:#38bdf8;}";
        html += ".footer{text-align:center;margin-top:22px;font-size:12px;color:#64748b;}";
        html += "</style></head><body>";

        // Loading 遮罩
        html += "<div class='loading-overlay' id='loadingOverlay'>";
        html += "<div class='spinner'></div>";
        html += "<div class='loading-title' id='loadingTitle'>正在处理...</div>";
        html += "<div class='loading-sub' id='loadingSub'>请勿断开设备电源，操作完成后设备将自动重启。</div>";
        html += "<div class='loading-countdown' id='loadingCountdown'></div>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<h2>THS Monitor 配网与配置</h2>";

        // 温湿度实时看板
        if (global_sensor_ready) {
            unsigned long ago_sec = (millis() - global_last_read_time) / 1000;
            html += "<div class='sensor-container'>";
            html += "<div class='sensor-item'><div class='sensor-label'>当前温度</div>";
            html += "<div class='sensor-value temp-color'>" + String(global_last_temp, 1) + "<span class='unit'>°C</span></div></div>";
            html += "<div class='sensor-separator'></div>";
            html += "<div class='sensor-item'><div class='sensor-label'>当前湿度</div>";
            html += "<div class='sensor-value humi-color'>" + String(global_last_humi, 1) + "<span class='unit'>%</span></div></div>";
            html += "</div>";
            html += "<div class='sensor-time'>上次数据更新于：" + String(ago_sec) + " 秒前 (刷新网页可同步)</div>";
        } else {
            html += "<div class='sensor-container'><div class='sensor-item' style='width:100%'>";
            html += "<div class='sensor-label'>当前温湿度</div>";
            html += "<div class='sensor-value' style='font-size:15px;color:#94a3b8;font-weight:500'>⏳ 传感器准备中...</div>";
            html += "</div></div>";
        }

        // Tab 切换按钮
        html += "<div class='tabs'>";
        html += "<button class='tab-btn active' id='tab-config-btn' onclick=\"switchTab('config')\">⚙️  配置</button>";
        html += "<button class='tab-btn' id='tab-ota-btn' onclick=\"switchTab('ota')\">🔄 升级</button>";
        html += "</div>";

        // ===== 配置 Tab =====
        html += "<div class='tab-content active' id='tab-config'>";
        html += "<form method='POST' action='/save'>";

        html += "<div class='form-row'>";
        html += "  <div class='form-group'><label>Wi-Fi 名称 (SSID)</label>";
        html += "  <input type='text' name='ssid' value='" + global_wifi_ssid + "' required autocomplete='off'></div>";
        html += "  <div class='form-group'><label>Wi-Fi 密码</label>";
        html += "  <input type='password' name='pass' value='" + global_wifi_password + "' autocomplete='off'></div>";
        html += "</div>";

        html += "<div class='form-group'><label>后端数据接收接口 URL</label>";
        html += "<input type='url' name='url' value='" + global_server_url + "' required autocomplete='off'></div>";

        html += "<div class='form-group'><label>安全密钥 (X-API-Key)</label>";
        html += "<input type='text' name='key' value='" + global_api_key + "' required autocomplete='off'></div>";

        html += "<div class='form-group'><label>在线升级服务地址 (OTA URL)</label>";
        html += "<input type='url' name='ota_url' value='" + global_ota_base_url + "' autocomplete='off'></div>";

        html += "<div class='form-row'>";
        html += "  <div class='form-group'><label>设备标识 (Device ID)</label>";
        html += "  <input type='text' name='device_id' value='" + global_device_id + "' required autocomplete='off'></div>";
        html += "  <div class='form-group'><label>设备别名 (Device Name)</label>";
        html += "  <input type='text' name='device_name' value='" + global_device_name + "' autocomplete='off'></div>";
        html += "</div>";

        html += "<div class='form-row'>";
        html += "  <div class='form-group'><label>采集周期 (秒)</label>";
        html += "  <input type='number' name='sample_sec' min='5' max='86400' value='" + String(global_sample_interval_ms / 1000) + "' required>";
        html += "  <div class='hint'>省电模式下为休眠周期</div></div>";
        html += "  <div class='form-group'><label>上报周期 (秒)</label>";
        html += "  <input type='number' name='interval_sec' min='5' max='86400' value='" + String(global_report_interval_ms / 1000) + "' required>";
        html += "  <div class='hint'>建议是采集周期整数倍</div></div>";
        html += "</div>";

        html += "<div class='form-row'>";
        html += "  <div class='form-group'><label>断连报警</label>";
        html += "    <div class='switch-box'><span>飞书推送报警</span>";
        html += "      <label class='switch'><input type='checkbox' name='sensor_alert' value='1'" + String(global_sensor_alert_enabled ? " checked" : "") + ">";
        html += "      <span class='slider'></span></label>";
        html += "    </div>";
        html += "  </div>";
        html += "  <div class='form-group'><label>省电模式</label>";
        html += "    <div class='switch-box'><span>电池低功耗睡眠</span>";
        html += "      <label class='switch'><input type='checkbox' name='low_power' value='1'" + String(global_low_power_mode ? " checked" : "") + ">";
        html += "      <span class='slider'></span></label>";
        html += "    </div>";
        html += "  </div>";
        html += "</div>";

        html += "<button class='btn btn-primary' type='submit'>保存配置并重启设备</button>";
        html += "</form></div>";

        // ===== 升级 Tab =====
        html += "<div class='tab-content' id='tab-ota'>";
        html += "<div class='ota-info-bar'>";
        html += "  <div class='ota-info-item'>";
        html += "    <div class='ota-ver-label'>当前固件版本</div>";
        html += "    <div class='ota-ver-value ver-current'>v" FIRMWARE_VERSION "</div>";
        html += "  </div>";
        html += "  <div class='ota-info-separator'></div>";
        html += "  <div class='ota-info-item'>";
        html += "    <div class='ota-ver-label'>最新可用版本</div>";
        html += "    <div class='ota-ver-value ver-latest' id='otaLatestVer'>--</div>";
        html += "  </div>";
        html += "</div>";

        html += "<button class='btn btn-outline' id='btnCheck' onclick='checkVersions()'>检查可用版本</button>";

        html += "<div class='form-group' style='margin-top:14px;'><label>选择目标版本</label>";
        html += "<select id='otaVersionSelect' disabled>";
        html += "<option value=''>请先点击'检查可用版本'</option>";
        html += "</select></div>";

        html += "<button class='btn btn-primary' id='btnUpgrade' disabled onclick='triggerUpgrade()'>立即执行升级 / 降级</button>";
        html += "<button class='btn btn-warn' id='btnRollback' style='margin-top:10px;' onclick='triggerRollback()'>一键回退到更新前版本</button>";
        html += "<div class='hint' id='rollbackHint' style='text-align:center;margin-top:6px;'>正在检查备份分区...</div>";
        html += "</div>";

        html += "<div class='footer'>设备芯片: ESP32-C3 &nbsp;|&nbsp; I2C 传感器: SHT40</div>";
        html += "</div>";

        // JavaScript
        html += "<script>";
        html += "var _otaBaseUrl='';";
        html += "var _latestOtaUrl='';"; // 暂存从 AnyFlash 获取的带签名升级包 URL
        html += "function switchTab(t){";
        html += "['config','ota'].forEach(function(id){";
        html += "document.getElementById('tab-'+id).classList.toggle('active',id===t);";
        html += "document.getElementById('tab-'+id+'-btn').classList.toggle('active',id===t);";
        html += "});if(t==='ota')loadOtaInfo();}";
        html += "function showLoading(title,sub,sec){";
        html += "document.getElementById('loadingTitle').textContent=title;";
        html += "document.getElementById('loadingSub').textContent=sub;";
        html += "document.getElementById('loadingOverlay').classList.add('show');";
        html += "var cd=document.getElementById('loadingCountdown'),n=sec;";
        html += "function tick(){cd.textContent=n>0?n+'s':'';if(n<=0){window.location.reload();}n--;setTimeout(tick,1000);}tick();}";
        html += "function loadOtaInfo(){";
        html += "var rb=document.getElementById('btnRollback'),hint=document.getElementById('rollbackHint');";
        html += "rb.disabled=true;";
        html += "fetch('/api/ota_info').then(function(r){return r.json();}).then(function(d){";
        html += "_otaBaseUrl=d.ota_base_url;";
        html += "if(d.has_backup){rb.disabled=false;hint.textContent='检测到备份分区，可一键回退';}";
        html += "else{rb.disabled=true;hint.textContent='无可用备份分区，请通过版本列表降级';}";
        html += "}).catch(function(){hint.textContent='获取分区状态失败';});}";
        html += "function checkVersions(){";
        html += "if(!_otaBaseUrl){loadOtaInfo();}";
        html += "var btn=document.getElementById('btnCheck');btn.textContent='获取中...';btn.disabled=true;";
        html += "fetch(_otaBaseUrl+'/api/ota/check?project=microth&chip=ESP32C3').then(function(r){return r.json();}).then(function(d){";
        html += "var sel=document.getElementById('otaVersionSelect');";
        html += "sel.innerHTML='<option value=\"\">-- 请选择目标版本 --</option>';";
        html += "if(d.version){";
        html += "  var o=document.createElement('option');o.value=d.version;o.textContent='v'+d.version + (d.version==='" FIRMWARE_VERSION "'?' (当前最新)':'');sel.appendChild(o);";
        html += "  _latestOtaUrl=d.url;";
        html += "  sel.disabled=false;";
        html += "  sel.onchange=function(){document.getElementById('btnUpgrade').disabled=!sel.value;};";
        html += "  var el=document.getElementById('otaLatestVer');el.textContent='v'+d.version;";
        html += "  if(d.version==='" FIRMWARE_VERSION "'){el.className='ota-ver-value ver-new';el.textContent+=' (最新)';}";
        html += "  else{el.className='ota-ver-value';el.style.color='#eab308';}";
        html += "}else{";
        html += "  sel.innerHTML='<option value=\"\">-- 无可用固件 --</option>';";
        html += "}";
        html += "btn.textContent='刷新版本';btn.disabled=false;";
        html += "}).catch(function(e){btn.textContent='获取失败，重试';btn.disabled=false;alert('获取版本失败: '+e);});}";
        html += "function triggerUpgrade(){";
        html += "var ver=document.getElementById('otaVersionSelect').value;if(!ver)return;";
        html += "if(!_latestOtaUrl){alert('未获取到有效升级链接，请重新刷新');return;}";
        html += "if(!confirm('确认将固件升级/降级至 v'+ver+'?\\n操作期间请勿断电。'))return;";
        html += "showLoading('正在准备固件...','正在向设备发送写入指令，请稍候...',0);";
        html += "document.getElementById('loadingTitle').textContent='正在写入固件...';";
        html += "document.getElementById('loadingSub').textContent='固件下载写入中，请勿断开电源，写入完成后设备将自动重启。';";
        html += "fetch('/api/trigger_ota',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url:_latestOtaUrl})})";
        html += ".then(function(r){";
        html += "if(!r.ok)throw new Error('设备拒绝了升级请求');";
        html += "showLoading('固件写入中...','设备将在完成后自动重启，请等待...',90);})";
        html += ".catch(function(e){document.getElementById('loadingOverlay').classList.remove('show');alert('升级失败: '+e);});}";
        html += "function triggerRollback(){";
        html += "if(!confirm('确认回退到更新前的备份固件?\\n设备将立即重启，约10~15秒后恢复正常运行。'))return;";
        html += "showLoading('正在切换分区...','设备即将重启并切换到备份固件，请稍候...',20);";
        html += "fetch('/api/trigger_rollback',{method:'POST'}).catch(function(){});}";
        html += "</script>";
        html += "</body></html>";
        return html;
    }

    String get_success_page() {
        String html = "<!DOCTYPE html>";
        html += "<html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>配置已保存</title>";
        html += "<style>";
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
        html += "background:#0f172a;color:#f8fafc;text-align:center;display:flex;justify-content:center;";
        html += "align-items:center;min-height:100vh;margin:0;}";
        html += ".card{background:rgba(30,41,59,0.8);border-radius:16px;padding:40px;max-width:400px;border:1px solid rgba(255,255,255,0.05);}";
        html += "h2{color:#10b981;margin-top:0;}p{color:#94a3b8;font-size:15px;line-height:1.6;}";
        html += ".redirect-tips{color:#64748b;font-size:12px;margin-top:15px;}";
        html += ".spinner{border:4px solid rgba(255,255,255,0.1);width:36px;height:36px;border-radius:50%;";
        html += "border-left-color:#10b981;animation:spin 1s linear infinite;margin:20px auto 0;}";
        html += "@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}";
        html += "</style></head><body>";
        html += "<div class='card'>";
        html += "<h2>配置保存成功！</h2>";
        html += "<p>设备参数已成功写入本地闪存。<br>ESP32-C3 正在重启以尝试连接您的新网络并上报数据。</p>";
        html += "<div class='spinner'></div>";
        html += "<div class='redirect-tips'>设备正在重启，6 秒后将自动尝试返回配置首页...</div>";
        html += "</div>";
        html += "<script>setTimeout(function(){window.location.href='/';},6000);</script>";
        html += "</body></html>";
        return html;
    }

    void handle_captive_redirect() {
        last_web_visit_time = millis();
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    }

    void init() {
        if (routes_initialized) return;

        // 1. 主配置页
        server.on("/", HTTP_GET, []() {
            last_web_visit_time = millis();
            server.send(200, "text/html", get_html_page());
        });

        // 2. 保存配置接口
        server.on("/save", HTTP_POST, []() {
            last_web_visit_time = millis();
            String ssid         = server.arg("ssid");
            String pass         = server.arg("pass");
            String url          = server.arg("url");
            String key          = server.arg("key");
            String dev_id       = server.arg("device_id");
            String dev_name     = server.arg("device_name");
            String sample_str   = server.arg("sample_sec");
            String interval_str = server.arg("interval_sec");
            String sensor_alert_str = server.arg("sensor_alert");
            String low_power_str    = server.arg("low_power");
            String ota_url      = server.arg("ota_url");

            if (ssid.isEmpty() || url.isEmpty() || key.isEmpty() || dev_id.isEmpty()) {
                server.send(400, "text/plain; charset=utf-8", "Error: SSID, URL, Key and Device ID are required.");
                return;
            }
            if (ssid.length() > 32)   { server.send(400, "text/plain; charset=utf-8", "Error: SSID too long (max 32 chars)."); return; }
            if (key.length() > 64)    { server.send(400, "text/plain; charset=utf-8", "Error: API Key too long (max 64 chars)."); return; }
            if (dev_id.length() > 32) { server.send(400, "text/plain; charset=utf-8", "Error: Device ID too long (max 32 chars)."); return; }
            if (dev_name.length() > 64){ server.send(400, "text/plain; charset=utf-8", "Error: Device Name too long (max 64 chars)."); return; }
            if (url.length() > 128)   { server.send(400, "text/plain; charset=utf-8", "Error: Server URL too long (max 128 chars)."); return; }
            if (ota_url.length() > 128){ server.send(400, "text/plain; charset=utf-8", "Error: OTA URL too long (max 128 chars)."); return; }

            if (ota_url.isEmpty()) ota_url = global_ota_base_url;

            uint32_t sample = sample_str.toInt();
            if (sample < 5 || sample > 86400) sample = 30;

            uint32_t interval = interval_str.toInt();
            if (interval < 5 || interval > 86400) interval = 300;
            if (interval < sample) interval = sample;

            // HTML checkbox 开关在未选中时不发送任何参数。因此需要通过 hasArg 来检查是否被勾选
            bool sensor_alert = server.hasArg("sensor_alert");
            bool low_power    = server.hasArg("low_power");

            NvsStorage::save_configs(ssid, pass, url, key, dev_id, dev_name, sample, interval, sensor_alert, low_power, ota_url);
            server.send(200, "text/html", get_success_page());
            save_success = true;
        });

        // 3. OTA 信息查询
        server.on("/api/ota_info", HTTP_GET, []() {
            last_web_visit_time = millis();
            bool has_backup = false;
            String ver = Ota::get_ota_info(&has_backup);
            String json = "{\"version\":\"" + ver + "\",\"ota_base_url\":\"" + global_ota_base_url + "\",\"has_backup\":" + (has_backup ? "true" : "false") + "}";
            server.send(200, "application/json", json);
        });

        // 4. 触发 OTA 升级/降级
        server.on("/api/trigger_ota", HTTP_POST, []() {
            last_web_visit_time = millis();
            if (!server.hasArg("plain")) {
                server.send(400, "application/json", "{\"error\":\"Missing body\"}");
                return;
            }
            String body = server.arg("plain");
            int urlStart = body.indexOf("\"url\":\"");
            if (urlStart < 0) { server.send(400, "application/json", "{\"error\":\"Missing url field\"}"); return; }
            urlStart += 7;
            int urlEnd = body.indexOf('"', urlStart);
            if (urlEnd < 0) { server.send(400, "application/json", "{\"error\":\"Malformed url field\"}"); return; }
            String signed_url = body.substring(urlStart, urlEnd);
            server.send(200, "application/json", "{\"status\":\"started\"}");
            delay(150);
            String result = Ota::start_upgrade(signed_url);
            if (!result.isEmpty()) {
                Serial.printf("[WebConfig] OTA 结果: %s\n", result.c_str());
            }
        });

        // 5. 触发物理分区一键回退
        server.on("/api/trigger_rollback", HTTP_POST, []() {
            last_web_visit_time = millis();
            bool ok = Ota::rollback_to_previous_partition();
            if (ok) {
                server.send(200, "application/json", "{\"status\":\"rebooting\"}");
                delay(200);
                ESP.restart();
            } else {
                server.send(500, "application/json", "{\"error\":\"No valid backup partition\"}");
            }
        });

        routes_initialized = true;
    }

    void start_ap_server() {
        init();
        Serial.println("[WebConfig] 启动 AP 配网模式...");
        IPAddress apIP(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apIP, subnet);
        WiFi.softAP("THS-Monitor-Setup");
        Serial.printf("[WebConfig] 热点 \"THS-Monitor-Setup\" 已启用，IP: %s\n", WiFi.softAPIP().toString().c_str());
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", apIP);
        server.onNotFound(handle_captive_redirect);
        server.begin();
        save_success = false;
    }

    void start_sta_server() {
        init();
        Serial.println("[WebConfig] 启动 STA 局域网配置服务...");
        Serial.printf("[WebConfig] 局域网配置 URL: http://%s/\n", WiFi.localIP().toString().c_str());
        server.onNotFound([]() { server.send(404, "text/plain", "Not Found"); });
        server.begin();
        save_success = false;
    }

    bool handle() {
        dnsServer.processNextRequest();
        server.handleClient();
        if (save_success) { delay(2000); return true; }
        return false;
    }

    bool handle_sta() {
        server.handleClient();
        if (save_success) { delay(2000); return true; }
        return false;
    }

    void stop_ap_server() {
        server.stop();
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        Serial.println("[WebConfig] AP 服务器已关闭。");
    }

    void stop_sta_server() {
        server.stop();
        Serial.println("[WebConfig] STA 配置服务器已关闭。");
    }
}

#endif // WEB_CONFIG_H
