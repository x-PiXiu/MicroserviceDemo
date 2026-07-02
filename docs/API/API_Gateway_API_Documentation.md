# 🌐 API网关服务API文档

## **📋 概述**

本文档详细描述API网关服务(API Gateway)的HTTP REST API，基于实际代码实现，确保信息的准确性和完整性。API网关作为微服务架构的核心组件，负责动态路由管理、服务发现、负载均衡、认证授权、限流熔断等功能，为整个微服务集群提供统一的入口。

---

## **🏢 API网关服务 (ApiGateway)**

### **基础信息**
- **服务名称**: `api_gateway`
- **默认端口**: `8080` (HTTP)
- **服务版本**: `1.0.0`
- **基础路径**: `/api/v1/`
- **数据库**: 无专用数据库 (使用内存缓存和Redis)
- **架构特点**: 微服务网关、动态路由、服务发现、负载均衡、中间件系统

---

## **🛠️ 1. 网关管理API**

### **1.1 健康检查**

**端点**: `GET /health`

**功能**: 检查API网关及其组件的健康状态

**成功响应** (200):

```json
{
  "status": "UP",
  "message": "OK",
  "timestamp": 1758707400,
  "version": "1.0.0",
  "components": {
    "http_server": "UP",
    "route_manager": "UP",
    "load_balancer": "UP",
    "rate_limiter": "UP",
    "circuit_breakers": "UP",
    "service_discovery": "UP"
  },
  "statistics": {
    "total_requests": 15234,
    "successful_requests": 14892,
    "failed_requests": 342,
    "average_response_time": 145.5,
    "success_rate": 0.977
  },
  "services": {
    "total_registered": 3,
    "healthy_services": 3,
    "circuit_breaker_open": 0
  }
}
```

**异常响应** (503):
```json
{
  "status": "DOWN",
  "message": "Service discovery not responding",
  "timestamp": 1758707400,
  "version": "1.0.0",
  "components": {
    "http_server": "UP",
    "route_manager": "UP",
    "load_balancer": "UP",
    "rate_limiter": "UP",
    "circuit_breakers": "DOWN",
    "service_discovery": "DOWN"
  },
  "issues": [
    "Service discovery connection timeout",
    "Circuit breaker for user_service is OPEN"
  ]
}
```

---

### **1.2 系统资源信息**

**端点**: `GET /api/v1/gateway/system`

**功能**: 获取网关系统资源使用情况和性能指标

**成功响应** (200):

```json
{
  "cpu_usage_percent": 25.6,
  "memory_usage_percent": 42.3,
  "timestamp": 1758707400,
  "gateway": {
    "total_requests": 15234,
    "successful_requests": 14892,
    "failed_requests": 342,
    "average_response_time": 145.5,
    "success_rate": 0.977
  },
  "service_discovery": {
    "total_services": 3,
    "healthy_services": 3
  },
  "routing": {
    "total_routes": 45,
    "enabled_routes": 42,
    "dynamic_routes": 39,
    "static_routes": 6
  },
  "load_balancer": {
    "total_instances": 8,
    "healthy_instances": 7,
    "strategy": "round_robin"
  }
}
```

**异常响应** (500):
```json
{
  "error": "获取系统资源信息失败",
  "timestamp": 1758707400
}
```

---

### **1.3 网关统计信息**

**端点**: `GET /api/v1/gateway/metrics`

**功能**: 获取详细的网关性能和业务指标

**成功响应** (200):

```json
{
  "timestamp": 1758707400,
  "uptime_seconds": 86400,
  "version": "1.0.0",
  "request_metrics": {
    "total_requests": 15234,
    "successful_requests": 14892,
    "failed_requests": 342,
    "rate_limited_requests": 89,
    "circuit_breaker_rejected": 12,
    "requests_per_second": 17.6,
    "peak_requests_per_second": 156.3
  },
  "response_time_metrics": {
    "average_ms": 145.5,
    "p50_ms": 98.2,
    "p95_ms": 287.6,
    "p99_ms": 456.8,
    "max_ms": 1234.5
  },
  "service_metrics": {
    "registered_services": 3,
    "healthy_services": 3,
    "total_service_instances": 8,
    "healthy_instances": 7
  },
  "routing_metrics": {
    "total_routes": 45,
    "enabled_routes": 42,
    "route_cache_hits": 12876,
    "route_cache_misses": 234
  },
  "circuit_breaker_metrics": {
    "total_breakers": 3,
    "open_breakers": 0,
    "half_open_breakers": 0,
    "closed_breakers": 3
  }
}
```

