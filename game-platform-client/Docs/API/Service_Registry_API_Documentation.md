# 服务注册中心 API 文档

## 概述

本文档详细描述服务注册中心 (Service Registry) 的 HTTP REST API。服务注册中心是游戏微服务架构中的核心组件，负责服务实例的注册、发现、健康监控和负载均衡推荐。

---

## 服务基础信息

| 属性 | 值 |
|------|------|
| **服务名称** | `service_registry` |
| **默认端口** | `8081` (HTTP) |
| **服务版本** | `1.0.0` |
| **基础路径** | `/api/v1/services/` |
| **存储** | Redis (持久化 + TTL) |
| **架构特点** | 无状态设计、支持水平扩展、Redis Pub/Sub 事件通知 |

---

## API 端点总览

### 服务注册相关

| 方法 | 端点 | 描述 |
|------|------|------|
| POST | `/api/v1/services/register` | 注册单个服务实例 |
| POST | `/api/v1/services/batch-register` | 批量注册服务实例 |
| POST | `/api/v1/services/heartbeat` | 更新心跳和元数据 |
| DELETE | `/api/v1/services/deregister` | 注销服务实例 |

### 服务查询相关

| 方法 | 端点 | 描述 |
|------|------|------|
| GET | `/api/v1/services` | 获取服务列表（支持过滤，兼容路径） |
| GET | `/api/v1/services/lists` | 获取服务列表（推荐路径，避免 CORS 问题） |
| GET | `/api/v1/services/{service_name}` | 获取特定服务的所有实例 |
| GET | `/api/v1/services/{service_name}/{host}:{port}` | 获取特定实例详情 |
| GET | `/api/v1/services/{service_name}/health` | 获取服务健康状态 |
| GET | `/api/v1/services/stats` | 获取统计信息 |
| POST | `/api/v1/services/matches/report` | 报告对局完成 |
| GET | `/api/v1/services/recommend` | 获取推荐的服务实例 |

### 系统管理相关

| 方法 | 端点 | 描述 |
|------|------|------|
| GET | `/api/v1/services/health` | 服务注册中心健康检查 |
| POST | `/api/v1/services/admin/cleanup` | 手动清理过期服务 |
| POST | `/api/v1/services/admin/rebuild-index` | 重建索引 |

---

## 1. 服务注册 API

### 1.1 注册服务实例

**端点**: `POST /api/v1/services/register`

**功能**: 注册新的服务实例到注册中心

**请求头**:
```http
Content-Type: application/json
```

**请求体**:
```json
{
    "service_name": "gomoku_server",
    "service_version": "1.0.0",
    "host": "192.168.1.100",
    "port": 8085,
    "health_check_endpoint": "/health",
    "weight": 100,
    "metadata": {
        "game_type": "gomoku",
        "region": "cn-east",
        "current_players": "5",
        "max_players": "100",
        "active_rooms": "3",
        "cpu_usage": "35.5",
        "memory_usage": "42.3",
        "server_status": "online"
    },
    "endpoints": [
        "/api/v1/gomoku/rooms",
        "/api/v1/gomoku/games",
        "/ws/gomoku/"
    ]
}
```

**请求字段说明**:

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| `service_name` | string | 是 | 服务名称（如 gomoku_server） |
| `service_version` | string | 否 | 服务版本，默认 "1.0.0" |
| `host` | string | 是 | 服务实例的主机地址 |
| `port` | integer | 是 | 服务实例的端口号 |
| `health_check_endpoint` | string | 否 | 健康检查端点，默认 "/health" |
| `weight` | integer | 否 | 负载均衡权重 (1-1000)，默认 100 |
| `metadata` | object | 否 | 服务元数据键值对 |
| `endpoints` | array | 否 | API 端点列表 |

**预定义元数据键**:

