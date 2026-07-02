import logging
import re
from logging.handlers import TimedRotatingFileHandler
from contextlib import asynccontextmanager
from fastapi import FastAPI, Header, HTTPException, Depends, Request, Query
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel, Field, field_validator

from config import settings, ensure_directories
import database
import alerts
import scheduler

# 创建日志模块配置
def setup_logger():
    # 每天凌晨滚动，最多保留 30 天日志记录
    handler = TimedRotatingFileHandler("logs/app.log", when="midnight", backupCount=30, encoding="utf-8")
    formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(name)s: %(message)s")
    handler.setFormatter(formatter)
    
    root_logger = logging.getLogger()
    root_logger.setLevel(logging.INFO)
    
    if not root_logger.handlers:
        root_logger.addHandler(handler)
        # 同时在终端控制台进行打印，方便本地开发排查
        console = logging.StreamHandler()
        console.setFormatter(formatter)
        root_logger.addHandler(console)

# 统一管理应用的启动与关闭生命周期
@asynccontextmanager
async def lifespan(app: FastAPI):
    # 1. 先确保必要的目录存在 (M3 修复: 显式调用而非模块导入时隐式执行)
    ensure_directories()
    # 2. 开启日志
    setup_logger()
    logger = logging.getLogger("lifespan")
    logger.info("监控系统正在启动...")
    
    # 3. 异步初始化 SQLite 数据库
    await database.init_db()
    
    # 4. 启动定时任务调度器 (时序老化)
    scheduler.start_scheduler()
    logger.info("监控系统启动完毕，开始提供服务。")
    
    yield
    
    # 5. 关闭服务时清理资源
    logger.info("监控系统正在关闭，开始清理资源...")
    scheduler.shutdown_scheduler()
    logger.info("监控系统已成功安全关闭。")

# 创建 FastAPI 实例
app = FastAPI(
    title="温湿度智能远程监控系统",
    version="2.1",
    lifespan=lifespan
)

# 绑定模板引擎，用于渲染 Glassmorphism 大屏主页
templates = Jinja2Templates(directory="templates")

# ==================== Pydantic 入参模型 ====================
class SensorRecord(BaseModel):
    device_id: str = Field(..., description="设备唯一标识", min_length=1, max_length=32)
    temp: float = Field(..., description="温度数据 (摄氏度)", ge=-50.0, le=100.0)
    humi: float = Field(..., description="湿度数据 (百分比)", ge=0.0, le=100.0)

    # O1 修复：对 device_id 增加正则格式校验，防止特殊字符导致日志污染或前端 XSS 风险
    @field_validator('device_id')
    @classmethod
    def validate_device_id(cls, v: str) -> str:
        if not re.match(r'^[a-zA-Z0-9_\-]+$', v):
            raise ValueError("device_id 只允许包含字母、数字、下划线和连字符")
        return v

# ==================== 安全校验拦截器 ====================
async def verify_api_key(request: Request, x_api_key: str = Header(..., description="安全验证密钥 Header")):
    """
    API 鉴权拦截器。如果 Header 中不存在 X-API-Key 或不匹配，将拒绝请求 (修复严重缺陷 1)
    """
    if x_api_key != settings.API_KEY:
        # M2 修复：不记录传入的 Key 内容（防止安全日志泄露敏感信息），改为记录来源 IP
        client_ip = request.client.host if request.client else "unknown"
        logging.warning(f"[Security] 拦截到未授权的 API 访问尝试，来源 IP: {client_ip}")
        raise HTTPException(status_code=403, detail="Forbidden: Invalid API Key")

# ==================== 接口定义 (API Routes) ====================

@app.post("/api/data")
async def receive_sensor_data(record: SensorRecord, _: None = Depends(verify_api_key)):
    """
    接口 1：接收 ESP32-C3 采集数据的上报端点 (具备 X-API-Key 头校验)
    """
    # 1. 插入时序数据到 SQLite
    ts = await database.add_record(record.device_id, record.temp, record.humi)
    
    logging.info(f"[Data] 接收设备 '{record.device_id}' 数据成功 -> 温度: {record.temp:.2f} °C, 湿度: {record.humi:.2f} %")
    
    # 2. 状态机检查并触发飞书消息推送 (防轰炸冷却)
    await alerts.check_and_trigger_alerts(record.device_id, record.temp, record.humi)
    
    return {"status": "ok", "ts": ts}

@app.get("/api/latest")
async def get_latest_data(device_id: str = Query(..., description="指定查询的设备 ID")):
    """
    接口 2：获取指定设备的最新一条记录 (用于前端仪表盘的周期性 Ajax 局部刷新)
    """
    record = await database.get_latest_record_for_device(device_id)
    if not record:
        raise HTTPException(status_code=404, detail=f"No data found for device: {device_id}")
    return record

@app.get("/api/history")
async def get_history_data(
    device_id: str = Query(..., description="设备 ID"),
    days: int = Query(1, ge=1, le=30, description="获取历史记录的时间跨度（天），最大 30 天")
):
    """
    接口 3：拉取指定时间范围的时序数据列表 (按时间升序，供前端重画 Chart.js 曲线)
    """
    records = await database.get_history_records(device_id, days)
    return records

@app.get("/api/devices")
async def get_active_devices():
    """
    接口 4：拉取目前已登记上报过数据的所有活跃设备列表 (供前端下拉切换菜单使用)
    """
    devices = await database.get_all_devices()
    return devices

# ==================== 页面渲染 (Web Router) ====================

@app.get("/", response_class=HTMLResponse)
async def serve_dashboard_page(request: Request):
    """
    页面：渲染温湿度监控大屏主页
    """
    return templates.TemplateResponse("index.html", {"request": request})
