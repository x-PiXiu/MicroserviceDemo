# 🎯 微服务API文档 - 认证服务、API网关、游戏基类

## **📋 概述**

本文档详细描述了三个核心微服务的HTTP API端点，基于实际代码实现分析，确保信息的准确性。

---

## **🔐 1. 认证服务 (AuthService)**

### **基础信息**
- **服务名称**: `auth_service`
- **默认端口**: `8008`
- **基础路径**: `/api/v1/auth/`
- **Content-Type**: `application/json`

---

### **1.1 用户注册**

**端点**: `POST /api/v1/auth/register`

**功能**: 注册新用户账户

**请求头**:
```http
Content-Type: application/json
User-Agent: [客户端标识]
```

**请求JSON格式**:
```json
{
  "username": "string",     // 必填：用户名
  "email": "string",        // 必填：邮箱
  "password": "string",     // 必填：密码
  "nickname": "string",     // 可选：昵称
  "device_id": "string",    // 可选：设备ID
  "device_type": "string",  // 可选：设备类型 (web/mobile/desktop)
  "metadata": {}            // 可选：附加元数据
}
```

**成功响应** (200):
```json
{
    "data": {
        "access_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IiIsImVtYWlsIjoiY2FkYXZzdkBnYXMtc3RhdGlvbi5vbmxpbmUiLCJleHAiOjE3NTc5MjM3MzEsImlhdCI6MTc1NzkyMDEzMSwiaXNzIjoiZ2FtZS1taWNyb3NlcnZpY2VzLWF1dGgiLCJqdGkiOiI1OTlkYjYxOC03NWYyLTQzNTktYTU1YS01ZTdjMTUzOWQ0NmYiLCJuYmYiOjE3NTc5MjAxMzEsIm5pY2tuYW1lIjoiVGVzdCBVc2VyIiwicm9sZXMiOiJbXCJ1c2VyXCJdIiwic2Vzc2lvbl9pZCI6ImVkNGRlZGFhLTVhMTItNGRmOC04ZTgwLTVjMjYwYTUzOGMxOSIsInN1YiI6IjMyIiwidG9rZW5fdHlwZSI6ImFjY2Vzc190b2tlbiIsInVzZXJuYW1lIjoidGVzdDM1In0.cWb27TqoVEG2-mdFDxYcuakveINSIzLJeEaN6CThYDI",
        "error_code": "",
        "message": "认证成功",
        "metadata": {},
        "refresh_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IiIsImVtYWlsIjoiY2FkYXZzdkBnYXMtc3RhdGlvbi5vbmxpbmUiLCJleHAiOjE3NTg1MjQ5MzEsImlhdCI6MTc1NzkyMDEzMSwiaXNzIjoiZ2FtZS1taWNyb3NlcnZpY2VzLWF1dGgiLCJqdGkiOiI3ZDJhZjI4OS1jYWNiLTQ2ZGQtYmYxYS1lMzY0YzE0M2FhNmMiLCJuYmYiOjE3NTc5MjAxMzEsIm5pY2tuYW1lIjoiVGVzdCBVc2VyIiwicm9sZXMiOiJbXCJ1c2VyXCJdIiwic2Vzc2lvbl9pZCI6ImVkNGRlZGFhLTVhMTItNGRmOC04ZTgwLTVjMjYwYTUzOGMxOSIsInN1YiI6IjMyIiwidG9rZW5fdHlwZSI6InJlZnJlc2hfdG9rZW4iLCJ1c2VybmFtZSI6InRlc3QzNSJ9.-dLuzI4Il3HnayMS_wMJOtUEbnNzUf6kI-YrWOGYbAc",
        "session": {
            "client_ip": "unknown",
            "created_at": 1757920131437,
            "device_id": "4fae849c-265d-4700-a73e-b093523541ec",
            "device_type": "desktop",
            "expires_at": 1757923731437,
            "is_active": true,
            "last_activity_at": 1757920131437,
            "metadata": {},
            "session_id": "ed4dedaa-5a12-4df8-8e80-5c260a538c19",
            "user_agent": "HttpClient/1.0",
            "user_id": 32
        },
        "success": true,
        "token_expires_at": 1757923731441,
        "user_info": {
            "avatar_url": "",
            "created_at": 1757920131392,
            "email": "cadavsv@gas-station.online",
            "last_login_at": 0,
            "last_login_ip": "",
            "locked_until": 0,
            "login_attempts": 0,
            "metadata": {},
            "nickname": "Test User",
            "online_status": 0,
            "roles": [
                "user"
            ],
            "status": 1,
            "updated_at": 1757920131392,
            "user_id": 32,
            "username": "test35"
        }
    },
    "service": "auth_service",
    "success": true,
    "timestamp": 1757920131441
}
```