| 键 | 描述 | 示例值 |
|----|------|--------|
| `service_type` | **服务类型（必须）** | core, game, gateway |
| `game_type` | 游戏类型（游戏服务必须） | gomoku, snake, tetris |
| `region` | 区域标识 | cn-east, cn-west, us-west |
| `current_players` | 当前玩家数 | 5 |
| `max_players` | 最大玩家数 | 100 |
| `active_rooms` | 活跃房间数 | 3 |
| `cpu_usage` | CPU 使用率 (%) | 35.5 |
| `memory_usage` | 内存使用率 (%) | 42.3 |
| `server_status` | 服务器状态 | online, maintenance, draining |

**服务类型说明**:

| 类型 | 描述 | 示例服务 |
|------|------|----------|
| `core` | 核心基础服务 | user_service, auth_service, game_data_service |
| `game` | 游戏逻辑服务 | gomoku_server, snake_server |
| `gateway` | 网关服务 | api_gateway |

**成功响应** (200):
```json
{
    "success": true,
    "message": "Service registered successfully",
    "timestamp": 1708123456789,
    "data": {
        "service_name": "gomoku_server",
        "instance_id": "192.168.1.100:8085",
        "redis_key": "service:registry:gomoku_server:192.168.1.100:8085"
    }
}
```

**失败响应** (400):
```json
{
    "success": false,
    "error": {
        "code": "INVALID_DATA",
        "message": "service_name, host and port are required"
    },
    "timestamp": 1708123456789
}
```

**cURL 示例**:
```bash
curl -X POST http://localhost:8081/api/v1/services/register \
  -H "Content-Type: application/json" \
  -d '{
    "service_name": "gomoku_server",
    "host": "192.168.1.100",
    "port": 8085,
    "metadata": {
      "game_type": "gomoku",
      "region": "cn-east"
    }
  }'
```

---

### 1.2 批量注册服务

**端点**: `POST /api/v1/services/batch-register`

**功能**: 批量注册多个服务实例，适用于服务启动时一次性注册

**请求头**:
```http
Content-Type: application/json
```

**请求体**:
```json
{
    "services": [
        {
            "service_name": "gomoku_server",
            "host": "192.168.1.100",
            "port": 8085,
            "metadata": {
                "game_type": "gomoku",
                "region": "cn-east"
            }
        },
        {
            "service_name": "gomoku_server",
            "host": "192.168.1.101",
            "port": 8085,
            "metadata": {
                "game_type": "gomoku",
                "region": "cn-west"
            }
        }
    ]
}
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Batch registration completed",
    "timestamp": 1708123456789,
    "data": {
        "total": 2,
        "success_count": 2,
        "failed_count": 0,
        "results": [
            {
                "instance_id": "192.168.1.100:8085",
                "success": true
            },
            {
                "instance_id": "192.168.1.101:8085",
                "success": true
            }
        ]
    }
}
```

---

### 1.3 更新心跳

**端点**: `POST /api/v1/services/heartbeat`

**功能**: 更新服务实例的心跳时间戳和元数据

**请求头**:
```http
Content-Type: application/json
```

**请求体**:
```json
{
    "service_name": "gomoku_server",
    "host": "192.168.1.100",
    "port": 8085,
    "status": "healthy",
    "metadata": {
        "current_players": "12",
        "active_rooms": "8",
        "cpu_usage": "55.2",
        "memory_usage": "48.1"
    }
}
```

**请求字段说明**:

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| `service_name` | string | 是 | 服务名称 |
| `host` | string | 是 | 主机地址 |
| `port` | integer | 是 | 端口号 |
| `status` | string | 否 | 状态标识（healthy/unhealthy） |
| `metadata` | object | 否 | 要更新的元数据 |

**成功响应** (200):
```json
{
    "success": true,
    "message": "Heartbeat updated successfully",
    "timestamp": 1708123516789,
    "data": {
        "instance_id": "192.168.1.100:8085",
        "health_score": 0.92
    }
}
```

**失败响应** (404):
```json
{
    "success": false,
    "error": {
        "code": "NOT_FOUND",
        "message": "Service instance not found"
    },
    "timestamp": 1708123516789
}
```

