#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_arduino_version.h>

#include "config.h"
#include "nvs_storage.h"
#include "sensor.h"
#include "wifi_heal.h"
#include "http_client.h"

// ==================== 运行期全局变量定义 (对应 config.h 中的 extern 声明) ====================
String global_wifi_ssid;
String global_wifi_password;
String global_server_url;
String global_api_key;
String global_device_id;
String global_device_name;
unsigned long global_report_interval_ms;
bool global_sensor_alert_enabled = DEFAULT_SENSOR_ALERT_ENABLED;
bool global_low_power_mode = DEFAULT_LOW_POWER_MODE;

// 记录上一次数据上报的时间戳 (在低功耗模式下主要用作容错或开机参考)
unsigned long last_report_time = 0;

// ==================== 传感器最新读数全局缓存定义 ====================
float global_last_temp = 0.0f;
float global_last_humi = 0.0f;
bool global_sensor_ready = false;
unsigned long global_last_read_time = 0;

// ==================== 极致省电模式全局变量 ====================
unsigned long last_web_visit_time = 0;       // 上一次网页访问时间戳 (对应 config.h 声明)
bool run_config_web_server = false;          // 是否在后台常驻运行网页配置服务
unsigned long config_mode_start_time = 0;    // 开启配置模式的时间戳

/**
 * @brief 初始化硬件看门狗 (WDT)
 * 兼容旧版 (Core 2.x) 与新版 (Core 3.0+) Arduino ESP32 库
 */
void init_watchdog() {
    Serial.println("[System] 配置硬件看门狗...");
    
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
        // 兼容 ESP32 Arduino Core 3.0+
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = WDT_TIMEOUT_SEC * 1000,
            .idle_core_mask = (1 << 0) | (1 << 1), // 监控 Core 0 和 Core 1
            .trigger_panic = true
        };
        esp_err_t err = esp_task_wdt_init(&wdt_config);
    #else
        // 兼容 ESP32 Arduino Core 2.x
        esp_err_t err = esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    #endif

    if (err == ESP_OK) {
        esp_task_wdt_add(NULL); // 将当前 loop 任务线程加入看门狗监控
        Serial.printf("[System] 看门狗已启用，超时时间: %d 秒\n", WDT_TIMEOUT_SEC);
    } else {
        Serial.println("[System] 错误: 看门狗初始化失败！");
    }
}

void setup() {
    // 初始化串口，便于本地调试
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n==================================================");
    Serial.println("  轻量化温湿度监控系统 ESP32-C3 固件 v" FIRMWARE_VERSION " 启动");
    Serial.println("==================================================");

    // 1. 初始化看门狗
    init_watchdog();

    // 2. 从 NVS 闪存中加载运行时配置 (如果没有则使用退避默认值)
    NvsStorage::load_configs();

    // 3. 配置 BOOT 按键引脚，设置输入上拉
    pinMode(BOOT_PIN, INPUT_PULLUP);
    delay(50); // 电平滤波防抖

    // 4. 配网触发判定：如果 SSID 为空、未配置，或者是开机检测到 BOOT 按键被拉低 (按下)
    bool should_config = (global_wifi_ssid.isEmpty() || 
                           global_wifi_ssid == DEFAULT_WIFI_SSID || 
                           digitalRead(BOOT_PIN) == LOW);

    if (should_config) {
        // ==================== AP配网模式分支 ====================
        Serial.println("[System] 检测到配网触发条件(手动按键或无配置)，启动 AP 配网模式...");
        run_config_web_server = true;
        config_mode_start_time = millis();
        last_web_visit_time = millis();

        // 初始化 SHT40 传感器，预读取一次温湿度，使 Web 面板渲染时能立即看见数值
        Sensor::init();
        float temp_init = 0.0f;
        float humi_init = 0.0f;
        if (Sensor::read(&temp_init, &humi_init)) {
            global_last_temp = temp_init;
            global_last_humi = humi_init;
            global_sensor_ready = true;
            global_last_read_time = millis();
        }

        // 初始化 WiFi 状态机并强制切换至 AP 配置服务
        WiFiHeal::init();
        WiFiHeal::transition_to(WiFiHeal::STATE_AP_CONFIG);
        Serial.println("[System] 本地 AP 网页配置热点已开启，等待用户配置。");
    } 
    else if (!global_low_power_mode) {
        // ==================== 常驻在线模式分支 (电源供电，省电模式关闭) ====================
        Serial.println("[System] 检测到工作在【电源供电模式】(关闭极致省电)，常驻局域网连接。");
        
        // 初始化 I2C 并读取一次传感器，让 Web 配置页面能立即显示数值
        Sensor::init();
        float temp_init = 0.0f;
        float humi_init = 0.0f;
        if (Sensor::read(&temp_init, &humi_init)) {
            global_last_temp = temp_init;
            global_last_humi = humi_init;
            global_sensor_ready = true;
            global_last_read_time = millis();
        }

        // 初始化局域网连接与自愈网络（和原本的系统工作逻辑相同）
        WiFiHeal::init();
        
        // 开机 5 秒后触发第一次数据上报
        last_report_time = millis() - global_report_interval_ms + 5000;
        Serial.println("[System] 插电模式系统初始化就绪，进入常驻运行 loop。");
    }
    else {
        // ==================== 极致省电模式分支 (电池供电，深睡眠循环) ====================
        Serial.println("[System] 检测到工作在【电池供电模式】(极致省电启动)，执行单次采集上报...");
        
        // A. 采集传感器数据
        Sensor::init();
        float temp = -999.0f;
        float humi = -999.0f;
        bool read_ok = Sensor::read(&temp, &humi);
        
        if (read_ok) {
            Serial.printf("[System] 传感器读取成功: 温度: %.2f °C, 湿度: %.2f %%\n", temp, humi);
            global_last_temp = temp;
            global_last_humi = humi;
            global_sensor_ready = true;
            global_last_read_time = millis();
        } else {
            Serial.println("[System] 警告: 传感器数据读取失败！");
        }

        // B. 快速连接 Wi-Fi 并上报
        bool enter_remote_config = false;
        if (WiFiHeal::quick_connect_wifi()) {
            // 连接成功，执行数据上报，并接收服务器是否下发了远程配置指令
            HttpClient::post_data(temp, humi, enter_remote_config);
        } else {
            Serial.println("[System] 快速连接 Wi-Fi 失败，数据已存入 RAM 缓存，等待下个周期唤醒补发。");
            HttpClient::post_data(temp, humi, enter_remote_config);
        }

        // C. 根据服务器响应决定是进入深度睡眠，还是唤醒 Web 服务器保持在线配置
        if (enter_remote_config) {
            // ==================== 接收到配置唤醒分支 ====================
            Serial.println("[System] 收到服务器下发的远程配置唤醒指令！");
            Serial.println("[System] 设备将保持 Wi-Fi 连网在线，并启动局域网 Web 服务器以进行配置。");
            run_config_web_server = true;
            config_mode_start_time = millis();
            last_web_visit_time = millis();

            // 启动局域网 STA Web 服务器
            WebConfig::start_sta_server();
        } else {
            // ==================== 正常深度睡眠分支 ====================
            uint64_t sleep_us = (uint64_t)global_report_interval_ms * 1000;
            Serial.printf("[System] 本轮数据处理完毕。准备进入 Deep Sleep (深睡眠)，时长: %lu 秒\n", 
                          global_report_interval_ms / 1000);
            Serial.flush();
            esp_deep_sleep(sleep_us);
        }
    }
}