---

## **🔧 2. 服务发现管理API**

### **2.1 服务注册**

**端点**: `POST /api/v1/services/register`

**功能**: 注册新的微服务实例到网关

**请求头**:
```http
Content-Type: application/json
Authorization: Bearer valid_service-api-key-2025
```

**请求JSON格式**:

```json
{
  "service_name": "user_service",           // 必填：服务名称
  "host": "0.0.0.0",                       // 必填：服务主机地址
  "port": 8082,                            // 必填：服务端口
  "service_version": "1.0.0",              // 必填：服务版本
  "health_check_url": "/health",           // 可选：健康检查路径
  "heartbeat_interval": 30,                // 可选：心跳间隔(秒)
  "weight": 100,                           // 可选：负载均衡权重
  "api_endpoints": [                       // 可选：API端点列表
    "/api/v1/users",
    "/api/v1/users/{user_id}",
    "/api/v1/users/{user_id}/profile"
  ],
  "metadata": {                            // 可选：服务元数据
    "environment": "production",
    "region": "us-west-1",
    "team": "backend"
  }
}
```

**成功响应** (200):

```json
{
  "success": true,
  "message": "服务注册成功",
  "data": {
    "service_name": "user_service",
    "host": "0.0.0.0",
    "port": 8082
  },
  "timestamp": 1758707400,
  "routing": {
    "total_routes_created": 17,
    "public_routes": 8,
    "authenticated_routes": 9
  },
  "load_balancer": {
    "strategy": "round_robin",
    "weight": 100,
    "healthy": true
  }
}
```

**失败响应** (400):
```json
{
  "error": "服务注册数据格式无效",
  "error_code": "INVALID_REQUEST",
  "timestamp": 1758707400,
  "details": {
    "missing_fields": ["service_name", "host"],
    "invalid_fields": ["port"]
  }
}
```

**失败响应** (409):
```json
{
  "error": "服务已存在",
  "error_code": "SERVICE_EXISTS",
  "timestamp": 1758707400
}
```

---

### **2.2 服务注销**

**端点**: `DELETE /api/v1/services/deregister`

**功能**: 从网关注销微服务实例

**请求JSON格式**:
```json
{
  "service_name": "user_service",       // 必填：服务名称
  "host": "0.0.0.0",                   // 必填：服务主机
  "port": 8082                         // 必填：服务端口
}
```

**成功响应** (200):

```json
{
  "success": true,
  "message": "服务注销成功",
  "data": {
    "service_name": "user_service",
    "host": "0.0.0.0",
    "port": 8082
  },
  "timestamp": 1758707400,
  "cleanup": {
    "routes_removed": 17,
    "circuit_breaker_reset": true,
    "load_balancer_updated": true
  }
}
```

**失败响应** (404):
```json
{
  "error": "服务不存在",
  "error_code": "SERVICE_NOT_FOUND",
  "timestamp": 1758707400
}
```

---

### **2.3 服务心跳**

**端点**: `POST /api/v1/services/heartbeat`

**功能**: 更新服务实例的健康状态和元数据

**请求JSON格式**:
```json
{
  "service_name": "user_service",           // 必填：服务名称
  "host": "0.0.0.0",                       // 必填：服务主机
  "port": 8082,                            // 必填：服务端口
  "status": "healthy",                     // 必填：服务状态
  "metadata": {                            // 可选：更新的元数据
    "cpu_usage": "25.6",
    "memory_usage": "42.3",
    "active_connections": "156",
    "requests_per_second": "23.4",
    "uptime_seconds": "7200"
  }
}
```

**成功响应** (200):

```json
{
  "success": true,
  "message": "心跳更新成功",
  "data": {
    "service_name": "user_service",
    "health_score": 1.0,
    "last_heartbeat": 1758707400,
    "consecutive_successes": 15
  },
  "timestamp": 1758707400
}
```

