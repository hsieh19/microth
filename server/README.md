# 轻量化监控系统后端与可视化

本目录存放基于 Python FastAPI 和 SQLite 的监控系统后端程序，支持多设备数据接收、时序存储、历史折线图查看以及飞书自动联动报警。

## 技术栈
- **Web 框架**：FastAPI + Uvicorn
- **数据库**：SQLite（使用异步驱动 `aiosqlite` + WAL 模式）
- **告警推送**：飞书 Webhook 自定义群机器人
- **前端展示**：自适应单页 HTML + Chart.js + Ajax 周期轮询

## 快速开始
1. 准备 Python 3.11+ 环境。
2. 安装依赖：
   ```bash
   pip install fastapi uvicorn aiosqlite apscheduler httpx
   ```
3. 在 `config.py` 中配置 `API_KEY` 和飞书 Webhook 链接。
4. 运行服务：
   ```bash
   uvicorn main:app --host 0.0.0.0 --port 8000
   ```