**失败响应** (400):
```json
{
  "success": false,
  "message": "用户名已存在",
  "error_code": "USERNAME_EXISTS"
}
```

---

### **1.2 用户登录**

**端点**: `POST /api/v1/auth/login`

**功能**: 用户登录验证

**请求头**:
```http
Content-Type: application/json
User-Agent: [客户端标识]
```

**请求JSON格式**:
```json
{
  "username": "string",     // 必填：用户名或邮箱
  "password": "string",     // 必填：密码
  "device_id": "string",    // 可选：设备ID
  "device_type": "string",  // 可选：设备类型
  "metadata": {}            // 可选：附加元数据
}
```

**成功响应** (200):
```json
{
    "data": {
        "access_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiY2FkYXZzdkBnYXMtc3RhdGlvbi5vbmxpbmUiLCJleHAiOjE3NTc5MjM4OTEsImlhdCI6MTc1NzkyMDI5MSwiaXNzIjoiZ2FtZS1taWNyb3NlcnZpY2VzLWF1dGgiLCJqdGkiOiJiM2M2MjM1MS1kY2ZmLTQ3NzYtYTc2Mi04ZTI2NWZmOWJkY2EiLCJuYmYiOjE3NTc5MjAyOTEsIm5pY2tuYW1lIjoiVGVzdCBVc2VyIiwic2Vzc2lvbl9pZCI6IjRiOWFiY2E0LWY2NjAtNGFkOC1iZmFkLWQxMDY5OGJhYTY4MyIsInN1YiI6IjMyIiwidG9rZW5fdHlwZSI6ImFjY2Vzc190b2tlbiIsInVzZXJuYW1lIjoidGVzdDM1In0.1b_pxivYl_f22wQ3d2n2aFtOYhIwDpu5f-pk76Ffm6Y",
        "error_code": "",
        "message": "认证成功",
        "metadata": {},
        "refresh_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiY2FkYXZzdkBnYXMtc3RhdGlvbi5vbmxpbmUiLCJleHAiOjE3NTg1MjUwOTEsImlhdCI6MTc1NzkyMDI5MSwiaXNzIjoiZ2FtZS1taWNyb3NlcnZpY2VzLWF1dGgiLCJqdGkiOiJmZGJmZTQ3OC0yNWNhLTQ1NDUtOGI3My05OWQ2MDQ4MTAwZGMiLCJuYmYiOjE3NTc5MjAyOTEsIm5pY2tuYW1lIjoiVGVzdCBVc2VyIiwic2Vzc2lvbl9pZCI6IjRiOWFiY2E0LWY2NjAtNGFkOC1iZmFkLWQxMDY5OGJhYTY4MyIsInN1YiI6IjMyIiwidG9rZW5fdHlwZSI6InJlZnJlc2hfdG9rZW4iLCJ1c2VybmFtZSI6InRlc3QzNSJ9.aLXQ_lvvQjXg4dtIyvazNWH3A3NKOQVMkv44RVsJwqY",
        "session": {
            "client_ip": "unknown",
            "created_at": 1757920291865,
            "device_id": "web_browser_001",
            "device_type": "desktop",
            "expires_at": 1757923891865,
            "is_active": true,
            "last_activity_at": 1757920291865,
            "metadata": {},
            "session_id": "4b9abca4-f660-4ad8-bfad-d10698baa683",
            "user_agent": "HttpClient/1.0",
            "user_id": 32
        },
        "success": true,
        "token_expires_at": 1757923891866,
        "user_info": {
            "avatar_url": "",
            "created_at": 2025,
            "email": "cadavsv@gas-station.online",
            "last_login_at": 1757920291839,
            "last_login_ip": "unknown",
            "locked_until": 0,
            "login_attempts": 0,
            "metadata": {},
            "nickname": "Test User",
            "online_status": 1,
            "roles": [],
            "status": 1,
            "updated_at": 2025,
            "user_id": 32,
            "username": "test35"
        }
    },
    "service": "auth_service",
    "success": true,
    "timestamp": 1757920291866
}
```

