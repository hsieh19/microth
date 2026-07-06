import logging
import re
from typing import Optional
from logging.handlers import TimedRotatingFileHandler
from contextlib import asynccontextmanager
from fastapi import FastAPI, Header, HTTPException, Depends, Request, Query
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel, Field, field_validator

from config import settings, ensure_directories, save_settings_to_env
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

def sync_firmware_version():
    """
    自动读取 firmware/version.json 里的版本号，并将其同步写入 firmware/monitor/config.h
    以实现单点版本管理，避免固件和版本日志各处手工改动的不同步
    """
    import os
    import json
    import re
    try:
        base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        fw_json_path = os.path.join(base_dir, "firmware", "version.json")
        fw_config_path = os.path.join(base_dir, "firmware", "monitor", "config.h")
        
        if not os.path.exists(fw_json_path) or not os.path.exists(fw_config_path):
            return
            
        with open(fw_json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
            version = data.get("version", "1.0.0")
            
        with open(fw_config_path, "r", encoding="utf-8") as f:
            config_content = f.read()
            
        new_content = re.sub(
            r'#define FIRMWARE_VERSION\s+".*?"',
            f'#define FIRMWARE_VERSION     "{version}"',
            config_content
        )
        
        if new_content != config_content:
            with open(fw_config_path, "w", encoding="utf-8") as f:
                f.write(new_content)
            logging.getLogger("lifespan").info(f"[Version] 成功将固件版本号 {version} 同步至 config.h")
    except Exception as e:
        logging.getLogger("lifespan").warning(f"[Version] 自动同步固件版本号失败: {e}")

# 统一管理应用的启动与关闭生命周期
@asynccontextmanager
async def lifespan(app: FastAPI):
    # 1. 先确保必要的目录存在 (M3 修复: 显式调用而非模块导入时隐式执行)
    ensure_directories()
    # 2. 开启日志
    setup_logger()
    logger = logging.getLogger("lifespan")
    logger.info("监控系统正在启动...")
    
    # 自动同步固件端版本号
    sync_firmware_version()
    
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
    version="1.0.0",
    lifespan=lifespan
)

# 绑定模板引擎，用于渲染 Glassmorphism 大屏主页
templates = Jinja2Templates(directory="templates")

# 动态加载服务端版本号，作为 Single Source of Truth
SERVER_VERSION = "1.0.0"
try:
    import os
    import json
    with open("server/version.json", "r", encoding="utf-8") as f:
        _v_data = json.load(f)
        SERVER_VERSION = _v_data.get("version", "1.0.0")
except Exception:
    try:
        with open("version.json", "r", encoding="utf-8") as f:
            _v_data = json.load(f)
            SERVER_VERSION = _v_data.get("version", "1.0.0")
    except Exception:
        pass

# ==================== Pydantic 入参模型 ====================
class SensorRecord(BaseModel):
    device_id: str = Field(..., description="设备唯一标识", min_length=1, max_length=32)
    temp: float = Field(..., description="温度 data (摄氏度)", ge=-1000.0, le=100.0)
    humi: float = Field(..., description="湿度 data (百分比)", ge=-1000.0, le=100.0)
    offset_sec: int = Field(default=0, description="相对于当前接收时间的采集偏移秒数", ge=0)
    device_name: Optional[str] = Field(default="", description="设备别名名称")
    device_ip: Optional[str] = Field(default="", description="设备本地局域网 IP")
    sensor_alert_enabled: bool = Field(default=True, description="是否启用传感器故障飞书报警")

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
    # 0. 验证设备是否注册
    if not await database.is_device_registered(record.device_id):
        logging.warning(f"[Security] 拦截到未注册的设备数据上报尝试，设备 ID: {record.device_id}")
        raise HTTPException(status_code=403, detail="Forbidden: Device not registered")

    # 1. 更新上报设备的 IP 与 设备别名 (自动检测与防覆盖)
    await database.update_device_ip_and_name_if_changed(record.device_id, record.device_name or "", record.device_ip or "")

    # 获取本设备最新的上一条数据记录 (在插入新记录前获取，用于状态机判断)
    last_record = await database.get_latest_record_for_device(record.device_id)

    # 2. 插入时序数据到 SQLite
    ts = await database.add_record(record.device_id, record.temp, record.humi, record.offset_sec)
    
    logging.info(f"[Data] 接收设备 '{record.device_id}' 数据成功 -> 温度: {record.temp:.2f} °C, 湿度: {record.humi:.2f} %")
    
    # 3. 状态机检查并触发飞书消息推送 (防轰炸冷却)
    # 首先处理温湿度传感器连接断开/恢复的警报逻辑
    await alerts.check_sensor_status_and_alert(
        record.device_id, 
        record.temp, 
        record.humi, 
        record.sensor_alert_enabled,
        record.device_name or "",
        record.device_ip or "",
        last_record
    )
    
    # 接着如果传感器状态是正常的，我们才去进行正常的温湿度越限警报判断
    if record.temp != -999.0:
        await alerts.check_and_trigger_alerts(record.device_id, record.temp, record.humi)
    
    # 4. 获取数据库中最新的设备别名以便同步回固件端
    db_device = await database.get_device_by_id(record.device_id)
    latest_device_name = db_device['device_name'] if db_device else (record.device_name or "")
    
    return {
        "status": "ok",
        "ts": ts,
        "report_interval": settings.REPORT_INTERVAL_SEC,
        "api_key": settings.API_KEY,
        "device_name": latest_device_name
    }

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
    days: Optional[int] = Query(None, ge=1, le=730, description="获取历史记录的时间跨度（天），最大 730 天"),
    start_ts: Optional[int] = Query(None, description="自定义开始 Unix 时间戳 (秒)"),
    end_ts: Optional[int] = Query(None, description="自定义结束 Unix 时间戳 (秒)")
):
    """
    接口 3：拉取指定时间范围的时序数据列表 (按时间升序，供前端重画 Chart.js 曲线)
    """
    records = await database.get_history_records(device_id, days, start_ts, end_ts)
    return records

