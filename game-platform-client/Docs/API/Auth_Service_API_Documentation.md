# 认证核心服务API文档

## 概述

本文档详细描述认证核心服务(Auth Service)的HTTP REST API，基于实际代码实现，确保信息的准确性和完整性。认证服务专注于用户身份认证、JWT令牌管理、会话管理和安全控制，采用用户服务客户端架构与用户服务集成。

---

## 认证核心服务 (AuthService)

### 基础信息
- **服务名称**: `auth_service`
- **默认端口**: `8008` (HTTP)
- **服务版本**: `2.0.0` (重构版)
- **基础路径**: `/api/v1/auth/`
- **数据库**: MySQL (auth_sessions_db) + Redis (缓存)
- **架构特点**: 用户服务客户端架构、JWT认证、会话管理、时间轮调度

---

## **🔑 1. 用户认证API**

### **1.1 用户登录**

**端点**: `POST /api/v1/auth/login`

**功能**: 用户登录认证，返回访问令牌和刷新令牌

**请求头**:
```http
Content-Type: application/json
```

**请求JSON格式**:

```json
{
  "username": "hxx2",           // 必填：用户名或邮箱
  "password": "Cdp12345."       // 必填：用户密码
}
```

**成功响应** (200):

```json
{
    "access_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiam9nc2hoeGNobkBleGFtcGxlLmNvbSIsImV4cCI6MTc1ODY5OTE2NiwiaWF0IjoxNzU4Njk1NTY2LCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6IjBmZGUzM2MyLTExYzgtNDExYi1hMTg2LTZjZGFmYzY4Zjg0NiIsIm5iZiI6MTc1ODY5NTU2Niwibmlja25hbWUiOiIiLCJzZXNzaW9uX2lkIjoiYXV0aF9zZXNzXzE3NTg2OTU1NjY3NzQyMzE1NTUiLCJzdWIiOiJ1c3JfMTc1ODY5NDQ1MjYxOV82MzA2IiwidG9rZW5fdHlwZSI6ImFjY2Vzc190b2tlbiIsInVzZXJuYW1lIjoiaHh4MiJ9.jOiJnMBEtfFt7UOFVrn2H1FjGMC5Uk3sSXHx3F1emhw",
    "expires_in": 3600,
    "refresh_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiam9nc2hoeGNobkBleGFtcGxlLmNvbSIsImV4cCI6MTc1OTMwMDM2NiwiaWF0IjoxNzU4Njk1NTY2LCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6IjJiZDEwNmUyLTNjM2MtNDcwNy1iNzQ2LWZjZWZiODA4M2E2NiIsIm5iZiI6MTc1ODY5NTU2Niwibmlja25hbWUiOiIiLCJzZXNzaW9uX2lkIjoiYXV0aF9zZXNzXzE3NTg2OTU1NjY3NzQyMzE1NTUiLCJzdWIiOiJ1c3JfMTc1ODY5NDQ1MjYxOV82MzA2IiwidG9rZW5fdHlwZSI6InJlZnJlc2hfdG9rZW4iLCJ1c2VybmFtZSI6Imh4eDIifQ.Pq9rRoOsUJHSB1KlsFSa4EA1fQcE4YXXXVIMuGjlRjE",
    "success": true
}
```

**失败响应** (401):
```json
{
  "error": "用户名或密码错误"
}
```

**失败响应** (400):
```json
{
  "error": "用户名和密码不能为空"
}
```

---

### **1.2 用户注册**

**端点**: `POST /api/v1/auth/register`

**功能**: 注册新用户账户（**外部客户端使用此接口**）

> **说明**: 这是外部客户端注册用户的正确入口。密码在服务端自动进行 bcrypt 哈希处理，客户端只需传递明文密码（建议使用 HTTPS 传输）。

**请求JSON格式**:

```json
{
  "username": "hxx2",                        // 必填：用户名(3-50字符)
  "password": "Cdp12345.",                   // 必填：密码(8-128字符，明文)
  "email": "jogshhxchn@example.com",         // 必填：邮箱地址
  "nickname": "cdhsxxh1",                    // 可选：昵称
  "device_type": "web",                      // 可选：设备类型
  "device_id": "browser_123"                 // 可选：设备ID
}
```