**心跳说明**:
- 心跳超时默认为 90 秒，超过此时间未收到心跳，服务将被标记为不健康
- 建议心跳间隔为 15-30 秒
- 元数据会与现有值合并，不会覆盖未指定的字段

---

### 1.4 注销服务

**端点**: `DELETE /api/v1/services/deregister`

**功能**: 从注册中心移除服务实例

**请求头**:
```http
Content-Type: application/json
```

**请求体**:
```json
{
    "service_name": "gomoku_server",
    "host": "192.168.1.100",
    "port": 8085
}
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Service deregistered successfully",
    "timestamp": 1708123600000
}
```

**失败响应** (404):
```json
{
    "success": false,
    "error": {
        "code": "NOT_FOUND",
        "message": "Service not found"
    },
    "timestamp": 1708123600000
}
```

---

## 2. 服务查询 API

### 2.1 获取服务列表

**端点**: `GET /api/v1/services` 或 `GET /api/v1/services/lists`（推荐）

**功能**: 获取所有已注册的服务列表，支持多维度过滤

> **注意**: 推荐使用 `/api/v1/services/lists` 路径，可避免 Nginx location 匹配的尾部斜杠问题导致的 CORS 错误。

**查询参数**:

| 参数 | 类型 | 必填 | 描述 |
|------|------|------|------|
| `service_name` | string | 否 | 按服务名称过滤 |
| `service_type` | string | 否 | 按服务类型过滤 (core, game, gateway) |
| `game_type` | string | 否 | 按游戏类型过滤 |
| `region` | string | 否 | 按区域过滤 |
| `healthy_only` | boolean | 否 | 仅返回健康服务，默认 false |
| `min_health_score` | number | 否 | 最低健康分数 (0.0-1.0)，默认 0.0 |
| `max_current_players` | integer | 否 | 最大当前玩家数 |
| `limit` | integer | 否 | 返回数量限制，默认 100 |
| `offset` | integer | 否 | 分页偏移量，默认 0 |

**请求示例**:
```http
GET /api/v1/services?game_type=gomoku&healthy_only=true&limit=10
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Query successful",
    "timestamp": 1708123520000,
    "data": {
        "services": [
            {
                "service_name": "gomoku_server",
                "service_version": "1.0.0",
                "host": "192.168.1.100",
                "port": 8085,
                "healthy": true,
                "health_score": 0.92,
                "weight": 100,
                "last_heartbeat": 1708123516789,
                "heartbeat_age_seconds": 3,
                "register_time": 1708120000000,
                "uptime_seconds": 3520,
                "instance_id": "192.168.1.100:8085",
                "metadata": {
                    "game_type": "gomoku",
                    "region": "cn-east",
                    "current_players": "12",
                    "max_players": "100",
                    "active_rooms": "8",
                    "cpu_usage": "55.2",
                    "memory_usage": "48.1"
                },
                "endpoints": [
                    "/api/v1/gomoku/rooms",
                    "/api/v1/gomoku/games",
                    "/ws/gomoku/"
                ]
            }
        ],
        "pagination": {
            "total": 1,
            "limit": 10,
            "offset": 0,
            "has_more": false
        }
    }
}
```

---

### 2.2 获取特定服务的所有实例

**端点**: `GET /api/v1/services/{service_name}`

**功能**: 获取指定服务名称的所有实例

**路径参数**:

| 参数 | 类型 | 描述 |
|------|------|------|
| `service_name` | string | 服务名称 |

**请求示例**:
```http
GET /api/v1/services/gomoku_server
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Query successful",
    "timestamp": 1708123530000,
    "data": {
        "service_name": "gomoku_server",
        "instances": [
            {
                "host": "192.168.1.100",
                "port": 8085,
                "healthy": true,
                "health_score": 0.92,
                "metadata": { ... }
            },
            {
                "host": "192.168.1.101",
                "port": 8085,
                "healthy": true,
                "health_score": 0.88,
                "metadata": { ... }
            }
        ],
        "total_instances": 2,
        "healthy_instances": 2
    }
}
```

