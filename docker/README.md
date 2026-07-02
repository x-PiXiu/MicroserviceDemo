# 游戏微服务 Docker 部署指南

本目录包含两种 Docker 部署方式，适用于不同的使用场景。

## 目录结构

```
docker/
├── multi-stage/                  # 多阶段构建版本（推荐生产环境）
│   ├── Dockerfile               # 多阶段构建 Dockerfile（每服务独立镜像）
│   ├── docker-compose.yml       # Docker Compose 编排文件
│   ├── .env                     # 环境变量配置（需从 .env.example 复制）
│   └── deploy.sh                # 一键部署脚本
├── single-image/                 # 单镜像版本（推荐开发/测试/云端快速部署）
│   ├── Dockerfile               # 单镜像 Dockerfile（所有服务打包为一个镜像）
│   ├── docker-compose.yml       # Docker Compose 编排文件
│   └── deploy.sh                # 一键部署脚本
├── shared/                       # 共享资源
│   ├── config/                   # 配置文件
│   │   ├── nginx.conf           # Nginx 主配置（容器内网关）
│   │   ├── host-nginx.conf      # 宿主机 Nginx 配置（边缘代理）
│   │   ├── redis.conf           # Redis 配置
│   │   ├── conf.d/
│   │   │   └── game-platform.conf  # API 路由规则
│   │   └── services/            # 各服务 YAML 配置（支持环境变量替换）
│   │       ├── service_registry.yml
│   │       ├── user_service.yml
│   │       ├── auth_service_config.yml
│   │       ├── game_data_service.yml
│   │       └── gomoku_server.yml
│   ├── init/                     # 数据库初始化脚本
│   │   └── mysql/
│   │       ├── 01_user_service_db.sql
│   │       ├── 02_auth_sessions_db.sql
│   │       ├── 03_game_service_db.sql
│   │       └── 04_gomoku_game_db.sql
│   ├── scripts/                  # 启动脚本
│   │   └── entrypoint.sh        # 容器入口脚本（环境变量替换）
│   └── third_party/              # 第三方依赖（需手动下载）
│       ├── jwt-cpp/             # JWT C++ 库
│       └── libbcrypt/           # BCrypt 库
└── README.md                     # 本文档
```

## 部署方式对比

| 特性 | 多阶段构建 | 单镜像版 |
|------|-----------|--------|
| 镜像数量 | 6 个独立镜像 | 1 个镜像 |
| 构建时间 | 较长（首次） | 较短 |
| 缓存支持 | 支持 BuildKit 缓存 | 不依赖 BuildKit |
| 磁盘占用 | 较小（每服务独立） | 较大（包含所有服务） |
| 更新效率 | 高（只重建修改的服务） | 低（需要完全重建） |
| 云端推送 | 需推送多个镜像 | 只需推送一个镜像 |
| 适用场景 | **生产环境** | **开发/测试/云端快速部署** |

## 服务架构