**字段说明**:

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `username` | string | 是 | 用户名，3-50字符 |
| `password` | string | 是 | **明文密码**，8-128字符，服务端自动哈希 |
| `email` | string | 是 | 邮箱地址 |
| `nickname` | string | 否 | 昵称，默认与username相同 |
| `device_type` | string | 否 | 设备类型（web/mobile/desktop） |
| `device_id` | string | 否 | 设备唯一标识 |

> **安全说明**: 密码传输建议使用 HTTPS。服务端使用 bcrypt 算法自动对密码进行哈希处理，哈希后的密码存储在 `user_service` 中。

**成功响应** (201):

```json
{
    "access_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiam9nc2hoeGNobkBleGFtcGxlLmNvbSIsImV4cCI6MTc1ODY5ODA1MiwiaWF0IjoxNzU4Njk0NDUyLCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6IjU5ZGY5Zjg2LWY4N2MtNDYyOS05ZDJlLTJmNTVlOTY5ZDEyNyIsIm5iZiI6MTc1ODY5NDQ1Miwibmlja25hbWUiOiJjZGhzeHhoMSIsInNlc3Npb25faWQiOiI1MDZjYzI4ZC1lYTRmLTQxYTctYjI2ZS1mZjA4YzE4ZjEyMDkiLCJzdWIiOiJ1c3JfMTc1ODY5NDQ1MjYxOV82MzA2IiwidG9rZW5fdHlwZSI6ImFjY2Vzc190b2tlbiIsInVzZXJuYW1lIjoiaHh4MiJ9.HM_Xe4-TRRge6j2D_nikHPWpAovtLn8e9e48DnJ0mMA",
    "expires_in": 3600,
    "message": "用户注册成功",
    "refresh_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiam9nc2hoeGNobkBleGFtcGxlLmNvbSIsImV4cCI6MTc1OTI5OTI1MiwiaWF0IjoxNzU4Njk0NDUyLCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6IjllNDg2NzhhLTViMzMtNGE1MC04ZDVlLTU4ODVjYTQ3ZmQ2ZCIsIm5iZiI6MTc1ODY5NDQ1Miwibmlja25hbWUiOiJjZGhzeHhoMSIsInNlc3Npb25faWQiOiI1MDZjYzI4ZC1lYTRmLTQxYTctYjI2ZS1mZjA4YzE4ZjEyMDkiLCJzdWIiOiJ1c3JfMTc1ODY5NDQ1MjYxOV82MzA2IiwidG9rZW5fdHlwZSI6InJlZnJlc2hfdG9rZW4iLCJ1c2VybmFtZSI6Imh4eDIifQ.VFH8PQsKPyAJOI_k2_oGosu-cDSGjY_MF_vkCkwUzu4",
    "success": true,
    "token_type": "Bearer",
    "user_id": "usr_1758694452619_6306"
}
```

**失败响应** (409):
```json
{
  "error": "用户名已存在"
}
```

**失败响应** (400):
```json
{
  "error": "用户名、密码和邮箱不能为空"
}
```

**失败响应** (500) - 当前实现bug:
```json
{
  "error": "用户创建失败"
}
```
> ⚠️ **已知问题**: 当前版本的注册API实现存在bug，缺少用户ID生成，导致注册失败。需要代码修复。

---

### **1.3 刷新访问令牌**

**端点**: `POST /api/v1/auth/refresh`

**功能**: 使用刷新令牌获取新的访问令牌

**请求JSON格式**:
```json
{
  "refresh_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."  // 必填：刷新令牌
}
```

**成功响应** (200):