**失败响应** (404):
```json
{
  "error": "服务未注册",
  "error_code": "SERVICE_NOT_REGISTERED",
  "timestamp": 1758707400
}
```

---

### **2.4 服务列表查询**

**端点**: `GET /api/v1/services`

**功能**: 获取所有注册的服务列表

**查询参数**:
- `status` (可选): 过滤服务状态 (`healthy`, `unhealthy`, `all`)
- `service_name` (可选): 过滤特定服务名称

**成功响应** (200):

```json
{
  "success": true,
  "total_services": 3,
  "healthy_services": 3,
  "services": [
    {
      "service_name": "user_service",
      "host": "0.0.0.0",
      "port": 8082,
      "status": "healthy",
      "version": "1.0.0",
      "weight": 100,
      "health_score": 1.0,
      "last_heartbeat": 1758707400,
      "registered_at": 1758707200,
      "route_count": 17,
      "metadata": {
        "cpu_usage": "25.6",
        "memory_usage": "42.3",
        "active_connections": "156"
      }
    },
    {
      "service_name": "auth_service",
      "host": "0.0.0.0",
      "port": 8008,
      "status": "healthy",
      "version": "2.0.0",
      "weight": 100,
      "health_score": 0.95,
      "last_heartbeat": 1758707395,
      "registered_at": 1758707150,
      "route_count": 8,
      "metadata": {
        "cpu_usage": "18.2",
        "memory_usage": "38.7"
      }
    },
    {
      "service_name": "game_data_service",
      "host": "0.0.0.0",
      "port": 8083,
      "status": "healthy",
      "version": "1.0.0",
      "weight": 100,
      "health_score": 1.0,
      "last_heartbeat": 1758707398,
      "registered_at": 1758707180,
      "route_count": 19,
      "metadata": {
        "cpu_usage": "15.8",
        "memory_usage": "35.2"
      }
    }
  ],
  "timestamp": 1758707400
}
```

---

### **2.5 服务统计信息**

**端点**: `GET /api/v1/services/stats`

**功能**: 获取服务发现和注册的统计信息

**成功响应** (200):

```json
{
  "success": true,
  "timestamp": 1758707400,
  "service_discovery": {
    "total_services": 3,
    "healthy_services": 3,
    "unhealthy_services": 0,
    "total_instances": 8,
    "healthy_instances": 7,
    "registration_rate": 0.5,
    "heartbeat_success_rate": 0.998
  },
  "routing": {
    "total_routes": 45,
    "enabled_routes": 42,
    "disabled_routes": 3,
    "dynamic_routes": 39,
    "static_routes": 6
  },
  "load_balancing": {
    "strategy": "round_robin",
    "total_requests_distributed": 15234,
    "distribution_efficiency": 0.95,
    "average_instance_load": 1904.25
  },
  "health_checks": {
    "total_checks": 2890,
    "successful_checks": 2845,
    "failed_checks": 45,
    "success_rate": 0.984,
    "average_response_time": 23.6
  }
}
```

---

### **2.6 特定服务统计**

**端点**: `GET /api/v1/services/{service_name}/stats`

**功能**: 获取特定服务的详细统计信息

**路径参数**:
- `service_name`: 服务名称

**成功响应** (200):

```json
{
  "success": true,
  "service_name": "user_service",
  "timestamp": 1758707400,
  "service_info": {
    "host": "0.0.0.0",
    "port": 8082,
    "version": "1.0.0",
    "status": "healthy",
    "health_score": 1.0,
    "uptime_seconds": 7200
  },
  "traffic_stats": {
    "total_requests": 8765,
    "successful_requests": 8634,
    "failed_requests": 131,
    "success_rate": 0.985,
    "requests_per_second": 1.22,
    "average_response_time": 128.5
  },
  "routing": {
    "total_routes": 17,
    "enabled_routes": 17,
    "route_matches": 8765,
    "route_cache_hits": 8432
  },
  "load_balancing": {
    "weight": 100,
    "current_load": 23.6,
    "distribution_ratio": 0.334
  },
  "circuit_breaker": {
    "state": "CLOSED",
    "failure_rate": 0.015,
    "slow_call_rate": 0.002,
    "last_state_change": 1758707200
  }
}
```