**失败响应** (401):
```json
{
  "success": false,
  "message": "用户名或密码错误",
  "error_code": "INVALID_CREDENTIALS"
}
```

---

### **1.3 用户登出**

**端点**: `POST /api/v1/auth/logout`

**功能**: 注销用户会话

**请求头**:
```http
Authorization: Bearer [access_token]
```

**请求体**: 空

**成功响应** (200):
```json
{
  "success": true,
  "message": "登出成功",
  "data": {
    "message": "登出成功"
  }
}
```

**失败响应** (400):
```json
{
  "success": false,
  "message": "缺少访问令牌",
  "error_code": "MISSING_TOKEN"
}
```

---

### **1.4 刷新令牌**

**端点**: `POST /api/v1/auth/refresh`

**功能**: 使用刷新令牌获取新的访问令牌

**请求头**:
```http
Content-Type: application/json
Authorization: Bearer eyJhbGciOiJIUzI1NiJ9...
```

**请求JSON格式**:
```json
{
  "refresh_token": "refresh_eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9..."
}
```

**成功响应** (200):
```json
{
    "data": {
        "access_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6ImRlc2t0b3AiLCJlbWFpbCI6ImNhZGF2c3ZAZ2FzLXN0YXRpb24ub25saW5lIiwiZXhwIjoxNzU3OTI0MzQzLCJpYXQiOjE3NTc5MjA3NDMsImlzcyI6ImdhbWUtbWljcm9zZXJ2aWNlcy1hdXRoIiwianRpIjoiMzEwOTBjMmQtYzQ2OS00NmRiLWE0MzMtOGZiYWM2NjJkOTA2IiwibmJmIjoxNzU3OTIwNzQzLCJuaWNrbmFtZSI6IlRlc3QgVXNlciIsInNlc3Npb25faWQiOiI0YjlhYmNhNC1mNjYwLTRhZDgtYmZhZC1kMTA2OThiYWE2ODMiLCJzdWIiOiIzMiIsInRva2VuX3R5cGUiOiJhY2Nlc3NfdG9rZW4iLCJ1c2VybmFtZSI6InRlc3QzNSJ9.6Iim_Ld3pE52qM_3G1CdpoGmKm1_CdL3DkOrCFrMfhE",
        "error_code": "",
        "message": "认证成功",
        "metadata": {},
        "refresh_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6ImRlc2t0b3AiLCJlbWFpbCI6ImNhZGF2c3ZAZ2FzLXN0YXRpb24ub25saW5lIiwiZXhwIjoxNzU4NTI1NTQzLCJpYXQiOjE3NTc5MjA3NDMsImlzcyI6ImdhbWUtbWljcm9zZXJ2aWNlcy1hdXRoIiwianRpIjoiZDk0OWNkMTgtYjJiMy00NTdlLTkzYWMtNjE1NjI4NDYwZGFmIiwibmJmIjoxNzU3OTIwNzQzLCJuaWNrbmFtZSI6IlRlc3QgVXNlciIsInNlc3Npb25faWQiOiI0YjlhYmNhNC1mNjYwLTRhZDgtYmZhZC1kMTA2OThiYWE2ODMiLCJzdWIiOiIzMiIsInRva2VuX3R5cGUiOiJyZWZyZXNoX3Rva2VuIiwidXNlcm5hbWUiOiJ0ZXN0MzUifQ.Km6ww3yOkvDuvNHZXLelNZIOVp5Tt5kwHqDflYfd34Y",
        "session": {
            "client_ip": "unknown",
            "created_at": 1757920291865,
            "device_id": "web_browser_001",
            "device_type": "desktop",
            "expires_at": 1757923891865,
            "is_active": true,
            "last_activity_at": 1757920291865,
            "metadata": {},
            "session_id": "4b9abca4-f660-4ad8-bfad-d10698baa683",
            "user_agent": "HttpClient/1.0",
            "user_id": 32
        },
        "success": true,
        "token_expires_at": 1757924343702,
        "user_info": {
            "avatar_url": "",
            "created_at": 2025,
            "email": "cadavsv@gas-station.online",
            "last_login_at": 2025,
            "last_login_ip": "unknown",
            "locked_until": 0,
            "login_attempts": 0,
            "metadata": {},
            "nickname": "Test User",
            "online_status": 1,
            "roles": [],
            "status": 1,
            "updated_at": 2025,
            "user_id": 32,
            "username": "test35"
        }
    },
    "service": "auth_service",
    "success": true,
    "timestamp": 1757920743702
}
```