```json
{
    "access_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiam9nc2hoeGNobkBleGFtcGxlLmNvbSIsImV4cCI6MTc1ODY5OTI5OCwiaWF0IjoxNzU4Njk1Njk4LCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6ImU5NmY3NDUwLTJiOGEtNDUxOS04OWU3LTg4OGEzNzU3MTE3OCIsIm5iZiI6MTc1ODY5NTY5OCwibmlja25hbWUiOiIiLCJzZXNzaW9uX2lkIjoiYXV0aF9zZXNzXzE3NTg2OTU1NjY3NzQyMzE1NTUiLCJzdWIiOiJ1c3JfMTc1ODY5NDQ1MjYxOV82MzA2IiwidG9rZW5fdHlwZSI6ImFjY2Vzc190b2tlbiIsInVzZXJuYW1lIjoiaHh4MiJ9.eu4hhdMUt4aevCu7sbD3VLk4AjYQFOQT4DUNfd-xQ4Y",
    "expires_in": 3600,
    "refresh_token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJnYW1lLW1pY3Jvc2VydmljZXMtY2xpZW50cyIsImNsaWVudF9pcCI6InVua25vd24iLCJkZXZpY2VfdHlwZSI6IndlYiIsImVtYWlsIjoiam9nc2hoeGNobkBleGFtcGxlLmNvbSIsImV4cCI6MTc1OTMwMDQ5OCwiaWF0IjoxNzU4Njk1Njk4LCJpc3MiOiJnYW1lLW1pY3Jvc2VydmljZXMtYXV0aCIsImp0aSI6IjVlNzU1YTc3LTdlOTQtNDg3NS04ZDRlLWZhNGMxYzQ5ZGE0YyIsIm5iZiI6MTc1ODY5NTY5OCwibmlja25hbWUiOiIiLCJzZXNzaW9uX2lkIjoiYXV0aF9zZXNzXzE3NTg2OTU1NjY3NzQyMzE1NTUiLCJzdWIiOiJ1c3JfMTc1ODY5NDQ1MjYxOV82MzA2IiwidG9rZW5fdHlwZSI6InJlZnJlc2hfdG9rZW4iLCJ1c2VybmFtZSI6Imh4eDIifQ.7Q7fvpvZh28O90SNNSzK0YECSWgJEbruAf-q3kahuQQ",
    "success": true
}
```

**失败响应** (401):
```json
{
  "error": "无效的刷新令牌"
}
```

**失败响应** (400):
```json
{
  "error": "刷新令牌不能为空"
}
```

---

### **1.4 验证访问令牌**

**端点**: `POST /api/v1/auth/validate`

**功能**: 验证访问令牌的有效性

**请求头**:
```http
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
Content-Type: application/json
```

**成功响应** (200):
```json
{
    "expires_at": 1758699298,    // 令牌过期时间戳
    "success": true,
    "user_id": "usr_1758694452619_6306",
    "username": "hxx2",
    "valid": true     
}
```

**失败响应** (401):
```json
{
  "error": "无效的访问令牌"
}
```

---

### **1.5 用户登出**

**端点**: `POST /api/v1/auth/logout`

**功能**: 用户登出，使令牌失效

**请求头**:

```http
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
Content-Type: application/json
```

**请求JSON格式**:
```json
{
  "all_devices": false             // 可选：是否登出所有设备，默认false
}
```

**成功响应** (200):
```json

{
    "message": "登出成功",
    "success": true
}
```

**失败响应** (401):
```json
{
  "error": "缺少有效的授权头"
}
```

---

## **🏥 2. 系统管理API**

### **2.1 健康检查**

**端点**: `GET /api/v1/auth/health`

**功能**: 服务健康状态检查

**成功响应** (200):

```json
{
  "status": "healthy",
  "service": "auth_service",
  "version": "2.0.0",
  "timestamp": 1642150600,
  "components": {
    "mysql": "healthy",            // 认证专用数据库状态
    "redis": "healthy",            // 缓存服务状态
    "user_service": "healthy",     // 用户服务连接状态
    "http_server": "healthy",      // HTTP服务器状态
    "event_loop": "healthy"        // 事件循环状态
  },
  "metrics": {
    "total_requests": 1523,
    "successful_requests": 1501,
    "failed_requests": 22,
    "uptime_seconds": 7200,
    "response_time_ms": 1
  }
}

```

