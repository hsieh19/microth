#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <HTTPClient.h>
#include "config.h"
#include "wifi_heal.h"

namespace HttpClient {

    /**
     * @brief 发送温湿度数据到 Python 后端
     * 
     * @param temp 温度物理量
     * @param humi 湿度物理量
     * @return true 上报成功且服务端验证通过 (HTTP 200/201)
     * @return false 上报失败 (超时、无网络或密钥错误)
     */
    bool post_data(float temp, float humi) {
        // 先行检查网络状态，不强行发起请求
        if (!WiFiHeal::is_connected()) {
            Serial.println("[HTTP] 上报终止: 当前 Wi-Fi 处于断开状态");
            return false;
        }

        HTTPClient http;

        // 1. 初始化 HTTPClient，指定 URL (使用全局 NVS 配置加载的值)
        if (!http.begin(global_server_url)) {
            Serial.println("[HTTP] 无法初始化 HTTP 客户端连接");
            return false;
        }

        // 2. 严格设置 4000ms 响应超时，防止由于服务端假死导致主线程挂起
        http.setTimeout(HTTP_TIMEOUT_MS);

        // 3. 配置 HTTP 标头：JSON 传输与 API Key 身份认证
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", global_api_key);

        // 4. 构建轻量化的 JSON 载荷
        String payload = "{\"device_id\":\"" + global_device_id + "\","
                         "\"temp\":" + String(temp, 2) + ","
                         "\"humi\":" + String(humi, 2) + "}";

        Serial.printf("[HTTP] 准备上报数据 -> URL: %s, 载荷: %s\n", global_server_url.c_str(), payload.c_str());

        // 5. 执行 POST 请求
        int http_code = http.POST(payload);

        bool success = false;

        // 6. 处理返回状态
        if (http_code > 0) {
            if (http_code == 200 || http_code == 201) {
                Serial.printf("[HTTP] 数据上报成功！响应状态码: %d\n", http_code);
                success = true;
            } else if (http_code == 403) {
                Serial.println("[HTTP] 严重错误: 权限被拒绝 (403)，请核对 NVS 配置中的 API_KEY 是否与服务端一致！");
            } else {
                Serial.printf("[HTTP] 服务端返回非预期错误，状态码: %d\n", http_code);
            }
            
            // 读取流式响应内容，确保连接被优雅关闭，防止 Socket 句柄泄露
            String response = http.getString();
            if (response.length() > 0) {
                Serial.printf("[HTTP] 服务端应答: %s\n", response.c_str());
            }
        } else {
            // 请求层面出错，如 DNS 解析失败、连接超时或主机不可达
            Serial.printf("[HTTP] 网络请求出错: %s\n", http.errorToString(http_code).c_str());
        }

        // 7. 关闭 HTTP 连接，回收连接池资源
        http.end();
        return success;
    }
}

#endif // HTTP_CLIENT_H
