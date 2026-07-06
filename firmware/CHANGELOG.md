# Changelog - Firmware

All notable changes to the single-chip firmware project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [1.1.0] - 2026-07-06

### Added
- **传感器异常心跳上报**：在传感器读取故障或未连接时，固件仍坚持定期发送上报包（温湿度使用占位值 `-999.0f`），以确保设备的局域网 IP 与别名能在第一时间注册上报到服务器。
- **本地报警配置开关**：
  - 在本地 AP 门户配置网页中新增了“传感器断连报警 (飞书推送)”的下拉启用选项。
  - 支持将该开关配置参数持久化写入本地 NVS 闪存中，并在每次启动时自动加载。
  - 上报载荷中新增 `"sensor_alert_enabled"` 字段，通知服务器是否对当前设备的传感器故障发送通知。

## [1.0.0] - 2026-07-06

### Added
- **上报与解析调试日志**：增加上报前上报周期、接收原始响应 JSON 以及同步解析结果的详细 `Serial.printf` 日志输出，极大方便串口排查通信和解析行为。
- **本地 IP 上报**：在温湿度上报 payload 载荷中增加对 `WiFi.localIP()` 局域网 IP 地址的自动获取与发送，支持服务端大屏实时展示。
- **设备别名同步**：
  - Web 配置页新增 `设备别名 (Device Name)` 的表单配置项。
  - 扩充了本地 NVS 闪存读写，支持对 `device_name` 进行持久化存储与热更新。
  - 收到服务器响应后，原生解析下发的 `device_name`，如不一致则自动同步写入 NVS 并热更新内存，实现双向设备别名同步。

### Fixed
- **配置保存卡死问题**：在配网保存成功页面 (`/save`) 中，新增 JavaScript 倒计时 6 秒自动跳转至根目录 `/` 的重定向逻辑，防止界面卡死，实现免手动改路径返回。

