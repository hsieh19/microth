import os
import sys
import json
import hashlib
import datetime
import re
import boto3
from botocore.config import Config

# 安全的正则校验限制，防止 GitHub Actions 工作流在不安全地传入项目名或芯片名时造成路径越权
SAFE_ID_PATTERN = re.compile(r"^[a-zA-Z0-9_\-]+$")

def get_required_env(var_name):
    value = os.environ.get(var_name)
    if not value:
        print(f"[ERROR] 缺失必填环境变量: {var_name}。请在 GitHub Secrets 中进行配置。")
        sys.exit(1)
    return value

# 从环境变量中安全获取 R2 访问凭证与配置
R2_ACCESS_KEY_ID = get_required_env('R2_ACCESS_KEY_ID')
R2_SECRET_ACCESS_KEY = get_required_env('R2_SECRET_ACCESS_KEY')
R2_ACCOUNT_ID = get_required_env('R2_ACCOUNT_ID')
BUCKET_NAME = get_required_env('R2_BUCKET')

# 【microth 仓库特定的变量配置】
PROJECT_ID = os.environ.get('PROJECT_ID', 'microth')
PROJECT_NAME = os.environ.get('PROJECT_NAME', 'Microth 监视器')
PROJECT_DESC = os.environ.get('PROJECT_DESC', '环境温湿度监视器，提供温湿度及照度监测、支持 WiFi 配网、在线配置与 OTA 升级')
CHIP_PLATFORM = os.environ.get('CHIP_PLATFORM', 'ESP32C3')
VERSION = os.environ.get('FIRMWARE_VERSION', '1.0.0') # 工作流传入的编译版本号

LOCAL_FULL_BIN = os.environ.get('LOCAL_FULL_BIN', './firmware/build/microth_Full.bin') # 编译出的量产合并单 bin
LOCAL_OTA_BIN = os.environ.get('LOCAL_OTA_BIN', './firmware/build/monitor.ino.bin')     # 编译出的 OTA 包

# 安全校验项目与芯片标识符，防止路径遍历或恶意注入
if not SAFE_ID_PATTERN.match(PROJECT_ID) or not SAFE_ID_PATTERN.match(CHIP_PLATFORM):
    print(f"[ERROR] 非法的 PROJECT_ID ('{PROJECT_ID}') 或 CHIP_PLATFORM ('{CHIP_PLATFORM}') 命名规范。只能包含字母、数字、中划线及下划线。")
    sys.exit(1)

# 初始化 R2 S3 客户端
s3 = boto3.client(
    service_name='s3',
    endpoint_url=f"https://{R2_ACCOUNT_ID}.r2.cloudflarestorage.com",
    aws_access_key_id=R2_ACCESS_KEY_ID,
    aws_secret_access_key=R2_SECRET_ACCESS_KEY,
    config=Config(signature_version='s3v4')
)

def get_md5(file_path):
    """
    计算文件 MD5 值 (流式分块读取，防止超大文件溢出内存)
    """
    hash_md5 = hashlib.md5()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def publish_file(local_path, r2_folder, suffix):
    """
    上传固件到 R2 并更新对应的芯片目录清单
    """
    if not os.path.exists(local_path):
        print(f"[Warning] 找不到本地固件文件: {local_path}，跳过此类型的发布。")
        return None
        
    file_md5 = get_md5(local_path)
    
    # 强制将版本号中的特殊字符替换，保障云端文件名安全
    safe_version = re.sub(r'[^a-zA-Z0-9_\-\.]', '', VERSION)
    r2_filename = f"{PROJECT_ID}_{suffix}_v{safe_version}.bin"
    r2_key = f"{PROJECT_ID}/Firmware/{r2_folder}/{CHIP_PLATFORM}/{r2_filename}"
    
    print(f"-> 开始上传固件包到 R2: {r2_key}...")
    s3.upload_file(local_path, BUCKET_NAME, r2_key, ExtraArgs={'ContentType': 'application/octet-stream'})
    
    # 刷新并上传对应的芯片目录 manifest.json 清单
    manifest_key = f"{PROJECT_ID}/Firmware/{r2_folder}/{CHIP_PLATFORM}/manifest.json"
    manifest_data = {
        "version": VERSION,
        "filename": r2_filename,
        "md5": file_md5,
        "release_time": datetime.datetime.now(datetime.timezone.utc).isoformat().replace('+00:00', 'Z')
    }
    
    s3.put_object(
        Bucket=BUCKET_NAME,
        Key=manifest_key,
        Body=json.dumps(manifest_data, indent=2, ensure_ascii=False),
        ContentType='application/json'
    )
    print(f"-> 对应清单已刷新上传: {manifest_key}")
    return file_md5

def main():
    # 1. 刷新并上传项目基础描述信息 project.json
    project_key = f"{PROJECT_ID}/project.json"
    project_data = {
        "id": PROJECT_ID,
        "name": PROJECT_NAME,
        "description": PROJECT_DESC
    }
    s3.put_object(
        Bucket=BUCKET_NAME,
        Key=project_key,
        Body=json.dumps(project_data, indent=2, ensure_ascii=False),
        ContentType='application/json'
    )
    print(f"-> 项目全局信息注册完成: {project_key}")

    # 2. 上传量产合并包 (Full Firmware)
    publish_file(LOCAL_FULL_BIN, "Full Firmware Packages", "Full")
    
    # 3. 上传 OTA 升级包 (OTA Updates)
    publish_file(LOCAL_OTA_BIN, "OTA Updates", "OTA")
    
    print("===== 所有固件及清单自动化发布上传完成 =====")

if __name__ == "__main__":
    main()