```
┌─────────────────────────────────────────────────────┐
│                    云服务器                           │
│                                                     │
│  ┌──────────────┐     ┌──────────────────────────┐  │
│  │  宿主机 Nginx │────→│   容器 Nginx (端口 10080) │  │
│  │  (端口 80/443)│     │     API 网关 / 路由分发   │  │
│  └──────────────┘     └────────────┬─────────────┘  │
│                                    │                 │
│              ┌─────────────────────┼─────────────┐   │
│              │         Docker 内部网络             │   │
│              │                                     │   │
│              │  ┌──────────────────┐               │   │
│              │  │ service_registry │               │   │
│              │  │    (端口 8090)    │               │   │
│              │  └──────────────────┘               │   │
│              │                                     │   │
│              │  ┌──────────┐  ┌──────────┐         │   │
│              │  │ user_svc │  │ auth_svc │         │   │
│              │  │ (8082)   │  │ (8083)   │         │   │
│              │  └──────────┘  └──────────┘         │   │
│              │                                     │   │
│              │  ┌──────────────┐  ┌────────────┐   │   │
│              │  │ game_data_svc│  │ gomoku_svc  │   │   │
│              │  │   (8084)     │  │ (8085/8086) │   │   │
│              │  └──────────────┘  └────────────┘   │   │
│              │                                     │   │
│              │  ┌──────────┐  ┌──────────┐         │   │
│              │  │  MySQL   │  │  Redis   │         │   │
│              │  │ (13306)  │  │ (16379)  │         │   │
│              │  └──────────┘  └──────────┘         │   │
│              └─────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

## 数据库说明

本项目使用 4 个独立数据库，每个微服务拥有自己的数据库：

| 数据库 | 所属服务 | 表数量 | 说明 |
|--------|---------|--------|------|
| `user_service_db` | user_service | 5 | 用户基础信息、档案、偏好、会话、审计日志 |
| `auth_sessions_db` | auth_service | 6 | JWT 黑名单、会话、审计、限流、安全事件、配置 |
| `game_service_db` | game_data_service | 8 | 游戏档案、成就、库存、货币、排行榜等 |
| `gomoku_game_db` | gomoku_server | 7 | 房间、对局记录、落子、观战、聊天、配置 |

MySQL 容器首次启动时会自动执行 `docker/shared/init/mysql/` 目录下的 SQL 文件（按文件名排序），自动创建所有数据库、表结构、存储过程和初始数据。

**重要**：SQL 初始化仅在**首次创建 Volume 时**执行。如需重新初始化，需先删除 Volume：
```bash
docker volume rm game-microservices_mysql-data
```

---

## 快速开始

### 前置条件

- Docker 20.10+
- Docker Compose v2（或 docker-compose 1.29+）
- Git（用于下载第三方依赖）
- 至少 4GB 可用内存
- 至少 10GB 可用磁盘空间

验证环境：
```bash
docker --version
docker compose version
git --version
```

### 第一步：下载第三方依赖

```bash
cd docker/shared/third_party

# JWT C++ 库
git clone --depth 1 https://github.com/Thalhammer/jwt-cpp.git

# BCrypt 库
git clone --depth 1 https://github.com/trusch/libbcrypt.git
```

### 第二步：配置环境变量

根据所选部署方式，复制并修改环境变量文件：

```bash
cd /path/to/MicroserviceDemo

# 多阶段构建版本
cp docker/shared/.env.example docker/multi-stage/.env

# 或 单镜像版本（不需要单独的 .env，使用 docker/.env）
cp docker/shared/.env.example docker/.env
```

关键配置项说明：

```bash
# ===== 必须修改的配置 =====

# MySQL 密码（生产环境必须使用强密码）
MYSQL_ROOT_PASSWORD=YourStrongPassword123!

# Redis 密码
REDIS_PASSWORD=YourRedisPassword456!

# JWT 密钥（至少 32 位，包含大小写字母和数字）
JWT_SECRET_KEY=YourJwtSecretKeyAtLeast32CharactersLong
SESSION_ENCRYPTION_KEY=YourSessionEncryptionKeyAtLeast32Chars

# ===== 数据库模式选择 =====

# false = 使用 Docker 容器内的 MySQL（推荐初次使用）
# true  = 使用外部已有的 MySQL 服务
USE_EXTERNAL_DB=false

# false = 使用 Docker 容器内的 Redis
# true  = 使用外部已有的 Redis 服务
USE_EXTERNAL_REDIS=false