**失败响应** (404):
```json
{
    "success": false,
    "error": {
        "code": "NOT_FOUND",
        "message": "Service not found: unknown_service"
    },
    "timestamp": 1708123530000
}
```

---

### 2.3 获取特定实例详情

**端点**: `GET /api/v1/services/{service_name}/{host}:{port}`

**功能**: 获取指定服务实例的详细信息

**路径参数**:

| 参数 | 类型 | 描述 |
|------|------|------|
| `service_name` | string | 服务名称 |
| `host` | string | 主机地址 |
| `port` | integer | 端口号 |

**请求示例**:
```http
GET /api/v1/services/gomoku_server/192.168.1.100:8085
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Query successful",
    "timestamp": 1708123540000,
    "data": {
        "service": {
            "service_name": "gomoku_server",
            "service_version": "1.0.0",
            "host": "192.168.1.100",
            "port": 8085,
            "healthy": true,
            "health_score": 0.92,
            "weight": 100,
            "total_requests": 1523,
            "failed_requests": 12,
            "avg_response_time_ms": 45,
            "last_heartbeat": 1708123516789,
            "heartbeat_age_seconds": 23,
            "register_time": 1708120000000,
            "uptime_seconds": 3540,
            "metadata": { ... },
            "endpoints": [ ... ]
        }
    }
}
```

---

### 2.4 获取服务健康状态

**端点**: `GET /api/v1/services/{service_name}/health`

**功能**: 获取指定服务的整体健康状态和各实例的健康详情

**路径参数**:

| 参数 | 类型 | 描述 |
|------|------|------|
| `service_name` | string | 服务名称 |

**请求示例**:
```http
GET /api/v1/services/gomoku_server/health
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Service health retrieved",
    "timestamp": 1708123550000,
    "data": {
        "service_name": "gomoku_server",
        "total_instances": 3,
        "healthy_instances": 2,
        "degraded_instances": 1,
        "unhealthy_instances": 0,
        "avg_health_score": 0.85,
        "overall_status": "degraded",
        "instances": [
            {
                "instance_id": "192.168.1.100:8085",
                "healthy": true,
                "health_score": 0.92,
                "last_heartbeat_age": 3
            },
            {
                "instance_id": "192.168.1.101:8085",
                "healthy": true,
                "health_score": 0.88,
                "last_heartbeat_age": 5
            },
            {
                "instance_id": "192.168.1.102:8085",
                "healthy": false,
                "health_score": 0.65,
                "last_heartbeat_age": 45
            }
        ]
    }
}
```

**健康状态说明**:

| 状态 | 健康分数范围 | 描述 |
|------|-------------|------|
| `healthy` | >= 0.7 | 服务运行正常 |
| `degraded` | 0.3 - 0.7 | 服务性能下降 |
| `unhealthy` | < 0.3 或心跳超时 | 服务不可用 |

---

### 2.5 获取统计信息

**端点**: `GET /api/v1/services/stats`

**功能**: 获取服务注册中心的整体统计信息

**请求示例**:
```http
GET /api/v1/services/stats
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Statistics retrieved",
    "timestamp": 1708123560000,
    "data": {
        "total_services": 5,
        "total_instances": 12,
        "healthy_instances": 10,
        "unhealthy_instances": 1,
        "degraded_instances": 1,
        "instances_by_service": {
            "gomoku_server": 3,
            "snake_server": 2,
            "user_service": 2,
            "auth_service": 2,
            "game_data_service": 3
        },
        "instances_by_game_type": {
            "gomoku": 3,
            "snake": 2
        },
        "instances_by_region": {
            "cn-east": 5,
            "cn-west": 4,
            "us-west": 3
        },
        "avg_health_score": 0.89,
        "avg_cpu_usage": 42.5,
        "avg_memory_usage": 38.2,
        "total_current_players": 156,
        "today_matches": 1024,
        "updated_at": 1708123550000
    }
}
```

**响应字段说明**:

