# JWT 认证中间件集成指南

## 概述

本文档说明如何在各微服务中集成 JWT 认证中间件，实现统一的 Token 验证。

## 架构说明

```
┌─────────────┐     ┌─────────────┐     ┌─────────────────────┐
│   Client    │────>│    Nginx    │────>│   Backend Service   │
│             │     │   (80)      │     │   (8082/8083/...)   │
└─────────────┘     └─────────────┘     └──────────┬──────────┘
                                                   │
                                                   ▼
                                        ┌─────────────────────┐
                                        │   AuthMiddleware    │
                                        │                     │
                                        │  ┌───────────────┐  │
                                        │  │ JwtValidator  │  │
                                        │  │ (common/auth) │  │
                                        │  └───────────────┘  │
                                        └─────────────────────┘
```

## 模块说明

### 1. JwtValidator (`common/auth/jwt_validator.h`)

独立的 JWT 验证模块，提供：
- JWT Token 解析和验证
- 签名验证（HS256）
- 过期时间检查
- 用户信息提取

### 2. AuthMiddleware (`common/http/http_middleware.h`)

HTTP 中间件，提供：
- 自动提取 Authorization 头
- Token 格式验证
- 调用 JwtValidator 验证
- 将用户信息存储到请求上下文

## 快速开始

### 1. 配置 JWT 密钥

在服务配置文件中添加：

```yaml
# config/user_service.yml
jwt:
  secret_key: "${JWT_SECRET_KEY:your-super-secret-key}"
  issuer: "game-microservices-auth"
  audience: "game-microservices-clients"
  algorithm: "HS256"
  clock_skew_seconds: 60
  validate_issuer: true
  validate_audience: false
  validate_expiration: true

auth_middleware:
  enabled: true
  token_header: "Authorization"
  token_prefix: "Bearer "
  exclude_paths:
    - "/health"
    - "/info"
    - "/api/v1/user/check-username"
    - "/api/v1/user/check-email"
```

### 2. 初始化 JwtValidator

在服务启动时初始化：

```cpp
#include "common/auth/jwt_validator.h"
#include "common/config/config_manager.h"

bool initializeAuth() {
    auto& config_mgr = common::config::ConfigManager::getInstance();

    common::auth::JwtValidatorConfig jwt_config;
    jwt_config.secret_key = config_mgr.get<std::string>("jwt.secret_key", "");
    jwt_config.issuer = config_mgr.get<std::string>("jwt.issuer", "game-microservices-auth");
    jwt_config.validate_issuer = config_mgr.get<bool>("jwt.validate_issuer", true);
    jwt_config.validate_expiration = config_mgr.get<bool>("jwt.validate_expiration", true);

    if (!common::auth::JwtValidator::getInstance().initialize(jwt_config)) {
        LOG_ERROR("JwtValidator 初始化失败");
        return false;
    }

    LOG_INFO("JwtValidator 初始化成功");
    return true;
}
```

### 3. 添加认证中间件

在 HTTP 服务器中注册中间件：

```cpp
#include "common/http/http_middleware.h"

void setupMiddlewares(common::http::MiddlewareManager& manager) {
    auto& config_mgr = common::config::ConfigManager::getInstance();

    // CORS 中间件（优先级最高）
    auto cors_middleware = std::make_shared<common::http::CorsMiddleware>();
    manager.use(cors_middleware);

    // 认证中间件
    bool auth_enabled = config_mgr.get<bool>("auth_middleware.enabled", true);
    if (auth_enabled) {
        common::http::AuthMiddleware::Config auth_config;
        auth_config.secret_key = config_mgr.get<std::string>("jwt.secret_key", "");
        auth_config.token_header = config_mgr.get<std::string>("auth_middleware.token_header", "Authorization");
        auth_config.token_prefix = config_mgr.get<std::string>("auth_middleware.token_prefix", "Bearer ");

        // 获取排除路径
        auto exclude_paths = config_mgr.getArray<std::string>("auth_middleware.exclude_paths");
        auth_config.exclude_paths = exclude_paths;

        auto auth_middleware = std::make_shared<common::http::AuthMiddleware>(auth_config);
        manager.use(auth_middleware);
    }

    // 日志中间件
    auto logging_middleware = std::make_shared<common::http::LoggingMiddleware>();
    manager.use(logging_middleware);
}
```

### 4. 在请求处理中获取用户信息

```cpp
void handleGetUser(const HttpRequest& req, HttpResponse& res) {
    // 从上下文获取用户信息
    std::string user_id = req.getAttribute("user_id");
    std::string username = req.getAttribute("username");

    if (user_id.empty()) {
        res.unauthorized("未认证");
        return;
    }

    // 处理业务逻辑...
}
```

## API 响应格式

### 成功响应

```json
{
    "success": true,
    "data": { ... }
}
```

### 认证失败响应

```json
{
    "success": false,
    "error": "INVALID_TOKEN",
    "message": "签名验证失败"
}
```

### 错误代码

| 错误代码 | 说明 |
|---------|------|
| `MISSING_AUTH_HEADER` | 缺少 Authorization 头 |
| `INVALID_AUTH_FORMAT` | 认证格式无效 |
| `INVALID_SIGNATURE` | 签名验证失败 |
| `TOKEN_EXPIRED` | Token 已过期 |
| `TOKEN_NOT_ACTIVE` | Token 尚未生效 |
| `INVALID_ISSUER` | 发行者不匹配 |
| `INVALID_AUDIENCE` | 受众不匹配 |
| `MISSING_USER_ID` | Token 缺少用户标识 |
| `TOKEN_REVOKED` | Token 已被撤销 |

## 配置要点

### 密钥管理

1. **开发环境**: 使用默认密钥
2. **生产环境**: 使用环境变量 `JWT_SECRET_KEY`
3. **密钥要求**: 至少 256 位（32 字节）

```bash
# 生成安全密钥
openssl rand -base64 32

# 设置环境变量
export JWT_SECRET_KEY="your-generated-key"
```

### 排除路径

以下路径通常不需要认证：
- `/health` - 健康检查
- `/info` - 服务信息
- `/stats` - 统计信息
- `/api/v1/auth/login` - 登录
- `/api/v1/auth/register` - 注册
- `/api/v1/user/check-username` - 用户名检查
- `/api/v1/user/check-email` - 邮箱检查

## 测试

### 使用 curl 测试

```bash
# 不带 Token（应返回 401）
curl -X GET http://localhost:8082/api/v1/user/usr_123

# 带 Token
curl -X GET http://localhost:8082/api/v1/user/usr_123 \
  -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
```

### 验证中间件是否生效

1. 检查日志中是否有 "JwtValidator 初始化成功"
2. 检查日志中是否有 "认证成功" 消息
3. 检查请求上下文中是否有 `user_id` 属性

## 故障排除

### Token 验证失败

1. 检查密钥是否与 auth_service 一致
2. 检查 issuer 配置是否匹配
3. 检查 Token 是否过期

### 中间件不生效

1. 确认 `auth_middleware.enabled` 为 `true`
2. 确认中间件优先级正确
3. 确认请求路径未被排除

## 相关文件

- `include/common/auth/jwt_validator.h` - JWT 验证器头文件
- `src/common/auth/jwt_validator.cpp` - JWT 验证器实现
- `include/common/http/http_middleware.h` - HTTP 中间件框架
- `src/common/http/http_middleware.cpp` - 中间件实现
