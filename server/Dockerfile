# ==============================================================================
# 阶段 1：构建器 (Builder)
# - 专职用于安装依赖库，下载并编译 Python 依赖包。
# ==============================================================================
FROM python:3.11-slim AS builder

WORKDIR /app

# 安装依赖项 (利用 --user 选项将包与可执行程序归口放置在 /root/.local 下)
COPY requirements.txt .
RUN pip install --user --no-cache-dir -r requirements.txt


# ==============================================================================
# 阶段 2：运行器 (Runner)
# - 极致瘦身的最小系统运行期镜像，不包含任何 pip 安装缓存和多余开发配置文件。
# ==============================================================================
FROM python:3.11-slim AS runner

WORKDIR /app

# 1. 仅从构建器复制安装好的 Python 依赖库与可执行路径，完全剔除缓存
COPY --from=builder /root/.local /root/.local

# 2. 将用户本地 bin 添加至环境变量，使系统能够直接寻找到 uvicorn
ENV PATH=/root/.local/bin:$PATH
ENV PYTHONUNBUFFERED=1

# 3. 仅复制程序运行必需的代码文件与静态模板，排除 Dockerfile、README 等一切开发无关项
COPY alerts.py .
COPY config.py .
COPY database.py .
COPY main.py .
COPY scheduler.py .
COPY version.json .
COPY templates/ ./templates/

# 4. 预先创建持久化所需的空目录 (供 docker-compose 进行挂载，保持容器权限清洁)
RUN mkdir -p data logs

# 暴露接口服务端口
EXPOSE 8000

# 启动最小系统
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
