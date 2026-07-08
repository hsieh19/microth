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

    static const int MAX_CACHE_SIZE = 1500;
    static SensorCache data_cache[MAX_CACHE_SIZE];
    static int cache_head = 0;
    static int cache_tail = 0;
    static int cache_count = 0;

    void push_cache(float temp, float humi) {
        data_cache[cache_head] = {temp, humi, millis()};
        cache_head = (cache_head + 1) % MAX_CACHE_SIZE;
        if (cache_count < MAX_CACHE_SIZE) {
            cache_count++;
        } else {
            cache_tail = (cache_tail + 1) % MAX_CACHE_SIZE;
            Serial.println("[HTTP] 警告: 缓存队列已满，覆盖了最早的一条记录！");
        }
        Serial.printf("[HTTP] 掉线数据已存入 RAM 缓存，当前积压: %d/%d\n", cache_count, MAX_CACHE_SIZE);
    }

    void parse_and_sync_response(const String& response, bool& out_enter_config) {
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

        int ecm_idx = response.indexOf("\"enter_config_mode\"");
        if (ecm_idx != -1) {
            int colon_idx = response.indexOf(":", ecm_idx);
            if (colon_idx != -1) {
                int val_start = colon_idx + 1;
                while (val_start < response.length() && (response[val_start] == ' ' || response[val_start] == '\t')) {
                    val_start++;
                }
                if (response.substring(val_start, val_start + 4) == "true") {
                    out_enter_config = true;
                }
            }
        }

        uint32_t current_interval_sec = global_report_interval_ms / 1000;
        String target_key = (new_api_key.length() > 0) ? new_api_key : global_api_key;
        uint32_t target_interval_sec = (new_interval_sec > 0) ? (uint32_t)new_interval_sec : current_interval_sec;
        String target_name = (new_device_name.length() > 0) ? new_device_name : global_device_name;

        Serial.printf("[HTTP] 解析结果 -> 提取上报周期: %ld 秒, 提取 API Key: %s, 提取设备别名: %s, 远程配置唤醒: %s\n",
            new_interval_sec, new_api_key.c_str(), new_device_name.c_str(), out_enter_config ? "是" : "否");

        if (target_key != global_api_key || target_interval_sec != current_interval_sec || target_name != global_device_name) {
            Serial.println("[HTTP] 检测到系统配置变更，开始自动同步！");
            Serial.printf("  API Key: %s -> %s\n",
                global_api_key.length() >= 4 ? (global_api_key.substring(0, 4) + "****").c_str() : "****",
                target_key.length() >= 4 ? (target_key.substring(0, 4) + "****").c_str() : "****");
            Serial.printf("  上报周期: %d 秒 -> %d 秒\n", current_interval_sec, target_interval_sec);
            Serial.printf("  设备别名: %s -> %s\n", global_device_name.c_str(), target_name.c_str());
            NvsStorage::save_configs(
                global_wifi_ssid,
                global_wifi_password,
                global_server_url,
                target_key,
                global_device_id,
                target_name,
                global_sample_interval_ms / 1000,
                target_interval_sec,
                global_sensor_alert_enabled,
                global_low_power_mode
            );
        }
    }

    bool post_data_with_offset(float temp, float humi, unsigned long offset_sec, bool& out_enter_config) {
        if (!WiFiHeal::is_connected()) {
            return false;
        }

        HTTPClient http;
        if (!http.begin(global_server_url)) {
            Serial.println("[HTTP] 无法初始化 HTTP 客户端连接");
            return false;
        }

        http.setTimeout(HTTP_TIMEOUT_MS);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", global_api_key);

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

        int http_code = http.POST(payload);
        bool success = false;

        if (http_code > 0) {
            if (http_code == 200 || http_code == 201) {
                success = true;
                String response = http.getString();
                Serial.printf("[HTTP] 成功收到服务端响应 JSON: %s\n", response.c_str());
                parse_and_sync_response(response, out_enter_config);
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

    void send_cached_data() {
        if (cache_count == 0) return;
        if (!WiFiHeal::is_connected()) return;

        Serial.printf("[HTTP] 网络已就绪，开始补发离线缓存数据 (%d 条)...\n", cache_count);

        while (cache_count > 0) {
            if (!WiFiHeal::is_connected()) {
                Serial.println("[HTTP] 补发中断: 网络连接再次断开");
                break;
            }

            SensorCache item = data_cache[cache_tail];
            unsigned long now = millis();

            unsigned long offset_ms = 0;
            if (now >= item.time_ms) {
                offset_ms = now - item.time_ms;
            } else {
                offset_ms = (0xFFFFFFFF - item.time_ms) + now + 1;
            }
            unsigned long offset_sec = offset_ms / 1000;

            bool dummy_config = false;
            if (post_data_with_offset(item.temp, item.humi, offset_sec, dummy_config)) {
                cache_tail = (cache_tail + 1) % MAX_CACHE_SIZE;
                cache_count--;
                Serial.printf("[HTTP] 缓存数据补发成功！当前积压剩余: %d/%d\n", cache_count, MAX_CACHE_SIZE);
                delay(200);
            } else {
                Serial.println("[HTTP] 补发失败，等待下个周期继续尝试");
                break;
            }
        }
    }

    bool post_bulk_data(float* temps, float* humis, uint32_t* offsets, int count, bool& out_enter_config) {
        if (count <= 0) return true;
        if (!WiFiHeal::is_connected()) {
            return false;
        }

        HTTPClient http;
        if (!http.begin(global_server_url)) {
            Serial.println("[HTTP] 无法初始化 HTTP 客户端连接");
            return false;
        }

        http.setTimeout(HTTP_TIMEOUT_MS);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", global_api_key);

        String local_ip = WiFi.localIP().toString();
        String sensor_alert_str = global_sensor_alert_enabled ? "true" : "false";

        String payload = "{\"device_id\":\"" + global_device_id + "\","
                         "\"device_name\":\"" + global_device_name + "\","
                         "\"device_ip\":\"" + local_ip + "\","
                         "\"sensor_alert_enabled\":" + sensor_alert_str + ","
                         "\"records\":[";

        for (int i = 0; i < count; i++) {
            payload += "{\"temp\":" + String(temps[i], 2) + ","
                       "\"humi\":" + String(humis[i], 2) + ","
                       "\"offset_sec\":" + String(offsets[i]) + "}";
            if (i < count - 1) {
                payload += ",";
            }
        }
        payload += "]}";

        Serial.printf("[HTTP] 准备向服务器合并上报 %d 条批量数据...\n", count);

        int http_code = http.POST(payload);
        bool success = false;

        if (http_code > 0) {
            if (http_code == 200 || http_code == 201) {
                success = true;
                String response = http.getString();
                Serial.printf("[HTTP] 成功收到服务端响应 JSON: %s\n", response.c_str());
                parse_and_sync_response(response, out_enter_config);
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

    bool post_data(float temp, float humi, bool& out_enter_config) {
        if (!WiFiHeal::is_connected()) {
            Serial.println("[HTTP] 上报终止: 当前 Wi-Fi 处于断开状态，数据已存入缓存。");
            push_cache(temp, humi);
            return false;
        }

        if (post_data_with_offset(temp, humi, 0, out_enter_config)) {
            Serial.println("[HTTP] 实时数据上报成功！");
            send_cached_data();
            return true;
        } else {
            Serial.println("[HTTP] 实时上报连接出错，数据已存入缓存。");
            push_cache(temp, humi);
            return false;
        }
    }
}

#endif // HTTP_CLIENT_H
