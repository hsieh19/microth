import httpx
import time
import logging
from config import settings
import database

logger = logging.getLogger("alerts")

async def send_feishu_card(template: str, title: str, content: str):
    """
    异步发送飞书卡片消息
    """
    if not settings.FEISHU_WEBHOOK:
        logger.info(f"[Alert] 飞书 Webhook 未配置，跳过发送消息：【{title}】 - {content}")
        return

    # 飞书卡片 JSON 结构定义
    payload = {
        "msg_type": "interactive",
        "card": {
            "config": {
                "wide_screen_mode": True
            },
            "header": {
                "template": template,  # "red" 代表告警, "green" 代表恢复
                "title": {
                    "content": title,
                    "tag": "plain_text"
                }
            },
            "elements": [
                {
                    "tag": "div",
                    "text": {
                        "content": content,
                        "tag": "lark_md"
                    }
                },
                {
                    "tag": "hr"
                },
                {
                    "tag": "note",
                    "elements": [
                        {
                            "tag": "plain_text",
                            "content": f"通知时间: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())}"
                        }
                    ]
                }
            ]
        }
    }

    try:
        async with httpx.AsyncClient(timeout=10.0) as client:
            response = await client.post(settings.FEISHU_WEBHOOK, json=payload)
            if response.status_code == 200:
                logger.info(f"[Alert] 飞书卡片消息推送成功：{title}")
            else:
                logger.error(f"[Alert] 飞书卡片推送失败，状态码: {response.status_code}, 响应: {response.text}")
    except Exception as e:
        logger.error(f"[Alert] 飞书接口请求发生异常: {str(e)}")

async def check_and_trigger_alerts(device_id: str, temp: float, humi: float):
    """
    状态机：判断温湿度并触发告警，实现 NVS/SQLite 状态持久化的 30 分钟防轰炸冷却及恢复通知
    """
    now = int(time.time())
    
    # 1. 从 SQLite 读取当前设备的历史告警状态 (修复中等缺陷 5)
    alert_info = await database.get_alert_state(device_id)
    is_alerting = alert_info["is_alerting"]
    last_alert_ts = alert_info["last_alert_ts"]
    
    # 2. 判断是否满足报警阈值
    temp_exceeded = temp > settings.TEMP_ALERT_THRESHOLD
    humi_exceeded = humi > settings.HUMI_ALERT_THRESHOLD
    is_over_threshold = temp_exceeded or humi_exceeded

    # 3. 告警判断逻辑
    if is_over_threshold:
        # 判断是否需要发送告警 (处于正常状态，或者处于告警状态但已过 30 分钟冷却期)
        cooldown_over = (now - last_alert_ts) >= settings.ALERT_COOLDOWN_SEC
        
        if is_alerting == 0 or cooldown_over:
            # 构建警报卡片文本
            status_text = ""
            if temp_exceeded:
                status_text += f"⚠️ **当前温度**: {temp:.2f} °C (超出阈值 {settings.TEMP_ALERT_THRESHOLD:.1f} °C)\n"
            else:
                status_text += f"🟢 温度指标: {temp:.2f} °C\n"
                
            if humi_exceeded:
                status_text += f"⚠️ **当前湿度**: {humi:.2f} % (超出阈值 {settings.HUMI_ALERT_THRESHOLD:.1f} %)\n"
            else:
                status_text += f"🟢 湿度指标: {humi:.2f} %\n"

            content = f"**设备标识**: `{device_id}`\n\n{status_text}"
            if is_alerting == 1:
                content += f"\n*注：该设备持续异常，已过 {settings.ALERT_COOLDOWN_SEC // 60} 分钟冷却期，进行重复警报。*"

            title = "🔴 设备温湿度异常警报"
            
            # 发送警报
            await send_feishu_card(template="red", title=title, content=content)
            
            # 状态持久化更新 (is_alerting = 1, 更新报警时间戳)
            await database.update_alert_state(device_id, 1, now)
            
    else:
        # 4. 恢复通知逻辑 (如果原先处于告警状态，现在回落到正常值)
        if is_alerting == 1:
            title = "🟢 设备温湿度恢复正常"
            content = f"**设备标识**: `{device_id}`\n\n**当前数值**:\n🟢 温度: {temp:.2f} °C\n🟢 湿度: {humi:.2f} %\n\n该设备各项温湿度指标均已降至安全警戒线以下，警报解除。"
            
            # 发送恢复
            await send_feishu_card(template="green", title=title, content=content)
            
            # 状态持久化复位 (is_alerting = 0, 时间戳清零)
            await database.update_alert_state(device_id, 0, 0)

async def check_sensor_status_and_alert(
    device_id: str, 
    temp: float, 
    humi: float, 
    sensor_alert_enabled: bool,
    device_name: str = "",
    device_ip: str = "",
    last_record: dict = None
):
    """
    检测传感器掉线/恢复状态，并在开启了报警时向飞书推送卡片
    """
    if not sensor_alert_enabled:
        return

    # 1. 判定前一状态
    is_last_fault = False
    if last_record and last_record.get("temp") == -999.0:
        is_last_fault = True

    # 2. 判定当前状态
    is_current_fault = (temp == -999.0)

    display_name = device_name if device_name else "未命名设备"

    # 3. 状态转换检测与飞书卡片推送
    if not is_last_fault and is_current_fault:
        # 正常 -> 异常
        title = "🔴 传感器断连/故障告警"
        content = (
            f"**设备标识**: `{device_id}`\n"
            f"**设备名称**: `{display_name}`\n"
            f"**设备 IP**: `{device_ip}`\n\n"
            f"🚨 **警报原因**: 远程监测终端检测到 **温湿度传感器未连接或读取故障**，已无法采集实时环境数据！\n"
            f"⚠️ **排查建议**: 请尽快派员现场检查板卡传感器接线或 SHT40 芯片运行状态。"
        )
        await send_feishu_card(template="red", title=title, content=content)
        
    elif is_last_fault and not is_current_fault:
        # 异常 -> 正常
        title = "🟢 传感器状态已恢复"
        content = (
            f"**设备标识**: `{device_id}`\n"
            f"**设备名称**: `{display_name}`\n"
            f"**设备 IP**: `{device_ip}`\n\n"
            f"✅ **恢复原因**: 监测终端的温湿度传感器 **已重新连接成功，读数恢复正常**。\n"
            f"🟢 **当前温度**: {temp:.2f} °C\n"
            f"🟢 **当前湿度**: {humi:.2f} %"
        )
        await send_feishu_card(template="green", title=title, content=content)