**失败响应** (404):
```json
{
  "error": "服务不存在",
  "error_code": "SERVICE_NOT_FOUND",
  "timestamp": 1758707400
}
```

---

### **2.7 特定服务健康检查**

**端点**: `GET /api/v1/services/{service_name}/health`

**功能**: 检查特定服务的健康状态

**成功响应** (200):

```json
{
  "service_name": "user_service",
  "status": "healthy",
  "health_score": 1.0,
  "checks": {
    "service": "healthy",
    "database": "healthy",
    "cache": "healthy",
    "external_dependencies": "healthy"
  },
  "details": {
    "host": "0.0.0.0",
    "port": 8082,
    "version": "1.0.0",
    "last_heartbeat": 1758707400,
    "consecutive_successes": 15,
    "consecutive_failures": 0,
    "response_time_ms": 25.6
  },
  "metadata": {
    "cpu_usage": "25.6",
    "memory_usage": "42.3",
    "active_connections": "156",
    "requests_per_second": "23.4"
  },
  "timestamp": 1758707400
}
```

**异常响应** (503):
```json
{
  "service_name": "user_service",
  "status": "unhealthy",
  "health_score": 0.2,
  "checks": {
    "service": "unhealthy",
    "database": "healthy",
    "cache": "degraded"
  },
  "issues": [
    "Service response timeout",
    "High error rate detected"
  ],
  "timestamp": 1758707400
}
```

---

## **🔄 3. API转发服务**

### **3.1 动态路由转发**

**端点**: `[METHOD] /api/*` 和 `[METHOD] /v1/*`

**功能**: 自动转发API请求到对应的微服务

**支持的HTTP方法**: `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `OPTIONS`

**请求处理流程**:
1. 路由匹配和解析
2. 服务发现和负载均衡
3. 认证和授权检查
4. 限流和熔断检查
5. 请求转发和响应处理

**认证要求**:
- 公开路径无需认证
- 受保护路径需要 `Authorization: Bearer <token>`

**示例 - 用户API转发**:

**请求**: `GET /api/v1/users/123`

**转发到**: `user_service` → `GET http://0.0.0.0:8082/api/v1/users/123`

**成功响应** (200):
```json
{
  "user_id": "usr_1758694452619_6306",
  "username": "hxx2",
  "email": "jogshhxchn@example.com",
  "nickname": "cddhjsxxh1",
  "status": "active",
  "created_at": "2025-01-25T10:30:00Z"
}
```

**错误响应 - 路由未找到** (404):
```json
{
  "error": "Route not found",
  "path": "/api/v1/unknown/endpoint",
  "method": "GET",
  "timestamp": 1758707400
}
```

**错误响应 - 服务不可用** (503):
```json
{
  "error": "Service unavailable",
  "service": "user_service",
  "reason": "Circuit breaker is open",
  "timestamp": 1758707400
}
```

**错误响应 - 认证失败** (401):
```json
{
  "error": "Authentication required",
  "path": "/api/v1/users/123",
  "reason": "Missing authorization header",
  "timestamp": 1758707400
}
```

**错误响应 - 限流** (429):
```json
{
  "error": "Rate limit exceeded",
  "limit": 100,
  "window": "60s",
  "retry_after": 45,
  "timestamp": 1758707400
}
```

---

## **📊 4. 监控和诊断API**

### **4.1 网关端点列表**

**端点**: `GET /api/v1/service/endpoints`

**功能**: 获取网关自身提供的所有API端点

**成功响应** (200):

