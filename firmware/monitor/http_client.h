#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <HTTPClient.h>
#include "config.h"
#include "wifi_heal.h"
#include "nvs_storage.h"

namespace HttpClient {

    struct SensorCache {
        float temp;
        float humi;
        unsigned long time_ms;
    };

    static const int MAX_CACHE_SIZE = 1500; // 缓存最大 1500 条 (约 25 小时数据)
    static SensorCache data_cache[MAX_CACHE_SIZE];
    static int cache_head = 0; // 下一个写入的索引
    static int cache_tail = 0; // 最早未发送的索引
    static int cache_count = 0; // 当前积压的条数

    /**
     * @brief 写入离线数据到 RAM 缓存中
     */
    void push_cache(float temp, float humi) {
        data_cache[cache_head] = {temp, humi, millis()};
        cache_head = (cache_head + 1) % MAX_CACHE_SIZE;
        if (cache_count < MAX_CACHE_SIZE) {
            cache_count++;
        } else {
            // 队列已满，覆盖最早的数据，将尾指针前移
            cache_tail = (cache_tail + 1) % MAX_CACHE_SIZE;
            Serial.println("[HTTP] 警告: 缓存队列已满，覆盖了最早的一条记录！");
        }
        Serial.printf("[HTTP] 掉线数据已存入 RAM 缓存，当前积压: %d/%d\n", cache_count, MAX_CACHE_SIZE);
    }

    /**
     * @brief 发送带有相对时间偏移的传感器数据到 Python 后端
     * 
     * @param temp 温度物理量
     * @param humi 湿度物理量
     * @param offset_sec 数据采集时距离当前发送时的相对时间差 (秒)
     * @return true 上报成功且服务端验证通过 (HTTP 200/201)
     * @return false 上报失败
     */
    bool post_data_with_offset(float temp, float humi, unsigned long offset_sec) {
        if (!WiFiHeal::is_connected()) {
            return false;
        }

        HTTPClient http;

        // 1. 初始化 HTTPClient，指定 URL
        if (!http.begin(global_server_url)) {
            Serial.println("[HTTP] 无法初始化 HTTP 客户端连接");
            return false;
        }

        // 2. 严格设置 4000ms 响应超时限制
        http.setTimeout(HTTP_TIMEOUT_MS);

        // 3. 配置 HTTP 标头
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", global_api_key);

        // 4. 构建带有 offset_sec 相对偏移时间的 JSON 载荷 (添加设备名称、IP 及警报开关配置)
        String local_ip = WiFi.localIP().toString();
        String sensor_alert_str = global_sensor_alert_enabled ? "true" : "false";
        String payload = "{\"device_id\":\"" + global_device_id + "\","
                         "\"device_name\":\"" + global_device_name + "\","
                         "\"device_ip\":\"" + local_ip + "\","
                         "\"temp\":" + String(temp, 2) + ","
                         "\"humi\":" + String(humi, 2) + ","
                         "\"sensor_alert_enabled\":" + sensor_alert_str + ","
                         "\"offset_sec\":" + String(offset_sec) + "}";

        Serial.printf("[HTTP] 准备向服务器上报数据... 当前系统上报周期: %lu 毫秒\n", global_report_interval_ms);

        // 5. 执行 POST 请求
        int http_code = http.POST(payload);
        bool success = false;

        // 6. 处理返回状态
        if (http_code > 0) {
            if (http_code == 200 || http_code == 201) {
                success = true;
                
                // 消耗并读取流式响应，保证连接被释放
                String response = http.getString();
                Serial.printf("[HTTP] 成功收到服务端响应 JSON: %s\n", response.c_str());
                
                // 原生解析上报周期 report_interval
                long new_interval_sec = -1;
                int ri_idx = response.indexOf("\"report_interval\"");
                if (ri_idx != -1) {
                    int colon_idx = response.indexOf(":", ri_idx);
                    if (colon_idx != -1) {
                        int val_start = colon_idx + 1;
                        while (val_start < response.length() && (response[val_start] == ' ' || response[val_start] == '\t')) {
                            val_start++;
                        }
                        int val_end = val_start;
                        while (val_end < response.length() && response[val_end] != ',' && response[val_end] != '}') {
                            val_end++;
                        }
                        String ri_str = response.substring(val_start, val_end);
                        ri_str.trim();
                        long parsed_val = ri_str.toInt();
                        if (parsed_val > 0) {
                            new_interval_sec = parsed_val;
                        }
                    }
                }

                // 原生解析 API 密钥 api_key
                String new_api_key = "";
                int ak_idx = response.indexOf("\"api_key\"");
                if (ak_idx != -1) {
                    int colon_idx = response.indexOf(":", ak_idx);
                    if (colon_idx != -1) {
                        int quote_start = response.indexOf("\"", colon_idx);
                        if (quote_start != -1) {
                            int quote_end = response.indexOf("\"", quote_start + 1);
                            if (quote_end != -1) {
                                new_api_key = response.substring(quote_start + 1, quote_end);
                            }
                        }
                    }
                }

                // 原生解析设备别名 device_name
                String new_device_name = "";
                int dn_idx = response.indexOf("\"device_name\"");
                if (dn_idx != -1) {
                    int colon_idx = response.indexOf(":", dn_idx);
                    if (colon_idx != -1) {
                        int quote_start = response.indexOf("\"", colon_idx);
                        if (quote_start != -1) {
                            int quote_end = response.indexOf("\"", quote_start + 1);
                            if (quote_end != -1) {
                                new_device_name = response.substring(quote_start + 1, quote_end);
                            }
                        }
                    }
                }

                // 比对当前配置是否发生变更并自动同步到 NVS
                uint32_t current_interval_sec = global_report_interval_ms / 1000;
                String target_key = (new_api_key.length() > 0) ? new_api_key : global_api_key;
                uint32_t target_interval_sec = (new_interval_sec > 0) ? (uint32_t)new_interval_sec : current_interval_sec;
                String target_name = (new_device_name.length() > 0) ? new_device_name : global_device_name;

                Serial.printf("[HTTP] 解析结果 -> 提取上报周期: %ld 秒, 提取 API Key: %s, 提取设备别名: %s\n", 
                    new_interval_sec, new_api_key.c_str(), new_device_name.c_str());

                if (target_key != global_api_key || target_interval_sec != current_interval_sec || target_name != global_device_name) {
                    Serial.println("[HTTP] 检测到系统配置变更，开始自动同步！");
                    Serial.printf("  API Key: %s -> %s\n", 
                        global_api_key.length() >= 4 ? (global_api_key.substring(0, 4) + "****").c_str() : "****", 
                        target_key.length() >= 4 ? (target_key.substring(0, 4) + "****").c_str() : "****");
                    Serial.printf("  上报周期: %d 秒 -> %d 秒\n", current_interval_sec, target_interval_sec);
                    Serial.printf("  设备别名: %s -> %s\n", global_device_name.c_str(), target_name.c_str());
                    
                    // 保存新参数到 NVS
                    NvsStorage::save_configs(
                        global_wifi_ssid,
                        global_wifi_password,
                        global_server_url,
                        target_key,
                        global_device_id,
                        target_name,
                        target_interval_sec,
                        global_sensor_alert_enabled
                    );
                }
            } else if (http_code == 403) {
                Serial.println("[HTTP] 严重错误: 权限被拒绝 (403)，请核对 API_KEY！");
            } else {
                Serial.printf("[HTTP] 服务端返回非预期错误，状态码: %d\n", http_code);
            }
        } else {
            Serial.printf("[HTTP] 网络请求出错: %s\n", http.errorToString(http_code).c_str());
        }

        http.end();
        return success;
    }

