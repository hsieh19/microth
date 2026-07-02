# ESP32-C3 温湿度采集端固件 (Arduino版)

本目录包含温湿度监控系统 ESP32-C3 采集端的完整源代码。项目采用**模块化头文件设计**，可以直接用 Arduino IDE 打开。

## 📂 文件目录说明
* **[monitor.ino](file:///E:/AI%20Project/microth/firmware/monitor/monitor.ino)**：主控制文件。包含 `setup()` 和 `loop()`、硬件看门狗配置、上报时间片轮询控制。
* **[config.h](file:///E:/AI%20Project/microth/firmware/monitor/config.h)**：全局配置文件。在此处修改您的 Wi-Fi SSID、密码、服务器 API URL 和 `API_KEY` 鉴权秘钥。
* **[sensor.h](file:///E:/AI%20Project/microth/firmware/monitor/sensor.h)**：SHT40 传感器驱动。**零外部依赖**，直接使用 `Wire` 库通过 I2C 寄存器读取温湿度数据，内置 Sensirion 官方 CRC-8 数据校验和总线防锁死自愈逻辑（连续 5 次读取失败重新拉起 I2C）。
* **[wifi_heal.h](file:///E:/AI%20Project/microth/firmware/monitor/wifi_heal.h)**：Wi-Fi 连接及断网非阻塞自愈逻辑。若连续断连超过 5 分钟，自动重启 ESP32 协议栈进行彻底恢复。
* **[http_client.h](file:///E:/AI%20Project/microth/firmware/monitor/http_client.h)**：数据上报客户端。使用 `HTTPClient` 以 HTTP POST 上报 JSON 格式数据。具备以下加固设计：

  - 严格设置 4000ms 超时限制，防止网络丢包挂起。
  - 请求头中包含 `X-API-Key`，用于后端认证安全。

---

## 🛠 硬件接线参考
本项目在 ESP32-C3 引脚的基础上进行了特定物理配置：

| SHT40 传感器引脚 | ESP32-C3 开发板引脚 | 说明 |
| :--- | :--- | :--- |
| **VCC** | **3.3V** | 电源正极 |
| **GND** | **GND** | 接地 |
| **SDA** | **GPIO 8** | I2C 数据总线线 |
| **SCL** | **GPIO 9** | I2C 时钟总线线 |

---

## 🚀 使用指南 (Arduino IDE)

### 1. 软件准备
- 安装 [Arduino IDE](https://www.arduino.cc/en/software)。
- 在“首选项”中，将 ESP32 开发板源地址加入“附加开发板管理器网址”中：
  ```
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
  ```
- 进入“开发板管理器”，搜索并安装 **esp32** 开发板库（推荐版本 2.x 或 3.x，代码已实现兼容处理）。

### 2. 打开项目
1. 将 `firmware/monitor` 文件夹拷贝至您的 Arduino 项目目录。
2. 双击打开 `monitor.ino`，Arduino IDE 会自动在新标签页加载同目录下的所有 `.h` 头文件。
3. 在 `config.h` 中配置您的 Wi-Fi 信息与服务端配置：
   ```cpp
   const char* const WIFI_SSID     = "您的WiFi名称";
   const char* const WIFI_PASSWORD = "您的WiFi密码";
   const char* const SERVER_URL    = "http://[服务器内网IP]:8000/api/data";
   const char* const API_KEY       = "自定义32位秘钥";
   ```

### 3. 开发板设置与烧录
- **开发板选择**：`ESP32C3 Dev Module` (或根据您的板卡型号选择，如 `NodeMCU-32S2` 等)。
- **USB CDC On Boot**：若烧录或串口打印无反应，请在工具菜单中开启/关闭 `USB CDC On Boot` 选项。
- 点击 **Upload (上传)** 按钮编译并烧录。

---

## 🔍 调试与验证
打开“串口监视器”，将波特率设为 `115200`，在上电时您将看到以下调试日志：
```
==================================================
  轻量化温湿度监控系统 ESP32-C3 固件 v2.0 启动
==================================================
[System] 配置硬件看门狗...
[System] 看门狗已启用，超时时间: 15 秒
[Sensor] 初始化 I2C 总线...
[WiFi] 开始初始化 Wi-Fi 模块...
[System] 系统初始化就绪，进入主循环。
[WiFi] 网络连接成功！分配的 IP 地址: 192.168.1.101

[Loop] 周期任务启动: 开始采集温湿度数据...
[Loop] 传感器采集成功: 温度: 26.50 °C, 湿度: 55.20 %
[HTTP] 准备上报数据 -> URL: http://192.168.1.100:8000/api/data, 载荷: {"device_id":"esp32-01","temp":26.50,"humi":55.20}
[HTTP] 数据上报成功！响应状态码: 200
```
