#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== 默认硬编码配置 (当 NVS 为空时作为退避默认值) ====================
#define DEFAULT_WIFI_SSID     "Your_WiFi_SSID"
#define DEFAULT_WIFI_PASSWORD "Your_WiFi_Password"
#define DEFAULT_SERVER_URL    "http://192.168.1.100:8000/api/data"
#define DEFAULT_API_KEY       "your-32-char-secret-key-here"
#define DEFAULT_DEVICE_ID     "esp32-01"
#define DEFAULT_DEVICE_NAME   "默认温湿度设备"
#define DEFAULT_SAMPLE_INTERVAL_MS 30000
#define DEFAULT_REPORT_INTERVAL_MS 300000

#define FIRMWARE_VERSION     "1.2.1"
#define DEFAULT_SENSOR_ALERT_ENABLED true
#define DEFAULT_LOW_POWER_MODE false

// ==================== 运行期全局变量 (启动时自 NVS 闪存中加载) ====================
extern String global_wifi_ssid;
extern String global_wifi_password;
extern String global_server_url;
extern String global_api_key;
extern String global_device_id;
extern String global_device_name;
extern unsigned long global_sample_interval_ms;
extern unsigned long global_report_interval_ms;
extern bool global_sensor_alert_enabled;
extern bool global_low_power_mode;

// ==================== 硬件级常量配置 (与物理接线或系统死线相关，不建议开放网页修改) ====================
// 硬件看门狗超时时间 (秒)
const uint32_t WDT_TIMEOUT_SEC = 15;

// Wi-Fi 连续断连最大忍受时间 (毫秒)，超出后切换回 AP 模式配网，默认 5 分钟 (300000ms)
const unsigned long WIFI_MAX_DISCONNECT_MS = 300000;

// HTTP 请求超时时间 (毫秒)
const int HTTP_TIMEOUT_MS = 4000;

// 极致省电模式相关常量
const int BOOT_PIN = 9;                           // BOOT 引脚 (GPIO 9)，用于手动强制触发配网模式
const unsigned long CONFIG_MODE_TIMEOUT_MS = 300000; // 配置模式无操作自动休眠超时时间 (5分钟)
const unsigned long STA_CONNECT_TIMEOUT_MS = 15000;  // 快速联网最大等待时间 (15秒)

// I2C 硬件配置
const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;

// ==================== 传感器最新读数全局缓存 (供网页展示) ====================
extern float global_last_temp;
extern float global_last_humi;
extern bool global_sensor_ready;
extern unsigned long global_last_read_time;
extern unsigned long last_web_visit_time;

#endif // CONFIG_H
