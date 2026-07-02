# 轻量化温湿度远程监控系统设计方案 v2.0

> **修订说明**：本版本（v2.0）在 v1.0 基础上，针对评审发现的 3 个严重缺陷、3 个中等缺陷进行了全面修正，并追加若干可维护性改进建议。所有改动均以 `【v2.0 修订】` 标注。

本方案采用高性价比、轻量化的硬件与软件架构，在全内网 Wi-Fi 覆盖的环境下，实现温湿度的远程实时采集、本地长效数据存储、历史趋势查看以及飞书自动联动报警。核心亮点在于**去中心化、组件极简、高稳定性与低维护成本**。

---

## 一、项目概述

本项目旨在利用高性价比、轻量化的硬件与软件架构，在全内网 Wi-Fi 覆盖的环境下，实现温湿度的远程实时采集、本地长效数据存储、历史趋势查看以及飞书自动联动报警。核心亮点在于**去中心化、组件极简、高稳定性与低维护成本**。

---

## 二、系统总体架构

为了满足"尽量轻量化"的核心需求，本方案摒弃了传统的复杂物联网全家桶（如 InfluxDB、Grafana、Message Broker 等），采用**直连架构**。

```
[ 终端硬件层 ] : ESP32-C3 + SHT40 传感器（多台，各含 device_id）
       │
       ▼ (内网 Wi-Fi / HTTP POST JSON + X-API-Key 认证)  【v2.0 修订：新增 API Key 认证】
[ 轻量服务端 ] : Python (FastAPI + aiosqlite)            【v2.0 修订：异步 SQLite 驱动】
       ├──> [ 存储层 ] : SQLite (WAL 模式 + 数据老化清理)
       ├──> [ 报警层 ] : 飞书 Webhook (告警状态持久化)   【v2.0 修订：状态持久化】
       └──> [ 展现层 ] : HTML5 自适应网页 (Chart.js + Ajax 轮询) 【v2.0 修订：明确前端方案】
```

### 上报频率规划（【v2.0 新增】）

| 参数 | 值 | 说明 |
|------|----|------|
| 上报间隔 | **60 秒** | 每分钟一条，每天 1440 条，兼顾实时性与存储压力 |
| 前端刷新间隔 | **60 秒** | 与设备上报同步，使用 Ajax 轮询 |
| 数据保留期 | **90 天** | 约 13 万条记录，SQLite 轻松应对 |

---

## 三、硬件端设计（Hardware Layer）

### 1. 硬件选型与接线

- **主控芯片**：ESP32-C3（建议选用带外置天线版本，确保在复杂内网环境下的 Wi-Fi 信号增益）。
- **传感器**：SHT40（高精度、低功耗 I2C 温湿度传感器）。
- **引脚物理连接**：

  | SHT40 引脚 | ESP32-C3 引脚 |
  |-----------|--------------|
  | VCC | 3.3V |
  | GND | GND |
  | SCL | GPIO 9（默认 I2C 时钟线） |
  | SDA | GPIO 8（默认 I2C 数据线） |

### 2. 设备标识（【v2.0 新增】）

每台 ESP32-C3 在固件中硬编码唯一的 `device_id`（如 `esp32-01`、`esp32-02`），并随每次 POST 请求一同上报，以支持多设备接入与独立展示。

### 3. API 认证（【v2.0 新增：修复严重缺陷 1】）

固件中内置预共享 API Key，每次 HTTP POST 均在 Header 中携带，服务端拒绝未认证请求：

```cpp
// Arduino/ESP-IDF 固件示例
http.addHeader("Content-Type", "application/json");
http.addHeader("X-API-Key", "your-32-char-secret-key-here");

String payload = "{\"device_id\":\"esp32-01\","
                 "\"temp\":" + String(temp, 2) + ","
                 "\"humi\":" + String(humi, 2) + "}";
int httpCode = http.POST(payload);
```

### 4. 硬件端鲁棒性（稳定性）软件设计

为保证 7×24 小时连续运行不卡死，固件开发须遵循以下核心逻辑：

- **硬件看门狗（Watchdog Timer）**：启用 ESP32-C3 硬件看门狗，设为 15 秒。主循环正常"喂狗"，一旦网络请求或 I2C 总线发生未知阻塞导致卡死，看门狗超时将强制硬件冷重启。

- **Wi-Fi 网络自愈机制**：在 `loop()` 中实时监测 `WiFi.status()`。若断连，立即启动非阻塞重连；若连续断连超过 5 分钟，触发 `ESP.restart()` 软重启以重置网络协议栈。

