# =============================================================================
# 游戏微服务 - 单镜像版本 Dockerfile
#
# 特点:
# - 单个镜像包含所有服务
# - 通过 ENTRYPOINT 参数选择启动的服务
# - 适合开发环境和快速部署
# - 避免 BuildKit 缓存问题
#
# 构建命令:
#   cd /path/to/MicroserviceDemo
#   docker build -f docker/single-image/Dockerfile -t game-microservices:latest .
#
# 运行命令:
#   docker run game-microservices:latest service_registry
#   docker run game-microservices:latest user_service
#   docker run game-microservices:latest auth_service
#   docker run game-microservices:latest game_data_service
#   docker run game-microservices:latest gomoku_server
#
# 使用 docker-compose:
#   docker-compose -f docker/single-image/docker-compose.yml up -d
# =============================================================================

FROM ubuntu:22.04

LABEL maintainer="game-microservices"
LABEL description="All-in-one game microservices image (single-image version)"
LABEL version="single-image"

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 使用阿里云镜像源
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 安装所有编译和运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    # 编译工具链
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    # SSL 和网络
    libssl-dev \
    libcurl4-openssl-dev \
    uuid-dev \
    # Boost 库
    libboost-system-dev \
    libboost-thread-dev \
    libboost-filesystem-dev \
    libboost-chrono-dev \
    libboost-atomic-dev \
    # 序列化和数据格式
    libprotobuf-dev \
    protobuf-compiler \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    # 数据库驱动
    libmysqlcppconn-dev \
    libmysqlclient-dev \
    libmongoc-dev \
    libbson-dev \
    # 缓存和消息队列
    libhiredis-dev \
    librdkafka-dev \
    # WebSocket
    libwebsocketpp-dev \
    # 测试框架
    libgtest-dev \
    # 其他工具
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 复制第三方依赖（需预先下载到 docker/shared/third_party/ 目录）
# 下载命令: cd docker/shared/third_party && git clone --depth 1 https://github.com/Thalhammer/jwt-cpp.git
# 下载命令: cd docker/shared/third_party && git clone --depth 1 https://github.com/trusch/libbcrypt.git
COPY docker/shared/third_party/jwt-cpp /tmp/jwt-cpp
RUN cd /tmp/jwt-cpp && mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DJWT_BUILD_EXAMPLES=OFF \
             -DJWT_CMAKE_FILES_INSTALL_DIR=/usr/lib/cmake/jwt-cpp && \
    make install && rm -rf /tmp/jwt-cpp

COPY docker/shared/third_party/libbcrypt /tmp/libbcrypt
RUN cd /tmp/libbcrypt && mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON && \
    make -j$(nproc) && make install && ldconfig && rm -rf /tmp/libbcrypt

# 复制源代码
WORKDIR /build
COPY CMakeLists.txt .
COPY src ./src
COPY include ./include
COPY config ./config

# 编译所有服务
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja && \
    ninja -j$(nproc) && \
    echo "=== 编译完成，检查生成的二进制文件 ===" && \
    ls -la bin/ && \
    echo "=== 检查库文件 ===" && \
    ls -la lib/ 2>/dev/null || true && \
    echo "=== 检查子目录中的可执行文件 ===" && \
    find . -type f -executable -name "*_service*" -o -name "*_server" 2>/dev/null || true

# 创建应用目录并复制二进制文件
RUN mkdir -p /app/bin /app/config /app/logs /app/data && \
    echo "=== 复制二进制文件 ===" && \
    cp -v /build/build/bin/* /app/bin/ 2>/dev/null || echo "No files in bin/" && \
    chmod +x /app/bin/* && \
    echo "=== 最终 /app/bin 内容 ===" && \
    ls -la /app/bin/ && \
    ldconfig

# 复制配置文件（使用环境变量化的配置）
COPY docker/shared/config/services/ /app/config/

# 安装 envsubst 工具（用于环境变量替换）
RUN apt-get update && apt-get install -y --no-install-recommends gettext-base && \
    rm -rf /var/lib/apt/lists/*

# 复制入口脚本
COPY docker/shared/scripts/entrypoint.sh /app/entrypoint-base.sh

# 创建启动脚本（包装 entrypoint-base.sh 以支持服务选择）
RUN echo '#!/bin/bash' > /app/entrypoint.sh && \
    echo 'set -e' >> /app/entrypoint.sh && \
    echo 'SERVICE_NAME="${1:-service_registry}"' >> /app/entrypoint.sh && \
    echo 'export SERVICE_NAME' >> /app/entrypoint.sh && \
    echo 'CONFIG_FILE="/app/config/${SERVICE_NAME}.yml"' >> /app/entrypoint.sh && \
    echo 'if [ "$SERVICE_NAME" = "auth_service" ]; then' >> /app/entrypoint.sh && \
    echo '    CONFIG_FILE="/app/config/auth_service_config.yml"' >> /app/entrypoint.sh && \
    echo 'fi' >> /app/entrypoint.sh && \
    echo '' >> /app/entrypoint.sh && \
    echo '# 设置配置目录环境变量' >> /app/entrypoint.sh && \
    echo 'export CONFIG_DIR=/app/config' >> /app/entrypoint.sh && \
    echo '' >> /app/entrypoint.sh && \
    echo '# 调用基础入口脚本进行环境变量替换' >> /app/entrypoint.sh && \
    echo 'source /app/entrypoint-base.sh' >> /app/entrypoint.sh && \
    echo '' >> /app/entrypoint.sh && \
    echo 'echo "========================================"' >> /app/entrypoint.sh && \
    echo 'echo "Starting ${SERVICE_NAME}"' >> /app/entrypoint.sh && \
    echo 'echo "Config: ${CONFIG_FILE}"' >> /app/entrypoint.sh && \
    echo 'echo "Binary: /app/bin/${SERVICE_NAME}"' >> /app/entrypoint.sh && \
    echo 'echo "========================================"' >> /app/entrypoint.sh && \
    echo '' >> /app/entrypoint.sh && \
    echo 'if [ ! -f "/app/bin/${SERVICE_NAME}" ]; then' >> /app/entrypoint.sh && \
    echo '    echo "ERROR: Binary not found: /app/bin/${SERVICE_NAME}"' >> /app/entrypoint.sh && \
    echo '    ls -la /app/bin/' >> /app/entrypoint.sh && \
    echo '    exit 1' >> /app/entrypoint.sh && \
    echo 'fi' >> /app/entrypoint.sh && \
    echo 'if [ ! -f "${CONFIG_FILE}" ]; then' >> /app/entrypoint.sh && \
    echo '    echo "ERROR: Config not found: ${CONFIG_FILE}"' >> /app/entrypoint.sh && \
    echo '    ls -la /app/config/' >> /app/entrypoint.sh && \
    echo '    exit 1' >> /app/entrypoint.sh && \
    echo 'fi' >> /app/entrypoint.sh && \
    echo 'exec /app/bin/${SERVICE_NAME} -c "${CONFIG_FILE}"' >> /app/entrypoint.sh && \
    chmod +x /app/entrypoint.sh /app/entrypoint-base.sh

# 创建非 root 用户
RUN groupadd -r gameuser && useradd -r -g gameuser gameuser && \
    chown -R gameuser:gameuser /app

USER gameuser
WORKDIR /app

# 默认启动服务注册中心
ENTRYPOINT ["/app/entrypoint.sh"]
CMD ["service_registry"]
