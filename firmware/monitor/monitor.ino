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

// 记录上一次数据上报的时间戳
unsigned long last_report_time = 0;

// ==================== 传感器最新读数全局缓存定义 ====================
float global_last_temp = 0.0f;
float global_last_humi = 0.0f;
bool global_sensor_ready = false;
unsigned long global_last_read_time = 0;

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

    // 3. 初始化 SHT40 传感器 (I2C)
    Sensor::init();
    // 开机预读取一次传感器，让 Web 配置页能立刻显示读数
    float temp_init = 0.0f;
    float humi_init = 0.0f;
    if (Sensor::read(&temp_init, &humi_init)) {
        global_last_temp = temp_init;
        global_last_humi = humi_init;
        global_sensor_ready = true;
        global_last_read_time = millis();
    }

    // 4. 初始化 Wi-Fi 自愈与状态机 (触发 STA 首次连接流程)
    WiFiHeal::init();

    // 初始化上报时间戳 (开机 5 秒后触发第一次数据上报，之后按配置周期上报)
    last_report_time = millis() - global_report_interval_ms + 5000;
    
    Serial.println("[System] 系统初始化就绪，进入主循环。");
}

void loop() {
    // 1. 硬件看门狗喂狗
    esp_task_wdt_reset();

    // 2. 运行网络自愈与配网状态机 (处理 STA 数据流转，或是 AP 配网服务器轮询)
    WiFiHeal::handle();

    // 3. 若当前处于 AP 配网状态，则挂起温湿度测量及上报逻辑，优先响应配网网页请求
    if (WiFiHeal::is_in_ap_mode()) {
        delay(5); // 微小延迟，让出系统给 Web 服务器处理
        return;
    }

    // 4. 非阻塞定时采集并上报数据 (正常工作模式)
    unsigned long now = millis();
    if (now - last_report_time >= global_report_interval_ms) {
        float temp = 0.0f;
        float humi = 0.0f;

        Serial.println("\n[Loop] 周期任务启动: 开始采集温湿度数据...");
        
        // 读取传感器
        if (Sensor::read(&temp, &humi)) {
            Serial.printf("[Loop] 传感器采集成功: 温度: %.2f °C, 湿度: %.2f %%\n", temp, humi);
            
            // 更新全局缓存供 Web 页展示
            global_last_temp = temp;
            global_last_humi = humi;
            global_sensor_ready = true;
            global_last_read_time = millis();

            // 上报数据 (如果 Wi-Fi 连接则上报，否则会缓存等待下个周期)
            HttpClient::post_data(temp, humi);
        } else {
            Serial.println("[Loop] 警告: 传感器数据读取失败，发送心跳数据以同步 IP 与别名...");
            global_sensor_ready = false;
            // 使用错误状态特殊值 -999.0f 进行心跳上报，保障本地 IP 与别名的注册同步
            HttpClient::post_data(-999.0f, -999.0f);
        }

        // 更新上报时间戳，无论成功与否均进入下一等待周期
        last_report_time = now;
    }

    // 5. 让出 CPU 资源给 FreeRTOS 后台任务，降低功耗，保证看门狗稳定运行
    delay(10);
}
