# 游戏微服务 Docker 容器化部署指南

## 概述

本指南详细说明如何使用**宝塔面板**部署游戏微服务项目。提供两种部署方式：

### 鸜署方式

| 方式 | 目录 | 特点 | 适用场景 |
|------|------|------|----------|
| **多阶段构建** | `docker/multi-stage/` | 每个服务独立镜像，支持 BuildKit 缓存 | 生产环境 |
| **单镜像版** | `docker/single-image/` | 单个镜像包含所有服务,避免 BuildKit 缓存问题 | 开发/测试环境 |

---

## 架构概览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           宝塔面板服务器                                  │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐                                                        │
│  │   Nginx     │  端口: 80/443 (对外)                                   │
│  │  (Docker)   │  反向代理 + WebSocket 支持                              │
│  └──────┬──────┘                                                        │
│         │                                                               │
│  ┌──────▼──────────────────────────────────────────────────────────┐   │
│  │                    game-network (Docker Network)                 │   │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  │   │
│  │  │ service_registry│  │   user_service  │  │  auth_service   │  │   │
│  │  │    端口: 8090   │  │    端口: 8082   │  │    端口: 8083   │  │   │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────┘  │   │
│  │  ┌─────────────────┐  ┌─────────────────┐                       │   │
│  │  │game_data_service│  │  gomoku_server  │                       │   │
│  │  │    端口: 8084   │  │ 8085/8086 (WS)  │                       │   │
│  │  └─────────────────┘  └─────────────────┘                       │   │
│  │  ┌─────────────────┐  ┌─────────────────┐                       │   │
│  │  │     MySQL       │  │     Redis       │  ← 可选（内部/外部）   │   │
│  │  │    端口: 3306   │  │    端口: 6379   │                       │   │
│  │  └─────────────────┘  └─────────────────┘                       │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 文件结构说明

```
MicroserviceDemo/
├── CMakeLists.txt                    # 项目构建配置
├── src/                              # 源代码
├── include/                          # 头文件
├── config/                           # 服务配置文件
│   ├── service_registry.yml
│   ├── user_service.yml
│   ├── auth_service_config.yml
│   ├── game_data_service.yml
│   └── gomoku_server.yml
└── docker/                           # Docker 相关文件
    ├── multi-stage/                  # 多阶段构建版本
    │   ├── Dockerfile
    │   ├── docker-compose.yml
    │   └── deploy.sh
    ├── single-image/                 # 单镜像版本
    │   ├── Dockerfile
    │   ├── docker-compose.yml
    │   └── deploy.sh
    ├── shared/                        # 共享资源
    │   ├── config/                   # Nginx/Redis 配置
    │   │   ├── nginx.conf
    │   │   ├── redis.conf
    │   │   └── conf.d/
    │   ├── init/                     # 初始化脚本
    │   │   └── mysql/
    │   ├── third_party/              # 第三方依赖
    │   └── .env.example              # 环境变量模板
    └── README.md                      # Docker 说明文档
```

---

## 快速开始

### 1. 上传项目代码

```bash
# SSH 登录服务器
cd /www/wwwroot/

# 克隆项目
git clone <your-repo-url> game-microservices
cd game-microservices
```

### 2. 配置环境变量

```bash
cd /www/wwwroot/game-microservices

# 复制环境变量模板到对应目录
cp docker/shared/.env.example docker/multi-stage/.env
# 或
cp docker/shared/.env.example docker/single-image/.env

# 编辑配置
nano docker/multi-stage/.env
```

### 3. 选择部署方式

#### 方式一： 多阶段构建（推荐生产环境）

```bash
cd /www/wwwroot/game-microservices

# 赋予执行权限
chmod +x docker/multi-stage/deploy.sh

# 执行部署
./docker/multi-stage/deploy.sh
```

#### 方式二: 单镜像版（推荐开发环境）

```bash
cd /www/wwwroot/game-microservices

# 赋予执行权限
chmod +x docker/single-image/deploy.sh

# 执行部署
./docker/single-image/deploy.sh
```

---

## 部署模式

### 模式一: 完全内部部署（默认）

所有服务（MySQL、Redis、微服务）都在 Docker 容器内运行。

**适用场景**：测试环境、独立部署、快速体验

### 模式二: 使用外部 MySQL

MySQL 使用已有的外部数据库,其他服务在 Docker 内运行。

**适用场景**：已有 MySQL 服务器、需要数据集中管理、生产环境

### 模式三: 使用外部 Redis

Redis 使用已有的外部缓存,其他服务在 Docker 内运行。

**适用场景**：已有 Redis 服务器、需要缓存集中管理

### 模式四: 完全外部依赖

MySQL 和 Redis 都使用外部服务,仅微服务在 Docker 内运行。

**适用场景**：生产环境、多服务共享数据库/缓存

---

## 使用外部 MySQL 和 Redis 详细配置

### 前置条件

1. **确保外部服务已运行**
   - MySQL 8.0+ 已安装并运行
   - Redis 6.0+ 已安装并运行
   - 网络连通性正常（从 Docker 宿主机可访问）

2. **数据库准备**

   ```sql
   -- 登录外部 MySQL
   mysql -u root -p

   -- 创建所需数据库
   CREATE DATABASE user_service_db CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
   CREATE DATABASE auth_sessions_db CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
   CREATE DATABASE game_service_db CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
   CREATE DATABASE gomoku_game_db CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;

   -- 创建专用用户（推荐）
   CREATE USER 'game_user'@'%' IDENTIFIED BY '你的强密码';
   GRANT ALL PRIVILEGES ON user_service_db.* TO 'game_user'@'%';
   GRANT ALL PRIVILEGES ON auth_sessions_db.* TO 'game_user'@'%';
   GRANT ALL PRIVILEGES ON game_service_db.* TO 'game_user'@'%';
   GRANT ALL PRIVILEGES ON gomoku_game_db.* TO 'game_user'@'%';
   FLUSH PRIVILEGES;
   ```

3. **Redis 准备**

   ```bash
   # 确保 Redis 配置允许远程连接
   # 编辑 redis.conf:
   # bind 0.0.0.0
   # requirepass 你的强密码
   # protected-mode no

   # 重启 Redis
   systemctl restart redis
   ```

### 配置步骤

#### 步骤 1: 修改 .env 文件

```bash
cd /www/wwwroot/game-microservices
nano docker/.env
```

#### 步骤 2: 配置外部数据库连接

```bash
# =============================================================================
# 启用外部数据库模式
# =============================================================================
USE_EXTERNAL_DB=true
USE_EXTERNAL_REDIS=true

# =============================================================================
# MySQL 外部连接配置
# =============================================================================
# 外部 MySQL 服务器地址
# - 同一服务器: 使用宿主机 IP（如 172.17.0.1 或实际内网 IP）
# - 局域网内: 使用内网 IP（如 192.168.1.100）
# - 云数据库: 使用云厂商提供的连接地址
MYSQL_HOST=10.0.12.8

# 外部 MySQL 端口
MYSQL_EXTERNAL_PORT=3306

# MySQL 用户名和密码
MYSQL_USER=root
MYSQL_ROOT_PASSWORD=你的MySQL密码

# =============================================================================
# Redis 外部连接配置
# =============================================================================
# 外部 Redis 服务器地址
REDIS_HOST=10.0.12.8

# 外部 Redis 端口
REDIS_EXTERNAL_PORT=6379

# Redis 密码
REDIS_PASSWORD=你的Redis密码
```

