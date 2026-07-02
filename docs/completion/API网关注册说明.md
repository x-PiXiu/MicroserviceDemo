# 五子棋游戏服务API网关注册说明

## 概述

五子棋游戏服务会在启动时自动注册到API网关，提供完整的服务元数据和端点信息，供客户端发现和访问。

## 注册流程

### 1. 启动时自动注册

五子棋服务器在初始化完成后会自动调用 `registerToApiGateway()` 方法，向配置的API网关注册服务信息。

```cpp
// 在 GameServerBase::initialize() 中调用
if (!registerToApiGateway()) {
    LOG_WARNING("Failed to register to API gateway, continuing anyway");
}
```

### 2. 注册信息结构

服务注册时会提供以下完整信息：

```json
{
  "service_name": "gomoku-service",
  "service_version": "1.0.0",
  "game_type": "gomoku",
  "host": "127.0.0.1",
  "port": 8084,
  "health_check_endpoint": "/health",
  "service_tags": ["game", "gomoku", "chess", "board-game", "multiplayer"],
  
  "endpoints": [
    "/api/gomoku/rooms",
    "/api/gomoku/rooms/{room_id}",
    "/api/gomoku/stats",
    "/api/gomoku/announcement",
    "/health",
    "/api/v1/rooms",
    "/api/v1/rooms/{room_id}",
    "/api/v1/server/status"
  ],
  
  "websocket": {
    "enabled": true,
    "port": 8085,
    "endpoint": "ws://127.0.0.1:8085",
    "protocols": ["gomoku-v1", "game-v1", "qt6-optimized"]
  },
  
  "capacity": {
    "max_concurrent_games": 100,
    "max_players_per_room": 2,
    "max_spectators_per_room": 50,
    "supports_spectators": true
  },
  
  "features": {
    "game_modes": ["freestyle", "renju", "swap2", "pro", "tournament"],
    "room_types": ["casual", "ranked", "tournament"],
    "time_controls": true,
    "ranking_system": true,
    "replay_system": false,
    "chat_support": true,
    "undo_support": true
  },
  
  "runtime_info": {
    "startup_time": 1640995200000,
    "active_rooms": 0,
    "total_players": 0,
    "server_load": 0.0
  }
}
```

## 配置API网关

### 1. JSON配置文件

在 `config/gomoku_server_config.json` 中配置：

```json
{
  "api_gateway_host": "localhost",
  "api_gateway_port": 8080,
  "registration_retry_count": 3,
  "service_name": "gomoku-service",
  "service_tags": [
    "game",
    "gomoku", 
    "chess",
    "board-game",
    "multiplayer"
  ]
}
```

### 2. YAML配置文件

在 `config/gomoku_server_config.yml` 中配置：

```yaml
api_gateway:
  host: "localhost"
  port: 8080
  registration_retry_count: 3
  service_name: "gomoku-service"
  service_tags: ["game", "gomoku", "chess", "board-game", "multiplayer"]
```

## 注册过程详解

### 1. 地址解析

```cpp
// 如果监听0.0.0.0，注册时使用127.0.0.1
std::string register_host = gomoku_config_.host;
if (register_host == "0.0.0.0" || register_host.empty()) {
    register_host = "127.0.0.1";
}
```

### 2. 重试机制

- 默认重试3次
- 递增延迟：第一次立即重试，第二次等待5秒，第三次等待10秒
- 支持配置重试次数

### 3. 请求头信息

```cpp
std::unordered_map<std::string, std::string> headers = {
    {"Content-Type", "application/json"},
    {"User-Agent", "GomokuGameService/1.0.0"},
    {"X-Service-Type", "game-service"},
    {"X-Game-Type", "gomoku"}
};
```

## API端点说明

### HTTP REST API

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/gomoku/rooms` | POST | 创建游戏房间 |
| `/api/gomoku/rooms` | GET | 获取房间列表 |
| `/api/gomoku/rooms/{room_id}` | GET | 获取房间详情 |
| `/api/gomoku/stats` | GET | 获取游戏统计 |
| `/api/gomoku/announcement` | POST | 广播系统公告 |
| `/health` | GET | 健康检查 |
| `/api/v1/server/status` | GET | 服务器状态 |

### WebSocket API

- **连接端点**: `ws://host:8085`
- **支持协议**: 
  - `gomoku-v1`: 五子棋专用协议
  - `game-v1`: 通用游戏协议  
  - `qt6-optimized`: Qt6客户端优化协议