---

### **1.5 验证令牌**

**端点**: `GET /api/v1/auth/validate`

**功能**: 验证访问令牌的有效性

**请求头**:
```http
Authorization: Bearer [access_token]
```

**成功响应** (200):
```json
{
    "data": {
        "blacklisted": false,
        "claims": {
            "audience": "game-microservices-clients",
            "client_ip": "unknown",
            "custom_claims": {},
            "device_type": "desktop",
            "email": "cadavsv@gas-station.online",
            "expires_at": 1757924343000,
            "issued_at": 1757920743000,
            "issuer": "game-microservices-auth",
            "jti": "31090c2d-c469-46db-a433-8fbac662d906",
            "nickname": "Test User",
            "not_before": 1757920743000,
            "roles": [],
            "session_id": "4b9abca4-f660-4ad8-bfad-d10698baa683",
            "user_id": 32,
            "username": "test35"
        },
        "error_code": "",
        "error_message": "",
        "expired": false,
        "format_valid": true,
        "signature_valid": true,
        "valid": true
    },
    "service": "auth_service",
    "success": true,
    "timestamp": 1757921921422
}
```

**失败响应** (400):
```json
{
    "error": {
        "code": "MISSING_TOKEN",
        "message": "缺少访问令牌"
    },
    "service": "auth_service",
    "success": false,
    "timestamp": 1757920979094
}
```

---

### **1.6 游戏登录**

**端点**: `POST /api/v1/game/login`

**功能**: 游戏专用登录，获取游戏会话和服务器信息

**请求头**:
```http
Authorization: Bearer [access_token]
Content-Type: application/json (可选，如果有请求体)
```

**请求JSON格式** (可选):
```json
{
  "game_type": "SNAKE",           // 可选：游戏类型 (SNAKE/TETRIS等)
  "preferred_server_region": "default", // 可选：偏好服务器区域
  "metadata": {}                  // 可选：附加元数据
}
```