```json
{
  "success": true,
  "service_name": "api_gateway",
  "service_version": "1.0.0",
  "host": "0.0.0.0",
  "port": 8080,
  "endpoints": [
    {
      "path": "/health",
      "method": "GET",
      "description": "网关健康检查",
      "requires_auth": false
    },
    {
      "path": "/api/v1/gateway/system",
      "method": "GET",
      "description": "系统资源信息",
      "requires_auth": false
    },
    {
      "path": "/api/v1/gateway/metrics",
      "method": "GET",
      "description": "网关性能指标",
      "requires_auth": false
    },
    {
      "path": "/api/v1/services/register",
      "method": "POST",
      "description": "服务注册",
      "requires_auth": true
    },
    {
      "path": "/api/v1/services/deregister",
      "method": "DELETE",
      "description": "服务注销",
      "requires_auth": true
    },
    {
      "path": "/api/v1/services/heartbeat",
      "method": "POST",
      "description": "服务心跳",
      "requires_auth": true
    },
    {
      "path": "/api/v1/services",
      "method": "GET",
      "description": "服务列表查询",
      "requires_auth": false
    },
    {
      "path": "/api/v1/services/stats",
      "method": "GET",
      "description": "服务统计信息",
      "requires_auth": false
    },
    {
      "path": "/api/v1/services/{service_name}/stats",
      "method": "GET",
      "description": "特定服务统计",
      "requires_auth": false
    },
    {
      "path": "/api/v1/services/{service_name}/health",
      "method": "GET",
      "description": "特定服务健康检查",
      "requires_auth": false
    },
    {
      "path": "/api/*",
      "method": "ALL",
      "description": "API转发服务",
      "requires_auth": "动态检查"
    }
  ],
  "metadata": {
    "architecture": "microservices_gateway",
    "description": "Game Microservices API Gateway",
    "features": [
      "动态路由",
      "服务发现",
      "负载均衡",
      "认证授权",
      "限流熔断",
      "性能监控"
    ]
  }
}
```

---

## **📝 错误响应格式**

所有HTTP API的错误响应都遵循统一格式：

```json
{
  "error": "详细错误描述信息",
  "error_code": "ERROR_CODE",
  "timestamp": 1758707400,
  "path": "/api/v1/example",
  "method": "GET",
  "details": {
    "additional_info": "补充信息"
  }
}
```

**常见错误码**:

### **网关相关**
- `GATEWAY_INTERNAL_ERROR` - 网关内部错误
- `ROUTE_NOT_FOUND` - 路由未找到
- `SERVICE_UNAVAILABLE` - 目标服务不可用
- `LOAD_BALANCER_ERROR` - 负载均衡器错误
- `CIRCUIT_BREAKER_OPEN` - 熔断器开启

### **服务发现相关**
- `SERVICE_NOT_FOUND` - 服务不存在
- `SERVICE_NOT_REGISTERED` - 服务未注册
- `SERVICE_EXISTS` - 服务已存在
- `INVALID_SERVICE_DATA` - 服务数据无效
- `REGISTRATION_FAILED` - 注册失败

### **认证授权相关**
- `AUTHENTICATION_REQUIRED` - 需要认证
- `INVALID_TOKEN` - 令牌无效
- `TOKEN_EXPIRED` - 令牌过期
- `AUTHORIZATION_FAILED` - 授权失败
- `MISSING_AUTHORIZATION_HEADER` - 缺少授权头

### **限流熔断相关**
- `RATE_LIMIT_EXCEEDED` - 超出限流
- `CIRCUIT_BREAKER_OPEN` - 熔断器开启
- `TOO_MANY_REQUESTS` - 请求过多
- `SERVICE_OVERLOADED` - 服务过载

### **系统相关**
- `INVALID_JSON` - JSON格式无效
- `MISSING_FIELDS` - 缺少必需字段
- `INTERNAL_ERROR` - 内部服务器错误
- `CONFIGURATION_ERROR` - 配置错误
- `RESOURCE_EXHAUSTED` - 资源耗尽

---

## **🎯 使用流程示例**

### **完整服务注册和API转发流程**

1. **微服务启动并注册**:
   ```http
   POST /api/v1/services/register
   Authorization: Bearer service-api-key
   {
     "service_name": "user_service",
     "host": "0.0.0.0",
     "port": 8082,
     "service_version": "1.0.0",
     "api_endpoints": ["/api/v1/users", "/api/v1/users/{user_id}"]
   }
   ```

2. **网关自动发现服务端点**:
   ```http
   GET http://0.0.0.0:8082/api/v1/service/endpoints
   ```

3. **网关创建动态路由**:
   - 自动注册 `GET /api/v1/users` → `user_service`
   - 自动注册 `GET /api/v1/users/{user_id}` → `user_service`

4. **客户端通过网关访问服务**:
   ```http
   GET /api/v1/users/123
   Authorization: Bearer user-token
   ```