#### 步骤 3: 配置 MySQL 远程访问权限

**方式 A: 宝塔面板配置**

1. 登录宝塔面板
2. 进入【数据库】->【MySQL设置】
3. 点击【权限配置】
4. 添加访问权限：
   - `game_user@%` 允许所有 IP（或指定 Docker 网段）

**方式 B: 命令行配置**

```sql
-- 允许 root 远程访问（不推荐生产环境）
GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY '你的密码' WITH GRANT OPTION;
FLUSH PRIVILEGES;

-- 或者只允许特定网段
GRANT ALL PRIVILEGES ON *.* TO 'root'@'172.%.%.%' IDENTIFIED BY '你的密码' WITH GRANT OPTION;
FLUSH PRIVILEGES;
```

**修改 MySQL 配置文件**

```bash
# 编辑 MySQL 配置
nano /etc/mysql/mysql.conf.d/mysqld.cnf

# 修改 bind-address
bind-address = 0.0.0.0

# 重启 MySQL
systemctl restart mysql
```

#### 步骤 4: 配置 Redis 远程访问

```bash
# 编辑 Redis 配置
nano /etc/redis/redis.conf

# 修改以下配置
bind 0.0.0.0
protected-mode no
requirepass 你的Redis密码

# 重启 Redis
systemctl restart redis
```

#### 步骤 5: 验证网络连通性

```bash
# 测试 MySQL 连接
mysql -h 10.0.12.8 -P 3306 -u root -p

# 测试 Redis 连接
redis-cli -h 10.0.12.8 -p 6379 -a 你的密码 ping

# 测试端口可达性
nc -zv 10.0.12.8 3306
nc -zv 10.0.12.8 6379
```

#### 步骤 6: 启动服务

**多阶段构建版本**

```bash
cd /www/wwwroot/game-microservices

# 方式 1: 使用 deploy.sh 脚本（推荐）
./docker/multi-stage/deploy.sh --external-all --build

# 方式 2: 手动启动（不启动内部 MySQL/Redis）
docker-compose --env-file docker/.env -f docker/multi-stage/docker-compose.yml up -d
```

**单镜像版本**

```bash
cd /www/wwwroot/game-microservices

# 确保 .env 配置正确后
./docker/single-image/deploy.sh --build
./docker/single-image/deploy.sh --start
```

### Docker 网络访问外部服务

当容器需要访问宿主机上的服务时，需要使用特殊地址：

| 场景 | 使用地址 | 说明 |
|------|----------|------|
| Linux + Docker | `172.17.0.1` | Docker 默认网桥网关 |
| Linux + Docker | `host.docker.internal` | Docker 20.10+ 支持 |
| Windows/Mac | `host.docker.internal` | Docker Desktop 提供 |
| 局域网内 | 实际内网 IP | 如 `192.168.1.100` |
| 云服务器 | 内网 IP 或公网 IP | 推荐使用内网 IP |

**查看 Docker 网桥网关**

```bash
# 查看 Docker 网络配置
docker network inspect bridge | grep Gateway

# 或使用 ip 命令
ip addr show docker0
```

### 配置参数说明

| 环境变量 | 说明 | 示例值 |
|----------|------|--------|
| `USE_EXTERNAL_DB` | 是否使用外部 MySQL | `true` / `false` |
| `USE_EXTERNAL_REDIS` | 是否使用外部 Redis | `true` / `false` |
| `MYSQL_HOST` | 外部 MySQL 地址 | `10.0.12.8` |
| `MYSQL_EXTERNAL_PORT` | 外部 MySQL 端口 | `3306` |
| `MYSQL_USER` | MySQL 用户名 | `root` |
| `MYSQL_ROOT_PASSWORD` | MySQL 密码 | `your_password` |
| `REDIS_HOST` | 外部 Redis 地址 | `10.0.12.8` |
| `REDIS_EXTERNAL_PORT` | 外部 Redis 端口 | `6379` |
| `REDIS_PASSWORD` | Redis 密码 | `your_password` |

### 常见问题排查

#### 1. 容器无法连接外部 MySQL

```bash
# 检查容器内网络
docker exec -it game-user-service ping 10.0.12.8

# 检查 MySQL 是否监听正确地址
netstat -tlnp | grep 3306

# 检查防火墙
firewall-cmd --list-ports
# 或
iptables -L -n | grep 3306
```

#### 2. 容器无法连接外部 Redis

```bash
# 检查 Redis 监听地址
netstat -tlnp | grep 6379

# 检查 Redis 是否需要密码
redis-cli -h 10.0.12.8 ping
# 如果返回 NOAUTH，需要配置密码
```

#### 3. 宝塔防火墙配置

```bash
# 方式 1: 通过宝塔面板
# 【安全】-> 添加端口规则 -> 放行 3306 和 6379

# 方式 2: 命令行
firewall-cmd --permanent --add-port=3306/tcp
firewall-cmd --permanent --add-port=6379/tcp
firewall-cmd --reload
```

#### 4. MySQL 认证插件问题

```sql
-- 如果遇到认证错误，修改用户认证方式
ALTER USER 'root'@'%' IDENTIFIED WITH mysql_native_password BY '你的密码';
FLUSH PRIVILEGES;
```

### 安全建议

1. **使用专用数据库用户**（而非 root）
2. **限制访问 IP 范围**（只允许 Docker 网段）
3. **使用强密码**（16 位以上，包含大小写字母、数字、特殊字符）
4. **启用 SSL 连接**（生产环境推荐）
5. **定期备份数据库**
6. **配置防火墙规则**（只开放必要端口）

---

## 环境变量配置详解

### 部署模式配置

```bash
# 是否使用外部数据库
USE_EXTERNAL_DB=false

# 是否使用外部 Redis
USE_EXTERNAL_REDIS=false
```

### MySQL 配置

```bash
# 外部数据库时使用
MYSQL_HOST=192.168.1.100
MYSQL_EXTERNAL_PORT=3306

# 内部数据库时使用
MYSQL_PORT=13306

# 通用配置
MYSQL_USER=root
MYSQL_ROOT_PASSWORD=你的强密码_至少16位
MYSQL_DATABASE=game_platform
```

### Redis 配置

```bash
# 外部 Redis 时使用
REDIS_HOST=192.168.1.100
REDIS_EXTERNAL_PORT=6379

# 内部 Redis 时使用
REDIS_PORT=16379

# 通用配置
REDIS_PASSWORD=你的强密码_至少16位
REDIS_DATABASE=0
```

### JWT 配置

```bash
JWT_SECRET_KEY=你的JWT密钥_至少32位
JWT_ACCESS_TOKEN_EXPIRE=3600
JWT_REFRESH_TOKEN_EXPIRE=604800
```

---

## 部署脚本命令说明

### 多阶段构建版本