**成功响应** (200):
```json
{
    "game_config": {
        "default_game_speed": 100,
        "max_players_per_room": 4,
        "supported_game_modes": [
            "classic",
            "battle",
            "survival"
        ]
    },
    "message": "Game authentication successful",
    "player_info": {
        "nickname": "Test User",
        "player_id": "player_1757924252798_32",
        "user_id": "32"
    },
    "server_info": {
        "game_type": "snake",
        "host": "127.0.0.1",
        "name": "Snake Game Server",
        "port": 8084,
        "region": "default",
        "server_id": "snake_service",
        "version": "1.2.0",
        "websocket_port": 8085
    },
    "session_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6IiIsImRldmljZV90eXBlIjoiZ2FtZSIsImVtYWlsIjoiY2FkYXZzdkBnYXMtc3RhdGlvbi5vbmxpbmUiLCJleHAiOjE3NTc5MzE0NTIsImdhbWVfdHlwZSI6InNuYWtlIiwiaWF0IjoxNzU3OTI0MjUyLCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6ImFjMzY1NDRhLWUzNTktNDY3My1iZmI5LWE2MjAxZGJhMzBkMCIsIm5iZiI6MTc1NzkyNDI1Miwibmlja25hbWUiOiJUZXN0IFVzZXIiLCJzZXJ2ZXJfaWQiOiJzbmFrZV9zZXJ2aWNlIiwic2Vzc2lvbl9pZCI6IjgwMjk0MWJmLWViZjgtNDI3OS1iYzU0LTU1NTQyODRiMTAyMiIsInN1YiI6IjMyIiwidG9rZW5fdHlwZSI6ImdhbWVfc2Vzc2lvbiIsInVzZXJuYW1lIjoidGVzdDM1In0.QbGCeyK1Kjap8znmbeWKoTrtky6OwhaFkBlOVl0JxW8",
    "success": true
}
```


**失败响应** (400):
```json
{
    "error": {
        "code": "NO_AVAILABLE_SERVER",
        "message": "没有可用的游戏服务器"
    },
    "service": "auth_service",
    "success": false,
    "timestamp": 1757923930563
}
```


---

### **1.7 获取游戏服务器列表**

**端点**: `GET /api/v1/game/servers`

**功能**: 获取可用的游戏服务器列表

**查询参数**:
```
game_type=SNAKE    // 必填：游戏类型
region=default     // 可选：区域筛选
```

**请求示例**:
```http
GET /api/v1/game/servers?game_type=SNAKE&region=default
```

**成功响应** (200):
```json
{
    "data": {
        "servers": []
    },
    "service": "auth_service",
    "success": true,
    "timestamp": 1757927791658
}
```

---

### **1.8 健康检查**

**端点**: `GET /health`

**功能**: 服务健康状态检查

**成功响应** (200):
```json
{
    "checks": {
        "components": {
            "jwt_manager": "initialized",
            "password_manager": "initialized",
            "session_manager": "initialized",
            "user_repository": "initialized"
        },
        "kafka": {
            "consumer_status": "initialized",
            "producer_status": "initialized"
        },
        "mysql": {
            "active_connections": 0,
            "pool_size": 20,
            "status": "healthy"
        },
        "redis": {
            "active_connections": 0,
            "pool_size": 5,
            "status": "healthy"
        }
    },
    "service": "auth_service",
    "status": "healthy",
    "timestamp": 1757924397395,
    "uptime_seconds": 1757924397,
    "version": "1.0.0"
}
```

---

### **1.9 服务状态**

**端点**: `GET /api/v1/admin/status`

**功能**: 获取详细的服务状态信息

**成功响应** (200):
```json
{
    "components": {
        "http_server": "initialized",
        "jwt_manager": "initialized",
        "kafka_consumer": "initialized",
        "kafka_producer": "initialized",
        "mysql_pool": "initialized",
        "password_manager": "initialized",
        "redis_pool": "initialized",
        "session_manager": "initialized",
        "user_repository": "initialized"
    },
    "config": {
        "enable_https": false,
        "enable_kafka": true,
        "enable_service_discovery": true,
        "host": "0.0.0.0",
        "port": 8008,
        "worker_threads": 8
    },
    "initialized": true,
    "service": "auth_service",
    "status": "running",
    "timestamp": 1757926414318,
    "version": "1.0.0"
}
```

---

## **🌐 2. API网关 (ApiGateway)**

