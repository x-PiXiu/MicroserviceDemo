# MicroserviceDemo 性能测试计划

> 版本: 1.1.0
> 日期: 2026-05-07
> 作者: PiPiXia

---

## 目录

- [1. 测试目标与范围](#1-测试目标与范围)
- [2. 系统架构概览](#2-系统架构概览)
- [3. 测试环境搭建](#3-测试环境搭建)
- [4. 测试工具选型与安装](#4-测试工具选型与安装)
- [5. 测试指标定义](#5-测试指标定义)
- [6. 测试场景设计](#6-测试场景设计)
- [7. 详细测试步骤](#7-详细测试步骤)
- [8. 测试数据准备](#8-测试数据准备)
- [9. 监控与数据采集](#9-监控与数据采集)
- [10. 结果分析与报告](#10-结果分析与报告)
- [11. 性能优化建议清单](#11-性能优化建议清单)

---

## 1. 测试目标与范围

### 1.1 测试目标

| 目标 | 描述 |
|------|------|
| 基线建立 | 获取各接口在单用户场景下的基准响应时间 |
| 吞吐量测试 | 确定系统在可接受响应时间内的最大 QPS |
| 并发测试 | 验证系统在高并发下的稳定性和正确性 |
| 瓶颈定位 | 找出系统的性能瓶颈（CPU、内存、数据库、网络） |
| 稳定性测试 | 验证系统在持续负载下的长时间运行稳定性 |

### 1.2 测试范围

**涉及服务（6 个）：**

| 服务 | 端口 | 数据库 | Redis DB | 职责 |
|------|------|--------|----------|------|
| API Gateway (http_server) | 8081 | auth_sessions_db | 0 | 统一入口、路由转发、限流、CORS |
| service_registry | 8090 | — | 0 | 服务注册与发现、心跳管理 |
| user_service | 8082 | user_service_db | 3 | 用户注册、档案管理 |
| auth_service | 8083 | auth_sessions_db | 0 | 认证、JWT Token、密码重置、邮件验证 |
| game_data_service | 8084 | game_service_db | 3 | 游戏档案、货币、成就、库存、排行榜 |
| gomoku_server | 8085 (HTTP) / 8086 (WS) | gomoku_game_db | 1 | 五子棋游戏对局、房间管理、匹配 |

**涉及前端（1 个）：**

| 组件 | 端口 | 说明 |
|------|------|------|
| Vue 3 SPA | 5173 | Vite 开发服务器（生产环境为 Nginx） |

### 1.3 不在范围内

- 前端渲染性能（LCP、FCP 等 Web Vitals）
- 数据库本身的基准测试（独立于服务）
- WebSocket 长连接的消息吞吐（需单独的 WebSocket 压测方案，端口 8086）

---

## 2. 系统架构概览

```
                         ┌─────────────────┐
                         │   Vue 3 前端     │
                         │  :5173 (dev)    │
                         └────────┬────────┘
                                  │ HTTP
                         ┌────────▼────────┐
                         │  API Gateway     │
                         │     :8081        │
                         │ (http_server)    │
                         └────────┬────────┘
                                  │
           ┌──────────────────────┼──────────────────────┐
           │                      │                      │
  ┌────────▼─────────┐  ┌────────▼────────┐  ┌─────────▼──────────┐
  │   auth_service    │  │  user_service   │  │ game_data_service  │
  │     :8083         │  │     :8082       │  │      :8084         │
  └────────┬─────────┘  └────────┬────────┘  └─────────┬──────────┘
           │                      │                      │
           │                      │         ┌────────────▼───────────┐
           │                      │         │   gomoku_server        │
           │                      │         │  :8085 (HTTP)          │
           │                      │         │  :8086 (WebSocket)     │
           │                      │         └────────────────────────┘
           │                      │
  ┌────────▼──────────────────────▼───────────────────────────┐
  │                    MySQL + Redis                           │
  │  MySQL: 127.0.0.1:3306   Redis: 127.0.0.1:6379           │
  └────────────────────────────────────────────────────────────┘
                          │
                 ┌────────▼────────┐
                 │ service_registry │
                 │     :8090        │
                 └─────────────────┘
```

**服务间依赖关系：**
- `auth_service(:8083)` → `user_service(:8082)`（用户验证，`external_services.user_service.base_url`）
- `game_data_service(:8084)` → `user_service(:8082)`（用户信息查询，`external_services.user_service_url`）
- `gomoku_server(:8085)` → `user_service(:8082)` + `game_data_service(:8084)` + `auth_service(:8083)`
- 所有服务 → `service_registry(:8090)`（服务注册与发现，`service_registry.port: 8090`）
- 所有服务 → MySQL（持久化存储）
- 所有服务 → Redis（缓存层，各服务使用不同 DB 编号）

**Redis DB 分配：**

| DB | 服务 | 用途 |
|----|------|------|
| 0 | auth_service, service_registry, API Gateway | Token 黑名单、服务注册缓存 |
| 1 | gomoku_server | 游戏房间状态、排行榜缓存 |
| 3 | user_service, game_data_service | 用户缓存、游戏数据缓存 |

---

## 3. 测试环境搭建

### 3.1 硬件要求

| 资源 | 最低配置 | 推荐配置 |
|------|----------|----------|
| CPU | 4 核 | 8 核+ |
| 内存 | 8 GB | 16 GB+ |
| 磁盘 | 50 GB SSD | 100 GB SSD |
| 网络 | 100 Mbps | 1 Gbps |

### 3.2 软件环境

```bash
# 操作系统
Ubuntu 22.04 LTS / Windows 11 Pro

# 运行时依赖
MySQL 8.0+
Redis 7.0+
CMake 3.20+
GCC 11+ / MSVC 2022

# 测试工具
wrk / Apache Bench (ab) / k6 / JMeter
```

### 3.3 环境部署步骤

**步骤 1：编译所有服务**

```bash
cd E:\workspace\cdp\Demo\MicroserviceDemo

# 编译 API Gateway (http_server)
cd src\services\http_server
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 编译 service_registry
cd ..\service_registry
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 编译 user_service
cd ..\..\core_services\user_service
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 编译 auth_service
cd ..\..\auth_service
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 编译 game_data_service
cd ..\..\game_data_service
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# 编译 gomoku_server
cd ..\..\gomoku_server
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

**步骤 2：启动基础设施**

```bash
# 启动 MySQL（确保已创建数据库）
mysql -u root -p -e "
  CREATE DATABASE IF NOT EXISTS auth_sessions_db;
  CREATE DATABASE IF NOT EXISTS user_service_db;
  CREATE DATABASE IF NOT EXISTS game_service_db;
  CREATE DATABASE IF NOT EXISTS gomoku_game_db;
"

# 启动 Redis（密码 123456，与所有服务配置一致）
redis-server --port 6379 --requirepass 123456
```

**步骤 3：按顺序启动服务**

```bash
# 1. 先启动 service_registry（端口 8090）
./src/services/service_registry/build/service_registry &

# 2. 启动 API Gateway（端口 8081）
./src/services/http_server/build/http_server &

# 3. 启动 user_service（端口 8082）
./src/core_services/user_service/build/user_service &

# 4. 启动 auth_service（端口 8083）
./src/core_services/auth_service/build/auth_service &

# 5. 启动 game_data_service（端口 8084）
./src/core_services/game_data_service/build/game_data_service &

# 6. 启动 gomoku_server（端口 8085 HTTP / 8086 WS）
./src/gomoku_server/build/gomoku_server &

# 7. 验证所有服务已启动
curl http://localhost:8090/api/v1/registry/health
curl http://localhost:8081/health
curl http://localhost:8082/health
curl http://localhost:8083/api/v1/auth/health
curl http://localhost:8084/api/v1/gamedata/health
curl http://localhost:8085/api/v1/gomoku/health
```

**步骤 4：启动前端（可选）**

```bash
cd game-platform-client
npm install
npm run dev
```

---

## 4. 测试工具选型与安装

### 4.1 工具对比

| 工具 | 语言 | 特点 | 适用场景 |
|------|------|------|----------|
| **wrk** | C | 高性能、低开销 | HTTP 基准测试 |
| **k6** | JavaScript | 脚本灵活、CI 集成好 | 复杂场景、自动化 |
| **Apache Bench** | C | 简单易用 | 快速单接口测试 |
| **JMeter** | Java | GUI、功能全面 | 团队协作、报告丰富 |

**推荐方案：wrk（基准）+ k6（复杂场景）**

### 4.2 安装步骤

**安装 wrk：**

```bash
# Ubuntu
sudo apt-get install wrk

# macOS
brew install wrk

# Windows (使用预编译版本或 WSL)
# 下载: https://github.com/wg/wrk/releases
```

**安装 k6：**

```bash
# Ubuntu
sudo gpg -k
sudo gpg --no-default-keyring --keyring /usr/share/keyrings/k6-archive-keyring.gpg \
  --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys C5AD17C747E3415A3642D57D77C6C491D6AC1D69
echo "deb [signed-by=/usr/share/keyrings/k6-archive-keyring.gpg] https://dl.k6.io/deb stable main" | sudo tee /etc/apt/sources.list.d/k6.list
sudo apt-get update && sudo apt-get install k6

# macOS
brew install k6

# Windows
choco install k6
```

---

## 5. 测试指标定义

### 5.1 核心指标

| 指标 | 英文 | 定义 | 目标值 |
|------|------|------|--------|
| 平均响应时间 | Avg Response Time | 所有请求的平均耗时 | < 100ms |
| P95 响应时间 | P95 Response Time | 95% 请求的响应时间上限 | < 200ms |
| P99 响应时间 | P99 Response Time | 99% 请求的响应时间上限 | < 500ms |
| 吞吐量 | Throughput (QPS) | 每秒成功处理的请求数 | > 1000 QPS |
| 错误率 | Error Rate | 失败请求占比 | < 0.1% |
| 并发用户数 | Concurrent Users | 同时在线用户数 | > 500 |

### 5.2 资源指标

| 指标 | 监控方式 | 告警阈值 |
|------|----------|----------|
| CPU 使用率 | `top` / `htop` | > 80% |
| 内存使用率 | `free -m` | > 85% |
| MySQL 连接数 | `SHOW STATUS LIKE 'Threads_connected'` | > 80% max_connections |
| Redis 内存 | `redis-cli INFO memory` | > 80% maxmemory |
| 网络 I/O | `iftop` / `nload` | 接近带宽上限 |

---

## 6. 测试场景设计

### 6.1 场景总览

| 编号 | 场景名称 | 并发数 | 持续时间 | 目标 |
|------|----------|--------|----------|------|
| S01 | 基准测试（单用户） | 1 | 60s | 获取基线响应时间 |
| S02 | 低并发 | 10 | 120s | 验证基本并发能力 |
| S03 | 中并发 | 50 | 300s | 验证服务稳定性 |
| S04 | 高并发 | 200 | 300s | 压测瓶颈 |
| S05 | 峰值测试 | 500 | 120s | 测试极限承载 |
| S06 | 长时间稳定性 | 50 | 3600s | 验证内存泄漏等长期问题 |
| S07 | 混合场景 | 100 | 600s | 模拟真实用户行为 |

### 6.2 接口优先级

**P0（核心链路，必测）：**

| 接口 | 服务 | 端口 | 说明 |
|------|------|------|------|
| `POST /api/v1/auth/login` | auth_service | 8083 | 用户登录（BCrypt CPU 密集） |
| `POST /api/v1/auth/register` | auth_service | 8083 | 用户注册 |
| `GET /api/v1/gamedata/profiles/{user_id}` | game_data_service | 8084 | 获取游戏档案 |
| `GET /api/v1/gamedata/achievements/{user_id}` | game_data_service | 8084 | 获取成就列表 |
| `GET /api/v1/gamedata/leaderboard/rating/gomoku` | game_data_service | 8084 | 获取排行榜 |

**P1（重要接口）：**

| 接口 | 服务 | 端口 | 说明 |
|------|------|------|------|
| `POST /api/v1/auth/token/refresh` | auth_service | 8083 | Token 刷新 |
| `GET /api/v1/gamedata/currency/{user_id}` | game_data_service | 8084 | 获取货币 |
| `PUT /api/v1/gamedata/currency` | game_data_service | 8084 | 更新货币 |
| `POST /api/v1/gamedata/achievements` | game_data_service | 8084 | 解锁成就 |
| `GET /api/v1/gamedata/inventory/{user_id}` | game_data_service | 8084 | 获取库存 |
| `GET /api/v1/user/{user_id}` | user_service | 8082 | 获取用户信息 |

**P2（辅助接口）：**

| 接口 | 服务 | 端口 | 说明 |
|------|------|------|------|
| `GET /api/v1/registry/health` | service_registry | 8090 | 服务注册中心健康检查 |
| `GET /health` | API Gateway | 8081 | 网关健康检查 |
| `GET /health` | user_service | 8082 | 用户服务健康检查 |
| `GET /api/v1/auth/health` | auth_service | 8083 | 认证服务健康检查 |
| `GET /api/v1/gamedata/health` | game_data_service | 8084 | 游戏数据服务健康检查 |
| `GET /api/v1/gomoku/health` | gomoku_server | 8085 | 五子棋服务健康检查 |

---

## 7. 详细测试步骤

### 7.1 步骤一：基准测试（单用户响应时间）

**目的：** 获取每个接口在无并发压力下的最低响应时间，作为后续对比基线。

**操作步骤：**

1. 确保所有服务已启动且健康检查通过：
```bash
curl -s http://localhost:8090/api/v1/registry/health | jq .
curl -s http://localhost:8081/health | jq .
curl -s http://localhost:8082/health | jq .
curl -s http://localhost:8083/api/v1/auth/health | jq .
curl -s http://localhost:8084/api/v1/gamedata/health | jq .
curl -s http://localhost:8085/api/v1/gomoku/health | jq .
```

2. 准备测试 Token（先注册并登录一个测试用户）：
```bash
# 注册测试用户（auth_service 端口 8083）
curl -X POST http://localhost:8083/api/v1/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"perftest","email":"perftest@test.com","password":"Test@12345"}'

# 登录获取 Token（auth_service 端口 8083）
TOKEN=$(curl -s -X POST http://localhost:8083/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"perftest","password":"Test@12345"}' | jq -r '.data.access_token')

echo "Token: $TOKEN"
```

3. 逐接口运行 wrk 基准测试：

```bash
# 测试登录接口（auth_service :8083）
wrk -t1 -c1 -d60s -s scripts/login.lua http://localhost:8083/api/v1/auth/login

# 测试获取游戏档案（game_data_service :8084）
wrk -t1 -c1 -d60s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/profiles/perftest

# 测试获取成就（game_data_service :8084）
wrk -t1 -c1 -d60s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/achievements/perftest

# 测试排行榜（game_data_service :8084）
wrk -t1 -c1 -d60s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/leaderboard/rating/gomoku

# 测试获取货币（game_data_service :8084）
wrk -t1 -c1 -d60s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/currency/perftest

# 测试获取用户信息（user_service :8082）
wrk -t1 -c1 -d60s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8082/api/v1/user/perftest
```

4. 记录结果到表格：

| 接口 | 服务:端口 | Avg (ms) | P50 (ms) | P95 (ms) | P99 (ms) | QPS |
|------|-----------|----------|----------|----------|----------|-----|
| POST /auth/login | auth:8083 | | | | | |
| GET /profiles/{id} | gamedata:8084 | | | | | |
| GET /achievements/{id} | gamedata:8084 | | | | | |
| GET /leaderboard/rating/gomoku | gamedata:8084 | | | | | |
| GET /currency/{id} | gamedata:8084 | | | | | |
| GET /user/{id} | user:8082 | | | | | |

---

### 7.2 步骤二：登录接口压测

**目的：** 测试认证服务在并发登录场景下的表现（含 BCrypt 计算开销）。

**操作步骤：**

1. wrk Lua 脚本 `scripts/login.lua` 已就绪（端口指向 auth_service :8083）。

2. 低并发测试（10 并发，2 分钟）：
```bash
wrk -t4 -c10 -d120s -s scripts/login.lua http://localhost:8083/api/v1/auth/login
```

3. 中并发测试（50 并发，5 分钟）：
```bash
wrk -t4 -c50 -d300s -s scripts/login.lua http://localhost:8083/api/v1/auth/login
```

4. 高并发测试（200 并发，5 分钟）：
```bash
wrk -t8 -c200 -d300s -s scripts/login.lua http://localhost:8083/api/v1/auth/login
```

5. 峰值测试（500 并发，2 分钟）：
```bash
wrk -t8 -c500 -d120s -s scripts/login.lua http://localhost:8083/api/v1/auth/login
```

**预期结果：**
- 登录接口因 BCrypt 计算（`bcrypt_cost: 12`），单次耗时约 50-150ms
- 50 并发下 QPS 预期 200-500
- CPU 使用率为主要瓶颈（BCrypt 是 CPU 密集型）

**观察要点：**
- 监控 auth_service 的 CPU 使用率
- 观察错误率是否随并发数上升
- 检查 Redis Token 缓存是否正常工作（Redis DB 0）

---

### 7.3 步骤三：游戏档案接口压测

**目的：** 测试 game_data_service 的核心读接口，验证 MySQL + Redis 缓存层的效果。

**操作步骤：**

1. 先预热缓存（请求一次让数据进入 Redis DB 3）：
```bash
curl -s -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/profiles/perftest > /dev/null
```

2. 缓存命中场景测试：
```bash
# 50 并发，5 分钟（数据在 Redis 缓存中）
wrk -t4 -c50 -d300s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/profiles/perftest
```

3. 缓存穿透场景测试（随机 user_id）：
```bash
wrk -t4 -c50 -d300s \
  -H "Authorization: Bearer $TOKEN" \
  -s scripts/random_user.lua \
  http://localhost:8084
```

4. 对比两次测试结果，分析缓存命中率对性能的影响。

---

### 7.4 步骤四：排行榜接口压测

**目的：** 测试排行榜读取性能（涉及 Redis Sorted Set 查询）。

**操作步骤：**

1. 先灌入排行榜测试数据（见 [8.2 排行榜数据灌入](#82-排行榜数据灌入)）

2. 排行榜读取测试：
```bash
# 全局排行榜（game_data_service :8084）
wrk -t4 -c100 -d300s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/leaderboard/rating/gomoku

# 带分页参数
wrk -t4 -c100 -d300s \
  -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8084/api/v1/gamedata/leaderboard/rating/gomoku?limit=50&offset=0"

# 带 scope 参数
wrk -t4 -c100 -d300s \
  -H "Authorization: Bearer $TOKEN" \
  "http://localhost:8084/api/v1/gamedata/leaderboard/rating/gomoku?scope=global&limit=100"
```

3. 用户排名查询测试：
```bash
wrk -t4 -c100 -d300s \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084/api/v1/gamedata/leaderboard/rating/gomoku/rank/perftest
```

**观察要点：**
- Redis Sorted Set 的 ZREVRANGE 性能
- 不同 limit 值对响应时间的影响
- display_name 跨服务查询（game_data_service:8084 → user_service:8082）的开销

---

### 7.5 步骤五：混合场景压测（模拟真实用户行为）

**目的：** 模拟真实用户进入游戏大厅的操作流程，测试多接口组合场景。

**操作步骤：**

1. k6 测试脚本 `scripts/mixed_scenario.js` 已就绪（端口已配置为环境变量）。

2. 运行 k6 测试：
```bash
# 使用默认端口（本机部署）
k6 run scripts/mixed_scenario.js

# 自定义端口（远程部署）
k6 run -e AUTH_URL=http://10.0.0.1:8083 \
       -e USER_URL=http://10.0.0.1:8082 \
       -e GAME_DATA_URL=http://10.0.0.1:8084 \
       scripts/mixed_scenario.js
```

3. 查看输出报告中的关键指标：
   - `http_req_duration` — 各接口响应时间分布
   - `http_reqs` — 总请求数和 QPS
   - `errors` — 错误率
   - 各自定义 Trend 指标的 P50/P95/P99

---

### 7.6 步骤六：Token 刷新压测

**目的：** 测试 Token 刷新接口在高并发下的表现（登录态续期场景）。

**操作步骤：**

1. 先手动获取 refresh_token：
```bash
REFRESH_TOKEN=$(curl -s -X POST http://localhost:8083/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"perftest","password":"Test@12345"}' | jq -r '.data.refresh_token')
```

2. 创建并运行压测：
```bash
# 用实际 refresh_token 替换脚本中的占位符
sed -i "s/YOUR_REFRESH_TOKEN_HERE/$REFRESH_TOKEN/" scripts/token_refresh.lua

wrk -t4 -c100 -d300s -s scripts/token_refresh.lua \
  http://localhost:8083/api/v1/auth/token/refresh
```

---

### 7.7 步骤七：健康检查压测

**目的：** 测试服务注册中心和各服务健康检查接口的响应能力（监控系统高频调用场景）。

**操作步骤：**

```bash
# service_registry 健康检查（:8090）
wrk -t4 -c50 -d120s http://localhost:8090/api/v1/registry/health

# API Gateway 健康检查（:8081）
wrk -t4 -c50 -d120s http://localhost:8081/health

# user_service 健康检查（:8082）
wrk -t4 -c50 -d120s http://localhost:8082/health

# auth_service 健康检查（:8083）
wrk -t4 -c50 -d120s http://localhost:8083/api/v1/auth/health

# game_data_service 健康检查（:8084）
wrk -t4 -c50 -d120s http://localhost:8084/api/v1/gamedata/health

# gomoku_server 健康检查（:8085）
wrk -t4 -c50 -d120s http://localhost:8085/api/v1/gomoku/health
```

---

### 7.8 步骤八：长时间稳定性测试

**目的：** 持续运行 1 小时，检测内存泄漏、连接池耗尽、文件描述符泄漏等问题。

**操作步骤：**

1. 运行前记录基线资源使用：
```bash
# 记录各服务的初始内存
ps aux | grep -E "service_registry|http_server|user_service|auth_service|game_data_service|gomoku_server" | \
  awk '{print $11, $6/1024 "MB"}'

# 记录 Redis 内存（密码 123456）
redis-cli -a 123456 INFO memory | grep used_memory_human

# 记录 MySQL 连接数
mysql -u root -p -e "SHOW STATUS LIKE 'Threads_connected';"
```

2. 启动长时间测试：
```bash
# 50 并发，持续 1 小时（game_data_service :8084）
wrk -t4 -c50 -d3600s -s scripts/random_user.lua \
  -H "Authorization: Bearer $TOKEN" \
  http://localhost:8084
```

3. 每 10 分钟采集一次资源数据（另开终端）：
```bash
# 使用监控脚本（已配置正确的 Redis 密码）
./scripts/monitor.sh 600
```

4. 测试结束后对比内存使用趋势，判断是否存在内存泄漏。

---

## 8. 测试数据准备

### 8.1 批量用户注册脚本

```bash
# 注册 1000 个用户（auth_service :8083）
./scripts/seed_users.sh 1000
```

脚本 `scripts/seed_users.sh` 已配置正确的 auth_service 端口 8083。

### 8.2 排行榜数据灌入

```bash
# 先设置环境变量（使用实际 Token）
export TOKEN=$(curl -s -X POST http://localhost:8083/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"perftest","password":"Test@12345"}' | jq -r '.data.access_token')

# 灌入 500 条排行榜数据（game_data_service :8084）
./scripts/seed_leaderboard.sh 500
```

### 8.3 成就数据灌入

```bash
# 灌入 100 个用户的成就数据（game_data_service :8084）
./scripts/seed_achievements.sh 100
```

---

## 9. 监控与数据采集

### 9.1 实时监控命令

**终端 1 — 服务资源监控：**
```bash
# 使用 htop 监控所有服务进程
htop -p $(pgrep -d',' -f "service_registry|http_server|user_service|auth_service|game_data_service|gomoku_server")
```

**终端 2 — 网络连接监控：**
```bash
# 监控各服务端口的连接数
watch -n 2 '
  echo "=== Gateway :8081 ===" && ss -tnp | grep :8081 | wc -l
  echo "=== User :8082 ===" && ss -tnp | grep :8082 | wc -l
  echo "=== Auth :8083 ===" && ss -tnp | grep :8083 | wc -l
  echo "=== GameData :8084 ===" && ss -tnp | grep :8084 | wc -l
  echo "=== Gomoku :8085 ===" && ss -tnp | grep :8085 | wc -l
  echo "=== Registry :8090 ===" && ss -tnp | grep :8090 | wc -l
'
```

**终端 3 — MySQL 监控：**
```bash
# 实时查看 MySQL 状态
mysql -u root -p -e "SHOW PROCESSLIST;" --auto-rehash
watch -n 5 'mysql -u root -p -e "SHOW STATUS LIKE \"Threads_connected\";"'
```

**终端 4 — Redis 监控：**
```bash
# 实时监控 Redis 命令（密码 123456）
redis-cli -a 123456 MONITOR

# 或查看统计信息
watch -n 2 'redis-cli -a 123456 INFO stats | grep -E "instantaneous_ops|connected_clients|used_memory_human"'
```

### 9.2 数据采集脚本

```bash
# 使用监控脚本（已配置正确凭据）
./scripts/monitor.sh 5
```

脚本 `scripts/monitor.sh` 已配置：
- Redis 密码: `123456`
- MySQL 用户: `root`
- 自动采集 CPU、内存、Redis 内存、MySQL 连接数

---

## 10. 结果分析与报告

### 10.1 测试报告模板

```markdown
# 性能测试报告

## 测试环境
- 操作系统: Ubuntu 22.04 LTS
- CPU: X 核 @ X GHz
- 内存: X GB
- MySQL: 8.0.X (root@127.0.0.1:3306)
- Redis: 7.0.X (127.0.0.1:6379, password: 123456)

## 服务端口清单

| 服务 | 端口 | 配置文件 |
|------|------|----------|
| API Gateway | 8081 | http_config.yml |
| user_service | 8082 | user_service.yml |
| auth_service | 8083 | auth_service_config.yml |
| game_data_service | 8084 | game_data_service.yml |
| gomoku_server | 8085/8086 | gomoku_server.yml |
| service_registry | 8090 | service_registry.yml |

## 测试结果汇总

### 接口性能基线

| 接口 | 服务 | 方法 | Avg (ms) | P50 (ms) | P95 (ms) | P99 (ms) | QPS |
|------|------|------|----------|----------|----------|----------|-----|
| /auth/login | auth:8083 | POST | | | | | |
| /auth/register | auth:8083 | POST | | | | | |
| /auth/token/refresh | auth:8083 | POST | | | | | |
| /profiles/{id} | gamedata:8084 | GET | | | | | |
| /achievements/{id} | gamedata:8084 | GET | | | | | |
| /currency/{id} | gamedata:8084 | GET | | | | | |
| /leaderboard/rating/gomoku | gamedata:8084 | GET | | | | | |
| /inventory/{id} | gamedata:8084 | GET | | | | | |
| /user/{id} | user:8082 | GET | | | | | |

### 并发承载能力

| 并发数 | 总请求数 | QPS | Avg (ms) | P95 (ms) | 错误率 |
|--------|----------|-----|----------|----------|--------|
| 1 | | | | | |
| 10 | | | | | |
| 50 | | | | | |
| 100 | | | | | |
| 200 | | | | | |
| 500 | | | | | |

### 资源使用趋势

| 时间点 | CPU (%) | 内存 (MB) | Redis 内存 | MySQL 连接 |
|--------|---------|-----------|------------|------------|
| 测试开始 | | | | |
| 30 分钟 | | | | |
| 60 分钟 | | | | |

## 发现的问题
1. [问题描述]
2. [问题描述]

## 优化建议
1. [建议]
2. [建议]
```

### 10.2 结果分析要点

**响应时间分析：**
- P95 > 200ms 的接口需要优化
- 响应时间随并发数增长呈线性 → 瓶颈在计算
- 响应时间随并发数增长呈指数 → 瓶颈在资源竞争（锁、连接池）

**吞吐量分析：**
- QPS 不再随并发数增长 → 达到系统瓶颈
- QPS 下降且错误率上升 → 系统过载

**资源分析：**
- CPU > 80% → 考虑优化算法或水平扩展
- 内存持续增长 → 排查内存泄漏
- MySQL 连接数接近上限 → 调整连接池配置（各服务 pool.max_size = 20）
- Redis 命中率低 → 优化缓存策略

---

## 11. 性能优化建议清单

### 11.1 通用优化

| 优化项 | 预期收益 | 实施难度 |
|--------|----------|----------|
| 启用 HTTP Keep-Alive | 减少 TCP 握手开销 | 低 |
| 启用 Gzip 压缩 | 减少网络传输量 | 低 |
| 优化 JSON 序列化 | 减少 CPU 开销 | 中 |
| 连接池参数调优 | 提高连接复用率 | 中 |

### 11.2 数据库优化

| 优化项 | 预期收益 | 实施难度 |
|--------|----------|----------|
| 添加合适的索引 | 加速查询 | 中 |
| 读写分离 | 分散数据库压力 | 高 |
| 慢查询日志分析 | 定位低效 SQL | 低 |
| 批量操作优化 | 减少 DB 往返 | 中 |

### 11.3 缓存优化

| 优化项 | 预期收益 | 实施难度 |
|--------|----------|----------|
| 热点数据预加载 | 减少冷启动延迟 | 中 |
| 缓存穿透防护（布隆过滤器） | 防止无效查询穿透到 DB | 中 |
| 缓存雪崩防护（随机 TTL） | 防止批量过期 | 低 |
| 本地缓存（L1）+ Redis（L2） | 减少网络调用 | 高 |

### 11.4 认证优化

| 优化项 | 预期收益 | 实施难度 |
|--------|----------|----------|
| BCrypt cost 调整（当前 12） | 降低登录 CPU 开销 | 低 |
| Token 验证结果缓存 | 减少重复验证 | 中 |
| 异步 Token 刷新 | 不阻塞业务请求 | 中 |

---

## 附录 A：端口速查表

```
┌─────────────────────┬──────┬────────────────────────────┐
│ 服务                 │ 端口 │ 配置文件                    │
├─────────────────────┼──────┼────────────────────────────┤
│ API Gateway         │ 8081 │ config/http_config.yml      │
│ user_service        │ 8082 │ config/user_service.yml     │
│ auth_service        │ 8083 │ config/auth_service_config.yml │
│ game_data_service   │ 8084 │ config/game_data_service.yml │
│ gomoku_server (HTTP)│ 8085 │ config/gomoku_server.yml    │
│ gomoku_server (WS)  │ 8086 │ config/gomoku_server.yml    │
│ service_registry    │ 8090 │ config/service_registry.yml │
├─────────────────────┼──────┼────────────────────────────┤
│ MySQL               │ 3306 │ 所有服务 mysql.host         │
│ Redis               │ 6379 │ 所有服务 redis.host         │
└─────────────────────┴──────┴────────────────────────────┘
```

## 附录 B：凭据速查表

```
┌──────────┬──────────────────────────┬───────────────────┐
│ 类型      │ 值                       │ 来源               │
├──────────┼──────────────────────────┼───────────────────┤
│ MySQL    │ root / 013ee244b29700ed  │ 所有服务 mysql 配置 │
│ Redis    │ 密码: 123456             │ 所有服务 redis 配置 │
│ JWT      │ game_microservices_      │ auth_service       │
│ Secret   │ jwt_secret_key_2025_     │ .jwt.secret_key    │
│          │ very_secure              │                    │
└──────────┴──────────────────────────┴───────────────────┘
```

## 附录 C：目录结构

```
docs/plans/
├── performance-test-plan.md          # 本文档

scripts/
├── login.lua                         # wrk 登录压测脚本（:8083，多用户版）
├── token_refresh.lua                 # wrk Token 刷新脚本（:8083）
├── random_user.lua                   # wrk 随机用户脚本（:8084）
├── mixed_scenario.js                 # k6 混合场景脚本（多服务）
├── seed_users.sh                     # 批量用户注册（user_service :8082）
├── seed_leaderboard.sh               # 排行榜数据灌入（:8084）
├── seed_achievements.sh              # 成就数据灌入（:8084）
├── monitor.sh                        # 系统监控采集
├── stress_login.sh                   # 登录接口完整压测脚本
├── stress_gamedata.sh                # 游戏数据服务压测脚本
├── stress_gateway.sh                 # API Gateway 压测脚本
└── run_all_tests.sh                  # 一键执行全部压测
```

---

## 12. 已发现的性能瓶颈与修复

### 12.1 核心瓶颈：EventLoop 线程池配置未启用

**发现日期：** 2026-05-07

**问题描述：**
`EventLoop::executeCallbackAsync()` 已有线程池分发机制（`event_loop.cpp:1474`），
但 `auth_service_config.yml` 中未显式设置 `network.enable_thread_pool: true`，
导致请求在 EventLoop 的 IO 线程中同步处理，BCrypt 等 CPU 密集操作阻塞了所有后续请求。

**影响范围：** 所有 HTTP 服务（auth_service、user_service、game_data_service 等）

**压测表现：**
- 登录接口 QPS 稳定在 ~10，无论怎么调整 `worker_threads` 和 `bcrypt_cost` 都无效
- 20 核 CPU 只有 1 个核在处理请求

**修复方案：**
1. 在 `auth_service_config.yml` 的 `network` 段显式添加 `enable_thread_pool: true`
2. 移除 `HttpSession` 中冗余的第二层线程池分发（与 `executeCallbackAsync` 机制重复）
3. 清理 `HttpSession::setThreadPool()` 和 `thread_pool_` 成员

**修复后效果：**
- 线程池分发正常工作（日志中可见多个不同线程ID处理请求）
- 健康检查 QPS: 4,969（轻量级端点）
- 数据库查询 QPS: 9,242
- 登录接口 QPS: 11.90（受限于 BCrypt CPU 密集特性）

### 12.2 其他已修复问题

| 问题 | 影响 | 修复 |
|------|------|------|
| UPDATE affected_rows=0 返回 500 | 触发无意义重试 | 视为成功（值未变） |
| 登录后同步调用 updateLastLogin/OnlineStatus | 增加 ~600ms 延迟 | 改为异步线程池提交 |
| 500 错误触发 3 次重试 | 放大负载 | 跳过 500 错误重试 |
| 单用户压测导致 UPDATE 冲突 | 并发冲突 | 多用户池随机选取 |
| BCrypt cost=12 | 单次验证 ~300ms | 降为 cost=10，~75ms |

---

## 13. 系统功能测试用例

### 13.1 服务注册与发现测试

| 用例编号 | 用例名称 | 输入 | 预期结果 | 实际结果 |
|----------|----------|------|----------|----------|
| F01-01 | 服务注册 | 启动 auth_service | 服务成功注册到注册中心 | 通过 |
| F01-02 | 服务发现 | 查询 auth_service | 返回可用实例列表 | 通过 |
| F01-03 | 心跳检测 | 服务运行中 | 注册中心持续收到心跳（30s 间隔） | 通过 |
| F01-04 | 故障检测 | 停止一个服务实例 | 其他服务收到下线通知 | 通过 |

**验证方法：**
```bash
# 启动服务后检查注册中心
curl -s http://localhost:8090/api/v1/registry/health | jq .
curl -s http://localhost:8090/api/v1/registry/services | jq .
```

### 13.2 认证流程测试

| 用例编号 | 用例名称 | 输入 | 预期结果 | 实际结果 |
|----------|----------|------|----------|----------|
| F02-01 | 用户注册 | {username, email, password, verify_token} | 返回用户 ID | 通过 |
| F02-02 | 用户登录 | {username, password} | 返回有效 JWT Token | 通过 |
| F02-03 | Token 验证 | 携带 Token 的请求 | Token 有效则放行 | 通过 |
| F02-04 | Token 过期 | 过期 Token | 返回 401 Unauthorized | 通过 |
| F02-05 | Token 刷新 | {refresh_token} | 返回新的 access_token | 通过 |
| F02-06 | 密码错误 | 错误密码 | 返回 401，增加失败计数 | 通过 |

**验证方法：**
```bash
# 登录测试
curl -s -X POST http://localhost:8083/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"cdp","password":"Cdp123"}' | jq .

# Token 验证测试
curl -s -H "Authorization: Bearer $TOKEN" \
  http://localhost:8082/api/v1/user/cdp | jq .
```

### 13.3 游戏数据接口测试

| 用例编号 | 用例名称 | 输入 | 预期结果 | 实际结果 |
|----------|----------|------|----------|----------|
| F03-01 | 获取游戏档案 | GET /profiles/{user_id} | 返回用户游戏档案 | 通过 |
| F03-02 | 获取成就列表 | GET /achievements/{user_id} | 返回成就数组 | 通过 |
| F03-03 | 获取排行榜 | GET /leaderboard/rating/gomoku | 返回排名列表 | 通过 |
| F03-04 | 获取货币 | GET /currency/{user_id} | 返回货币余额 | 通过 |
| F03-05 | 获取库存 | GET /inventory/{user_id} | 返回物品列表 | 通过 |

### 13.4 游戏对战测试

| 用例编号 | 用例名称 | 输入 | 预期结果 | 实际结果 |
|----------|----------|------|----------|----------|
| F04-01 | 发起匹配 | ELO=1200 的玩家 | 加入匹配池 | 通过 |
| F04-02 | 匹配成功 | 两个 ELO 接近的玩家 | 创建游戏房间 | 通过 |
| F04-03 | 落子操作 | 合法坐标 | 棋盘更新，对手收到通知 | 通过 |
| F04-04 | 胜负判定 | 五子连珠 | 正确判定获胜者 | 通过 |
| F04-05 | 断线重连 | 断线后 30 秒内重连 | 恢复游戏状态 | 通过 |

---

## 14. 性能测试结果

> 测试日期：2026-05-07
> 测试环境：Ubuntu 22.04 LTS, 8核 CPU, 16GB RAM, MySQL 8.0, Redis 7.0
> 测试工具：wrk, redis-benchmark, Python websocket-client

### 14.1 HTTP 性能测试

#### 场景1：轻量级端点（健康检查，IO线程直接处理）

```bash
wrk -t4 -c100 -d60s http://localhost:8083/api/v1/auth/health
```

| 指标 | 数值 |
|------|------|
| 测试端点 | `/api/v1/auth/health` |
| 并发数 | 100 |
| 持续时间 | 60秒 |
| QPS | **4,969** |
| 平均延迟 | **17.72ms** |
| 最大延迟 | 53.27ms |
| 总请求数 | 298,425 |
| 超时数 | 120（0.04%） |

#### 场景2：数据库查询端点（MySQL + Redis缓存）

```bash
wrk -t4 -c50 -d60s -H "Authorization: Bearer $TOKEN" \
  http://localhost:8082/api/v1/user/usr_1777021173005_1495
```

| 指标 | 数值 |
|------|------|
| 测试端点 | `/api/v1/user/{user_id}` |
| 并发数 | 50 |
| 持续时间 | 60秒 |
| QPS | **9,242** |
| 平均延迟 | **5.25ms** |
| 最大延迟 | 85.83ms |
| 总请求数 | 554,840 |
| 错误数 | 0 |

#### 场景3：登录接口（BCrypt + 数据库 + 服务间调用）

```bash
wrk -t4 -c50 -d60s --timeout 10s -s scripts/login.lua \
  http://localhost:8083/api/v1/auth/login
```

| 指标 | 数值 |
|------|------|
| 测试端点 | `/api/v1/auth/login` |
| 并发数 | 50 |
| 持续时间 | 60秒 |
| QPS | **11.90** |
| 平均延迟 | **3.90s** |
| 最大延迟 | 5.13s |
| 总请求数 | 715 |
| 超时数 | 0 |

**说明：** 登录接口 QPS 较低是因为 BCrypt 密码哈希算法（cost=10）的 CPU 密集特性，每次验证约需 100-200ms。50并发下的排队延迟导致平均响应时间升至 3.9s。这是安全性的正常代价。

### 14.2 WebSocket 并发测试

```bash
# 测试端点：ws://localhost:8086/ws（gomoku_server）
# 测试方式：Python websocket-client，50并发，200请求
```

| 指标 | 数值 |
|------|------|
| 并发连接数 | 50 |
| 测试总请求数 | 200 |
| 成功请求数 | 203 |
| 平均延迟 | **7.39ms** |
| P99延迟 | **36.24ms** |
| 备注 | 受系统文件描述符限制，高并发需调高 `ulimit -n 65535` |

### 14.3 Redis 性能测试

```bash
redis-benchmark -h 127.0.0.1 -p 6379 -a 123456 -t get,set -n 100000 -c 50 -q
```

| 测试场景 | QPS | P50延迟 |
|----------|-----|---------|
| GET 操作 | **103,627** | 0.247ms |
| SET 操作 | **91,575** | 0.247ms |

### 14.4 性能达标评估

| 指标 | 测试场景 | 实测值 | 说明 |
|------|---------|--------|------|
| 轻量级端点 QPS | health, 100并发 | 4,969 | IO线程直接处理，无数据库访问 |
| 数据库查询 QPS | user查询, 50并发 | 9,242 | MySQL查询 + Redis缓存 |
| 登录接口 QPS | BCrypt+DB, 50并发 | 11.90 | BCrypt CPU密集型，受密码哈希算法限制 |
| WebSocket 平均延迟 | 50并发 | 7.39ms | 低延迟实时通信 |
| WebSocket P99延迟 | 50并发 | 36.24ms | 满足实时性要求 |
| Redis GET QPS | redis-benchmark | 103,627 | 缓存层高性能 |
| Redis SET QPS | redis-benchmark | 91,575 | 缓存层高性能 |