```bash
# 基本操作
./docker/multi-stage/deploy.sh --build           # 仅构建镜像
./docker/multi-stage/deploy.sh --start           # 仅启动服务
./docker/multi-stage/deploy.sh --stop            # 停止所有服务
./docker/multi-stage/deploy.sh --restart         # 重启所有服务
./docker/multi-stage/deploy.sh --status          # 查看服务状态
./docker/multi-stage/deploy.sh --logs [服务名]   # 查看日志

# 清理操作
./docker/multi-stage/deploy.sh --clean           # 清理所有容器和镜像
./docker/multi-stage/deploy.sh --fix-cache       # 修复缓存问题
./docker/multi-stage/deploy.sh --prune-cache     # 清理构建缓存
./docker/multi-stage/deploy.sh --rebuild         # 完全重新构建（清理缓存 + 无缓存构建）

# 外部数据库选项
./docker/multi-stage/deploy.sh --external-db --start      # 使用外部 MySQL
./docker/multi-stage/deploy.sh --external-redis --start   # 使用外部 Redis
./docker/multi-stage/deploy.sh --external-all --start     # 使用外部 MySQL 和 Redis
```

### 单镜像版本

```bash
# 基本操作
./docker/single-image/deploy.sh --build          # 构建镜像
./docker/single-image/deploy.sh --rebuild        # 完全重新构建（清理缓存 + 无缓存构建）
./docker/single-image/deploy.sh --start          # 启动服务（使用内部数据库）
./docker/single-image/deploy.sh --stop           # 停止服务
./docker/single-image/deploy.sh --status         # 查看状态
./docker/single-image/deploy.sh --logs [服务名]  # 查看日志

# 清理操作
./docker/single-image/deploy.sh --clean          # 清理所有容器和镜像
./docker/single-image/deploy.sh --prune-cache    # 清理构建缓存

# 外部数据库选项（可组合使用）
./docker/single-image/deploy.sh --external-db --start      # 使用外部 MySQL
./docker/single-image/deploy.sh --external-redis --start   # 使用外部 Redis
./docker/single-image/deploy.sh --external-all --start     # 使用外部 MySQL 和 Redis
./docker/single-image/deploy.sh --external-all --rebuild   # 使用外部数据库完全重建
```

### 命令效果对比详解

#### 构建命令对比

| 命令 | 缓存使用 | 构建速度 | 适用场景 |
|------|----------|----------|----------|
| `--build` | 使用缓存 | 快 | 代码未大改，日常更新 |
| `--rebuild` | 清理后无缓存 | 慢 | 遇到缓存问题、重大更新、首次部署推荐 |
| `--no-cache --build` | 不清理，仅无缓存 | 中等 | 单次无缓存构建 |

**效果说明**：
- `--build`：利用 Docker 层缓存，只重新构建变化的层。适合日常开发。
- `--rebuild`：先执行 `docker builder prune -af` + `docker buildx prune -af` 清理所有构建缓存，再使用 `--no-cache` 重新构建。**推荐首次部署或遇到奇怪问题时使用**。

#### 数据库模式对比

| 启动方式 | 启动的容器 | 数据存储位置 | 资源占用 |
|----------|------------|--------------|----------|
| `--start` (默认) | 微服务 + MySQL + Redis | Docker 数据卷 | 较高 |
| `--external-all --start` | 仅微服务 | 外部服务器 | 较低 |

**效果说明**：
```bash
# 内部模式：会启动 7 个容器
# game-mysql, game-redis, game-service-registry, game-user-service,
# game-auth-service, game-data-service, game-gomoku-server

# 外部模式：只启动 5 个容器（无 MySQL 和 Redis）
# game-service-registry, game-user-service, game-auth-service,
# game-data-service, game-gomoku-server
```

#### 清理命令对比

| 命令 | 清理范围 | 数据影响 |
|------|----------|----------|
| `--prune-cache` | 仅构建缓存 | 不影响运行中的服务和数据 |
| `--fix-cache` | 构建缓存 + 悬空镜像 | 不影响运行中的服务和数据 |
| `--clean` | 所有容器 + 镜像 + **数据卷** | **会删除数据库数据！** |

**重要警告**：
```bash
# ⚠️ 危险操作 - 会删除所有数据！
./docker/single-image/deploy.sh --clean

# 如果想保留数据，只重启服务
./docker/single-image/deploy.sh --stop
./docker/single-image/deploy.sh --start
```

#### 脚本参数与 .env 配置的关系

| 场景 | 脚本参数 | .env 配置 | 实际效果 |
|------|----------|-----------|----------|
| 1 | 无 | `USE_EXTERNAL_DB=false` | 使用内部 MySQL |
| 2 | 无 | `USE_EXTERNAL_DB=true` | **仍使用内部 MySQL**（需脚本参数） |
| 3 | `--external-db` | 任意 | 使用外部 MySQL |
| 4 | `--external-all` | 任意 | 使用外部 MySQL + Redis |

**关键说明**：
- `.env` 中的 `MYSQL_HOST`、`REDIS_HOST` 等变量**必须配置正确**，否则服务无法连接
- 脚本参数（`--external-db`）决定是否启动内部 MySQL/Redis 容器
- 配置变量（`MYSQL_HOST`）决定服务连接哪个数据库

```bash
# 正确的外部数据库配置流程
# 1. 修改 .env
MYSQL_HOST=10.0.12.8
MYSQL_EXTERNAL_PORT=3306
REDIS_HOST=10.0.12.8
REDIS_EXTERNAL_PORT=6379

# 2. 使用外部模式启动
./docker/single-image/deploy.sh --external-all --start

# 错误示例：忘记加 --external-all
# 结果：启动了内部 MySQL/Redis，但服务配置指向外部地址
#       导致服务连接不到正确的数据库
```

#### 两种部署方式对比

| 特性 | 多阶段构建 | 单镜像版 |
|------|------------|----------|
| 镜像数量 | 5 个独立镜像 | 1 个共享镜像 |
| 总镜像大小 | 约 1.5GB (5×300MB) | 约 800MB |
| 构建时间 | 较长（每服务独立） | 较短（一次构建） |
| 更新粒度 | 可单独更新某服务 | 需整体重建 |
| 扩展性 | 易于水平扩展 | 适合小规模 |
| 缓存问题 | 可能遇到 BuildKit 问题 | 避免 BuildKit 问题 |

**选择建议**：
```bash
# 生产环境 - 推荐 multi-stage
- 需要独立扩缩容
- 需要滚动更新
- 多服务器部署

# 开发/测试环境 - 推荐 single-image
- 快速迭代测试
- 单服务器部署
- 避免缓存问题
```

---

## 验证部署

### 1. 检查容器状态

```bash
# 多阶段版本
./docker/multi-stage/deploy.sh --status

# 单镜像版本
./docker/single-image/deploy.sh --status
```

### 2. 测试健康检查

```bash
# 测试 Nginx 网关
curl http://localhost/health

# 测试各服务
curl http://localhost:8090/health
curl http://localhost:8082/api/v1/user/health
curl http://localhost:8083/api/v1/auth/health
curl http://localhost:8084/api/v1/gamedata/health
curl http://localhost:8085/api/v1/gomoku/health
```

---

## 常见问题

### 1. 多阶段构建缓存问题

如果遇到 BuildKit 缓存损坏:

```bash
# 修复缓存
./docker/multi-stage/deploy.sh --fix-cache

# 完全重建
./docker/multi-stage/deploy.sh --rebuild
```

### 2. 需要快速部署/测试

使用单镜像版本:

