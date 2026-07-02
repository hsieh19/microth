import time
import logging
import aiosqlite
from apscheduler.schedulers.asyncio import AsyncIOScheduler
from config import settings

logger = logging.getLogger("scheduler")

# 创建异步定时任务调度器
scheduler = AsyncIOScheduler()

async def cleanup_old_data():
    """
    定时任务：删除 90 天之前的历史传感器数据，并通过 VACUUM 命令释放 SQLite 空闲磁盘空间 (修复中等缺陷 4)
    """
    logger.info("[Scheduler] 启动每日数据清理任务...")
    
    # 90天转换成秒
    retention_sec = settings.DATA_RETENTION_DAYS * 86400
    cutoff_ts = int(time.time()) - retention_sec
    
    try:
        async with aiosqlite.connect(settings.DB_PATH, timeout=20.0) as db:
            # 1. 删除超时数据
            async with db.execute("DELETE FROM sensor_records WHERE ts < ?", (cutoff_ts,)) as cursor:
                deleted_rows = cursor.rowcount
            
            await db.commit()
            logger.info(f"[Scheduler] 成功清理 {deleted_rows} 条超期历史数据 (截止时间戳: {cutoff_ts})。")

            # S4 修复：WAL 模式下不能使用 VACUUM
            # VACUUM 在部分 SQLite 版本会破坏 WAL 日志模式设置，且执行期间加排他锁影响并发
            # 改用 PRAGMA incremental_vacuum，以增量方式安全释放空闲页，WAL 模式兼容
            logger.info("[Scheduler] 正在执行 SQLite 增量磁盘空间收缩 (incremental_vacuum)...")
            await db.execute("PRAGMA incremental_vacuum(1000);")
            await db.commit()
            logger.info("[Scheduler] 磁盘空间收缩完成（释放最多 1000 个空闲页）。")
            
    except Exception as e:
        logger.error(f"[Scheduler] 清理数据任务发生错误: {str(e)}")

def start_scheduler():
    """
    启动定时任务。每天凌晨 3:00 执行一次清理任务。
    """
    if not scheduler.running:
        scheduler.add_job(
            cleanup_old_data, 
            trigger="cron", 
            hour=3, 
            minute=0, 
            id="db_cleanup_job",
            replace_existing=True
        )
        scheduler.start()
        logger.info("[Scheduler] 定时清理任务调度器已成功启动。(触发时间: 每日 03:00)")

def shutdown_scheduler():
    """
    关闭定时任务
    """
    if scheduler.running:
        scheduler.shutdown()
        logger.info("[Scheduler] 定时任务调度器已关闭。")