5. **网关转发请求到用户服务**:
   ```http
   GET http://0.0.0.0:8082/api/v1/users/123
   Authorization: Bearer user-token
   ```

6. **返回用户服务响应**

### **服务健康监控流程**

1. **定期发送心跳**:
   ```http
   POST /api/v1/services/heartbeat
   {
     "service_name": "user_service",
     "host": "0.0.0.0",
     "port": 8082,
     "status": "healthy",
     "metadata": {"cpu_usage": "25.6"}
   }
   ```

2. **查询服务状态**:
   ```http
   GET /api/v1/services/user_service/health
   ```

3. **监控网关整体状态**:
   ```http
   GET /health
   ```

---

## **🔧 网关特性**

### **动态路由管理**
- **自动发现**: 服务注册时自动获取API端点
- **动态注册**: 无需重启即可添加/删除路由
- **路径转换**: 支持参数化路径转换 (`{id}` → `*`)
- **优先级匹配**: 精确匹配 > 参数匹配 > 通配符匹配

### **负载均衡策略**
- **轮询**: 默认轮询策略，均匀分发请求
- **权重轮询**: 支持按权重分配流量
- **最少连接**: 优先选择连接数较少的实例
- **健康检查**: 自动剔除不健康的服务实例

### **认证授权机制**
- **JWT验证**: 支持Bearer Token认证
- **公开路径**: 可配置无需认证的路径
- **中间件架构**: 灵活的认证中间件系统
- **动态权限**: 基于路由的动态权限控制

### **限流和熔断**
- **限流器**: 基于令牌桶的限流算法
- **熔断器**: 自动熔断失败率高的服务
- **慢调用检测**: 检测和处理慢响应
- **降级策略**: 支持服务降级和故障转移

### **监控和诊断**
- **性能指标**: 详细的请求和响应时间统计
- **健康检查**: 多层次的健康状态监控
- **链路追踪**: 请求在微服务间的传播追踪
- **日志集成**: 结构化日志和错误追踪

### **高可用特性**
- **故障隔离**: 单个服务故障不影响其他服务
- **优雅降级**: 服务不可用时的降级策略
- **自动恢复**: 服务恢复后自动重新纳入负载均衡
- **多实例支持**: 支持同一服务的多个实例

---

## **🚀 架构特点**

### **实现状态总览**

| 功能模块 | 实现状态 | 特色功能 |
|---------|---------|---------|
| 动态路由 | ✅ 完整实现 | 自动发现、实时更新、优先级匹配 |
| 服务发现 | ✅ 完整实现 | 自动注册、健康检查、故障转移 |
| 负载均衡 | ✅ 完整实现 | 多策略、权重分配、健康实例选择 |
| 认证授权 | ✅ 完整实现 | JWT验证、动态权限、中间件架构 |
| 限流熔断 | ✅ 完整实现 | 令牌桶限流、智能熔断、降级策略 |
| 监控诊断 | ✅ 完整实现 | 性能指标、健康检查、链路追踪 |
| 高可用性 | ✅ 完整实现 | 故障隔离、自动恢复、优雅降级 |

---

## **🔧 开发者备注**

### **架构设计**
- **微服务网关**: 作为微服务架构的统一入口
- **中间件系统**: 可插拔的中间件架构，支持自定义扩展
- **事件驱动**: 基于事件的异步处理模式
- **无状态设计**: 网关本身无状态，便于水平扩展

### **性能优化**
- **连接池**: HTTP客户端连接池，减少连接开销
- **缓存机制**: 路由缓存、服务发现缓存
- **异步处理**: 非阻塞IO，提高并发处理能力
- **资源管理**: RAII模式，确保资源安全释放

### **运维特性**
- **零停机部署**: 支持滚动更新和蓝绿部署
- **配置热更新**: 支持运行时配置更新
- **可观测性**: 完整的监控、日志和追踪体系
- **故障诊断**: 丰富的诊断API和健康检查

---

**📌 注意**: 本文档基于API网关服务的实际代码实现生成，所有API格式和功能都经过验证，确保准确性和可用性。API网关作为微服务架构的核心组件，为整个系统提供统一的入口、路由、安全和监控服务。API可能根据版本更新而变化，请参考对应版本的文档。