### **基础信息**
- **服务名称**: `api_gateway`
- **默认端口**: `8080`
- **功能**: 请求路由转发、服务发现、负载均衡

### **2.1 核心功能**

API网关主要作为代理服务，将客户端请求转发到对应的后端服务。支持的路径模式：

- `/api/*` - 通用API路径
- `/api/v1/*` - V1版本API
- `/api/v2/*` - V2版本API  
- `/v1/*` - 直接版本路径
- `/v2/*` - 直接版本路径

**支持的HTTP方法**: GET, POST, PUT, DELETE, PATCH, OPTIONS

---

### **2.2 服务注册**

**端点**: `POST /api/v1/services/register`

**功能**: 后端服务向API网关注册自己

**请求头**:
```http
Content-Type: application/json
```

**请求JSON格式**:
```json
{
  "service_name": "test_service1",
  "service_version": "1.0.0",
  "host": "127.0.0.1",
  "port": 8081,
  "health_check_endpoint": "/health",
  "weight": 100,
  "endpoints": [
    "/api/v1/auth/register",
    "/api/v1/auth/login",
    "/api/v1/auth/logout"
  ],
  "metadata": {
    "description": "认证服务",
    "tags": ["auth", "user"]
  }
}
```

**成功响应** (200):
```json
{
    "data": {
        "host": "127.0.0.1",
        "port": 8081,
        "service_name": "test_service1"
    },
    "message": "服务注册成功",
    "success": true,
    "timestamp": 1757925810
}
```

---

### **2.3 服务注销**

**端点**: `DELETE /api/v1/services/deregister`

**功能**: 注销服务

**请求JSON格式**:
```json
{
  "service_name": "auth_service",
  "host": "127.0.0.1",
  "port": 8008
}
```

**成功响应** (200):
```json
{
    "data": {
        "host": "127.0.0.1",
        "port": 8008,
        "service_name": "auth_service"
    },
    "message": "服务注销成功",
    "success": true,
    "timestamp": 1757925657
}
```

---

### **2.4 服务心跳**

**端点**: `POST /api/v1/services/heartbeat`

**功能**: 服务心跳保持活跃状态

**请求JSON格式**:
```json
{
  "service_name": "auth_service",
  "host": "127.0.0.1",
  "port": 8008,
  "status": "healthy",
  "metadata": {
    "load": 0.65,
    "connections": 234
  }
}
```

**成功响应** (200):
```json
{
    "data": {
        "metadata_entries": 2,
        "service_name": "auth_service"
    },
    "message": "心跳和metadata更新成功",
    "success": true,
    "timestamp": 1757925519
}
```

---

### **2.5 服务列表查询**

**端点**: `GET /api/v1/services`

**功能**: 获取所有注册的服务列表

**查询参数**:
```
service_type=auth    // 可选：按服务类型筛选
status=healthy       // 可选：按状态筛选
```

