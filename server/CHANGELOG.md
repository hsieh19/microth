# Changelog - Server

All notable changes to the monitor server project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [1.0.0] - 2026-07-06

### Added
- **数据库 Schema 无损升级**：在数据库初始化时自动执行 `ALTER TABLE registered_devices ADD COLUMN device_ip` 语句，无损增加设备 IP 存储字段。
- **设备 IP 自动更新**：上报接口支持接收 `device_ip`，并提供 `update_device_ip_and_name_if_changed` 自动刷新最新局域网 IP。
- **设备别名双向同步**：
  - 数据上报接口模型支持接收 `device_name`。
  - 上报成功响应中返回服务端最新别名，用于同步更新固件本地 NVS 闪存。
  - 支持设备别名与 IP 的管理和保存。
- **前端大屏 IP 显示**：前端“已注册设备列表”表格新增 `设备 IP` 列回显，并修正无设备时的 Table `colspan` 跨列数。

### Changed
- **配置文件持久化**：将配置文件路径改为绝对路径 `server/data/settings.env`，防容器销毁或服务重启后设置丢失。
- **配置覆盖优先级**：在系统启动时强行二次加载 `settings.env`，保证用户网页修改的最高优先级，防止被 docker-compose 环境变量覆盖。
- **大屏数据自动刷新**：大屏数据轮询由 60 秒缩短至 5 秒；并加入 `lastUpdateTimeStamp` 判定，有新数据时自动静默刷新折线图，实现 100% 免手动刷新。
- **数据表排序优化**：对历史表格渲染数据源进行非破坏性局部拷贝并倒序排列，使最新数据完美置顶且不影响趋势折线图的升序绘制逻辑。
