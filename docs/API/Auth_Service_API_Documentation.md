# 认证核心服务API文档

## 概述

本文档详细描述认证核心服务(Auth Service)的HTTP REST API，基于实际代码实现，确保信息的准确性和完整性。认证服务专注于用户身份认证、JWT令牌管理、会话管理、安全控制及邮箱验证码，采用用户服务客户端架构与用户服务集成。

---

## 认证核心服务 (AuthService)

### 基础信息
- **服务名称**: `auth_service`
- **默认端口**: `8083` (HTTP)
- **服务版本**: `2.0.0` (重构版)
- **基础路径**: `/api/v1/auth/`
- **数据库**: MySQL (auth_sessions_db) + Redis (缓存)
- **架构特点**: 用户服务客户端架构、JWT认证、会话管理、时间轮调度、邮箱验证码

---

## **🔑 1. 用户认证API**

### **1.1 用户登录**

**端点**: `POST /api/v1/auth/login`

**功能**: 用户登录认证，支持密码登录和邮箱验证码登录两种方式

**请求头**:
```http
Content-Type: application/json
```

#### 方式一：密码登录

```json
{
  "username": "hxx2",           // 必填：用户名
  "password": "Cdp12345."       // 必填：用户密码
}
```

#### 方式二：邮箱验证码登录

```json
{
  "email": "user@example.com",  // 必填：邮箱地址
  "code": "123456"              // 必填：6位验证码
}
```

**成功响应** (200):

```json
{
    "access_token": "eyJhbGciOiJIUzI1NiJ9...",
    "expires_in": 3600,
    "refresh_token": "eyJhbGciOiJIUzI1NiJ9...",
    "success": true,
    "login_type": "email_code"   // 验证码登录时返回此字段，密码登录则无此字段
}
```

**失败响应** (401):
```json
{
  "error": "用户名或密码错误"
}
```

**失败响应** (401) - 验证码登录时:
```json
{
  "error": "验证码错误",
  "error_code": "CODE_MISMATCH"
}
```

**失败响应** (410) - 验证码过期:
```json
{
  "error": "验证码已过期或不存在",
  "error_code": "CODE_EXPIRED"
}
```

---

### **1.2 用户注册**

**端点**: `POST /api/v1/auth/register`

**功能**: 注册新用户账户（**强制邮箱验证**）

> **说明**: 注册前需先调用 `/api/v1/auth/email/send-code` 发送验证码，再调用 `/api/v1/auth/email/verify-code` 验证后获取 `verify_token`，最后携带 `verify_token` 完成注册。

**请求JSON格式**:

```json
{
  "username": "hxx2",                                   // 必填：用户名(3-50字符)
  "password": "Cdp12345.",                              // 必填：密码(8-128字符，明文)
  "email": "jogshhxchn@example.com",                    // 必填：邮箱地址
  "verify_token": "vtoken_1758694452619_123456",       // 必填：邮箱验证后获取的临时token
  "nickname": "cdhsxxh1",                               // 可选：昵称
  "device_type": "web",                                 // 可选：设备类型
  "device_id": "browser_123"                            // 可选：设备ID
}
```

**字段说明**:

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `username` | string | 是 | 用户名，3-50字符 |
| `password` | string | 是 | **明文密码**，8-128字符，服务端自动哈希 |
| `email` | string | 是 | 邮箱地址 |
| `verify_token` | string | 是 | 邮箱验证后获取的临时token，10分钟内有效 |
| `nickname` | string | 否 | 昵称，默认与username相同 |
| `device_type` | string | 否 | 设备类型（web/mobile/desktop） |
| `device_id` | string | 否 | 设备唯一标识 |

> **安全说明**: 密码传输建议使用 HTTPS。服务端使用 bcrypt 算法自动对密码进行哈希处理。verify_token 验证通过后立即失效，不可重复使用。

**成功响应** (201):

```json
{
    "access_token": "eyJhbGciOiJIUzI1NiJ9...",
    "expires_in": 3600,
    "message": "用户注册成功",
    "refresh_token": "eyJhbGciOiJIUzI1NiJ9...",
    "success": true,
    "token_type": "Bearer",
    "user_id": "usr_1758694452619_6306"
}
```

**失败响应** (401):
```json
{
  "error": "验证token无效或已过期，请重新获取验证码",
  "error_code": "INVALID_VERIFY_TOKEN"
}
```

**失败响应** (400):
```json
{
  "error": "用户名、密码、邮箱和验证token不能为空"
}
```

**失败响应** (409):
```json
{
  "error": "用户名已存在"
}
```

---

### **1.3 刷新访问令牌**

**端点**: `POST /api/v1/auth/refresh`

**功能**: 使用刷新令牌获取新的访问令牌

**请求JSON格式**:
```json
{
  "refresh_token": "eyJhbGciOiJIUzI1IsInR5cCI6IkpXVCJ9..."  // 必填：刷新令牌
}
```

**成功响应** (200):

