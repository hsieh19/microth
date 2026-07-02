import aiosqlite
import time
from typing import List, Dict, Any, Optional
from config import settings

async def init_db():
    """
    初始化 SQLite 数据库，建立时序数据表、告警状态表以及相关复合索引，并开启 WAL 高性能读写模式。
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        # 1. 开启 WAL 模式以支持高并发读写
        await db.execute("PRAGMA journal_mode=WAL;")
        
        # 2. 建立温湿度时序数据表
        await db.execute("""
            CREATE TABLE IF NOT EXISTS sensor_records (
                id        INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT    NOT NULL DEFAULT 'esp32-01',
                ts        INTEGER NOT NULL,   -- Unix 时间戳 (秒)
                temp      REAL    NOT NULL,   -- 温度
                humi      REAL    NOT NULL    -- 湿度
            );
        """)
        
        # 3. 建立告警状态持久化表 (防止重启丢失冷却时间与状态)
        await db.execute("""
            CREATE TABLE IF NOT EXISTS alert_state (
                device_id     TEXT    PRIMARY KEY,
                is_alerting   INTEGER NOT NULL DEFAULT 0,  -- 0=正常, 1=告警中
                last_alert_ts INTEGER NOT NULL DEFAULT 0   -- 上次发送告警的 Unix 时间戳
            );
        """)
        
        # 4. 创建复合索引，保证按设备及时间范围拉取历史趋势时的查询性能
        await db.execute("CREATE INDEX IF NOT EXISTS idx_ts ON sensor_records (ts);")
        await db.execute("CREATE INDEX IF NOT EXISTS idx_device_ts ON sensor_records (device_id, ts);")
        
        await db.commit()
    print("[Database] 数据库初始化成功，WAL 模式及索引已就绪。")

async def add_record(device_id: str, temp: float, humi: float) -> int:
    """
    插入一条新的传感器温湿度记录
    """
    now_ts = int(time.time())
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        await db.execute(
            "INSERT INTO sensor_records (device_id, ts, temp, humi) VALUES (?, ?, ?, ?)",
            (device_id, now_ts, temp, humi)
        )
        await db.commit()
    return now_ts

async def get_latest_record_for_device(device_id: str) -> Optional[Dict[str, Any]]:
    """
    获取指定设备的最新一条温湿度记录
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT device_id, temp, humi, ts FROM sensor_records WHERE device_id = ? ORDER BY ts DESC LIMIT 1",
            (device_id,)
        ) as cursor:
            row = await cursor.fetchone()
            return dict(row) if row else None

async def get_history_records(device_id: str, days: int) -> List[Dict[str, Any]]:
    """
    查询指定设备在过去 N 天内的所有温湿度时序数据，按时间升序排列 (用于 Chart.js 图表渲染)
    """
    cutoff_ts = int(time.time()) - days * 86400
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        # O2 建议：增加 LIMIT 2000 降采样，防止 30 天约 43200 条记录一次性加载进内存造成 OOM
        async with db.execute(
            "SELECT ts, temp, humi FROM sensor_records WHERE device_id = ? AND ts >= ? ORDER BY ts ASC LIMIT 2000",
            (device_id, cutoff_ts)
        ) as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]

async def get_all_devices() -> List[str]:
    """
    获取系统中所有上报过数据的不同设备 ID 列表 (用于前端下拉框)
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        async with db.execute("SELECT DISTINCT device_id FROM sensor_records ORDER BY device_id ASC") as cursor:
            rows = await cursor.fetchall()
            return [row[0] for row in rows]

async def get_alert_state(device_id: str) -> Dict[str, Any]:
    """
    读取特定设备的持久化告警状态。如果不存在，则返回默认正常状态。
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT is_alerting, last_alert_ts FROM alert_state WHERE device_id = ?",
            (device_id,)
        ) as cursor:
            row = await cursor.fetchone()
            if row:
                return dict(row)
            else:
                return {"is_alerting": 0, "last_alert_ts": 0}

async def update_alert_state(device_id: str, is_alerting: int, last_alert_ts: int):
    """
    更新或插入特定设备的持久化告警状态
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        await db.execute(
            "INSERT OR REPLACE INTO alert_state (device_id, is_alerting, last_alert_ts) VALUES (?, ?, ?)",
            (device_id, is_alerting, last_alert_ts)
        )
        await db.commit()
