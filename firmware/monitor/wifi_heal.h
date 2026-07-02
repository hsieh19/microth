#ifndef WIFI_HEAL_H
#define WIFI_HEAL_H

#include <WiFi.h>
#include "config.h"
#include "web_config.h"

namespace WiFiHeal {

    // 系统网络状态枚举
    enum SystemState {
        STATE_STA_CONNECTING,  // STA 模式：连接路由器中
        STATE_STA_CONNECTED,   // STA 模式：网络已连接正常工作
        STATE_AP_CONFIG        // AP 模式：开启热点配网服务中
    };

    // 以下全部加 static，限定为单编译单元内部链接，防止多重定义链接错误 (S1 修复)
    static SystemState current_state = STATE_STA_CONNECTING;

    // 记录切换至当前状态的绝对时间戳
    static unsigned long state_timer = 0;
    // 记录上一次进行非阻塞 begin 重试的时间戳
    static unsigned long reconnect_timer = 0;
    // 记录上一次正常在线的绝对时间戳
    static unsigned long last_online_time = 0;
    // 标志是否已记录首次断连，用于重置 reconnect_timer (M4 修复)
    static bool disconnect_first_detected = false;

    /**
     * @brief 切换状态并执行状态初始化动作
     */
    void transition_to(SystemState new_state) {
        current_state = new_state;
        state_timer = millis();
        
        switch (new_state) {
            case STATE_STA_CONNECTING:
                Serial.println("[WiFiState] 进入 [STA 连接中] 状态...");
                WiFi.mode(WIFI_STA);
                WiFi.setSleep(false);
                WiFi.begin(global_wifi_ssid.c_str(), global_wifi_password.c_str());
                reconnect_timer = millis();
                break;
                
            case STATE_STA_CONNECTED:
                Serial.println("[WiFiState] 进入 [STA 已连接] 状态，网络正常。");
                last_online_time = millis();
                break;
                
            case STATE_AP_CONFIG:
                Serial.println("[WiFiState] 进入 [AP 配网中] 状态...");
                // 开启 WebServer & DNSServer
                WebConfig::start_ap_server();
                break;
        }
    }

    /**
     * @brief 初始化 Wi-Fi 并开启初次连接
     */
    void init() {
        Serial.println("[WiFi] 正在初始化网络自愈模块...");
        transition_to(STATE_STA_CONNECTING);
    }

    /**
     * @brief 网络状态检查与自愈状态机（应在 loop 中频繁调用）
     */
    void handle() {
        unsigned long now = millis();

        switch (current_state) {
            case STATE_STA_CONNECTING: {
                if (WiFi.status() == WL_CONNECTED) {
                    transition_to(STATE_STA_CONNECTED);
                } else if (now - state_timer > 30000) {
                    // 开机或重新连接 30 秒仍未连上，判定当前 Wi-Fi 配置失效，切入配网模式
                    Serial.println("[WiFi] 连接超时 (30秒未成功)，自动切入 AP 配网模式。");
                    transition_to(STATE_AP_CONFIG);
                }
                break;
            }

            case STATE_STA_CONNECTED: {
                if (WiFi.status() == WL_CONNECTED) {
                    // 连接正常，持续刷新最后在线时间
                    last_online_time = now;
                    disconnect_first_detected = false; // 网络正常时重置标志
                } else {
                    // 检测到网络异常断开
                    if (!disconnect_first_detected) {
                        // M4 修复：首次检测到断连时才重置 reconnect_timer
                        // 该变量可能是很久以前连接时设置的，必须重置否则会立即触发重连而不是等待 15 秒
                        reconnect_timer = now;
                        disconnect_first_detected = true;
                        Serial.println("[WiFi] 警告: 检测到网络已断开连接！开始计时 15 秒重连周期。");
                    }

                    // 1. 每 15 秒发起一次非阻塞 begin 连接，防止过度请求导致内部协议栈死锁
                    if (now - reconnect_timer > 15000) {
                        Serial.println("[WiFi] 正在发起非阻塞 STA 重连...");
                        WiFi.disconnect();
                        WiFi.begin(global_wifi_ssid.c_str(), global_wifi_password.c_str());
                        reconnect_timer = now;
                    }

                    // 2. 如果连续断连时间超出阈值 (WIFI_MAX_DISCONNECT_MS，默认 5分钟)，开启 AP 供用户重新排障或配网
                    if (now - last_online_time > WIFI_MAX_DISCONNECT_MS) {
                        Serial.printf("[WiFi] 连续断连超限 (%lu 秒)，自动激活 AP 配置热点。\n", 
                                      WIFI_MAX_DISCONNECT_MS / 1000);
                        disconnect_first_detected = false;
                        transition_to(STATE_AP_CONFIG);
                    }
                }
                break;
            }

            case STATE_AP_CONFIG: {
                // 轮询 DNS 服务与 Web 配置端请求
                bool config_submitted = WebConfig::handle();
                
                if (config_submitted) {
                    // 用户提交了新配置且页面已成功发送，此时硬重启使新参数生效
                    Serial.println("[WiFi] 侦测到配网参数提交完毕，即将重启系统以应用新配置！");
                    Serial.flush();
                    delay(500);
                    ESP.restart();
                }

                // 强制退避机制：若在 AP 配网模式下逗留了 5 分钟 (300000ms) 无人配网，
                // 则假定为临时断电或路由器短暂重启，切回 STA 重试，防止设备无限卡死在 AP
                if (now - state_timer > 300000) {
                    Serial.println("[WiFi] AP 模式超过 5 分钟无人配置，自动退避切回 STA 重新尝试连接。");
                    WebConfig::stop_ap_server();
                    transition_to(STATE_STA_CONNECTING);
                }
                break;
            }
        }
    }

    /**
     * @brief 提供全局查询接口，确认系统是否可以访问外部网络
     */
    bool is_connected() {
        return (current_state == STATE_STA_CONNECTED) && (WiFi.status() == WL_CONNECTED);
    }

    /**
     * @brief 提供全局查询接口，指示当前是否在 AP 状态
     */
    bool is_in_ap_mode() {
        return current_state == STATE_AP_CONFIG;
    }
}

#endif // WIFI_HEAL_H