## 服务发现

### 1. 客户端发现服务

客户端可以通过API网关查询可用的五子棋服务：

```bash
# 查询所有游戏服务
curl http://api-gateway:8080/api/v1/services?type=game

# 查询五子棋服务
curl http://api-gateway:8080/api/v1/services?game_type=gomoku
```

### 2. 负载均衡

API网关可以根据服务的 `server_load` 指标进行负载均衡：

```json
{
  "runtime_info": {
    "server_load": 0.75,  // 75%负载：75/100个房间在使用
    "active_rooms": 75,
    "total_players": 150
  }
}
```

## 健康检查

### 1. 健康检查端点

五子棋服务提供 `/health` 端点供API网关进行健康检查：

```json
{
  "status": "UP",
  "timestamp": "2024-01-01T12:00:00Z",
  "checks": {
    "database": "UP",
    "redis": "UP", 
    "websocket": "UP",
    "rooms": "UP"
  },
  "details": {
    "active_rooms": 10,
    "total_players": 20,
    "uptime": "02:30:45"
  }
}
```

### 2. 自动重新注册

如果健康检查失败，服务器会尝试重新注册到API网关。

## 性能监控

### 1. 实时指标

服务注册信息中包含实时性能指标：

```json
{
  "runtime_info": {
    "active_rooms": 45,
    "total_players": 90,
    "server_load": 0.45,
    "memory_usage_mb": 256.5,
    "cpu_usage_percent": 15.2
  }
}
```

### 2. 容量信息

```json
{
  "capacity": {
    "max_concurrent_games": 100,
    "max_players_per_room": 2,
    "max_spectators_per_room": 50,
    "current_utilization": 0.45
  }
}
```

## 故障处理

### 1. 注册失败

如果注册失败，服务器会：
- 记录详细错误日志
- 继续正常运行（不影响游戏服务）
- 定期尝试重新注册

### 2. 网关不可用

- 服务器可以独立运行
- 客户端可以直接连接到服务器
- 定期检查网关可用性并重新注册

### 3. 配置错误

```bash
# 检查配置文件格式
./gomoku_server --validate-config config.json

# 测试API网关连接
curl -X POST http://localhost:8080/api/v1/services/register \
  -H "Content-Type: application/json" \
  -d '{"service_name": "test"}'
```

## 部署建议

### 1. 生产环境配置

```json
{
  "api_gateway_host": "internal-api-gateway.example.com",
  "api_gateway_port": 8080,
  "registration_retry_count": 5,
  "service_name": "gomoku-service-prod",
  "service_tags": [
    "game", "gomoku", "production", 
    "board-game", "multiplayer"
  ]
}
```

### 2. 容器化部署

```yaml
# docker-compose.yml
version: '3.8'
services:
  gomoku-service:
    image: gomoku-game:latest
    environment:
      - API_GATEWAY_HOST=api-gateway
      - API_GATEWAY_PORT=8080
    depends_on:
      - api-gateway
      - mysql
      - redis
```

### 3. Kubernetes部署

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: gomoku-config
data:
  config.json: |
    {
      "api_gateway_host": "api-gateway-service",
      "api_gateway_port": 8080,
      "service_name": "gomoku-service"
    }
```

## 测试验证

### 1. 本地测试

```bash
# 启动API网关（假设在8080端口）
# 启动五子棋服务
./gomoku_server --config config/gomoku_server_config.json

# 验证注册是否成功
curl http://localhost:8080/api/v1/services | grep gomoku
```

### 2. 集成测试

```bash
# 测试服务发现
curl http://localhost:8080/api/v1/services?game_type=gomoku

# 测试健康检查
curl http://localhost:8084/health

# 测试WebSocket连接
wscat -c ws://localhost:8085
```

通过以上配置和说明，五子棋游戏服务可以完美集成到API网关系统中，提供完整的服务发现、负载均衡和健康监控功能。


