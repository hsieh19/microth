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
        
        # 5. 建立设备注册表 (增加 device_ip 字段)
        await db.execute("""
            CREATE TABLE IF NOT EXISTS registered_devices (
                device_id   TEXT    PRIMARY KEY,
                device_name TEXT    NOT NULL DEFAULT '',
                group_name  TEXT    NOT NULL DEFAULT '默认分组',
                device_ip   TEXT    NOT NULL DEFAULT '',
                created_at  INTEGER NOT NULL
            );
        """)
        
        # 6. 向后兼容性字段升级：增加 device_ip
        try:
            await db.execute("ALTER TABLE registered_devices ADD COLUMN device_ip TEXT NOT NULL DEFAULT '';")
        except Exception:
            pass

        await db.commit()
        
        # 6. 向后兼容性处理：若设备注册表为空，则将已有上报历史的设备自动迁移注册
        async with db.execute("SELECT COUNT(*) FROM registered_devices") as cursor:
            reg_count = (await cursor.fetchone())[0]
            
        if reg_count == 0:
            async with db.execute("SELECT DISTINCT device_id FROM sensor_records") as cursor:
                rows = await cursor.fetchall()
                existing_devs = [r[0] for r in rows if r[0]]
            
            now_ts = int(time.time())
            if existing_devs:
                for dev_id in existing_devs:
                    await db.execute(
                        "INSERT OR IGNORE INTO registered_devices (device_id, device_name, group_name, created_at) VALUES (?, ?, ?, ?)",
                        (dev_id, f"设备 {dev_id}", "默认分组", now_ts)
                    )
                print(f"[Database] 发现历史数据，已自动注册设备: {existing_devs}")
            else:
                await db.execute(
                    "INSERT OR IGNORE INTO registered_devices (device_id, device_name, group_name, created_at) VALUES (?, ?, ?, ?)",
                    ("esp32-01", "默认设备", "默认分组", now_ts)
                )
                print("[Database] 未检测到活跃设备，已自动注册默认设备 'esp32-01'")
            await db.commit()
            
    print("[Database] 数据库初始化成功，WAL 模式及索引已就绪。")

async def add_record(device_id: str, temp: float, humi: float, offset_sec: int = 0) -> int:
    """
    插入一条新的传感器温湿度记录
    """
    now_ts = int(time.time()) - offset_sec
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        await db.execute(
            "INSERT INTO sensor_records (device_id, ts, temp, humi) VALUES (?, ?, ?, ?)",
            (device_id, now_ts, temp, humi)
        )
        await db.commit()
    return now_ts

async def get_latest_record_for_device(device_id: str) -> Optional[Dict[str, Any]]:
    """
    获取指定设备的最新一条温湿度记录，并根据相邻两条记录的时间差估算上报周期。
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT device_id, temp, humi, ts FROM sensor_records WHERE device_id = ? ORDER BY ts DESC LIMIT 2",
            (device_id,)
        ) as cursor:
            rows = await cursor.fetchall()
            if not rows:
                return None
            
            # 第一条是最新的记录
            latest_record = dict(rows[0])
            
            # 如果有相邻两条记录，则计算时间戳的差值作为上报周期
            if len(rows) > 1:
                ts_diff = rows[0]['ts'] - rows[1]['ts']
                # 时间差如果合理（1秒到1天），则保留，否则用设置的默认值
                if 1 <= ts_diff <= 86400:
                    latest_record['report_interval'] = ts_diff
                else:
                    latest_record['report_interval'] = settings.REPORT_INTERVAL_SEC
            else:
                latest_record['report_interval'] = settings.REPORT_INTERVAL_SEC
                
            return latest_record

async def get_history_records(
    device_id: str,
    days: Optional[int] = None,
    start_ts: Optional[int] = None,
    end_ts: Optional[int] = None
) -> List[Dict[str, Any]]:
    """
    查询指定设备在特定时间段内或过去 N 天内的所有温湿度时序数据，按时间升序排列
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        if start_ts is not None and end_ts is not None:
            # 自定义时间段查询
            query = "SELECT ts, temp, humi FROM sensor_records WHERE device_id = ? AND ts >= ? AND ts <= ? ORDER BY ts ASC LIMIT 3000"
            params = (device_id, start_ts, end_ts)
        else:
            # 快捷天数查询 (默认 1 天)
            actual_days = days if days is not None else 1
            cutoff_ts = int(time.time()) - actual_days * 86400
            query = "SELECT ts, temp, humi FROM sensor_records WHERE device_id = ? AND ts >= ? ORDER BY ts ASC LIMIT 3000"
            params = (device_id, cutoff_ts)
            
        async with db.execute(query, params) as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]

