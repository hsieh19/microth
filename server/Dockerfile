FROM python:3.11-slim

# 设置工作目录
WORKDIR /app

# 安装依赖项 (分离步骤利用 Docker 缓存加速构建)
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# 复制项目代码
COPY . .

# 暴露 FastAPI 的外部访问端口
EXPOSE 8000

# 运行 Uvicorn 服务
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