| 字段 | 类型 | 描述 |
|------|------|------|
| `total_services` | integer | 服务种类数量 |
| `total_instances` | integer | 实例总数 |
| `healthy_instances` | integer | 健康实例数 |
| `unhealthy_instances` | integer | 不健康实例数 |
| `degraded_instances` | integer | 降级实例数 |
| `total_current_players` | integer | 当前在线玩家总数 |
| `today_matches` | integer | 今日对局总数（每日零点重置） |
| `avg_health_score` | number | 平均健康分数 |
| `avg_cpu_usage` | number | 平均 CPU 使用率 |
| `avg_memory_usage` | number | 平均内存使用率 |

---

### 2.6 报告对局完成

**端点**: `POST /api/v1/services/matches/report`

**功能**: 游戏服务报告对局完成，用于统计今日对局数

**请求头**:
```http
Content-Type: application/json
```

**请求体**:
```json
{
    "game_type": "gomoku",
    "service_name": "gomoku_server"
}
```

**请求字段说明**:

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| `game_type` | string | 是 | 游戏类型（如 "gomoku"） |
| `service_name` | string | 否 | 报告的服务名称 |

**成功响应** (200):
```json
{
    "success": true,
    "message": "Match reported successfully",
    "timestamp": 1708123580000,
    "data": {
        "game_type": "gomoku",
        "service_name": "gomoku_server",
        "today_matches": 1025
    }
}
```

---

### 2.7 获取推荐实例

**端点**: `GET /api/v1/services/recommend`

**功能**: 根据指定策略获取推荐的服务实例，用于负载均衡

**查询参数**:

| 参数 | 类型 | 必填 | 描述 |
|------|------|------|------|
| `service_type` | string | 否 | 服务类型过滤 (core, game, gateway) |
| `service_name` | string | 否 | 服务名称过滤 |
| `game_type` | string | 否 | 游戏类型过滤 |
| `region` | string | 否 | 区域过滤 |
| `strategy` | string | 否 | 推荐策略，默认 `least_load` |

**推荐策略**:

| 策略 | 描述 |
|------|------|
| `least_load` | 选择负载最低的实例（基于 CPU、内存、玩家数综合评估） |
| `random` | 从健康实例中随机选择 |
| `round_robin` | 轮询选择健康实例 |
| `weighted` | 基于权重加权随机选择 |

**请求示例**:
```http
GET /api/v1/services/recommend?game_type=gomoku&strategy=least_load
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Recommendation successful",
    "timestamp": 1708123570000,
    "data": {
        "recommended": {
            "service_name": "gomoku_server",
            "host": "192.168.1.101",
            "port": 8085,
            "health_score": 0.95,
            "load_score": 0.35,
            "metadata": {
                "current_players": "5",
                "cpu_usage": "25.5",
                "memory_usage": "30.2"
            }
        },
        "reason": "Lowest load score among healthy instances",
        "candidates": [
            {
                "instance_id": "192.168.1.101:8085",
                "load_score": 0.35
            },
            {
                "instance_id": "192.168.1.100:8085",
                "load_score": 0.55
            },
            {
                "instance_id": "192.168.1.102:8085",
                "load_score": 0.72
            }
        ]
    }
}
```

**失败响应** (404):
```json
{
    "success": false,
    "error": {
        "code": "NO_HEALTHY_INSTANCES",
        "message": "No healthy instances available for the specified criteria"
    },
    "timestamp": 1708123570000
}
```

---

## 3. 系统管理 API

### 3.1 健康检查

**端点**: `GET /api/v1/services/health`

**功能**: 检查服务注册中心自身的健康状态

**请求示例**:
```http
GET /api/v1/services/health
```

**成功响应** (200):
```json
{
    "status": "healthy",
    "service": "service_registry",
    "version": "1.0.0",
    "timestamp": 1708123600000,
    "stats": {
        "total_services": 5,
        "total_instances": 12,
        "healthy_instances": 10
    }
}
```

---