# 使用外部数据库时需配置以下项：
# MYSQL_HOST=192.168.1.100        # 外部 MySQL 地址
# MYSQL_EXTERNAL_PORT=3306         # 外部 MySQL 端口
# REDIS_HOST=192.168.1.100         # 外部 Redis 地址
# REDIS_EXTERNAL_PORT=6379         # 外部 Redis 端口
```

### 第三步：执行部署

选择以下任一方式：

---

## 方式一：单镜像版部署（推荐开发/测试/云端快速部署）

### 1.1 什么是单镜像版

单镜像版将所有 5 个微服务（service_registry、user_service、auth_service、game_data_service、gomoku_server）编译到**同一个 Docker 镜像**中，通过 `ENTRYPOINT` 参数选择启动哪个服务。适合：
- 开发和测试环境
- 快速部署到云端
- 只需推送/拉取一个镜像
- 避免 BuildKit 缓存兼容性问题

### 1.2 构建镜像

```bash
cd /path/to/MicroserviceDemo

# 标准构建
chmod +x docker/single-image/deploy.sh
./docker/single-image/deploy.sh --build

# 或完全重新构建（清理缓存 + 无缓存构建，推荐首次部署）
./docker/single-image/deploy.sh --rebuild
```

手动构建命令（等价）：
```bash
docker build -f docker/single-image/Dockerfile -t game-microservices:latest .
```

### 1.3 启动服务

#### 使用容器内数据库和 Redis（全容器化）

```bash
# 一键启动（使用 deploy.sh 脚本）
./docker/single-image/deploy.sh --start

# 或手动 docker compose
docker compose --env-file docker/.env \
  -f docker/single-image/docker-compose.yml \
  --profile internal-db \
  --profile internal-redis \
  up -d
```

#### 使用外部数据库和 Redis

```bash
# 确认 docker/.env 中已配置：
# USE_EXTERNAL_DB=true
# USE_EXTERNAL_REDIS=true
# MYSQL_HOST=你的MySQL地址
# REDIS_HOST=你的Redis地址

# 一键启动
./docker/single-image/deploy.sh --external-all --start

# 或手动 docker compose
docker compose --env-file docker/.env \
  -f docker/single-image/docker-compose.yml \
  up -d
```

**使用外部数据库时需手动初始化表结构：**
```bash
mysql -h <MYSQL_HOST> -u root -p < src/core_services/user_service/database/user_service_database_design.sql
mysql -h <MYSQL_HOST> -u root -p < src/core_services/auth_service/database/auth_sessions_database_design_fixed.sql
mysql -h <MYSQL_HOST> -u root -p < src/core_services/game_data_service/database/game_data_service_database_design.sql
mysql -h <MYSQL_HOST> -u root -p < src/game_services/gomoku/database/gomoku_database_design.sql
```

### 1.4 单镜像版常用命令

```bash
# 查看服务状态
./docker/single-image/deploy.sh --status

# 查看所有服务日志
./docker/single-image/deploy.sh --logs

# 查看指定服务日志
./docker/single-image/deploy.sh --logs user_service

# 停止所有服务
./docker/single-image/deploy.sh --stop

# 清理所有容器和镜像
./docker/single-image/deploy.sh --clean

# 清理构建缓存
./docker/single-image/deploy.sh --prune-cache
```

### 1.5 单镜像版部署到云端服务器（完整步骤）

以下是使用单镜像版部署到云服务器的完整流程：

#### 步骤 1：本地构建并推送镜像

```bash
# 在本地开发机执行

# 1) 构建镜像
cd /path/to/MicroserviceDemo
./docker/single-image/deploy.sh --build

# 2) 给镜像打标签（替换为你的镜像仓库地址）
# Docker Hub:
docker tag game-microservices:latest your-dockerhub-user/game-microservices:latest
docker push your-dockerhub-user/game-microservices:latest

# 阿里云容器镜像服务:
docker tag game-microservices:latest registry.cn-hangzhou.aliyuncs.com/your-ns/game-microservices:latest
docker push registry.cn-hangzhou.aliyuncs.com/your-ns/game-microservices:latest

# 腾讯云容器镜像服务:
docker tag game-microservices:latest ccr.ccs.tencentyun.com/your-ns/game-microservices:latest
docker push ccr.ccs.tencentyun.com/your-ns/game-microservices:latest
```

#### 步骤 2：云服务器准备

```bash
# SSH 登录云服务器
ssh root@your-server-ip

