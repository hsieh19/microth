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
unsigned long global_sample_interval_ms;
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

// ==================== 极致省电与合并上报全局变量 ====================
unsigned long last_web_visit_time = 0;       // 上一次网页访问时间戳 (对应 config.h 声明)
bool run_config_web_server = false;          // 是否在后台常驻运行网页配置服务
unsigned long config_mode_start_time = 0;    // 开启配置模式的时间戳

// 历史合并数据 RTC 慢速内存缓存，用于电池 Deep Sleep 离线缓存
#define MAX_RTC_CACHE 60
static RTC_DATA_ATTR float rtc_temp_cache[MAX_RTC_CACHE];
static RTC_DATA_ATTR float rtc_humi_cache[MAX_RTC_CACHE];
static RTC_DATA_ATTR uint32_t rtc_offset_sec_cache[MAX_RTC_CACHE];
static RTC_DATA_ATTR int rtc_cache_count = 0;

// 常驻插电在线模式下的计时器变量
unsigned long last_sample_time_plugged = 0;
unsigned long last_report_time_plugged = 0;

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
        Serial.printf("[System] 看门狗已启用，超时时间: %d 秒\n", WDT_TIMEOUT_SEC);
    } else {
        Serial.println("[System] 错误: 看门狗初始化失败！");
    }
}

static bool wdt_added = false;
void enable_wdt_for_current_task() {
    if (wdt_added) return;
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_OK) {
        Serial.println("[System] 成功将当前 loop 主线程加入看门狗监控。");
        wdt_added = true;
    } else {
        Serial.println("[System] 警告: 当前主线程加入看门狗监控失败。");
    }
}

/**
 * @brief 安全且彻底地注销看门狗并进入 Deep Sleep (微秒级)
 */