### 3.2 手动清理过期服务

**端点**: `POST /api/v1/services/admin/cleanup`

**功能**: 手动触发清理过期服务的操作（管理员接口）

**请求示例**:
```http
POST /api/v1/services/admin/cleanup
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Cleanup completed",
    "timestamp": 1708123610000,
    "data": {
        "cleaned_count": 3
    }
}
```

---

### 3.3 重建索引

**端点**: `POST /api/v1/services/admin/rebuild-index`

**功能**: 重建所有服务索引（管理员接口）

**请求示例**:
```http
POST /api/v1/services/admin/rebuild-index
```

**成功响应** (200):
```json
{
    "success": true,
    "message": "Index rebuilt successfully",
    "timestamp": 1708123620000,
    "data": {
        "rebuilt_count": 12,
        "total_instances": 12
    }
}
```

---

## 4. 数据结构

### 4.1 ServiceInfo 结构

```typescript
interface ServiceInfo {
    // 基础信息
    service_name: string;        // 服务名称
    service_version: string;     // 服务版本
    host: string;                // 主机地址
    port: number;                // 端口号
    service_id?: string;         // 服务唯一ID

    // 健康检查
    health_check_endpoint: string;  // 健康检查端点
    healthy: boolean;               // 健康状态
    health_score: number;           // 健康分数 (0.0-1.0)

    // 负载均衡
    weight: number;                 // 权重 (1-1000)

    // 时间戳
    register_time: number;          // 注册时间 (毫秒时间戳)
    last_heartbeat: number;         // 最后心跳时间 (毫秒时间戳)

    // 统计数据
    total_requests: number;         // 总请求数
    failed_requests: number;        // 失败请求数
    consecutive_failures: number;   // 连续失败次数
    consecutive_successes: number;  // 连续成功次数
    avg_response_time_ms: number;   // 平均响应时间 (毫秒)

    // 扩展数据
    endpoints: string[];            // API 端点列表
    metadata: Record<string, string>;  // 元数据键值对

    // 计算字段
    instance_id: string;            // 实例ID (host:port)
    heartbeat_age_seconds: number;  // 心跳延迟秒数
    uptime_seconds: number;         // 运行时长秒数
}
```

### 4.2 统一响应格式

**成功响应**:
```json
{
    "success": true,
    "message": "Operation successful",
    "timestamp": 1708123456789,
    "data": {
        // 响应数据
    }
}
```

**失败响应**:
```json
{
    "success": false,
    "error": {
        "code": "ERROR_CODE",
        "message": "Error description"
    },
    "timestamp": 1708123456789
}
```

### 4.3 错误码

| 错误码 | HTTP 状态码 | 描述 |
|--------|------------|------|
| `INVALID_REQUEST` | 400 | 请求格式无效 |
| `INVALID_DATA` | 400 | 请求数据无效 |
| `NOT_FOUND` | 404 | 资源不存在 |
| `NO_HEALTHY_INSTANCES` | 404 | 没有健康的实例可用 |
| `REGISTER_FAILED` | 500 | 注册失败 |
| `STATS_FAILED` | 500 | 获取统计信息失败 |
| `CLEANUP_FAILED` | 500 | 清理操作失败 |
| `REBUILD_FAILED` | 500 | 重建索引失败 |

---

## 5. 使用示例

### 5.0 统一客户端库（推荐）

项目提供了统一的 C++ 服务注册客户端库，位于 `src/common/service_registry/`。

**客户端特性**:
- 服务注册与注销
- 自动心跳维护
- 服务发现与查询
- 服务推荐与负载均衡
- 支持 `service_type` 过滤，区分核心服务和游戏服务

**使用示例**:

```cpp
#include "common/service_registry/service_registry_client.h"

// 创建客户端
common::service_registry::ServiceRegistryClientConfig config;
config.registry_host = "127.0.0.1";
config.registry_port = 8081;
config.heartbeat_interval_s = 30;

auto client = std::make_shared<common::service_registry::ServiceRegistryClient>(config);

// 注册游戏服务
common::service_registry::ServiceInstance game_instance;
game_instance.service_name = "gomoku_server";
game_instance.host = "192.168.1.100";
game_instance.port = 8085;
game_instance.metadata["service_type"] = "game";  // 必须设置
game_instance.metadata["game_type"] = "gomoku";
game_instance.metadata["region"] = "cn-east";

client->registerService(game_instance);
client->startHeartbeat(game_instance);  // 启动自动心跳

// 查询所有游戏服务（不会返回核心服务）
auto game_services = client->queryGameServices("gomoku", true);

// 获取推荐的游戏服务器
auto recommendation = client->recommendGameService("gomoku");
if (recommendation.valid) {
    std::cout << "Recommended server: "
              << recommendation.recommended.host << ":"
              << recommendation.recommended.port << std::endl;
}

// 查询核心服务
auto core_services = client->queryCoreServices("user_service", true);
```

**游戏客户端获取游戏服务**:

```cpp
// 游戏客户端只获取游戏服务，不会获取到 user_service, auth_service 等核心服务
auto game_services = client->queryGameServices("", true);  // 获取所有游戏服务

// 或者获取特定游戏的推荐服务器
auto recommendation = client->recommendGameService("gomoku");
if (recommendation.valid) {
    // 连接到推荐的游戏服务器
    connectToServer(recommendation.recommended.host, recommendation.recommended.port);
}
```

---

### 5.1 服务启动时注册

```bash
#!/bin/bash
# 服务启动脚本中的注册逻辑

SERVICE_NAME="gomoku_server"
HOST=$(hostname -I | awk '{print $1}')
PORT=8085

# 注册服务（注意：必须设置 service_type）
curl -X POST http://service-registry:8081/api/v1/services/register \
  -H "Content-Type: application/json" \
  -d "{
    \"service_name\": \"$SERVICE_NAME\",
    \"host\": \"$HOST\",
    \"port\": $PORT,
    \"metadata\": {
      \"service_type\": \"game\",
      \"game_type\": \"gomoku\",
      \"region\": \"cn-east\"
    }
  }"

# 启动心跳定时任务
while true; do
  curl -X POST http://service-registry:8081/api/v1/services/heartbeat \
    -H "Content-Type: application/json" \
    -d "{
      \"service_name\": \"$SERVICE_NAME\",
      \"host\": \"$HOST\",
      \"port\": $PORT,
      \"metadata\": {
        \"current_players\": \"$(get_current_players)\",
        \"cpu_usage\": \"$(get_cpu_usage)\"
      }
    }"
  sleep 15
done &
```

### 5.2 客户端服务发现

**游戏客户端只获取游戏服务**:

游戏客户端可以通过 `service_type=game` 参数，只获取游戏服务，避免获取到核心服务（如 user_service, auth_service）：

```bash
# 只获取游戏服务
curl "http://service-registry:8081/api/v1/services?service_type=game&healthy_only=true"

# 只获取五子棋游戏服务
curl "http://service-registry:8081/api/v1/services?service_type=game&game_type=gomoku&healthy_only=true"

# 获取推荐的游戏服务器
curl "http://service-registry:8081/api/v1/services/recommend?service_type=game&game_type=gomoku&strategy=least_load"
```

**JavaScript 客户端示例**:
```javascript
// 获取推荐的游戏服务器
async function getGameServer(gameType) {
    const response = await fetch(
        `http://service-registry:8081/api/v1/services/recommend?service_type=game&game_type=${gameType}&strategy=least_load`
    );
    const data = await response.json();

    if (data.success) {
        const { host, port } = data.data.recommended;
        return `http://${host}:${port}`;
    }

    throw new Error('No available game server');
}

// 获取所有健康的游戏服务（不会返回核心服务）
async function getAllGameServers() {
    const response = await fetch(
        'http://service-registry:8081/api/v1/services?service_type=game&healthy_only=true'
    );
    const data = await response.json();

    if (data.success) {
        return data.data.services;
    }

    return [];
}