# 安装 Docker（如未安装）
curl -fsSL https://get.docker.com | sh
systemctl enable docker && systemctl start docker

# 安装 Docker Compose v2（如未安装）
apt-get install docker-compose-plugin    # Debian/Ubuntu
# yum install docker-compose-plugin      # CentOS

# 创建项目目录
mkdir -p /opt/game-microservices
cd /opt/game-microservices
```

#### 步骤 3：上传配置文件到云服务器

在云服务器上创建必要的目录结构和配置文件：

```bash
cd /opt/game-microservices

# 创建目录
mkdir -p shared/config/services
mkdir -p shared/config/conf.d
mkdir -p shared/init/mysql
mkdir -p shared/scripts
mkdir -p shared/third_party

# 从本地 SCP 上传文件（在本地执行）
# scp -r docker/shared/* root@your-server-ip:/opt/game-microservices/shared/
# scp docker/single-image/docker-compose.yml root@your-server-ip:/opt/game-microservices/
```

需要上传到云服务器的文件清单：
```
/opt/game-microservices/
├── docker-compose.yml              # 从 docker/single-image/docker-compose.yml
├── .env                            # 从 docker/shared/.env.example 修改后
└── shared/
    ├── config/
    │   ├── nginx.conf              # 容器 Nginx 配置
    │   ├── redis.conf              # Redis 配置
    │   ├── conf.d/
    │   │   └── game-platform.conf  # API 路由配置
    │   └── services/               # 5 个服务 YAML 配置
    │       ├── service_registry.yml
    │       ├── user_service.yml
    │       ├── auth_service_config.yml
    │       ├── game_data_service.yml
    │       └── gomoku_server.yml
    ├── init/mysql/                 # 数据库初始化 SQL（仅使用内部数据库时需要）
    │   ├── 01_user_service_db.sql
    │   ├── 02_auth_sessions_db.sql
    │   ├── 03_game_service_db.sql
    │   └── 04_gomoku_game_db.sql
    └── scripts/
        └── entrypoint.sh           # 容器入口脚本
```

#### 步骤 4：修改云端配置文件

```bash
cd /opt/game-microservices

# 修改 docker-compose.yml 中的镜像地址
# 将 image: game-microservices:${VERSION:-latest}
# 改为你的远程镜像地址：
# image: registry.cn-hangzhou.aliyuncs.com/your-ns/game-microservices:latest

# 修改 .env 文件
vi .env
```

云端 `.env` 关键配置（示例）：

```bash
# 使用外部数据库（推荐，云数据库更稳定）
USE_EXTERNAL_DB=true
USE_EXTERNAL_REDIS=true

# 云数据库 MySQL（内网地址，低延迟）
MYSQL_HOST=rm-xxxxx.mysql.rds.aliyuncs.com
MYSQL_EXTERNAL_PORT=3306
MYSQL_ROOT_PASSWORD=YourCloudDBPassword

# 云 Redis
REDIS_HOST=r-xxxxx.redis.rds.aliyuncs.com
REDIS_EXTERNAL_PORT=6379
REDIS_PASSWORD=YourCloudRedisPassword

# 安全配置（必须修改）
JWT_SECRET_KEY=your_production_jwt_secret_at_least_32_chars
SESSION_ENCRYPTION_KEY=your_production_session_key_at_least_32

# 运行环境
ENVIRONMENT=production
LOG_LEVEL=INFO

# 端口映射（按需修改，避免冲突）
NGINX_HTTP_PORT=10080
NGINX_HTTPS_PORT=10443
SERVICE_REGISTRY_PORT=8090
```

#### 步骤 5：在云服务器拉取并启动

```bash
cd /opt/game-microservices

# 登录镜像仓库
docker login registry.cn-hangzhou.aliyuncs.com

# 拉取镜像
docker compose pull

# 启动服务（使用外部数据库模式）
docker compose --env-file .env up -d