- **非阻塞 HTTP 请求与严格超时**：向服务端发送数据时，设置 `http.setTimeout(4000)` 确保 4 秒超时，防止因服务端临时维护导致硬件线程挂起。

- **I2C 总线故障隔离**：每次读取 SHT40 失败时进行异常捕获，连续失败 5 次则重新初始化 `Wire.begin()`，防止传感器因电磁干扰锁死总线。

---

## 四、服务端设计（Backend & Storage Layer）

服务端部署于内网常开设备（轻量服务器、软路由或群晖 NAS）上，采用全 Python 栈实现。

### 1. 核心 Web 框架（FastAPI + Uvicorn）

采用生产级的 **FastAPI** 异步框架，高并发能力强，即使后续扩容多台 ESP32-C3 设备同时上报，异步处理机制也能确保服务端响应迅速。

### 2. API Key 认证中间件（【v2.0 新增：修复严重缺陷 1】）

```python
# auth.py
from fastapi import Header, HTTPException
from config import settings

async def verify_api_key(x_api_key: str = Header(...)):
    """统一 API Key 校验依赖，注入到需要保护的路由中"""
    if x_api_key != settings.API_KEY:
        raise HTTPException(status_code=403, detail="Forbidden: Invalid API Key")
```

```python
# main.py - 路由中注入认证
from fastapi import Depends
from auth import verify_api_key

@app.post("/api/data", dependencies=[Depends(verify_api_key)])
async def receive_data(record: SensorRecord):
    ...
```

### 3. 极致轻量化存储（SQLite + aiosqlite）（【v2.0 修订：修复严重缺陷 2】）

**选型原因**：SQLite 是单文件数据库，拷贝即备份，无需安装独立服务。

**v2.0 关键修正**：原方案使用同步 `sqlite3` 驱动，在 FastAPI 异步路由中会阻塞整个事件循环。v2.0 改用 **`aiosqlite`** 异步驱动，彻底解决并发问题。

**稳定性加固**：
- 开启 **WAL 模式**（`PRAGMA journal_mode=WAL;`），大幅提升并发读写性能。
- 数据库操作全部采用 `async with aiosqlite.connect()` 异步上下文管理器。

#### 数据库 Schema（【v2.0 新增：修复中等缺陷 4】）

```sql
-- 传感器数据表
CREATE TABLE IF NOT EXISTS sensor_records (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT    NOT NULL DEFAULT 'esp32-01',
    ts        INTEGER NOT NULL,   -- Unix 时间戳（秒），便于范围查询与老化
    temp      REAL    NOT NULL,
    humi      REAL    NOT NULL
);
-- 必须建立时间戳索引，保证历史查询性能
CREATE INDEX IF NOT EXISTS idx_ts ON sensor_records (ts);
CREATE INDEX IF NOT EXISTS idx_device_ts ON sensor_records (device_id, ts);

-- 告警状态持久化表（v2.0 新增）
CREATE TABLE IF NOT EXISTS alert_state (
    device_id      TEXT    PRIMARY KEY,
    is_alerting    INTEGER NOT NULL DEFAULT 0,   -- 0=正常, 1=告警中
    last_alert_ts  INTEGER NOT NULL DEFAULT 0    -- 上次发送告警的 Unix 时间戳
);
```

#### 数据老化清理（【v2.0 新增：修复中等缺陷 4】）

```python
# scheduler.py - 启动时注册定时清理任务
from apscheduler.schedulers.asyncio import AsyncIOScheduler
import aiosqlite, time

scheduler = AsyncIOScheduler()

@scheduler.scheduled_job("cron", hour=3, minute=0)  # 每天凌晨 3:00 执行
async def cleanup_old_data():
    cutoff = int(time.time()) - 90 * 86400  # 保留 90 天
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("DELETE FROM sensor_records WHERE ts < ?", (cutoff,))
        await db.commit()
    logging.info(f"[Cleanup] 已清理 90 天前的历史数据，截止时间戳：{cutoff}")
```

### 4. 结构化日志（【v2.0 新增：改进建议 2】）

```python
# logger.py
import logging
from logging.handlers import TimedRotatingFileHandler

def setup_logger():
    handler = TimedRotatingFileHandler(
        "logs/app.log", when="midnight", backupCount=30, encoding="utf-8"
    )
    formatter = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
    )
    handler.setFormatter(formatter)
    logging.basicConfig(handlers=[handler], level=logging.INFO)
```

日志按天滚动，保留最近 30 天，排障时可直接查阅。

### 5. 进程守护与防崩溃（【v2.0 修订：修复严重缺陷 3】）

