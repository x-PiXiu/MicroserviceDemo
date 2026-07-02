# =============================================================================
# 游戏微服务 - 多阶段构建版本 Dockerfile
#
# 特点:
# - 分层构建，每层独立缓存
# - 优化 COPY 顺序，最大化利用层缓存
# - 支持 BuildKit 和传统 Docker 构建
# - 更好的错误处理和调试支持
#
# 构建命令:
#   cd /path/to/MicroserviceDemo
#   docker build -f docker/multi-stage/Dockerfile -t game-service-registry:latest --target service_registry .
#
# 使用 docker-compose:
#   docker-compose -f docker/multi-stage/docker-compose.yml up -d
# =============================================================================

# 构建参数
ARG BUILD_TYPE=Release
ARG BUILD_JOBS=4
ARG UBUNTU_VERSION=22.04

ARG TARGETPLATFORM=linux/amd64

# =============================================================================
# 阶段 1: 构建基础镜像 - 包含所有编译依赖
# =============================================================================
FROM ubuntu:${UBUNTU_VERSION} AS builder-base

LABEL maintainer="game-microservices"
LABEL stage="builder-base"

ARG BUILD_TYPE
ARG BUILD_JOBS

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 使用阿里云镜像源加速
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 安装编译工具和依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git gdb \
    libssl-dev libcurl4-openssl-dev uuid-dev \
    libboost-system-dev libboost-thread-dev libboost-filesystem-dev \
    libboost-chrono-dev libboost-atomic-dev \
    libprotobuf-dev protobuf-compiler libyaml-cpp-dev nlohmann-json3-dev \
    libmysqlcppconn-dev libmysqlclient-dev \
    libhiredis-dev librdkafka-dev \
    libwebsocketpp-dev \
    libgtest-dev \
    ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

# 复制第三方依赖（从本地目录，需预先下载）
# 下载命令: cd docker/shared/third_party && git clone --depth 1 https://github.com/Thalhammer/jwt-cpp.git
# 下载命令: cd docker/shared/third_party && git clone --depth 1 https://github.com/trusch/libbcrypt.git
COPY docker/shared/third_party/jwt-cpp /tmp/jwt-cpp
COPY docker/shared/third_party/libbcrypt /tmp/libbcrypt

# 编译安装 jwt-cpp
RUN cd /tmp/jwt-cpp && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DJWT_BUILD_EXAMPLES=OFF \
             -DJWT_CMAKE_FILES_INSTALL_DIR=/usr/lib/cmake/jwt-cpp && \
    cmake --install build && \
    rm -rf /tmp/jwt-cpp

# 编译安装 libbcrypt
RUN cd /tmp/libbcrypt && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON && \
    cmake --build build -j${BUILD_JOBS} && \
    cmake --install build && \
    ldconfig && \
    rm -rf /tmp/libbcrypt

WORKDIR /build

# =============================================================================
# 阶段 2: 编译所有服务
# =============================================================================
FROM builder-base AS builder

LABEL stage="builder"

ARG BUILD_JOBS

# 复制源代码和配置
COPY CMakeLists.txt .
COPY include ./include
COPY src ./src
COPY config ./config

# 编译所有服务
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -GNinja && \
    ninja -j${BUILD_JOBS}

# =============================================================================
# 阶段 3: 运行时基础镜像 - 仅包含运行时依赖
# =============================================================================
FROM ubuntu:${UBUNTU_VERSION} AS runtime-base