**成功响应** (200):
```json
{
    "data": {
        "count": 2,
        "filters": {
            "game_type": "",
            "healthy_only": false,
            "service_name": ""
        },
        "services": [
            {
                "endpoints": [
                    "/api/v1/snake/rooms/public",
                    "/api/v1/snake/leaderboard",
                    "/api/v1/snake/stats",
                    "/api/v1/snake/config"
                ],
                "health_check_endpoint": "/health",
                "healthy": true,
                "host": "127.0.0.1",
                "last_heartbeat": 1757925076,
                "metadata": {
                    "active_rooms": "0.000000",
                    "cpu_usage": "0.000000",
                    "current_players": "0.000000",
                    "load_percentage": "13.730350",
                    "memory_usage": "68.651748",
                    "ping_average": "0.000000",
                    "uptime_seconds": "420.000000"
                },
                "port": 8084,
                "service_name": "snake_game_service",
                "service_version": "1.0.0",
                "weight": 100
            },
            {
                "endpoints": [
                    "/api/v1/auth/register",
                    "/api/v1/auth/login",
                    "/api/v1/auth/logout",
                    "/api/v1/auth/refresh",
                    "/api/v1/auth/validate",
                    "/api/v1/game/login",
                    "/api/v1/game/servers"
                ],
                "health_check_endpoint": "/health",
                "healthy": true,
                "host": "127.0.0.1",
                "last_heartbeat": 1757925079,
                "metadata": {
                    "active_sessions": "0.000000",
                    "display_name": "Auth Service",
                    "failed_requests": "0.000000",
                    "maintenance_mode": "false",
                    "protocol_version": "2.0",
                    "region": "default",
                    "server_status": "online",
                    "service_type": "authentication",
                    "successful_requests": "0.000000",
                    "total_requests": "0.000000",
                    "total_users": "30.000000",
                    "uptime_seconds": "1.000000",
                    "version": "1.0.0"
                },
                "port": 8008,
                "service_name": "auth_service",
                "service_version": "1.0.0",
                "weight": 100
            }
        ]
    },
    "message": "查询成功",
    "success": true,
    "timestamp": 1757925093
}
```

---

### **2.6 服务统计**

**端点**: `GET /api/v1/services/stats`

**功能**: 获取服务统计信息

**成功响应** (200):
```json
{
    "data": {
        "auth_service_healthy": 1,
        "auth_service_total": 1,
        "snake_game_service_healthy": 1,
        "snake_game_service_total": 1,
        "snake_service_healthy": 1,
        "snake_service_total": 1
    },
    "message": "统计信息获取成功",
    "success": true,
    "timestamp": 1757924699
}
```

---

### **2.7 健康检查**

**端点**: `GET /health`

**成功响应** (200):
```json
{
    "async_response": true,
    "components": {
        "circuit_breaker": "UP",
        "http_server": "UP",
        "load_balancer": "UP",
        "rate_limiter": "UP",
        "route_manager": "UP"
    },
    "message": "OK",
    "response_method": "async",
    "status": "UP",
    "timestamp": 1757924420,
    "version": "1.0.0"
}
```

---

### **2.8 指标接口**

**端点**: `GET /metrics`

**功能**: 获取Prometheus格式的监控指标

**成功响应** (200):
```
# TYPE http_requests_total counter
http_requests_total{method="GET",status="200"} 12345
http_requests_total{method="POST",status="200"} 8901
# TYPE response_time_seconds histogram
response_time_seconds_bucket{le="0.1"} 8234
response_time_seconds_bucket{le="0.5"} 11567
```

---

## **🎮 3. 游戏基类 (GameServerBase)**

### **基础信息**
- **默认端口**: `8084` (HTTP), `8081` (WebSocket)
- **基础路径**: `/api/v1/`
- **特点**: 提供通用的游戏服务器功能，具体游戏逻辑由子类实现

---

### **3.1 健康检查**

**端点**: `GET /health`

**功能**: 游戏服务器健康状态检查

**成功响应** (200):
```json
{
  "status": "healthy",
  "uptime_seconds": 3600,
  "game_type": "SNAKE",
  "stats": {
    "total_rooms": 12,
    "active_rooms": 8,
    "total_players": 34,
    "active_players": 28,
    "websocket_connections": 28
  }
}
```

---

### **3.2 服务器状态**

**端点**: `GET /api/v1/status`

**功能**: 获取详细的游戏服务器状态

**成功响应** (200):
```json
{
  "server_name": "snake_server",
  "version": "1.0.0",
  "uptime_seconds": 3600,
  "running": true,
  "config": {
    "host": "0.0.0.0",
    "port": 8084,
    "websocket_port": 8081,
    "max_players": 1000,
    "enable_leaderboard": true
  },
  "stats": {
    "total_rooms": 12,
    "active_rooms": 8,
    "total_players": 34,
    "active_players": 28,
    "rooms_created_today": 156,
    "peak_concurrent_players": 89
  },
  "features": {
    "qt6_support": false,
    "websocket": true,
    "leaderboard": true,
    "performance_monitoring": true,
    "dynamic_config": true,
    "security_manager": true,
    "alert_manager": true
  }
}
```