```bash
./docker/single-image/deploy.sh --build
./docker/single-image/deploy.sh --start
```

### 3. 外部服务连接失败

```bash
# 检查网络连通性
ping your_mysql_host
nc -zv your_mysql_host 3306
```

### 4. 从内部数据库迁移到外部数据库

```bash
# 步骤 1: 备份内部数据库数据
docker exec game-mysql mysqldump -u root -p --all-databases > backup.sql

# 步骤 2: 导入到外部数据库
mysql -h 10.0.12.8 -u root -p < backup.sql

# 步骤 3: 修改 .env 配置
MYSQL_HOST=10.0.12.8
REDIS_HOST=10.0.12.8

# 步骤 4: 停止旧服务
./docker/single-image/deploy.sh --stop

# 步骤 5: 使用外部模式启动
./docker/single-image/deploy.sh --external-all --start

# 步骤 6: 验证数据完整性
```

### 5. 服务启动顺序问题

服务有依赖关系，启动顺序为：
```
service_registry (无依赖)
    ↓
user_service, auth_service (依赖 service_registry)
    ↓
game_data_service (依赖 auth_service)
    ↓
gomoku_server (依赖 game_data_service)
```

**如果服务启动失败**：
```bash
# 查看具体服务日志
./docker/single-image/deploy.sh --logs user_service

# 检查依赖服务是否健康
curl http://localhost:8090/api/v1/services/health

# 手动重启单个服务（多阶段版本支持）
docker restart game-user-service
```

### 6. 配置修改后不生效

```bash
# 修改 .env 或配置文件后，需要重建并重启

# 方式 1: 快速重启（仅重启容器，不重建镜像）
./docker/single-image/deploy.sh --stop
./docker/single-image/deploy.sh --start

# 方式 2: 完整重建（推荐配置修改后使用）
./docker/single-image/deploy.sh --rebuild
./docker/single-image/deploy.sh --start
```

### 7. 查看实时资源占用

```bash
# 查看所有容器资源
docker stats

# 查看特定容器
docker stats game-mysql game-redis game-user-service

# 查看容器详细信息
docker inspect game-user-service | grep -A 10 "Memory"
```

---

## 推荐操作流程

### 首次部署（生产环境）

```bash
# 1. 配置环境变量
cp docker/shared/.env.example docker/.env
nano docker/.env  # 修改必要的配置

# 2. 准备第三方依赖
cd docker/shared/third_party
git clone https://github.com/Thalhammer/jwt-cpp.git
git clone https://github.com/trusch/libbcrypt.git

# 3. 完整构建（清理缓存）
./docker/multi-stage/deploy.sh --rebuild

# 4. 检查状态
./docker/multi-stage/deploy.sh --status
./docker/multi-stage/deploy.sh --logs

# 1. 修复换行符
sed -i 's/\r$//' docker/shared/scripts/entrypoint.sh
sed -i 's/\r$//' docker/multi-stage/deploy.sh

# 2. 验证
file docker/shared/scripts/entrypoint.sh

# 3. 完全重建
docker-compose -f docker/multi-stage/docker-compose.yml down
docker system prune -f
./docker/multi-stage/deploy.sh --external-all --rebuild
```

### 首次部署（开发环境）

```bash
# 1. 配置环境变量
cp docker/shared/.env.example docker/.env

# 2. 完整构建
./docker/single-image/deploy.sh --rebuild

# 3. 启动服务
./docker/single-image/deploy.sh --start

# 4. 检查状态
./docker/single-image/deploy.sh --status
```

### 代码更新后重新部署

```bash
# 拉取最新代码
git pull

# 快速构建（使用缓存）
./docker/single-image/deploy.sh --build

# 重启服务
./docker/single-image/deploy.sh --stop
./docker/single-image/deploy.sh --start
```

### 使用外部数据库部署

```bash
# 1. 确保外部数据库已准备好
mysql -h 10.0.12.8 -u root -p -e "SHOW DATABASES;"
redis-cli -h 10.0.12.8 ping

# 2. 配置 .env
nano docker/.env
# 设置: MYSQL_HOST, REDIS_HOST, 密码等

# 3. 使用外部模式构建和启动
./docker/single-image/deploy.sh --external-all --rebuild
```

---

## 服务端口汇总

| 服务 | 容器端口 | 宿主机端口 | 说明 |
|------|----------|------------|------|
| Nginx (容器) | 80, 443 | 10080, 10443 | 容器网关（宿主机 Nginx 代理到此） |
| Nginx (宿主机) | - | 80, 443 | 对外服务入口（SSL 终止） |
| MySQL（内部） | 3306 | 13306 | 数据库（仅内网） |
| Redis（内部） | 6379 | 16379 | 缓存（仅内网） |
| service_registry | 8090 | 8090 | 服务注册中心 |
| user_service | 8082 | 8082 | 用户服务 |
| auth_service | 8083 | 8083 | 认证服务 |
| game_data_service | 8084 | 8084 | 游戏数据服务 |
| gomoku_server | 8085, 8086 | 8085, 8086 | 五子棋服务 |

---

## 双层 Nginx 架构

本项目采用**双层 Nginx 架构**，实现 SSL 终止与服务路由的分离：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              外部请求                                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│  宿主机 Nginx (宝塔面板管理)                                                  │
│  端口: 80 (HTTP), 443 (HTTPS)                                               │
│  职责:                                                                       │
│    ✓ SSL 证书终止（HTTPS 解密）                                              │
│    ✓ 安全防护（HSTS, X-Frame-Options 等）                                   │
│    ✓ 静态资源服务（可选）                                                    │
│    ✓ HTTP → HTTPS 重定向                                                    │
│    ✓ WebSocket 升级支持                                                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ↓
                          127.0.0.1:10080
                                    ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│  容器 Nginx (Docker 内部)                                                    │
│  端口: 10080 → 80, 10443 → 443                                              │
│  职责:                                                                       │
│    ✓ 服务路由 (/api/v1/user → user-service:8082 等)                        │
│    ✓ 负载均衡（多实例场景）                                                  │
│    ✓ 健康检查代理                                                            │
│    ✓ WebSocket 路由                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ↓
         ┌────────────────────────────┼────────────────────────────┐
         ↓                            ↓                            ↓
  ┌─────────────────┐        ┌─────────────────┐        ┌─────────────────┐
  │  user-service   │        │  auth-service   │        │ game-data-svc   │
  │     :8082       │        │     :8083       │        │     :8084       │
  └─────────────────┘        └─────────────────┘        └─────────────────┘
```

### 双层架构优势

| 优势 | 说明 |
|------|------|
| **SSL 卸载** | 宿主机 Nginx 处理 SSL，容器内部使用 HTTP，降低容器负载 |
| **证书管理** | 使用宝塔面板统一管理证书，自动续期 |
| **安全隔离** | 外层 Nginx 提供 WAF、限流等安全防护 |
| **灵活部署** | 容器可随时重启重建，不影响 SSL 配置 |
| **性能优化** | 宿主机 Nginx 可启用缓存、压缩等优化 |

---

## 免费 SSL 证书配置指南

### 前置条件

1. **已备案域名**：国内服务器需要域名备案
2. **域名解析**：域名已解析到服务器 IP
3. **宝塔面板**：已安装宝塔 Linux 面板（7.x 或更高版本）
4. **Nginx**：宝塔面板已安装 Nginx

### 方式一：宝塔面板申请 Let's Encrypt 证书（推荐）

Let's Encrypt 是免费、自动化的证书颁发机构，证书有效期 90 天，支持自动续期。

#### 步骤 1：登录宝塔面板

访问 `http://你的服务器IP:8888` 登录宝塔面板

