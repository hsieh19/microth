#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== 默认硬编码配置 (当 NVS 为空时作为退避默认值) ====================
#define DEFAULT_WIFI_SSID     "Your_WiFi_SSID"
#define DEFAULT_WIFI_PASSWORD "Your_WiFi_Password"
#define DEFAULT_SERVER_URL    "http://192.168.1.100:8000/api/data"
#define DEFAULT_API_KEY       "your-32-char-secret-key-here"
#define DEFAULT_DEVICE_ID     "esp32-01"
#define DEFAULT_REPORT_INTERVAL_MS 60000

// ==================== 运行期全局变量 (启动时自 NVS 闪存中加载) ====================
extern String global_wifi_ssid;
extern String global_wifi_password;
extern String global_server_url;
extern String global_api_key;
extern String global_device_id;
extern unsigned long global_report_interval_ms;

// ==================== 硬件级常量配置 (与物理接线或系统死线相关，不建议开放网页修改) ====================
// 硬件看门狗超时时间 (秒)
const uint32_t WDT_TIMEOUT_SEC = 15;

// Wi-Fi 连续断连最大忍受时间 (毫秒)，超出后切换回 AP 模式配网，默认 5 分钟 (300000ms)
const unsigned long WIFI_MAX_DISCONNECT_MS = 300000;

// HTTP 请求超时时间 (毫秒)
const int HTTP_TIMEOUT_MS = 4000;

// I2C 硬件配置
const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;

#endif // CONFIG_H