void loop() {
    // 1. 硬件看门狗喂狗
    esp_task_wdt_reset();

    // 2. 情况一：处于 Web 网页配置服务轮询期 (AP 模式或被远程唤醒的局域网配置)
    if (run_config_web_server) {
        if (WiFiHeal::is_in_ap_mode()) {
            // AP 模式下：轮询 DNS 与 Web 网页请求
            bool config_submitted = WebConfig::handle();
            if (config_submitted) {
                Serial.println("[System] 网页端提交了配置，系统即将重启以应用！");
                Serial.flush();
                delay(500);
                ESP.restart();
            }
        } else {
            // STA 模式下 (即通过上报接收到服务器唤醒配置指令后)：轮询本地 Web Server 请求
            bool config_submitted = WebConfig::handle_sta();
            if (config_submitted) {
                Serial.println("[System] 局域网提交了配置更新，系统即将重启以应用新参数！");
                Serial.flush();
                delay(500);
                ESP.restart();
            }
        }

        // 3. 自动超时休眠保护：仅在电池省电模式下，被远程唤醒无操作时，强制重返休眠，保护电池电量
        if (global_low_power_mode) {
            unsigned long now = millis();
            if (now - last_web_visit_time > CONFIG_MODE_TIMEOUT_MS) {
                Serial.printf("[System] 配置模式无操作已达 %lu 秒超时限制，自动重新进入 Deep Sleep...\n", 
                              CONFIG_MODE_TIMEOUT_MS / 1000);
                Serial.flush();
                
                // 彻底断开 Wi-Fi 射频以节省电量
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                delay(100);
                
                uint64_t sleep_us = (uint64_t)global_report_interval_ms * 1000;
                esp_deep_sleep(sleep_us);
            }
        }

        delay(5); // 稍微延时，让出 CPU
        return;
    }

    // 4. 情况二：常驻在线模式 (电源供电，未启用省电模式)
    if (!global_low_power_mode) {
        // 运行网络自愈状态机 (维护长连接以及处理可能提交的局域网网页配置更新)
        WiFiHeal::handle();

        // 挂起温湿度采集和定时上报直到周期触发
        if (WiFiHeal::is_in_ap_mode()) {
            delay(5);
            return;
        }

        unsigned long now = millis();
        if (now - last_report_time >= global_report_interval_ms) {
            float temp = 0.0f;
            float humi = 0.0f;

            Serial.println("\n[Loop] 周期任务启动: 开始采集温湿度数据...");
            
            if (Sensor::read(&temp, &humi)) {
                Serial.printf("[Loop] 传感器采集成功: 温度: %.2f °C, 湿度: %.2f %%\n", temp, humi);
                global_last_temp = temp;
                global_last_humi = humi;
                global_sensor_ready = true;
                global_last_read_time = millis();

                bool dummy = false;
                HttpClient::post_data(temp, humi, dummy);
            } else {
                Serial.println("[Loop] 警告: 传感器数据读取失败，发送心跳数据以同步 IP 与别名...");
                global_sensor_ready = false;
                bool dummy = false;
                HttpClient::post_data(-999.0f, -999.0f, dummy);
            }

            last_report_time = now;
        }

        delay(10);
        return;
    }

    // 5. 容错防御：如果没有运行配置服务且异常来到了 loop，强制调用深睡眠
    uint64_t sleep_us = (uint64_t)global_report_interval_ms * 1000;
    Serial.println("[System] 警告: 异常执行至 loop() 循环，强制进入 Deep Sleep...");
    Serial.flush();
    esp_deep_sleep(sleep_us);
}