@app.get("/api/devices")
async def get_active_devices():
    """
    接口 4：拉取目前已登记上报过数据的所有活跃设备列表 (供前端下拉切换菜单使用)
    """
    devices = await database.get_all_devices()
    return devices

# ==================== 配置管理接口 (Settings Routes) ====================

class SettingsUpdateModel(BaseModel):
    api_key: str = Field(..., min_length=1, max_length=64, description="API 校验密钥")
    feishu_webhook: str = Field(default="", description="飞书机器人 Webhook 链接")
    temp_alert_threshold: float = Field(..., ge=-50.0, le=100.0, description="温度告警阈值")
    humi_alert_threshold: float = Field(..., ge=0.0, le=100.0, description="湿度告警阈值")
    alert_cooldown_sec: int = Field(..., ge=1, le=86400, description="告警防轰炸冷却时间(秒)")
    data_retention_days: int = Field(..., ge=1, le=730, description="历史数据保留天数")
    report_interval_sec: int = Field(..., ge=1, le=86400, description="上报周期(秒)")

@app.get("/api/settings")
async def get_system_settings():
    """
    获取当前系统配置
    """
    return {
        "api_key": settings.API_KEY,
        "feishu_webhook": settings.FEISHU_WEBHOOK,
        "temp_alert_threshold": settings.TEMP_ALERT_THRESHOLD,
        "humi_alert_threshold": settings.HUMI_ALERT_THRESHOLD,
        "alert_cooldown_sec": settings.ALERT_COOLDOWN_SEC,
        "data_retention_days": settings.DATA_RETENTION_DAYS,
        "report_interval_sec": settings.REPORT_INTERVAL_SEC
    }

@app.post("/api/settings")
async def update_system_settings(update_data: SettingsUpdateModel):
    """
    更新系统配置并写入本地 .env 文件
    """
    save_settings_to_env(
        api_key=update_data.api_key,
        feishu_webhook=update_data.feishu_webhook,
        temp_alert=update_data.temp_alert_threshold,
        humi_alert=update_data.humi_alert_threshold,
        cooldown=update_data.alert_cooldown_sec,
        retention=update_data.data_retention_days,
        report_interval=update_data.report_interval_sec
    )
    return {"status": "ok"}

# ==================== 设备注册与管理接口 (Device Registration Routes) ====================

class DeviceRegisterModel(BaseModel):
    device_id: str = Field(..., min_length=1, max_length=32, description="设备唯一 ID")
    device_name: str = Field(default="", max_length=64, description="设备别名")
    group_name: str = Field(default="默认分组", max_length=64, description="设备分组")
    device_ip: Optional[str] = Field(default="", max_length=64, description="设备 IP")

    @field_validator('device_id')
    @classmethod
    def validate_register_device_id(cls, v: str) -> str:
        if not re.match(r'^[a-zA-Z0-9_\-]+$', v):
            raise ValueError("device_id 只允许包含字母、数字、下划线和连字符")
        return v

@app.get("/api/devices/registered")
async def get_registered_devices():
    """
    拉取目前系统已注册的所有设备列表 (含名称, IP 和分组)
    """
    return await database.get_registered_devices()

@app.post("/api/devices/registered")
async def register_new_device(device: DeviceRegisterModel):
    """
    注册新设备，或更新已存在设备的名称/分组配置
    ```json
    {
        "device_id": "esp32-01",
        "device_name": "主卧室传感器",
        "group_name": "一楼生活区",
        "device_ip": "172.17.213.118"
    }
    ```
    """
    await database.register_device(device.device_id, device.device_name, device.group_name, device.device_ip or "")
    return {"status": "ok"}

@app.delete("/api/devices/registered/{device_id}")
async def unregister_device(device_id: str):
    """
    注销(删除)已注册的设备
    """
    if not re.match(r'^[a-zA-Z0-9_\-]+$', device_id):
        raise HTTPException(status_code=400, detail="Invalid device_id format")
    await database.unregister_device(device_id)
    return {"status": "ok"}

# ==================== 页面渲染 (Web Router) ====================

@app.get("/", response_class=HTMLResponse)
async def serve_dashboard_page(request: Request):
    """
    页面：渲染温湿度监控大屏主页 (动态注入服务端版本号)
    """
    return templates.TemplateResponse(request, "index.html", {"version": SERVER_VERSION})