#### 步骤 2：进入 SSL 证书管理

1. 点击左侧菜单【网站】
2. 点击【添加站点】（如果还没有站点）
3. 或点击已有站点的【设置】

#### 步骤 3：申请免费证书

1. 在站点设置中，点击左侧【SSL】选项卡
2. 选择【Let's Encrypt】
3. 填写以下信息：
   - **域名**：填写你的域名（如 `api.example.com`）
   - **邮箱**：填写你的邮箱（用于证书到期提醒）
4. 点击【申请】按钮

```
┌─────────────────────────────────────────────────────────────────┐
│  SSL 证书申请                                                    │
├─────────────────────────────────────────────────────────────────┤
│  域名: api.example.com                                          │
│  邮箱: your-email@example.com                                   │
│                                                                 │
│  ☑ 自动续期（推荐）                                              │
│                                                                 │
│  [申请]                                                         │
└─────────────────────────────────────────────────────────────────┘
```

#### 步骤 4：验证申请结果

申请成功后，你会看到：

```
证书状态: 有效
颁发机构: Let's Encrypt
有效期至: 2026-06-XX
到期剩余: 89 天
```

#### 步骤 5：开启强制 HTTPS

在 SSL 设置页面：
1. 开启【强制 HTTPS】开关
2. 这样所有 HTTP 请求会自动跳转到 HTTPS

### 方式二：使用 acme.sh 申请证书（高级用户）

如果宝塔面板申请失败，可以使用 acme.sh 命令行工具。

#### 步骤 1：安装 acme.sh

```bash
# 安装 acme.sh
curl https://get.acme.sh | sh

# 重新加载环境
source ~/.bashrc
```

#### 步骤 2：配置 API 密钥（DNS 验证方式）

如果使用阿里云 DNS：

```bash
# 配置阿里云 API 密钥
export Ali_Key="你的AccessKeyId"
export Ali_Secret="你的AccessKeySecret"
```

如果使用腾讯云 DNS：

```bash
# 配置腾讯云 API 密钥
export Tencent_SecretId="你的SecretId"
export Tencent_SecretKey="你的SecretKey"
```

#### 步骤 3：申请证书

**HTTP 验证方式**（需要 Nginx 已运行）：

```bash
# 申请证书
acme.sh --issue -d api.example.com --webroot /www/wwwroot/api.example.com
```

**DNS 验证方式**（不需要 Nginx，推荐）：

```bash
# 阿里云 DNS
acme.sh --issue --dns dns_ali -d api.example.com

# 腾讯云 DNS
acme.sh --issue --dns dns_tencent -d api.example.com
```

**泛域名证书**：

```bash
# 申请泛域名证书 (*.example.com 和 example.com)
acme.sh --issue --dns dns_ali -d example.com -d '*.example.com'
```

#### 步骤 4：安装证书到宝塔

```bash
# 创建证书目录
mkdir -p /www/server/panel/vhost/cert/api.example.com

# 安装证书
acme.sh --install-cert -d api.example.com \
  --key-file       /www/server/panel/vhost/cert/api.example.com/privkey.pem \
  --fullchain-file /www/server/panel/vhost/cert/api.example.com/fullchain.pem \
  --reloadcmd      "nginx -s reload"
```

#### 步骤 5：配置自动续期

```bash
# 查看自动续期任务
acme.sh --info -d api.example.com

# 手动续期测试
acme.sh --renew -d api.example.com --force

# 设置续期后重启 Nginx
acme.sh --install-cert -d api.example.com \
  --key-file       /www/server/panel/vhost/cert/api.example.com/privkey.pem \
  --fullchain-file /www/server/panel/vhost/cert/api.example.com/fullchain.pem \
  --reloadcmd      "systemctl reload nginx"
```

### 配置宿主机 Nginx 使用证书

#### 步骤 1：创建 Nginx 配置文件

```bash
# 创建配置文件
nano /www/server/nginx/conf/vhost/game-platform.conf
```

#### 步骤 2：粘贴以下配置

```nginx
# =============================================================================
# 游戏微服务 - 宿主机 Nginx 配置
# =============================================================================

# 上游服务器：容器 Nginx
upstream docker_nginx {
    server 127.0.0.1:10080;
    keepalive 32;
}

# HTTP 服务器 - 重定向到 HTTPS
server {
    listen 80;
    listen [::]:80;
    server_name api.example.com;  # 替换为你的域名

    # Let's Encrypt 验证路径（重要！不要删除）
    location /.well-known/acme-challenge/ {
        root /www/wwwroot/letsencrypt;
    }

    # 其他请求重定向到 HTTPS
    location / {
        return 301 https://$server_name$request_uri;
    }
}

# HTTPS 服务器 - 主入口
server {
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name api.example.com;  # 替换为你的域名

    # =========================================================================
    # SSL 证书配置
    # =========================================================================
    ssl_certificate /www/server/panel/vhost/cert/api.example.com/fullchain.pem;
    ssl_certificate_key /www/server/panel/vhost/cert/api.example.com/privkey.pem;

    # SSL 优化配置
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384;
    ssl_prefer_server_ciphers off;
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_session_tickets off;

    # HSTS（可选，建议启用）
    add_header Strict-Transport-Security "max-age=63072000" always;

    # =========================================================================
    # 安全头
    # =========================================================================
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    # =========================================================================
    # 日志
    # =========================================================================
    access_log /www/wwwlogs/game-platform.access.log;
    error_log /www/wwwlogs/game-platform.error.log;

    # =========================================================================
    # WebSocket 支持（五子棋游戏）
    # =========================================================================
    location /ws/ {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # WebSocket 超时设置（1小时）
        proxy_connect_timeout 60s;
        proxy_send_timeout 3600s;
        proxy_read_timeout 3600s;
    }

    # =========================================================================
    # API 请求代理到容器 Nginx
    # =========================================================================
    location /api/ {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # 连接设置
        proxy_connect_timeout 30s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;

        # 缓冲设置
        proxy_buffering on;
        proxy_buffer_size 4k;
        proxy_buffers 8 16k;
    }

    # =========================================================================
    # 健康检查端点
    # =========================================================================
    location /health {
        proxy_pass http://docker_nginx;
        access_log off;
    }

    # =========================================================================
    # 根路径
    # =========================================================================
    location / {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

#### 步骤 3：测试并重载配置

```bash
# 测试配置语法
nginx -t

# 如果显示 "syntax is ok"，重载配置
nginx -s reload
```

### 验证 SSL 配置

#### 1. 测试 HTTPS 访问

```bash
# 测试 HTTPS 连接
curl -I https://api.example.com/health

# 预期输出
HTTP/2 200
server: nginx
content-type: application/json
...
```

#### 2. 测试证书链

```bash
# 检查证书信息
openssl s_client -connect api.example.com:443 -servername api.example.com | openssl x509 -noout -dates