LABEL maintainer="game-microservices"
LABEL stage="runtime-base"

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 使用阿里云镜像源加速
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 安装运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libcurl4 libuuid1 \
    libboost-system1.74.0 libboost-thread1.74.0 libboost-filesystem1.74.0 \
    libboost-chrono1.74.0 libboost-atomic1.74.0 \
    libprotobuf23 libyaml-cpp0.7 \
    libmysqlclient21 \
    libhiredis0.14 \
    librdkafka++1 \
    ca-certificates libgcc-s1 libstdc++6 curl \
    && rm -rf /var/lib/apt/lists/*

# 从构建阶段复制运行时库
COPY --from=builder-base /usr/local/lib/ /tmp/libs/
RUN mkdir -p /usr/local/lib && \
    if ls /tmp/libs/*.so* 1> /dev/null 2>&1; then \
        cp /tmp/libs/*.so* /usr/local/lib/ 2>/dev/null || true; \
    fi && \
    rm -rf /tmp/libs

# 复制 MySQL Connector/C++ 运行时库
COPY --from=builder-base /usr/lib/x86_64-linux-gnu/libmysqlcppconn.so* /usr/lib/x86_64-linux-gnu/

# 更新动态链接库缓存
RUN ldconfig

# 安装 envsubst 工具（用于环境变量替换）
RUN apt-get update && apt-get install -y --no-install-recommends gettext-base && \
    rm -rf /var/lib/apt/lists/*

# 复制入口脚本
COPY docker/shared/scripts/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

# 创建非 root 用户
RUN groupadd -r gameuser && useradd -r -g gameuser gameuser && \
    mkdir -p /app/config /app/logs /app/data && \
    chown -R gameuser:gameuser /app

WORKDIR /app

# 设置入口点
ENTRYPOINT ["/app/entrypoint.sh"]

# =============================================================================
# 阶段 4-8: 各服务镜像
# =============================================================================

# 服务注册中心
FROM runtime-base AS service_registry
LABEL service="service_registry"

COPY --from=builder /build/build/bin/service_registry /app/
COPY docker/shared/config/services/service_registry.yml /app/config/

RUN chown -R gameuser:gameuser /app
USER gameuser
EXPOSE 8090
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8090/api/v1/services/health || exit 1
ENV SERVICE_NAME=service_registry
CMD ["/app/service_registry", "-c", "/app/config/service_registry.yml"]

# 用户服务
FROM runtime-base AS user_service
LABEL service="user_service"

COPY --from=builder /build/build/bin/user_service /app/
COPY docker/shared/config/services/user_service.yml /app/config/

RUN chown -R gameuser:gameuser /app
USER gameuser
EXPOSE 8082
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8082/api/v1/user/health || exit 1
ENV SERVICE_NAME=user_service
CMD ["/app/user_service", "-c", "/app/config/user_service.yml"]

# 认证服务
FROM runtime-base AS auth_service
LABEL service="auth_service"

COPY --from=builder /build/build/bin/auth_service /app/
COPY docker/shared/config/services/auth_service_config.yml /app/config/

RUN chown -R gameuser:gameuser /app
USER gameuser
EXPOSE 8083
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8083/api/v1/auth/health || exit 1
ENV SERVICE_NAME=auth_service
CMD ["/app/auth_service", "-c", "/app/config/auth_service_config.yml"]

# 游戏数据服务
FROM runtime-base AS game_data_service
LABEL service="game_data_service"

COPY --from=builder /build/build/bin/game_data_service /app/
COPY docker/shared/config/services/game_data_service.yml /app/config/

RUN chown -R gameuser:gameuser /app
USER gameuser
EXPOSE 8084
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8084/api/v1/gamedata/health || exit 1
ENV SERVICE_NAME=game_data_service
CMD ["/app/game_data_service", "-c", "/app/config/game_data_service.yml"]

# 五子棋游戏服务
FROM runtime-base AS gomoku_server
LABEL service="gomoku_server"

COPY --from=builder /build/build/bin/gomoku_server /app/
COPY docker/shared/config/services/gomoku_server.yml /app/config/

RUN chown -R gameuser:gameuser /app
USER gameuser
EXPOSE 8085 8086
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8085/api/v1/gomoku/health || exit 1
ENV SERVICE_NAME=gomoku_server
CMD ["/app/gomoku_server", "-c", "/app/config/gomoku_server.yml"]