```json
{
    "access_token": "eyJhbGciOiJIUzI1NiJ9...",
    "expires_in": 3600,
    "refresh_token": "eyJhbGciOiJIUzI1NiJ9...",
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

### **1.6 发送邮箱验证码**

**端点**: `POST /api/v1/auth/email/send-code`

**功能**: 向指定邮箱发送验证码，支持注册、登录、忘记密码三种用途

**请求JSON格式**:
```json
{
  "email": "user@example.com",    // 必填：目标邮箱地址
  "purpose": "register"           // 必填：用途 register | login | reset_password
}
```

**purpose 说明**:

| 值 | 用途 | 前置条件 |
|----|------|---------|
| `register` | 注册 | 邮箱不能已注册 |
| `login` | 登录 | 邮箱必须已注册 |
| `reset_password` | 重置密码 | 邮箱必须已注册 |

**成功响应** (200):
```json
{
  "success": true,
  "message": "验证码已发送",
  "cooldown_seconds": 60          // 提示冷却时间
}
```

**失败响应** (400) - 邮箱格式错误:
```json
{
  "success": false,
  "error": "邮箱格式无效",
  "error_code": "INVALID_EMAIL"
}
```

**失败响应** (409) - 用途为 register 但邮箱已注册:
```json
{
  "success": false,
  "error": "该邮箱已被注册"
}
```

**失败响应** (404) - 用途为 login/reset_password 但邮箱未注册:
```json
{
  "success": false,
  "error": "该邮箱未注册"
}
```

**失败响应** (429) - 发送过于频繁:
```json
{
  "success": false,
  "error": "发送过于频繁，请稍后再试",
  "cooldown_seconds": 45
}
```

**失败响应** (429) - 达到每日发送上限:
```json
{
  "success": false,
  "error": "今日发送次数已达上限",
  "error_code": "DAILY_LIMIT"
}
```

> **说明**: 验证码有效期 5 分钟，每个邮箱每天最多发送 10 次，同一邮箱发送间隔不少于 60 秒。

---

### **1.7 验证邮箱验证码**

**端点**: `POST /api/v1/auth/email/verify-code`

**功能**: 校验用户输入的验证码是否正确，验证成功返回 `verify_token`（注册用途）或直接执行操作（重置密码用途）

**请求JSON格式**:
```json
{
  "email": "user@example.com",    // 必填：邮箱地址
  "code": "123456",               // 必填：6位验证码
  "purpose": "register"           // 必填：用途 register | login | reset_password
}
```

**成功响应** (200) - register 用途返回 verify_token:
```json
{
  "success": true,
  "message": "验证码正确",
  "verify_token": "vtoken_1758694452619_123456"   // 10分钟内有效，用于完成注册
}
```

**成功响应** (200) - login/reset_password 用途直接成功:
```json
{
  "success": true,
  "message": "验证码正确"
}
```

**失败响应** (410) - 验证码过期:
```json
{
  "success": false,
  "error": "验证码已过期或不存在",
  "error_code": "CODE_EXPIRED"
}
```

**失败响应** (401) - 验证码错误:
```json
{
  "success": false,
  "error": "验证码错误",
  "error_code": "CODE_MISMATCH"
}
```

---

### **1.8 重置密码（忘记密码）**

**端点**: `POST /api/v1/auth/password/reset`

**功能**: 通过邮箱验证码验证后重设新密码

> **说明**: 无需登录。流程：send-code (reset_password) -> verify-code -> password/reset

**请求JSON格式**:
```json
{
  "email": "user@example.com",    // 必填：邮箱地址
  "code": "123456",               // 必填：6位验证码
  "new_password": "NewPass123!"  // 必填：新密码（明文）
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "密码重设成功，请使用新密码登录"
}
```

**失败响应** (401) - 验证码错误:
```json
{
  "success": false,
  "error": "验证码错误",
  "error_code": "CODE_MISMATCH"
}
```

**失败响应** (410) - 验证码过期:
```json
{
  "success": false,
  "error": "验证码已过期或不存在",
  "error_code": "CODE_EXPIRED"
}
```

**失败响应** (404) - 邮箱未注册:
```json
{
  "success": false,
  "error": "该邮箱未注册"
}
```

**失败响应** (400):
```json
{
  "success": false,
  "error": "邮箱、验证码和新密码不能为空"
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
    "mysql": "healthy",
    "redis": "healthy",
    "user_service": "healthy",
    "http_server": "healthy",
    "event_loop": "healthy"
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
  "port": 8083,
  "endpoints": [
    {
      "path": "/api/v1/auth/login",
      "method": "POST",
      "description": "用户登录（密码/验证码）",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/register",
      "method": "POST",
      "description": "用户注册（需先验证邮箱）",
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
      "requires_auth": true
    },
    {
      "path": "/api/v1/auth/logout",
      "method": "POST",
      "description": "用户登出",
      "requires_auth": true
    },
    {
      "path": "/api/v1/auth/email/send-code",
      "method": "POST",
      "description": "发送邮箱验证码",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/email/verify-code",
      "method": "POST",
      "description": "验证邮箱验证码",
      "requires_auth": false
    },
    {
      "path": "/api/v1/auth/password/reset",
      "method": "POST",
      "description": "重置密码（忘记密码）",
      "requires_auth": false
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
      "用户服务集成",
      "邮箱验证码"
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

### **验证码相关**
- `INVALID_EMAIL` - 邮箱格式无效
- `COOLDOWN` - 发送过于频繁
- `DAILY_LIMIT` - 每日发送次数已达上限
- `CODE_EXPIRED` - 验证码已过期或不存在
- `CODE_MISMATCH` - 验证码错误
- `INVALID_VERIFY_TOKEN` - 验证token无效或已过期
- `TOKEN_EMAIL_MISMATCH` - 验证token与邮箱不匹配

### **系统相关**
- `INVALID_JSON` - JSON格式无效
- `MISSING_FIELDS` - 缺少必需字段
- `INTERNAL_ERROR` - 内部服务器错误
- `SERVICE_UNAVAILABLE` - 服务不可用
- `DATABASE_ERROR` - 数据库错误
- `USER_SERVICE_ERROR` - 用户服务调用失败

---

## **🎯 使用流程示例**

### **注册流程（强制邮箱验证）**

```
1. POST /api/v1/auth/email/send-code
   Body: {"email": "user@example.com", "purpose": "register"}
   -> 收到验证码邮件

2. POST /api/v1/auth/email/verify-code
   Body: {"email": "user@example.com", "code": "123456", "purpose": "register"}
   -> 返回 verify_token

3. POST /api/v1/auth/register
   Body: {"username": "john_doe", "password": "strong_password123",
          "email": "user@example.com", "verify_token": "vtoken_xxx"}
   -> 注册成功，返回 JWT token
```

### **邮箱验证码登录流程**

```
1. POST /api/v1/auth/email/send-code
   Body: {"email": "user@example.com", "purpose": "login"}
   -> 收到验证码邮件

2. POST /api/v1/auth/login
   Body: {"email": "user@example.com", "code": "123456"}
   -> 登录成功，返回 JWT token
```

### **忘记密码流程**

```
1. POST /api/v1/auth/email/send-code
   Body: {"email": "user@example.com", "purpose": "reset_password"}
   -> 收到验证码邮件

2. POST /api/v1/auth/email/verify-code
   Body: {"email": "user@example.com", "code": "123456", "purpose": "reset_password"}
   -> 验证成功

3. POST /api/v1/auth/password/reset
   Body: {"email": "user@example.com", "code": "123456", "new_password": "NewPass123!"}
   -> 密码重设成功
```

### **密码登录流程（原有方式）**

```
1. POST /api/v1/auth/login
   Body: {"username": "john_doe", "password": "strong_password123"}
   -> 登录成功，返回 JWT token
```

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

### **邮箱验证码功能**
- **验证码位数**: 6位数字
- **有效期**: 5分钟
- **发送冷却**: 60秒（同一邮箱）
- **每日上限**: 10次（同一邮箱/天）
- **verify_token有效期**: 10分钟
- **邮件发送**: SMTP 465端口 SSL加密（163网易邮箱）

### **安全策略**
- **密码强度**: 最少8字符，支持复杂度验证
- **登录限制**: 支持登录失败次数限制
- **IP记录**: 记录登录IP和设备信息
- **审计日志**: 完整的认证事件审计记录

### **数据缓存策略**
- **用户信息缓存**: Redis缓存用户基础信息
- **会话缓存**: Redis缓存活跃会话信息
- **验证码缓存**: Redis存储，带TTL过期
- **缓存TTL**: 默认5分钟，可配置

### **定时任务**
- **会话清理**: 每30分钟执行，清理过期会话
- **令牌清理**: 每60分钟执行，清理过期令牌
- **API网关心跳**: 每30秒执行，维持服务注册状态

---

## **🚀 架构特点**

### **实现状态总览**

| 功能模块 | 实现状态 | 特色功能 |
|---------|---------|---------|
| JWT认证 | ✅ 完整实现 | 双Token机制、自动刷新 |
| 用户注册 | ✅ 完整实现 | 强制邮箱验证、与用户服务集成 |
| 会话管理 | ✅ 完整实现 | 多设备支持、异常检测 |
| 令牌管理 | ✅ 完整实现 | 黑名单机制、自动清理 |
| 安全策略 | ✅ 完整实现 | 密码强度验证、登录限制 |
| 服务集成 | ✅ 完整实现 | 用户服务客户端、API网关注册 |
| 缓存机制 | ✅ 完整实现 | Redis缓存、自动失效 |
| 定时任务 | ✅ 完整实现 | 时间轮调度、资源清理 |
| **邮箱验证码** | ✅ 新增 | SMTP发送、Redis存储、多用途、冷却限流 |
| **密码登录** | ✅ 完整实现 | 支持用户名+密码 |
| **验证码登录** | ✅ 新增 | 邮箱+验证码登录方式 |
| **忘记密码** | ✅ 新增 | 验证码+重设新密码 |

---

**📌 注意**: 本文档基于认证核心服务的实际代码实现生成，所有API格式和功能都经过验证，确保准确性和可用性。认证服务作为微服务架构的核心安全组件，为整个系统提供统一的身份认证和授权服务。API可能根据版本更新而变化，请参考对应版本的文档。