---

### **3.3 增强状态信息** (企业级组件)

**端点**: `GET /api/v1/enhanced_status` (如果实现了企业级组件)

**功能**: 获取包含企业级组件的完整服务器状态

**成功响应** (200):
```json
{
  "server_name": "snake_server",
  "version": "1.0.0",
  "uptime_seconds": 3600,
  "running": true,
  "features": {
    "performance_monitoring": true,
    "dynamic_config": true,
    "security_manager": true,
    "alert_manager": true
  },
  "dynamic_config": {
    "total_configs": 25,
    "current_version": 3,
    "hot_reloadable_configs": 18,
    "restart_required_count": 0
  },
  "security": {
    "total_connections": 150,
    "rejected_connections": 5,
    "connection_rejection_rate": 0.032,
    "blacklisted_ips": 2,
    "security_events": 12
  },
  "alerts": {
    "active_alerts": 1,
    "total_rules": 8,
    "enabled_rules": 7
  },
  "performance_metrics": {
    "counters": {
      "http_requests_total": 1234,
      "websocket_connections_total": 567
    },
    "gauges": {
      "websocket_connections_current": 28,
      "system_cpu_usage_percent": 65.2
    }
  }
}
```

---

### **3.4 游戏特定路由**

游戏特定的API路由由具体的游戏服务器子类实现。常见模式：

**房间管理**:
- `GET /api/v1/{game_type}/rooms` - 获取房间列表
- `POST /api/v1/{game_type}/rooms` - 创建房间  
- `GET /api/v1/{game_type}/rooms/{room_id}` - 获取房间详情

**排行榜**:
- `GET /api/v1/{game_type}/leaderboard` - 获取排行榜

**统计信息**:  
- `GET /api/v1/{game_type}/stats` - 获取游戏统计

**注**: 具体的游戏路由实现需要查看对应的游戏服务器子类代码。

---

### **3.5 WebSocket连接**

**端点**: `ws://host:websocket_port/`

**功能**: 实时游戏通信

**连接协议**:
- `snake-game-v1` - 贪吃蛇游戏协议
- `qt6-optimized` - Qt6优化协议

**消息格式** (JSON):
```json
{
  "type": "join_room",
  "room_name": "room_001",  
  "player_name": "Player1",
  "game_session_token": "game_sess_uuid_1234567890"
}
```

**响应格式**:
```json
{
  "type": "join_room_response",
  "success": true,
  "room_id": "room_001",
  "player_id": "player_uuid_123",
  "room_state": {
    "players": 2,
    "status": "waiting"
  }
}
```

---

## **📝 错误响应格式**

所有服务的错误响应都遵循统一格式：

```json
{
  "success": false,
  "message": "错误描述信息",
  "error_code": "ERROR_CODE",
  "timestamp": 1726406400000
}
```

**常见错误码**:
- `INVALID_JSON` - JSON格式无效
- `MISSING_FIELDS` - 缺少必需字段
- `UNAUTHORIZED` - 未授权访问
- `TOKEN_INVALID` - 令牌无效
- `SERVICE_UNAVAILABLE` - 服务不可用
- `INTERNAL_ERROR` - 内部服务器错误

---

## **🔍 使用说明**

1. **认证流程**: 先调用 `/api/v1/auth/login` 获取访问令牌
2. **游戏登录**: 使用访问令牌调用 `/api/v1/game/login` 获取游戏会话
3. **WebSocket连接**: 使用游戏会话令牌建立WebSocket连接
4. **API调用**: 所有需要认证的API都需要在请求头中包含 `Authorization: Bearer [token]`

---

**📌 注意**: 本文档基于实际代码实现分析生成，所有API格式和字段都来源于真实的服务器实现，确保准确性和可用性。