# 预期输出
notBefore=Mar  1 00:00:00 2026 GMT
notAfter=May 30 23:59:59 2026 GMT
```

#### 3. 使用在线工具检测

访问以下网站检测 SSL 配置：
- https://www.ssllabs.com/ssltest/
- https://myssl.com/

目标评级：**A 或 A+**

### 证书自动续期配置

#### 宝塔面板自动续期

宝塔面板默认会自动续期 Let's Encrypt 证书，确保证书不过期。

检查自动续期状态：
1. 进入宝塔面板
2. 点击【计划任务】
3. 查看是否有 `续签Let's Encrypt证书` 任务

#### 手动续期

如果需要手动续期：

```bash
# 宝塔面板续期
bt 14  # 选择对应的站点续期

# 或使用 acme.sh
acme.sh --renew -d api.example.com
```

### 常见问题

#### 1. 证书申请失败：域名解析未生效

```bash
# 检查域名解析
dig api.example.com +short

# 应该返回你的服务器 IP
```

#### 2. 证书申请失败：80 端口不可访问

```bash
# 检查 80 端口是否开放
netstat -tlnp | grep :80

# 检查防火墙
firewall-cmd --list-ports
# 或宝塔面板【安全】中检查 80 端口
```

#### 3. HTTPS 访问显示证书无效

可能原因：
- 证书未正确安装
- 域名不匹配
- 证书过期

```bash
# 检查证书文件
ls -la /www/server/panel/vhost/cert/api.example.com/

# 应该有这两个文件
# fullchain.pem  - 证书链
# privkey.pem    - 私钥
```

#### 4. WebSocket 连接失败

确保宿主机 Nginx 配置了 WebSocket 支持：

```nginx
location /ws/ {
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    ...
}
```

#### 5. 跨域问题

在宿主机 Nginx 添加 CORS 头（如果需要）：

```nginx
location /api/ {
    # CORS 配置
    add_header Access-Control-Allow-Origin *;
    add_header Access-Control-Allow-Methods 'GET, POST, PUT, DELETE, OPTIONS';
    add_header Access-Control-Allow-Headers 'Authorization, Content-Type';

    # 处理 OPTIONS 预检请求
    if ($request_method = OPTIONS) {
        return 204;
    }

    proxy_pass http://docker_nginx;
    ...
}
```

### SSL 配置最佳实践

| 配置项 | 推荐值 | 说明 |
|--------|--------|------|
| TLS 版本 | TLSv1.2, TLSv1.3 | 禁用 TLSv1.0 和 TLSv1.1 |
| HSTS | max-age=63072000 | 强制 HTTPS，有效期 2 年 |
| 证书类型 | ECC (ECDSA) | 比 RSA 更快，更安全 |
| 证书有效期监控 | 自动续期 | 到期前 30 天自动续期 |
| OCSP Stapling | 启用 | 提高 SSL 握手速度 |

---

## 使用 Cloudflare 配置指南

### Cloudflare 简介

Cloudflare 是全球领先的 CDN 和网络安全服务商，提供以下功能：

| 功能 | 说明 | 优势 |
|------|------|------|
| **CDN 加速** | 全球 280+ 节点分发 | 加速国内访问海外源站 |
| **DDoS 防护** | 免费的基础防护 | 抵御大规模攻击 |
| **SSL/TLS** | 免费 SSL 证书 | 自动配置，无需手动申请 |
| **DNS 解析** | 快速稳定的 DNS | 支持中国大陆节点 |
| **WAF** | Web 应用防火墙 | 防止 SQL 注入、XSS 等 |

### 为什么选择 Cloudflare

对于游戏微服务项目，Cloudflare 的优势：

1. **无需备案即可使用 HTTPS**：通过 Cloudflare 代理，可以绕过国内域名备案限制
2. **免费 SSL 证书**：Universal SSL 自动为域名配置证书
3. **WebSocket 支持**：完美支持五子棋游戏的 WebSocket 连接
4. **API 加速**：全球 CDN 节点加速 API 请求
5. **安全防护**：免费 DDoS 防护和 WAF

### Cloudflare vs 传统方式对比

| 特性 | 传统宝塔 SSL | Cloudflare |
|------|-------------|------------|
| 需要备案 | ✅ 是 | ❌ 否（代理模式） |
| SSL 证书 | 手动申请/续期 | 自动配置 |
| DDoS 防护 | 需额外购买 | 免费基础防护 |
| CDN 加速 | 无 | 全球节点 |
| WebSocket | 支持 | 支持 |
| 配置复杂度 | 中等 | 简单 |

---

### 步骤 1：注册 Cloudflare 账号