void enter_deep_sleep(uint64_t sleep_us) {
    Serial.println("[System] 正在注销任务看门狗并关闭 Wi-Fi 射频...");
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
        // 兼容 ESP32 Arduino Core 3.0+
        esp_task_wdt_delete(NULL);
        esp_task_wdt_deinit();
    #else
        // 兼容 ESP32 Arduino Core 2.x
        esp_task_wdt_delete(NULL);
    #endif

    // 彻底断开 Wi-Fi 射频以节省电量并停止所有挂起的网络事务
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    Serial.printf("[System] 进入 Deep Sleep (深睡眠)，时长: %llu 秒...\n", sleep_us / 1000000ULL);
    Serial.flush();
    esp_deep_sleep(sleep_us);
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
        
        Sensor::init();
        
        // 强制初始化 Wi-Fi 状态机与自愈网络（和原本的系统工作逻辑相同）
        WiFiHeal::init();
        
        // 重置常驻模式缓存和计时器
        rtc_cache_count = 0;
        last_sample_time_plugged = millis();
        last_report_time_plugged = millis();
        Serial.println("[System] 插电模式系统初始化就绪，进入常驻运行 loop。");
    }
    else {
        // ==================== 极致省电模式分支 (电池供电，深睡眠循环) ====================
        Serial.println("[System] 检测到工作在【电池供电模式】(极致省电启动)，执行单次采集...");

        // A0. [H3 修复] 检查唤醒原因：首次上电/手动复位时 RTC_DATA_ATTR 值未定义，强制清零防止随机垃圾值导致逻辑混乱
        if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
            rtc_cache_count = 0;
            Serial.println("[System] 检测到非定时器唤醒（首次上电/手动复位），RTC 缓存已强制清零。");
        }

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

        // B. [H2 修复] 仅在传感器读取成功时才将数据追加存入 RTC 缓存，防止 -999 哨兵值污染历史数据库
        if (read_ok) {
            if (rtc_cache_count < MAX_RTC_CACHE) {
                rtc_temp_cache[rtc_cache_count] = temp;
                rtc_humi_cache[rtc_cache_count] = humi;
                rtc_offset_sec_cache[rtc_cache_count] = 0; // 最新一笔偏移是 0 秒
                rtc_cache_count++;
                Serial.printf("[System] 数据已存入 RTC 缓存，当前积压: %d/%d\n", rtc_cache_count, MAX_RTC_CACHE);
            } else {
                // 缓存队列已满，覆盖最早的数据
                Serial.println("[System] 警告: RTC 缓存队列已满，覆盖最早的数据");
                for (int i = 1; i < MAX_RTC_CACHE; i++) {
                    rtc_temp_cache[i-1] = rtc_temp_cache[i];
                    rtc_humi_cache[i-1] = rtc_humi_cache[i];
                    rtc_offset_sec_cache[i-1] = rtc_offset_sec_cache[i];
                }
                rtc_temp_cache[MAX_RTC_CACHE - 1] = temp;
                rtc_humi_cache[MAX_RTC_CACHE - 1] = humi;
                rtc_offset_sec_cache[MAX_RTC_CACHE - 1] = 0;
            }
        }

        // C. [H1 修复] 先更新旧记录的年龄偏移，再判断是否上报
        //    - 只对 rtc_cache_count-1 条旧记录累加，跳过刚追加的最新记录（其 offset 应保持为 0）
        //    - 必须在上报（可能清零缓存）之前执行，避免清零后变成无效空循环
        uint32_t sample_sec = global_sample_interval_ms / 1000;
        uint32_t interval_sec = global_report_interval_ms / 1000;
        for (int i = 0; i < rtc_cache_count - 1; i++) {
            rtc_offset_sec_cache[i] += sample_sec;
        }

        // D. 根据采集数与周期设定判定是否触发连网批量上报
        uint32_t report_count_threshold = interval_sec / sample_sec;
        if (report_count_threshold == 0) report_count_threshold = 1;

        bool should_report = (rtc_cache_count >= report_count_threshold) || (rtc_cache_count >= MAX_RTC_CACHE - 1);
        bool enter_remote_config = false;

        if (should_report) {
            Serial.printf("[System] 达到数据上报阈值 (%d/%d)，开始快速连接 Wi-Fi...\n", rtc_cache_count, report_count_threshold);
            if (WiFiHeal::quick_connect_wifi()) {
                // 连网成功，合并上报 RTC 缓存中记录
                if (HttpClient::post_bulk_data(rtc_temp_cache, rtc_humi_cache, rtc_offset_sec_cache, rtc_cache_count, enter_remote_config)) {
                    Serial.println("[System] 批量合并历史数据上报成功！重置 RTC 缓存记录。");
                    rtc_cache_count = 0;
                } else {
                    Serial.println("[System] 合并上报失败，数据暂时留在 RTC 缓存，待下个周期重试。");
                }
            } else {
                Serial.println("[System] 快速连接 Wi-Fi 失败，数据暂时在 RTC 缓存保留。");
            }
        } else {
            Serial.printf("[System] 未达到上报阈值 (%d/%d)，跳过网络直接进入休眠。\n", rtc_cache_count, report_count_threshold);
        }

        // E. 检查是否收到远程配置唤醒
        if (enter_remote_config) {
            Serial.println("[System] 收到服务器下发的远程配置唤醒指令！");
            Serial.println("[System] 设备将保持 Wi-Fi 连网在线，并启动局域网 Web 服务器以进行配置。");
            run_config_web_server = true;
            config_mode_start_time = millis();
            last_web_visit_time = millis();
            WebConfig::start_sta_server();
        } else {
            uint64_t sleep_us = (uint64_t)global_sample_interval_ms * 1000;
            enter_deep_sleep(sleep_us);
        }
    }
}

