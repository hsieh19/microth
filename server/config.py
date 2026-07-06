import os
from pydantic_settings import BaseSettings, SettingsConfigDict

# 计算绝对的配置文件路径，确保存放到已持久化挂载的 data 目录下，防止工作目录偏移
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ENV_PATH = os.path.join(BASE_DIR, "data", "settings.env")

class Settings(BaseSettings):
    # API 校验密钥 (需与 ESP32 固件端配置的 API_KEY 保持一致)
    API_KEY: str = "your-32-char-secret-key-here"

    # 飞书机器人 Webhook 链接 (如果不配置则不发送告警，但系统正常运行)
    FEISHU_WEBHOOK: str = ""

    # SQLite 数据库文件存放路径 (容器部署下建议挂载 /app/data 目录)
    DB_PATH: str = "data/monitor.db"

    # 告警阈值配置
    TEMP_ALERT_THRESHOLD: float = 35.0
    HUMI_ALERT_THRESHOLD: float = 80.0

    # 告警防轰炸冷却时间 (秒)，默认 30 分钟 (1800 秒)
    ALERT_COOLDOWN_SEC: int = 1800

    # 时序历史数据保留天数
    DATA_RETENTION_DAYS: int = 730

    # 设备数据上报周期 (秒)，默认 60 秒
    REPORT_INTERVAL_SEC: int = 60

    # 支持通过环境变量加载配置，同时支持从已挂载持久化的 data/settings.env 文件中加载
    model_config = SettingsConfigDict(env_file=ENV_PATH, env_file_encoding="utf-8")

# 实例化全局配置对象
settings = Settings()

# 手动从 data/settings.env 中强制加载配置进行最高优先级覆盖，防止在容器重启时被系统环境变量覆盖
def load_settings_from_env_file():
    if os.path.exists(ENV_PATH):
        try:
            with open(ENV_PATH, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    if "=" in line:
                        k, v = line.split("=", 1)
                        k = k.strip()
                        v = v.strip()
                        if hasattr(settings, k):
                            orig_val = getattr(settings, k)
                            if isinstance(orig_val, float):
                                setattr(settings, k, float(v))
                            elif isinstance(orig_val, int):
                                setattr(settings, k, int(v))
                            else:
                                setattr(settings, k, v)
        except Exception as e:
            import logging
            logging.error(f"[Config] 手动从 settings.env 加载运行时配置失败: {e}")

load_settings_from_env_file()

def ensure_directories():
    """
    M3 修复：将目录创建操作从模块顶层封装到函数，避免 import 时产生 I/O 副作用
    应在 lifespan 中显式调用
    """
    db_dir = os.path.dirname(settings.DB_PATH)
    if db_dir:
        os.makedirs(db_dir, exist_ok=True)
    os.makedirs("logs", exist_ok=True)

def save_settings_to_env(
    api_key: str, 
    feishu_webhook: str, 
    temp_alert: float, 
    humi_alert: float, 
    cooldown: int, 
    retention: int, 
    report_interval: int
):
    """
    保存最新配置到本地 settings.env 文件并动态更新内存中的 settings 对象。
    """
    settings.API_KEY = api_key
    settings.FEISHU_WEBHOOK = feishu_webhook
    settings.TEMP_ALERT_THRESHOLD = temp_alert
    settings.HUMI_ALERT_THRESHOLD = humi_alert
    settings.ALERT_COOLDOWN_SEC = cooldown
    settings.DATA_RETENTION_DAYS = retention
    settings.REPORT_INTERVAL_SEC = report_interval

    env_content = f"""# 系统自动更新的配置
API_KEY={api_key}
FEISHU_WEBHOOK={feishu_webhook}
TEMP_ALERT_THRESHOLD={temp_alert}
HUMI_ALERT_THRESHOLD={humi_alert}
ALERT_COOLDOWN_SEC={cooldown}
DATA_RETENTION_DAYS={retention}
REPORT_INTERVAL_SEC={report_interval}
DB_PATH={settings.DB_PATH}
"""
    # 确保 data 目录存在
    os.makedirs(os.path.dirname(ENV_PATH), exist_ok=True)
    with open(ENV_PATH, "w", encoding="utf-8") as f:
        f.write(env_content)

