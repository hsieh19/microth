# 轻量化温湿度监控系统后端与可视化 (v2.1)

本目录存放基于 **FastAPI + SQLite (aiosqlite) + Chart.js** 构建的远程温湿度监控系统后端服务。服务支持接收 ESP32 上报的数据、异步落库时序表、飞书告警及恢复推送、定时数据老化清理以及提供高水准的 Glassmorphism 数据展示大屏。

---

## 📂 文件目录说明
* **[main.py](file:///E:/AI%20Project/microth/server/main.py)**：主程序入口。包含接口路由声明、API Key 依赖验证拦截器，以及利用 lifespan 管理的数据库与调度器初始化生命周期。
* **[config.py](file:///E:/AI%20Project/microth/server/config.py)**：配置读取模块。使用 Pydantic Settings 加载环境变量和本地配置参数，自动确保 data/ 及 logs/ 目录就绪。
* **[database.py](file:///E:/AI%20Project/microth/server/database.py)**：全异步 SQLite 数据库封装。包含首创的 WAL 读写优化模式、数据库 Schema 复合索引建立，以及各种时序检索/修改 API。
* **[alerts.py](file:///E:/AI%20Project/microth/server/alerts.py)**：告警处理服务。封装飞书群机器人的富文本卡片通知。内置 30 分钟持久化状态冷却，防轰炸通知并支持温湿度恢复时自动发送解警通知。
* **[scheduler.py](file:///E:/AI%20Project/microth/server/scheduler.py)**：定时任务。使用 APScheduler 在每日凌晨 3:00 执行过期时序数据老化删除（保留 90 天），并使用 WAL 兼容的 `incremental_vacuum` 释放空余页以整理磁盘空间。
* **[templates/index.html](file:///E:/AI%20Project/microth/server/templates/index.html)**：前端大屏文件。采用 sleek dark 风格和磨砂玻璃物理卡片质感，内含 Chart.js 历史时序折线图（支持切换 24h、7天、30天），以及卡片最新数据的 60 秒 Ajax 定时轮询局部动态刷新。

---

## 🚀 部署与运行

### 方式一：本地 Python 环境直接运行（调试推荐）

1. **创建并进入 Python 虚拟环境**：
   ```bash
   python -m venv .venv
   # Windows (PowerShell):
   .venv\Scripts\Activate.ps1
   # Linux/macOS:
   source .venv/bin/activate
   ```

2. **安装依赖依赖包**：
   ```bash
   pip install -r requirements.txt
   ```

3. **配置参数**：
   在当前目录创建 `.env` 文件，可配置上报安全校验 Key 及 飞书 Webhook 接口：
   ```ini
   API_KEY=your-32-char-secret-key-here
   FEISHU_WEBHOOK=https://open.feishu.cn/open-apis/bot/v2/hook/xxxxxxx
   ```

4. **开启 Uvicorn 本地服务**：
   ```bash
   uvicorn main:app --host 0.0.0.0 --port 8000 --reload
   ```
   服务成功运行后，可在浏览器中访问 `http://localhost:8000/` 打开监控大屏。

---

### 方式二：Docker Compose 一键部署（生产推荐）

在项目**根目录**下（即 `docker-compose.yml` 所在的目录），运行以下命令可自动拉取镜像编译并在后台以守护状态启动服务：
```bash
# 启动容器服务
docker-compose up -d --build

# 查看运行状态
docker-compose ps

# 实时追踪日志
docker-compose logs -f
```
系统启动后，会将数据库文件挂载到本地的 `./server/data/` 目录，日常日志挂载到 `./server/logs/` 目录，防止更新镜像造成历史时序丢失。

---

## 🔍 系统维护与接口说明

### 1. HTTP 接口 API 表
* **`POST /api/data`**：设备数据上报。
  - 请求头必须包含：`X-API-Key: <your_key>`
  - Body 示例：
    ```json
    {
      "device_id": "esp32-01",
      "temp": 28.52,
      "humi": 65.34
    }
    ```
* **`GET /api/latest?device_id=esp32-01`**：获取最新温湿度卡片数据。
* **`GET /api/history?device_id=esp32-01&days=1`**：拉取折线图数据。
* **`GET /api/devices`**：自动获取当前上报过数据的所有活跃设备列表。

### 2. 时序数据维护与日志
- **老化策略**：设备 7×24 小时运行下，系统默认每天凌晨 3:00 清除 `ts < 90天` 的记录，随后执行 `PRAGMA incremental_vacuum(1000);` 安全地释放空闲磁盘空间，同时避免破坏 WAL 高性能读写模式。
- **系统日志**：所有事件（启动、接收数据、清理、告警推送）均写入 `logs/app.log`，按天滚动生成副本（如 `app.log.2026-07-02`），保留最近 30 天以供审计。