原方案的 `pm2 start main.py` 命令**有误**：PM2 默认以 Node.js 解释器执行脚本，将导致 Python 服务启动失败。v2.0 提供两种正确方案：

#### 方案 A：Systemd（推荐，适用于 Linux 服务器/软路由）

```ini
# /etc/systemd/system/ths-monitor.service
[Unit]
Description=THS Monitor - Temperature & Humidity Service
After=network-online.target
Wants=network-online.target

[Service]
User=pi
WorkingDirectory=/opt/ths-monitor
ExecStart=/usr/bin/python3 -m uvicorn main:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

```bash
# 启用并启动服务
sudo systemctl daemon-reload
sudo systemctl enable ths-monitor
sudo systemctl start ths-monitor
```

#### 方案 B：PM2（若已有 PM2 环境，需显式指定 Python 解释器）

```bash
# 必须使用 --interpreter 参数，否则无法运行
pm2 start main.py --name "ths-monitor" --interpreter python3
pm2 save
pm2 startup  # 配置开机自启
```

#### 方案 C：Docker（推荐，适用于群晖 NAS 等不支持 Systemd 的环境）（【v2.0 新增：改进建议 4】）

```dockerfile
# Dockerfile
FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY . .
EXPOSE 8000
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
```

```yaml
# docker-compose.yml
version: "3.8"
services:
  ths-monitor:
    build: .
    container_name: ths-monitor
    restart: always
    ports:
      - "8000:8000"
    volumes:
      - ./data:/app/data      # SQLite 文件持久化挂载
      - ./logs:/app/logs      # 日志持久化挂载
    environment:
      - API_KEY=your-32-char-secret-key-here
```

---

## 五、报警与联动机制（Alerting Layer）

### 1. 飞书机器人对接

服务端通过 HTTP POST 方式，将告警信息推送到飞书群聊自定义机器人的 Webhook 接口。

> **注意**：内网服务器需具备外网访问权限（能访问 `open.feishu.cn`）。

### 2. 告警阈值

| 指标 | 告警阈值 |
|------|---------|
| 温度 | > 35°C |
| 湿度 | > 80% |

### 3. 告警稳定性加固（防轰炸机制）

- **冷却时间（Cooldown）**：首次触发告警并成功发送飞书后，该设备进入 30 分钟的"告警冷却期"。冷却期内即使数据持续超标，也不重复发送。

- **恢复通知**：温湿度降回正常范围后，发送"【恢复通知】设备温湿度已恢复正常"，并重置告警状态。

### 4. 告警状态持久化（【v2.0 修订：修复中等缺陷 5】）

**原方案缺陷**：告警状态仅存储在 Python 进程内存中，进程重启后状态全部丢失，冷却机制失效，会产生误报。

**v2.0 修正**：告警状态写入 SQLite `alert_state` 表，重启后从数据库恢复状态，确保冷却机制持续有效。

```python
# alert.py - 告警状态持久化读写示例
async def check_and_alert(device_id: str, temp: float, humi: float):
    now = int(time.time())
    async with aiosqlite.connect(DB_PATH) as db:
        # 读取持久化的告警状态
        async with db.execute(
            "SELECT is_alerting, last_alert_ts FROM alert_state WHERE device_id=?",
            (device_id,)
        ) as cursor:
            row = await cursor.fetchone()
        
        is_alerting = row[0] if row else 0
        last_alert_ts = row[1] if row else 0
        cooldown_over = (now - last_alert_ts) > 1800  # 30 分钟 = 1800 秒

        is_over_threshold = temp > 35 or humi > 80

        if is_over_threshold and (not is_alerting or cooldown_over):
            # 发送飞书告警
            await send_feishu_alert(device_id, temp, humi)
            # 持久化更新状态
            await db.execute(
                "INSERT OR REPLACE INTO alert_state VALUES (?, 1, ?)",
                (device_id, now)
            )
            await db.commit()
        elif not is_over_threshold and is_alerting:
            # 发送恢复通知
            await send_feishu_recovery(device_id)
            await db.execute(
                "INSERT OR REPLACE INTO alert_state VALUES (?, 0, 0)",
                (device_id,)
            )
            await db.commit()