    /**
     * @brief 补发离线缓存数据中积压的所有记录
     */
    void send_cached_data() {
        if (cache_count == 0) return;
        if (!WiFiHeal::is_connected()) return;

        Serial.printf("[HTTP] 网络已就绪，开始补发离线缓存数据 (%d 条)...\n", cache_count);

        while (cache_count > 0) {
            // 实时检查 Wi-Fi 连接
            if (!WiFiHeal::is_connected()) {
                Serial.println("[HTTP] 补发中断: 网络连接再次断开");
                break;
            }

            SensorCache item = data_cache[cache_tail];
            unsigned long now = millis();
            
            // 计算自采集以来流逝的时间毫秒数，支持 millis() 约 50 天溢出回零逻辑
            unsigned long offset_ms = 0;
            if (now >= item.time_ms) {
                offset_ms = now - item.time_ms;
            } else {
                offset_ms = (0xFFFFFFFF - item.time_ms) + now + 1;
            }
            unsigned long offset_sec = offset_ms / 1000;

            // 调用带时间偏移的发送接口进行数据补发
            if (post_data_with_offset(item.temp, item.humi, offset_sec)) {
                // 发送成功，将此条数据出队，修改尾指针
                cache_tail = (cache_tail + 1) % MAX_CACHE_SIZE;
                cache_count--;
                Serial.printf("[HTTP] 缓存数据补发成功！当前积压剩余: %d/%d\n", cache_count, MAX_CACHE_SIZE);
                delay(200); // 间隔避让，降低对服务端瞬间请求的压力
            } else {
                Serial.println("[HTTP] 补发失败，等待下个周期继续尝试");
                break;
            }
        }
    }

    /**
     * @brief 供主循环调用的统一发送接口 (封装了掉线自动缓存、自动链式补发)
     */
    bool post_data(float temp, float humi) {
        // A. 掉线状态直接入队，避免请求超时挂起系统
        if (!WiFiHeal::is_connected()) {
            Serial.println("[HTTP] 上报终止: 当前 Wi-Fi 处于断开状态，数据已存入缓存。");
            push_cache(temp, humi);
            return false;
        }

        // B. 尝试进行本次实时数据上报
        if (post_data_with_offset(temp, humi, 0)) {
            Serial.println("[HTTP] 实时数据上报成功！");
            // 实时数据发送成功，开始补发积压的历史缓存
            send_cached_data();
            return true;
        } else {
            // C. 若实时上报失败（例如服务器临时维护故障，但 Wi-Fi 本身已连通）
            Serial.println("[HTTP] 实时上报连接出错，数据已存入缓存。");
            push_cache(temp, humi);
            return false;
        }
    }
}

#endif // HTTP_CLIENT_H