async def get_all_devices() -> List[str]:
    """
    获取系统中所有已注册固件的设备 ID 列表 (用于前端向后兼容拉取)
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        async with db.execute("SELECT device_id FROM registered_devices ORDER BY device_id ASC") as cursor:
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

async def is_device_registered(device_id: str) -> bool:
    """
    检查指定设备 ID 是否已注册
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        async with db.execute(
            "SELECT 1 FROM registered_devices WHERE device_id = ? LIMIT 1",
            (device_id,)
        ) as cursor:
            row = await cursor.fetchone()
            return row is not None

async def get_registered_devices() -> List[Dict[str, Any]]:
    """
    获取系统中所有已注册的固件设备列表 (包含最新的 IP 信息)
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT device_id, device_name, group_name, device_ip, created_at FROM registered_devices ORDER BY created_at ASC"
        ) as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]

async def get_device_by_id(device_id: str) -> Optional[Dict[str, Any]]:
    """
    获取特定设备的所有注册信息
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT device_id, device_name, group_name, device_ip, created_at FROM registered_devices WHERE device_id = ?",
            (device_id,)
        ) as cursor:
            row = await cursor.fetchone()
            return dict(row) if row else None

async def register_device(device_id: str, device_name: str, group_name: str, device_ip: str = ""):
    """
    注册一个新设备，如果已存在则更新其名称和分组信息 (支持记录 IP 且不覆盖空)
    """
    now_ts = int(time.time())
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        await db.execute("""
            INSERT INTO registered_devices (device_id, device_name, group_name, device_ip, created_at)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                device_name = excluded.device_name,
                group_name = excluded.group_name,
                device_ip = CASE WHEN excluded.device_ip <> '' THEN excluded.device_ip ELSE device_ip END
        """, (device_id, device_name, group_name, device_ip, now_ts))
        await db.commit()

async def update_device_ip_and_name_if_changed(device_id: str, device_name: str, device_ip: str):
    """
    在固件端上报数据时，自动更新设备的最新局域网 IP 地址，
    如果当前服务端未对别名命名（或以默认的'设备'开头且上传了不同的自定义名称），则也进行同步更新
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute("SELECT device_name, device_ip FROM registered_devices WHERE device_id = ?", (device_id,)) as cursor:
            row = await cursor.fetchone()
            if row:
                db_name = row['device_name']
                db_ip = row['device_ip']
                
                needs_update = False
                update_fields = []
                params = []
                
                if db_ip != device_ip and device_ip != "":
                    update_fields.append("device_ip = ?")
                    params.append(device_ip)
                    needs_update = True
                    
                if (db_name == "" or db_name.startswith("设备 ")) and device_name != "" and device_name != db_name:
                    update_fields.append("device_name = ?")
                    params.append(device_name)
                    needs_update = True
                    
                if needs_update:
                    params.append(device_id)
                    await db.execute(f"UPDATE registered_devices SET {', '.join(update_fields)} WHERE device_id = ?", tuple(params))
                    await db.commit()

async def unregister_device(device_id: str):
    """
    注销指定设备，即从注册设备表中将其删除
    """
    async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
        await db.execute("DELETE FROM registered_devices WHERE device_id = ?", (device_id,))
        await db.commit()