// 使用示例
const gameServer = await getGameServer('gomoku');
console.log(`Connecting to: ${gameServer}`);
```

### 5.3 服务停止时注销

```bash
#!/bin/bash
# 服务停止脚本中的注销逻辑

SERVICE_NAME="gomoku_server"
HOST=$(hostname -I | awk '{print $1}')
PORT=8085

# 注销服务
curl -X DELETE http://service-registry:8081/api/v1/services/deregister \
  -H "Content-Type: application/json" \
  -d "{
    \"service_name\": \"$SERVICE_NAME\",
    \"host\": \"$HOST\",
    \"port\": $PORT
  }"
```

---

## 6. 配置参考

### 6.1 默认配置

```yaml
# config/service_registry.yml
redis:
  host: 127.0.0.1
  port: 6379
  pool_size: 20
  connection_timeout_ms: 5000
  socket_timeout_ms: 3000

http:
  host: 0.0.0.0
  port: 8081
  thread_count: 4
  max_connections: 1000

registry:
  service_ttl_s: 60              # 服务 TTL (秒)
  heartbeat_timeout_s: 90        # 心跳超时 (秒)
  heartbeat_warning_threshold_s: 30
  cleanup_interval_s: 30         # 清理间隔 (秒)
  health_check_interval_s: 15    # 健康检查间隔 (秒)
  stats_update_interval_s: 60    # 统计更新间隔 (秒)
  response_time_threshold_ms: 500
  enable_events: true            # 启用事件发布
  enable_local_cache: true       # 启用本地缓存
  local_cache_size: 1000
  local_cache_ttl_s: 5
```

---

## 7. 最佳实践

### 7.1 心跳策略

- **心跳间隔**: 建议 15-30 秒
- **心跳超时**: 默认 90 秒，超过此时间服务将被标记为不健康
- **元数据更新**: 在心跳中携带动态变化的元数据（如玩家数、CPU 使用率）

### 7.2 服务注册

- **启动顺序**: 先启动服务注册中心，再启动其他服务
- **重试机制**: 注册失败时实现指数退避重试
- **优雅关闭**: 服务停止前调用注销 API

### 7.3 服务发现

- **缓存策略**: 客户端应缓存服务列表，定期刷新
- **故障转移**: 推荐使用 `recommend` API 获取最优实例
- **超时处理**: 设置合理的请求超时时间

---

## 8. CORS 配置

服务注册中心支持跨域请求（CORS），便于前端应用直接调用。

### 8.1 CORS 响应头

| 头部 | 值 |
|------|------|
| `Access-Control-Allow-Origin` | `*` 或请求的 Origin |
| `Access-Control-Allow-Methods` | `GET, POST, PUT, DELETE, OPTIONS, PATCH` |
| `Access-Control-Allow-Headers` | `Content-Type, Authorization, X-Requested-With, Accept, Origin` |
| `Access-Control-Allow-Credentials` | `true` |
| `Access-Control-Max-Age` | `86400` (24小时) |

### 8.2 预检请求处理

OPTIONS 预检请求会返回 200 状态码，包含完整的 CORS 头。

### 8.3 推荐的 API 路径

为避免 Nginx location 匹配的尾部斜杠问题导致的 CORS 错误，推荐使用以下路径：

| 旧路径 | 推荐路径 | 说明 |
|--------|----------|------|
| `GET /api/v1/services` | `GET /api/v1/services/lists` | 避免尾部斜杠重定向 |

**示例**:
```javascript
// 推荐：使用 /lists 路径
const response = await fetch(
    'http://api.example.com/api/v1/services/lists?service_type=game&healthy_only=true'
);

// 兼容：旧路径（可能在某些 Nginx 配置下有 CORS 问题）
const response = await fetch(
    'http://api.example.com/api/v1/services?service_type=game&healthy_only=true'
);
```

---

**文档版本**: 1.1.0
**最后更新**: 2026-02-19
**维护者**: 微服务架构团队