```

---

## 六、数据可视化层（Visualization Layer）

极简的前端无需部署大型可视化软件，直接由 Python 服务端渲染一个单页 HTML。

### 1. 实时数据（【v2.0 修订：修复中等缺陷 6】）

**原方案**的 `Ajax/WebSocket` 并列写法存在技术决策含糊的问题。

**v2.0 明确选型**：采用 **Ajax 轮询（setInterval）**。理由：每 60 秒刷新一次温湿度数据，无需双向实时推送，Ajax 轮询实现更简单、更符合轻量化原则，WebSocket 在此场景属于过度设计。

```javascript
// 每 60 秒轮询一次最新数据
setInterval(async () => {
    const res = await fetch('/api/latest');
    const { temp, humi, ts } = await res.json();
    document.getElementById('temp-display').textContent = temp.toFixed(1) + ' °C';
    document.getElementById('humi-display').textContent = humi.toFixed(1) + ' %';
    document.getElementById('last-update').textContent = new Date(ts * 1000).toLocaleString();
}, 60000);
```

### 2. 历史趋势查看

- 前端集成轻量级 **Chart.js** 折线图组件。
- 用户可自由选择时间范围（最近 24 小时 / 7 天 / 30 天）。
- Python 后端从 SQLite 检索对应时段数据点，通过 `device_id` 过滤，支持多设备切换展示。
- 支持手机与 PC 浏览器自适应查看（响应式布局）。

---

## 七、多设备扩展预留（【v2.0 新增：改进建议 1】）

所有接口和数据库设计均已预留 `device_id` 字段，支持**横向扩展**多台 ESP32-C3，无需修改服务端代码，仅需在新设备固件中配置不同的 `device_id` 即可。

```python
# 数据上报接口 Schema 示例
class SensorRecord(BaseModel):
    device_id: str = "esp32-01"   # 设备唯一标识
    temp: float                    # 温度 (°C)
    humi: float                    # 湿度 (%)
```

---

## 八、项目实施与部署清单

### 阶段一：环境准备

1. 在内网目标主机为其配置**静态内网 IP**。
2. 安装 Python 3.11+ 及依赖：
   ```bash
   pip install fastapi uvicorn aiosqlite apscheduler httpx
   ```
3. 生成 32 位随机 API Key 并写入配置文件（`config.py` 或环境变量）。

### 阶段二：硬件固件开发

编写 ESP32-C3 固件，包含：
- SHT40 I2C 驱动（60 秒采样间隔）
- `device_id` 硬编码配置
- API Key Header 注入
- Wi-Fi 自愈逻辑 + 硬件看门狗（15 秒）
- HTTP POST 4 秒超时

### 阶段三：服务端开发

1. 实现 FastAPI 服务（含 API Key 认证、`aiosqlite` 异步写入）。
2. 初始化 SQLite 数据库（执行建表 SQL，建立索引）。
3. 注册定时任务（每日凌晨 3:00 清理 90 天前数据）。
4. 集成结构化日志（按日滚动，保留 30 天）。

### 阶段四：飞书机器人配置

在飞书群聊添加自定义机器人，设置关键词安全校验（如"警报"），获取 Webhook URL 写入配置文件。

### 阶段五：服务上线

根据部署环境选择守护方式：

| 环境 | 推荐方式 | 命令 |
|------|---------|------|
| Linux 服务器 / 软路由 | **Systemd**（推荐） | `systemctl enable && start ths-monitor` |
| 群晖 NAS / 容器环境 | **Docker Compose** | `docker-compose up -d` |
| 已有 PM2 的环境 | PM2（需指定解释器） | `pm2 start main.py --interpreter python3` |

---

## 九、v1.0 → v2.0 变更对照表

| 问题类型 | 问题点 | v1.0 原方案 | v2.0 修订方案 |
|---------|--------|------------|--------------|
| 🔴 严重 | API 安全 | 无任何认证 | 预共享 API Key Header 校验 |
| 🔴 严重 | SQLite 驱动 | 同步 `sqlite3` 阻塞事件循环 | `aiosqlite` 全异步驱动 |
| 🔴 严重 | PM2 命令错误 | `pm2 start main.py`（执行失败） | 补充 `--interpreter python3`；新增 Systemd / Docker 方案 |
| 🟡 中等 | 数据库 Schema | 完全缺失 | 明确表结构 + 时间戳双索引 |
| 🟡 中等 | 数据老化策略 | 无 | 每日清理 90 天前数据 |
| 🟡 中等 | 告警状态管理 | 仅内存，重启即丢失 | 持久化到 SQLite `alert_state` 表 |
| 🟡 中等 | 前端技术决策 | `Ajax/WebSocket` 含糊并列 | 明确为 Ajax 轮询（setInterval） |
| 🟢 改进 | 多设备支持 | 无预留 | 全链路预留 `device_id` 字段 |
| 🟢 改进 | 日志 | 无 | Python logging + 按日滚动（30 天） |
| 🟢 改进 | 上报频率 | 未说明 | 明确为 60 秒 |
| 🟢 改进 | NAS 部署 | 未区分说明 | 新增 Docker Compose 方案 |