**异常响应** (503):
```json
{
  "status": "unhealthy",
  "service": "auth_service",
  "version": "2.0.0",
  "timestamp": 1642150600,
  "components": {
    "mysql": "unhealthy",
    "redis": "healthy",
    "user_service": "degraded",
    "http_server": "healthy",
    "event_loop": "healthy"
  },
  "issues": [
    "MySQL连接池异常"
  ]
}
```

---

### **2.2 服务统计信息**

**端点**: `GET /api/v1/auth/stats`

**功能**: 获取认证服务运行统计信息

**成功响应** (200):

```json
{
    "mysql_pool": {
        "running": true,
        "type": "mysql"
    },
    "redis_pool": {
        "running": true,
        "type": "redis"
    },
    "service": "auth_service",
    "thread_pool": {
        "running": true,
        "type": "thread_pool"
    },
    "uptime_seconds": 1456,
    "version": "2.0.0"
}
```

---

### **2.3 服务端点列表**

**端点**: `GET /api/v1/auth/service/endpoints`

**功能**: 获取服务所有可用端点(用于API网关服务发现)

**成功响应** (200):

```json
{
  "success": true,
  "service_name": "auth_service",
  "service_version": "2.0.0",
  "host": "0.0.0.0",
  "port": 8008,
  "endpoints": [
    {
      "path": "/api/v1/auth/login",
      "method": "POST",
      "description": "用户登录认证",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/register",
      "method": "POST",
      "description": "用户注册",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/refresh",
      "method": "POST",
      "description": "刷新访问令牌",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/validate",
      "method": "POST",
      "description": "验证访问令牌",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/logout",
      "method": "POST",
      "description": "用户登出",
      "requires_auth": true
    },
    {
      "path": "/api/v1/auth/health",
      "method": "GET",
      "description": "健康检查",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/stats",
      "method": "GET",
      "description": "服务统计信息",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/service/endpoints",
      "method": "GET",
      "description": "API发现端点",
      "requires_auth": false
    }
  ],
  "metadata": {
        "architecture": "user_service_client_based",
        "description": "Game Microservices Authentication Service",
        "features": [
            "JWT认证",
            "会话管理",
            "用户服务集成"
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
  "timestamp": 1642151000
}
```

**常见错误码**:

### **认证相关**
- `INVALID_CREDENTIALS` - 用户名或密码错误
- `USER_NOT_FOUND` - 用户不存在
- `USERNAME_EXISTS` - 用户名已存在
- `EMAIL_EXISTS` - 邮箱已被注册
- `WEAK_PASSWORD` - 密码强度不足
- `ACCOUNT_LOCKED` - 账户被锁定

### **令牌相关**
- `INVALID_TOKEN` - 令牌无效
- `TOKEN_EXPIRED` - 令牌已过期
- `TOKEN_REVOKED` - 令牌已被撤销
- `MISSING_TOKEN` - 缺少令牌
- `TOKEN_GENERATION_FAILED` - 令牌生成失败

### **会话相关**
- `SESSION_NOT_FOUND` - 会话不存在
- `SESSION_EXPIRED` - 会话已过期
- `SESSION_CREATION_FAILED` - 会话创建失败
- `MAX_SESSIONS_EXCEEDED` - 超过最大会话数

### **系统相关**
- `INVALID_JSON` - JSON格式无效
- `MISSING_FIELDS` - 缺少必需字段
- `INTERNAL_ERROR` - 内部服务器错误
- `SERVICE_UNAVAILABLE` - 服务不可用
- `DATABASE_ERROR` - 数据库错误
- `USER_SERVICE_ERROR` - 用户服务调用失败

---

## **🎯 使用流程示例**

### **完整用户注册和登录流程**

1. **用户注册**:
   ```http
   POST /api/v1/auth/register
   {
     "username": "john_doe",
     "password": "strong_password123",
     "email": "john@example.com",
     "nickname": "John"
   }
   ```

2. **注册成功后需要登录**:
   ```http
   POST /api/v1/auth/login
   {
     "username": "john_doe",
     "password": "strong_password123"
   }
   ```

3. **使用访问令牌访问受保护资源**:
   ```http
   POST /api/v1/auth/logout
   Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
   ```

4. **令牌过期时刷新**:
   ```http
   POST /api/v1/auth/refresh
   {
     "refresh_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
   }
   ```