void loop() {
    // 1. 硬件看门狗启用与喂狗
    enable_wdt_for_current_task();
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
                
                uint64_t sleep_us = (uint64_t)global_report_interval_ms * 1000;
                enter_deep_sleep(sleep_us);
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
 
        // A. 采集周期触发：每隔 global_sample_interval_ms 进行一次本地采集并缓存
        if (now - last_sample_time_plugged >= global_sample_interval_ms) {
            float temp = 0.0f;
            float humi = 0.0f;
            bool read_ok = Sensor::read(&temp, &humi);
 
            if (read_ok) {
                Serial.printf("[Loop] 传感器采集成功: 温度: %.2f °C, 湿度: %.2f %%\n", temp, humi);
                global_last_temp = temp;
                global_last_humi = humi;
                global_sensor_ready = true;
                global_last_read_time = now;
            } else {
                Serial.println("[Loop] 警告: 传感器数据读取失败！");
                temp = -999.0f;
                humi = -999.0f;
                global_sensor_ready = false;
            }
 
            // 缓存到内存中，并将 offset 字段暂时存为采集时的绝对 millis() 时间戳
            if (rtc_cache_count < MAX_RTC_CACHE) {
                rtc_temp_cache[rtc_cache_count] = temp;
                rtc_humi_cache[rtc_cache_count] = humi;
                rtc_offset_sec_cache[rtc_cache_count] = now; // 存入当前的 millis()
                rtc_cache_count++;
            } else {
                // 缓存满则覆盖最早记录
                for (int i = 1; i < MAX_RTC_CACHE; i++) {
                    rtc_temp_cache[i-1] = rtc_temp_cache[i];
                    rtc_humi_cache[i-1] = rtc_humi_cache[i];
                    rtc_offset_sec_cache[i-1] = rtc_offset_sec_cache[i];
                }
                rtc_temp_cache[MAX_RTC_CACHE - 1] = temp;
                rtc_humi_cache[MAX_RTC_CACHE - 1] = humi;
                rtc_offset_sec_cache[MAX_RTC_CACHE - 1] = now;
            }
 
            last_sample_time_plugged = now;
        }
 
        // B. 上报周期触发：每隔 global_report_interval_ms 打包上报全部合并数据
        if (now - last_report_time_plugged >= global_report_interval_ms) {
            if (rtc_cache_count > 0 && WiFiHeal::is_connected()) {
                Serial.printf("\n[Loop] 上报任务启动: 准备打包上报缓存中 %d 条温湿度记录...\n", rtc_cache_count);
                
                // 转换 offset_sec：从采集时的绝对 millis() 转换为距离当前发送时间的相对秒差
                uint32_t send_offsets[MAX_RTC_CACHE];
                for (int i = 0; i < rtc_cache_count; i++) {
                    unsigned long sample_ms = rtc_offset_sec_cache[i];
                    unsigned long diff_ms = (now >= sample_ms) ? (now - sample_ms) : 0;
                    send_offsets[i] = diff_ms / 1000;
                }
 
                bool dummy = false;
                if (HttpClient::post_bulk_data(rtc_temp_cache, rtc_humi_cache, send_offsets, rtc_cache_count, dummy)) {
                    Serial.println("[Loop] 常驻合并数据上报成功！清空缓存。");
                    rtc_cache_count = 0;
                } else {
                    Serial.println("[Loop] 合并数据上报失败，继续在内存积压缓存。");
                }
            } else if (rtc_cache_count == 0) {
                // 容错：如果暂时没有任何采集数据，补发单点心跳
                bool dummy = false;
                HttpClient::post_data(-999.0f, -999.0f, dummy);
            }
 
            last_report_time_plugged = now;
        }
 
        delay(10);
        return;
    }

    // 5. 容错防御：如果没有运行配置服务且异常来到了 loop，强制调用深睡眠
    uint64_t sleep_us = (uint64_t)global_report_interval_ms * 1000;
    Serial.println("[System] 警告: 异常执行至 loop() 循环，强制进入 Deep Sleep...");
    Serial.flush();
    enter_deep_sleep(sleep_us);
}