1. 访问 [Cloudflare 官网](https://dash.cloudflare.com/sign-up)
2. 填写邮箱和密码，完成注册
3. 验证邮箱地址

### 步骤 2：添加域名到 Cloudflare

#### 2.1 添加站点

1. 登录 Cloudflare Dashboard
2. 点击 **"Add a site"** 或 **"添加站点"**
3. 输入你的域名（如 `example.com`）
4. 点击 **"Add site"**

```
┌─────────────────────────────────────────┐
│  Add a site                              │
├─────────────────────────────────────────┤
│  Enter your domain:                      │
│  [example.com                      ]     │
│                                          │
│  [Add site]                              │
└─────────────────────────────────────────┘
```

#### 2.2 选择计划

选择 **Free** 免费计划即可满足大部分需求：

```
○ Free        $0/月     - 适合个人项目
○ Pro         $20/月    - 适合小型商业网站
○ Business    $200/月   - 适合企业应用
○ Enterprise  定制      - 大规模企业
```

### 步骤 3：修改域名 DNS 服务器

Cloudflare 会显示需要修改的 NS 记录：

```
┌─────────────────────────────────────────────────────────────┐
│  Change your nameservers                                    │
├─────────────────────────────────────────────────────────────┤
│  Replace your current nameservers with:                     │
│                                                             │
│  Nameserver 1: ada.ns.cloudflare.com                        │
│  Nameserver 2: bob.ns.cloudflare.com                        │
│                                                             │
│  ⚠️ 这一步需要在域名注册商处修改                              │
└─────────────────────────────────────────────────────────────┘
```

#### 3.1 在域名注册商处修改 NS

**腾讯云 DNSPod**：

1. 登录 [腾讯云控制台](https://console.cloud.tencent.com/domain)
2. 进入 **域名注册** → 选择域名 → **管理**
3. 点击 **DNS 管理** → **修改 DNS 服务器**
4. 选择 **自定义 DNS**
5. 填入 Cloudflare 提供的两个 NS 地址
6. 保存修改

**阿里云**：

1. 登录 [阿里云域名控制台](https://dc.console.aliyun.com/)
2. 选择域名 → **管理** → **DNS 修改**
3. 选择 **自定义 DNS**
4. 填入 Cloudflare 的 NS 地址
5. 保存

**GoDaddy**：

1. 登录 GoDaddy 账户
2. 进入 **My Products** → **Domains**
3. 点击域名的 **DNS**
4. 滚动到底部 **Host Names** → **Change**
5. 选择 **I'll use my own nameservers**
6. 填入 Cloudflare NS 地址

#### 3.2 等待 DNS 生效

- 生效时间：通常 10 分钟 - 48 小时
- 检查命令：

```bash
# 检查 NS 记录
dig NS example.com +short

# 预期输出（显示 cloudflare 的 NS）
ada.ns.cloudflare.com.
bob.ns.cloudflare.com.
```

### 步骤 4：配置 DNS 记录

在 Cloudflare DNS 设置页面添加记录：

#### 4.1 添加 A 记录

```
┌─────────────────────────────────────────────────────────────┐
│  DNS Records                                                 │
├─────────────────────────────────────────────────────────────┤
│  Type   Name           Content              Proxy status    │
│  ─────────────────────────────────────────────────────────── │
│  A      api            43.143.124.220       Proxied  🟠     │
│  A      game           43.143.124.220       Proxied  🟠     │
│  A      ws             43.143.124.220       Proxied  🟠     │
└─────────────────────────────────────────────────────────────┘
```

**配置说明**：

| 字段 | 值 | 说明 |
|------|-----|------|
| Type | A | IPv4 地址记录 |
| Name | api | 子域名（api.example.com） |
| Content | 43.143.124.220 | 你的服务器公网 IP |
| Proxy status | Proxied（橙色云） | 启用 Cloudflare 代理 |
| TTL | Auto | 自动 TTL |

#### 4.2 代理模式说明

| 模式 | 图标 | 说明 | 适用场景 |
|------|------|------|----------|
| **Proxied** | 🟠 橙色云朵 | 流量经过 Cloudflare | 需要 CDN、防护、SSL |
| **DNS only** | ⚪ 灰色云朵 | 仅 DNS 解析 | 不需要代理的服务 |

**推荐配置**：

```
# API 服务 - 启用代理
api.example.com      A    43.143.124.220    Proxied 🟠

# WebSocket 服务 - 启用代理（支持 WebSocket）
ws.example.com       A    43.143.124.220    Proxied 🟠

# 直接访问（如需要）- 仅 DNS
direct.example.com   A    43.143.124.220    DNS only ⚪
```

### 步骤 5：配置 SSL/TLS

#### 5.1 SSL/TLS 加密模式

进入 **SSL/TLS** → **Overview**，选择加密模式：

```
┌─────────────────────────────────────────────────────────────┐
│  SSL/TLS encryption mode                                    │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ○ Off (Not secure)          - 不加密（不推荐）              │
│  ○ Flexible                   - 用户到 CF 加密，CF 到源站不加密 │
│  ● Full                       - 全程加密（源站证书可以是自签名）│
│  ○ Full (Strict)             - 全程严格加密（源站需有效证书）   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**模式选择建议**：

| 模式 | 源站证书 | 推荐场景 |
|------|----------|----------|
| **Flexible** | 无需证书 | 快速测试、源站无证书 |
| **Full** | 自签名/有效证书 | **推荐**：宝塔面板部署 |
| **Full (Strict)** | 有效证书 | 源站有正式证书 |

**推荐配置**：选择 **Full** 模式

#### 5.2 启用 Always Use HTTPS

在 **SSL/TLS** → **Edge Certificates**：

```
☑ Always Use HTTPS        On    - 自动 HTTP → HTTPS
☑ Automatic HTTPS Rewrites On    - 自动重写 HTTP 链接
☑ Minimum TLS Version     1.2   - 最低 TLS 版本
```

### 步骤 6：配置网络设置

进入 **Network** 页面：

```
┌─────────────────────────────────────────────────────────────┐
│  Network Settings                                            │
├─────────────────────────────────────────────────────────────┤
│  ☑ HTTP/3 (with QUIC)      On    - 启用 HTTP/3              │
│  ☑ 0-RTT Connection Resumption On - 快速重连                │
│  ☑ WebSockets              On    - 启用 WebSocket（重要！）   │
│  ☑ gRPC                    Off   - 按需启用                  │
└─────────────────────────────────────────────────────────────┘
```

**重要**：确保 **WebSockets** 设置为 **On**，否则五子棋游戏无法正常工作。

### 步骤 7：配置页面规则（可选）

#### 7.1 WebSocket 专用规则

进入 **Rules** → **Page Rules**：

```
┌─────────────────────────────────────────────────────────────┐
│  Page Rule                                                   │
├─────────────────────────────────────────────────────────────┤
│  URL: ws.example.com/*                                       │
│                                                              │
│  Settings:                                                   │
│  - Cache Level: Bypass                                       │
│  - Disable Performance                                       │
│  - SSL: Full                                                 │
└─────────────────────────────────────────────────────────────┘
```

#### 7.2 API 缓存规则

```
┌─────────────────────────────────────────────────────────────┐
│  Page Rule                                                   │
├─────────────────────────────────────────────────────────────┤
│  URL: api.example.com/api/v1/*/health                        │
│                                                              │
│  Settings:                                                   │
│  - Cache Level: Cache Everything                            │
│  - Edge Cache TTL: 30 seconds                                │
│  - Browser Cache TTL: 10 seconds                             │
└─────────────────────────────────────────────────────────────┘
```

### 步骤 8：修改宿主机 Nginx 配置

使用 Cloudflare 后，需要修改 Nginx 配置以支持 **Full SSL** 模式：

#### 8.1 生成源站证书（可选）

如果使用 **Full** 模式，可以使用自签名证书：

```bash
# 创建证书目录
mkdir -p /www/server/panel/vhost/cert/origin

# 生成自签名证书
openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
  -keyout /www/server/panel/vhost/cert/origin/privkey.pem \
  -out /www/server/panel/vhost/cert/origin/fullchain.pem \
  -subj "/CN=origin.example.com"
```

#### 8.2 修改 Nginx 配置

```nginx
# =============================================================================
# 游戏微服务 - 使用 Cloudflare 的 Nginx 配置
# =============================================================================

# 上游服务器：容器 Nginx
upstream docker_nginx {
    server 127.0.0.1:10080;
    keepalive 32;
}

# HTTP 服务器 - Cloudflare 会自动重定向到 HTTPS
server {
    listen 80;
    listen [::]:80;
    server_name api.example.com;

    # Cloudflare 真实 IP
    set_real_ip_from 103.21.244.0/22;
    set_real_ip_from 103.22.200.0/22;
    set_real_ip_from 103.31.4.0/22;
    set_real_ip_from 104.16.0.0/13;
    set_real_ip_from 104.24.0.0/14;
    set_real_ip_from 108.162.192.0/18;
    set_real_ip_from 131.0.72.0/22;
    set_real_ip_from 141.101.64.0/18;
    set_real_ip_from 162.158.0.0/15;
    set_real_ip_from 172.64.0.0/13;
    set_real_ip_from 173.245.48.0/20;
    set_real_ip_from 188.114.96.0/20;
    set_real_ip_from 190.93.240.0/20;
    set_real_ip_from 197.234.240.0/22;
    set_real_ip_from 198.41.128.0/17;
    real_ip_header CF-Connecting-IP;

    # 代理到容器
    location / {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}

# HTTPS 服务器（如果使用 Full 模式需要源站证书）
server {
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name api.example.com;

    # 源站证书（自签名或正式证书）
    ssl_certificate /www/server/panel/vhost/cert/origin/fullchain.pem;
    ssl_certificate_key /www/server/panel/vhost/cert/origin/privkey.pem;

    # SSL 配置
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;
    ssl_prefer_server_ciphers on;

    # Cloudflare 真实 IP
    set_real_ip_from 103.21.244.0/22;
    set_real_ip_from 103.22.200.0/22;
    set_real_ip_from 103.31.4.0/22;
    set_real_ip_from 104.16.0.0/13;
    set_real_ip_from 104.24.0.0/14;
    set_real_ip_from 108.162.192.0/18;
    set_real_ip_from 131.0.72.0/22;
    set_real_ip_from 141.101.64.0/18;
    set_real_ip_from 162.158.0.0/15;
    set_real_ip_from 172.64.0.0/13;
    set_real_ip_from 173.245.48.0/20;
    set_real_ip_from 188.114.96.0/20;
    set_real_ip_from 190.93.240.0/20;
    set_real_ip_from 197.234.240.0/22;
    set_real_ip_from 198.41.128.0/17;
    real_ip_header CF-Connecting-IP;

    # WebSocket 支持
    location /ws/ {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        proxy_connect_timeout 60s;
        proxy_send_timeout 3600s;
        proxy_read_timeout 3600s;
    }

    # API 代理
    location /api/ {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # 健康检查
    location /health {
        proxy_pass http://docker_nginx;
        access_log off;
    }

    # 其他请求
    location / {
        proxy_pass http://docker_nginx;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

### 步骤 9：验证 Cloudflare 配置

#### 9.1 检查 DNS 解析

```bash
# 检查域名解析
dig api.example.com +short

# 如果启用代理，应该返回 Cloudflare 的 IP
# 例如: 104.21.50.100

# 如果是 DNS only，返回源站 IP
# 例如: 43.143.124.220
```

#### 9.2 测试 HTTPS 访问

```bash
# 测试 HTTPS
curl -I https://api.example.com/health

# 检查证书信息
openssl s_client -connect api.example.com:443 -servername api.example.com 2>/dev/null | openssl x509 -noout -issuer

# 应该显示 Cloudflare 的证书
# issuer=CN=Cloudflare Inc ECC CA-3
```

#### 9.3 测试 WebSocket

```javascript
// 浏览器控制台测试
const ws = new WebSocket('wss://ws.example.com/ws');

ws.onopen = () => {
    console.log('WebSocket 连接成功');
    ws.send('Hello Server');
};

ws.onmessage = (event) => {
    console.log('收到消息:', event.data);
};

ws.onerror = (error) => {
    console.error('WebSocket 错误:', error);
};
```

---

### Cloudflare 常见问题

#### 1. 域名显示 "Pending Nameserver Update"

**原因**：NS 记录尚未生效

**解决方案**：
```bash
# 检查当前 NS
dig NS example.com +short

# 等待 DNS 传播（最多 48 小时）
# Cloudflare 会自动检测并通知
```

#### 2. 502 Bad Gateway 错误

**可能原因**：
- 源站服务器未运行
- 防火墙阻止了 Cloudflare IP
- SSL 模式配置错误

**排查步骤**：
```bash
# 1. 检查源站服务
curl -I http://43.143.124.220/health

# 2. 检查防火墙
# 确保允许 Cloudflare IP 访问

# 3. 检查 SSL 模式
# 尝试切换到 Flexible 模式测试
```

#### 3. WebSocket 连接失败

**检查清单**：
- [ ] Cloudflare Network 设置中 WebSocket 已启用
- [ ] Nginx 配置了 WebSocket 代理头
- [ ] 使用 `wss://` 而非 `ws://`

#### 4. 国内访问速度慢

**原因**：Cloudflare 免费版国内优化有限

**解决方案**：
- 使用 Cloudflare 中国合作伙伴版
- 考虑使用 Cloudflare Speed Optimization
- 开启 Argo Smart Routing（付费）

#### 5. 真实 IP 获取问题

**问题**：应用获取的是 Cloudflare IP 而非用户真实 IP

**解决方案**：
```nginx
# Nginx 配置
set_real_ip_from 172.64.0.0/13;  # Cloudflare IP 段
real_ip_header CF-Connecting-IP;

# 应用代码中使用
X-Forwarded-For 或 CF-Connecting-IP 头
```

#### 6. SSL 证书不被信任

**场景**：使用 Full (Strict) 模式时

**解决方案**：
- 切换到 Full 模式（允许自签名证书）
- 或配置有效的源站证书

---

### Cloudflare 推荐配置汇总

```
┌─────────────────────────────────────────────────────────────┐
│  Cloudflare 推荐配置清单                                     │
├─────────────────────────────────────────────────────────────┤
│  DNS 设置:                                                   │
│    ☑ api.example.com    A    服务器IP    Proxied 🟠          │
│    ☑ ws.example.com     A    服务器IP    Proxied 🟠          │
│                                                              │
│  SSL/TLS:                                                    │
│    ☑ 模式: Full                                             │
│    ☑ Always Use HTTPS: On                                   │
│    ☑ Automatic HTTPS Rewrites: On                           │
│    ☑ Minimum TLS Version: 1.2                               │
│                                                              │
│  Network:                                                    │
│    ☑ HTTP/3: On                                             │
│    ☑ WebSockets: On （关键！）                               │
│    ☑ 0-RTT: On                                              │
│                                                              │
│  Speed:                                                      │
│    ☑ Auto Minify: HTML, CSS, JS                             │
│    ☑ Brotli: On                                             │
│    ☑ Rocket Loader: Off（可能影响 WebSocket）                │
│                                                              │
│  Caching:                                                    │
│    ☑ Caching Level: Standard                                │
│    ☑ Browser Cache TTL: 4 hours                             │
└─────────────────────────────────────────────────────────────┘
```

---

### Cloudflare vs 备案对比

| 场景 | 推荐方案 | 说明 |
|------|----------|------|
| 国内用户为主 | **备案 + 宝塔 SSL** | 访问速度最快 |
| 海外用户为主 | **Cloudflare** | 无需备案，全球加速 |
| 内网测试 | **自签名证书** | 快速部署，无需域名 |
| 生产环境（国内） | **备案 + Cloudflare 中国版** | 合规且高性能 |

---

## 更多信息

- 详细构建问题排查: 见 `docs/deployment/docker_build_troubleshooting.md`
- Docker 架构说明: 见 `docker/README.md`
- Cloudflare 官方文档: https://developers.cloudflare.com/

---

## 注意事项

### Windows 用户注意事项

如果在 Windows 上编辑脚本文件后上传到 Linux 服务器，可能会遇到以下错误：

```bash
bash: ./deploy.sh: /bin/bash^M: bad interpreter: No such file or directory
```

**原因**：Windows 使用 CRLF (`\r\n`) 行尾，Linux 使用 LF (`\n`) 行尾。

**解决方案**：

```bash
# 方式 1: 使用 dos2unix 工具
yum install dos2unix -y
dos2unix docker/single-image/deploy.sh
dos2unix docker/multi-stage/deploy.sh

# 方式 2: 使用 sed 转换
sed -i 's/\r$//' docker/single-image/deploy.sh
sed -i 's/\r$//' docker/multi-stage/deploy.sh

# 方式 3: 在 vim 中设置
:set ff=unix
:wq
```

**预防措施**：
- Git 配置自动转换：`git config --global core.autocrlf false`
- VS Code 右下角选择 "LF" 行尾
- 使用 Notepad++ 时，编辑 -> 文档格式转换 -> Unix (LF)