# 或使用内部数据库模式
docker compose --env-file .env --profile internal-db --profile internal-redis up -d

# 查看启动状态
docker compose ps
```

#### 步骤 6：配置宿主机 Nginx 反向代理

```bash
# 安装 Nginx（如未安装）
apt-get install nginx    # Ubuntu
# yum install nginx       # CentOS

# 复制宿主机 Nginx 配置
cp shared/config/host-nginx.conf /etc/nginx/conf.d/game-platform.conf

# 修改域名和 SSL 证书路径
vi /etc/nginx/conf.d/game-platform.conf
# 将 your-domain.com 替换为你的实际域名
# 将 SSL 证书路径修改为实际路径

# 测试配置
nginx -t

# 重载 Nginx
systemctl reload nginx
```

#### 步骤 7：配置 SSL 证书（可选，推荐）

```bash
# 使用 Let's Encrypt 免费证书
apt-get install certbot python3-certbot-nginx
certbot --nginx -d your-domain.com

# 或使用宝塔面板自动申请 SSL 证书
# 在宝塔面板 → 网站 → 设置 → SSL → Let's Encrypt → 申请
```

#### 步骤 8：验证部署

```bash
# 检查所有容器运行状态
docker compose ps

# 检查服务健康
curl http://localhost:8090/api/v1/services/health     # 服务注册中心
curl http://localhost:8082/api/v1/user/health          # 用户服务
curl http://localhost:8083/api/v1/auth/health          # 认证服务
curl http://localhost:8084/api/v1/gamedata/health      # 游戏数据服务
curl http://localhost:8085/api/v1/gomoku/health        # 五子棋服务
curl http://localhost:10080/health                      # Nginx 网关

# 通过域名验证（外部访问）
curl https://your-domain.com/health
curl https://your-domain.com/api/v1/services/health

# 检查数据库连接
docker compose logs user_service | grep -i mysql | head -5

# 检查日志
docker compose logs -f --tail=100
```

#### 步骤 9：配置防火墙和安全组

```bash
# 云服务器安全组需开放以下端口：
# 80    - HTTP（Nginx）
# 443   - HTTPS（Nginx）
# 8086  - WebSocket（五子棋，如需直连）

# 以下端口仅用于调试，生产环境建议关闭外网访问：
# 8090  - 服务注册中心
# 8082  - 用户服务
# 8083  - 认证服务
# 8084  - 游戏数据服务
# 8085  - 五子棋 HTTP
# 13306 - MySQL（如使用容器内数据库，绝对不要开放）
# 16379 - Redis（如使用容器内 Redis，绝对不要开放）
```

### 1.6 更新部署（重新部署流程）

当代码更新后，重新部署的步骤：

```bash
# 在本地开发机执行
cd /path/to/MicroserviceDemo

# 重新构建镜像
./docker/single-image/deploy.sh --rebuild

# 推送到镜像仓库
docker tag game-microservices:latest registry.cn-hangzhou.aliyuncs.com/your-ns/game-microservices:latest
docker push registry.cn-hangzhou.aliyuncs.com/your-ns/game-microservices:latest

# SSH 到云服务器
ssh root@your-server-ip
cd /opt/game-microservices

# 拉取新镜像并重启
docker compose pull
docker compose --env-file .env up -d

# 查看新容器状态
docker compose ps
docker compose logs -f --tail=50
```

---

## 方式二：多阶段构建部署（推荐生产环境）

### 2.1 什么是多阶段构建

多阶段构建为每个微服务生成**独立的 Docker 镜像**，适合：
- 生产环境
- 需要独立更新单个服务的场景
- 需要更细粒度资源控制的场景

### 2.2 构建与启动

```bash
cd /path/to/MicroserviceDemo

# 配置环境变量
cp docker/shared/.env.example docker/multi-stage/.env
# 编辑 docker/multi-stage/.env