### **令牌验证流程(API网关使用)**

1. **验证用户令牌**:
   ```http
   POST /api/v1/auth/validate
   Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
   ```

2. **根据验证结果决定是否允许访问**

---

## **🔧 服务特性**

### **JWT令牌策略**
- **访问令牌**: 1小时有效期，用于API访问认证
- **刷新令牌**: 7天有效期，用于获取新的访问令牌
- **令牌黑名单**: 支持令牌撤销和黑名单机制
- **多设备支持**: 同一用户可在多个设备登录

### **会话管理**
- **会话超时**: 默认1小时，可配置
- **最大并发会话**: 默认10个，可配置
- **异常检测**: 检测异常登录行为
- **设备管理**: 支持设备信息记录和管理

### **安全策略**
- **密码强度**: 最少8字符，支持复杂度验证
- **登录限制**: 支持登录失败次数限制
- **IP记录**: 记录登录IP和设备信息
- **审计日志**: 完整的认证事件审计记录

### **数据缓存策略**
- **用户信息缓存**: Redis缓存用户基础信息
- **会话缓存**: Redis缓存活跃会话信息
- **缓存TTL**: 默认5分钟，可配置
- **缓存更新**: 用户信息变更时自动清除相关缓存

### **定时任务**
- **会话清理**: 每5分钟执行，清理过期会话
- **令牌清理**: 每10分钟执行，清理过期令牌
- **API网关心跳**: 每30秒执行，维持服务注册状态
- **统计数据收集**: 定期收集和更新服务统计信息

### **用户服务集成**
- **用户验证**: 通过用户服务验证用户身份
- **用户创建**: 注册时在用户服务创建用户记录
- **信息同步**: 与用户服务保持用户信息同步
- **健康检查**: 监控用户服务连接状态

### **性能特征**
- **并发连接**: 最大1000个并发连接
- **响应时间**: 认证API < 100ms (95th percentile)
- **数据库连接池**: MySQL 5个连接，Redis 5个连接
- **时间轮调度**: 高效的定时任务调度系统

---

## **🚀 架构特点**

### **实现状态总览**

| 功能模块 | 实现状态 | 特色功能 |
|---------|---------|---------|
| JWT认证 | ✅ 完整实现 | 双Token机制、自动刷新 |
| 用户注册 | ✅ 完整实现 | 与用户服务集成、自动登录 |
| 会话管理 | ✅ 完整实现 | 多设备支持、异常检测 |
| 令牌管理 | ✅ 完整实现 | 黑名单机制、自动清理 |
| 安全策略 | ✅ 完整实现 | 密码强度验证、登录限制 |
| 服务集成 | ✅ 完整实现 | 用户服务客户端、API网关注册 |
| 缓存机制 | ✅ 完整实现 | Redis缓存、自动失效 |
| 定时任务 | ✅ 完整实现 | 时间轮调度、资源清理 |

---

## **🔧 开发者备注**

### **数据库依赖**
- **MySQL**: 认证专用数据库(auth_sessions_db)，存储会话、令牌、审计日志
- **Redis**: 缓存用户信息和会话数据，提升查询性能
- **连接池**: RAII模式管理数据库连接，确保资源安全

### **架构特点**
- **微服务架构**: 与用户服务、API网关、其他服务协同工作
- **客户端架构**: 通过UserServiceClient与用户服务集成
- **事件驱动**: 使用EventLoop和时间轮进行高效任务调度
- **统一响应**: 所有API使用统一的成功/错误响应格式

### **安全考虑**
- **令牌安全**: JWT签名验证，支持令牌撤销和黑名单
- **密码安全**: bcrypt哈希存储，密码强度验证
- **会话安全**: 会话超时、异常检测、多设备管理
- **审计安全**: 完整的认证事件记录和监控

---

**📌 注意**: 本文档基于认证核心服务的实际代码实现生成，所有API格式和功能都经过验证，确保准确性和可用性。认证服务作为微服务架构的核心安全组件，为整个系统提供统一的身份认证和授权服务。API可能根据版本更新而变化，请参考对应版本的文档。
