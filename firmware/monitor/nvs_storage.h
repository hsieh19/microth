#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include <Preferences.h>
#include "config.h"

namespace NvsStorage {

    // NVS 命名空间定义
    const char* const NAMESPACE = "ths-monitor";

    /**
     * @brief 从 NVS 闪存中加载所有配置。如果某项配置尚未保存，则使用默认值。
     */
    void load_configs() {
        Preferences prefs;
        
        // 以只读模式打开命名空间
        prefs.begin(NAMESPACE, true);

        // 加载 Wi-Fi SSID
        global_wifi_ssid = prefs.getString("wifi_ssid", DEFAULT_WIFI_SSID);
        // 加载 Wi-Fi 密码
        global_wifi_password = prefs.getString("wifi_pass", DEFAULT_WIFI_PASSWORD);
        // 加载服务器 URL
        global_server_url = prefs.getString("server_url", DEFAULT_SERVER_URL);
        // 加载 API Key
        global_api_key = prefs.getString("api_key", DEFAULT_API_KEY);
        // 加载设备 ID
        global_device_id = prefs.getString("device_id", DEFAULT_DEVICE_ID);
        // 加载设备别名名称
        global_device_name = prefs.getString("device_name", DEFAULT_DEVICE_NAME);
        // 加载上报周期 (以秒为单位读取，然后转换为毫秒。若不存在则用毫秒默认值转换为秒保存/读取)
        uint32_t interval_sec = prefs.getUInt("interval_sec", DEFAULT_REPORT_INTERVAL_MS / 1000);
        global_report_interval_ms = (unsigned long)interval_sec * 1000;
        // 加载传感器未连接报警开关
        global_sensor_alert_enabled = prefs.getBool("sensor_alert", DEFAULT_SENSOR_ALERT_ENABLED);
        // 加载极致省电工作模式开关
        global_low_power_mode = prefs.getBool("low_power", DEFAULT_LOW_POWER_MODE);

        prefs.end();

        Serial.println("[NVS] 配置加载完毕：");
        Serial.printf("  SSID: %s\n", global_wifi_ssid.c_str());
        Serial.printf("  Server URL: %s\n", global_server_url.c_str());
        Serial.printf("  Device ID: %s\n", global_device_id.c_str());
        Serial.printf("  Device Name: %s\n", global_device_name.c_str());
        // S2 修复：API Key 以掩码形式输出，防止明文泄露至串口监视器
        String masked_key = global_api_key.length() >= 4
                            ? (global_api_key.substring(0, 4) + String("****"))
                            : "****";
        Serial.printf("  API Key: %s\n", masked_key.c_str());
        Serial.printf("  上报周期: %lu 毫秒\n", global_report_interval_ms);
        Serial.printf("  传感器报警: %s\n", global_sensor_alert_enabled ? "已启用" : "已禁用");
        Serial.printf("  极致省电模式: %s\n", global_low_power_mode ? "电池供电 (深睡眠)" : "电源供电 (常驻在线)");

    }

    /**
     * @brief 保存新配置到 NVS 闪存，并更新运行期的全局配置变量
     */
    void save_configs(String ssid, String pass, String url, String key, String dev_id, String dev_name, uint32_t interval_sec, bool sensor_alert, bool low_power) {
        Preferences prefs;
        
        // 以读写模式打开命名空间
        prefs.begin(NAMESPACE, false);

        prefs.putString("wifi_ssid", ssid);
        prefs.putString("wifi_pass", pass);
        prefs.putString("server_url", url);
        prefs.putString("api_key", key);
        prefs.putString("device_id", dev_id);
        prefs.putString("device_name", dev_name);
        prefs.putUInt("interval_sec", interval_sec);
        prefs.putBool("sensor_alert", sensor_alert);
        prefs.putBool("low_power", low_power);

        prefs.end();

        // 同时更新当前的全局运行变量，方便免重启测试
        global_wifi_ssid = ssid;
        global_wifi_password = pass;
        global_server_url = url;
        global_api_key = key;
        global_device_id = dev_id;
        global_device_name = dev_name;
        global_report_interval_ms = (unsigned long)interval_sec * 1000;
        global_sensor_alert_enabled = sensor_alert;
        global_low_power_mode = low_power;

        Serial.println("[NVS] 新配置已成功持久化写入 NVS 闪存！");
    }

    /**
     * @brief 清除 NVS 中保存的所有配置项，用于恢复出厂设置
     */
    void clear_configs() {
        Preferences prefs;
        prefs.begin(NAMESPACE, false);
        prefs.clear();
        prefs.end();
        Serial.println("[NVS] 已清空 NVS 中的所有温湿度系统配置！");
    }
}

#endif // NVS_STORAGE_H