# 完整部署（构建 + 启动）
chmod +x docker/multi-stage/deploy.sh
./docker/multi-stage/deploy.sh --deploy

# 使用外部数据库部署
./docker/multi-stage/deploy.sh --external-all --deploy
```

### 2.3 多阶段构建常用命令

```bash
# 查看帮助
./docker/multi-stage/deploy.sh --help

# 仅构建镜像（不启动）
./docker/multi-stage/deploy.sh --build

# 仅构建指定服务
./docker/multi-stage/deploy.sh --build-service user_service

# 仅启动服务
./docker/multi-stage/deploy.sh --start

# 查看状态
./docker/multi-stage/deploy.sh --status

# 查看日志
./docker/multi-stage/deploy.sh --logs
./docker/multi-stage/deploy.sh --logs user_service

# 修复缓存问题
./docker/multi-stage/deploy.sh --fix-cache

# 完全重新构建
./docker/multi-stage/deploy.sh --rebuild

# 使用外部数据库和 Redis
./docker/multi-stage/deploy.sh --external-all --deploy
```

---

## 环境变量配置详解

复制 `docker/shared/.env.example` 到对应目录并修改。完整配置项说明：

### 全局配置
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `COMPOSE_PROJECT_NAME` | game-microservices | Docker Compose 项目名 |
| `VERSION` | latest | 镜像版本标签 |
| `TZ` | Asia/Shanghai | 时区 |
| `ENVIRONMENT` | development | 运行环境 (development/testing/production) |
| `LOG_LEVEL` | INFO | 日志级别 (TRACE/DEBUG/INFO/WARN/ERROR) |

### 数据库模式
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `USE_EXTERNAL_DB` | false | 是否使用外部 MySQL |
| `USE_EXTERNAL_REDIS` | false | 是否使用外部 Redis |

### MySQL 配置
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `MYSQL_HOST` | mysql | MySQL 地址（容器内为 `mysql`） |
| `MYSQL_PORT` | 13306 | 映射到宿主机的端口 |
| `MYSQL_EXTERNAL_PORT` | 3306 | MySQL 实际服务端口 |
| `MYSQL_USER` | root | MySQL 用户名 |
| `MYSQL_ROOT_PASSWORD` | game2025secret | MySQL 密码（**生产环境必须修改**） |
| `MYSQL_DATABASE` | game_platform | 默认数据库 |
| `MYSQL_POOL_SIZE` | 20 | 连接池大小 |

### Redis 配置
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `REDIS_HOST` | redis | Redis 地址（容器内为 `redis`） |
| `REDIS_PORT` | 16379 | 映射到宿主机的端口 |
| `REDIS_EXTERNAL_PORT` | 6379 | Redis 实际服务端口 |
| `REDIS_PASSWORD` | | Redis 密码（**生产环境必须修改**） |
| `REDIS_DATABASE` | 0 | 默认数据库索引 |
| `REDIS_POOL_SIZE` | 10 | 连接池大小 |

### 服务端口配置
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SERVICE_REGISTRY_PORT` | 8090 | 服务注册中心端口 |
| `USER_SERVICE_PORT` | 8082 | 用户服务端口 |
| `AUTH_SERVICE_PORT` | 8083 | 认证服务端口 |
| `GAME_DATA_SERVICE_PORT` | 8084 | 游戏数据服务端口 |
| `GOMOKU_HTTP_PORT` | 8085 | 五子棋 HTTP 端口 |
| `GOMOKU_WS_PORT` | 8086 | 五子棋 WebSocket 端口 |
| `NGINX_HTTP_PORT` | 80 | Nginx HTTP 端口 |
| `NGINX_HTTPS_PORT` | 443 | Nginx HTTPS 端口 |

### 安全配置
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `JWT_SECRET_KEY` | | JWT 签名密钥（**至少 32 位**） |
| `JWT_ACCESS_TOKEN_EXPIRE` | 3600 | Access Token 过期时间（秒） |
| `JWT_REFRESH_TOKEN_EXPIRE` | 604800 | Refresh Token 过期时间（秒） |
| `SESSION_ENCRYPTION_KEY` | | 会话加密密钥 |

---

## 网络架构说明

本项目采用双层 Nginx 架构：

```
客户端 → 宿主机 Nginx (80/443, SSL终止) → 容器 Nginx (10080, 路由分发) → 微服务容器
```

### API 路由规则

| 路径 | 目标服务 | 说明 |
|------|---------|------|
| `/api/v1/services` | service_registry:8090 | 服务注册与发现 |
| `/api/v1/user` | user-service:8082 | 用户管理 |
| `/api/v1/auth` | auth-service:8083 | 认证授权 |
| `/api/v1/gamedata` | game-data-service:8084 | 游戏数据 |
| `/api/v1/gomoku` | gomoku-server:8085 | 五子棋 HTTP |
| `/ws/gomoku` | gomoku-server:8086 | 五子棋 WebSocket |
| `/ws` | gomoku-server:8086 | 通用 WebSocket |
| `/health` | Nginx 直接响应 | 健康检查 |

---

## 常见问题

### 1. 第三方依赖下载失败

```bash
cd docker/shared/third_party

# 使用镜像加速
git clone --depth 1 https://ghproxy.com/https://github.com/Thalhammer/jwt-cpp.git
git clone --depth 1 https://ghproxy.com/https://github.com/trusch/libbcrypt.git

# 或手动下载 ZIP 解压
wget https://github.com/Thalhammer/jwt-cpp/archive/refs/heads/master.zip -O jwt-cpp.zip
unzip jwt-cpp.zip && mv jwt-cpp-master jwt-cpp
```

### 2. 构建时 apt-get 源速度慢

Dockerfile 已配置阿里云镜像源。如果仍然很慢，可以替换为其他镜像：
```dockerfile
# 清华源
RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list
# 中科大源
RUN sed -i 's/archive.ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list
```

### 3. 多阶段构建缓存损坏

```bash
./docker/multi-stage/deploy.sh --fix-cache
# 或完全重建
./docker/multi-stage/deploy.sh --rebuild
```

### 4. 端口冲突

修改 `.env` 文件中的端口配置，避免与已有服务冲突：
```bash
# 例如修改 Nginx 端口
NGINX_HTTP_PORT=10080
NGINX_HTTPS_PORT=10443
```

### 5. MySQL 容器启动失败

```bash
# 查看详细日志
docker compose logs mysql

# 常见原因：
# 1. 端口被占用 → 修改 MYSQL_PORT
# 2. 内存不足 → 增加 Docker 内存限制
# 3. Volume 损坏 → 删除 Volume 重新初始化
docker volume rm game-microservices_mysql-data
```

### 6. 数据库需要重新初始化

```bash
# 停止服务
docker compose -f docker/single-image/docker-compose.yml down

# 删除 MySQL 数据卷
docker volume rm game-microservices_mysql-data

# 重新启动（会自动执行 init/mysql/ 下的 SQL）
docker compose -f docker/single-image/docker-compose.yml --profile internal-db up -d
```

### 7. 服务健康检查失败

```bash
# 查看服务日志
docker compose logs <service_name>

# 常见原因：
# 1. 数据库未就绪 → 等待 MySQL/Redis 健康检查通过
# 2. 配置文件环境变量未替换 → 检查 entrypoint.sh 是否正确执行
# 3. 端口冲突 → 检查端口是否被占用
```

### 8. WebSocket 连接失败

确保 Nginx 配置了 WebSocket 代理头：
```nginx
proxy_set_header Upgrade $http_upgrade;
proxy_set_header Connection "upgrade";
proxy_read_timeout 3600s;
```

---

## 更多信息

- [宝塔面板部署指南](../docs/deployment/baota_docker_deployment_guide.md)
- [构建问题排查](../docs/deployment/docker_build_troubleshooting.md)
