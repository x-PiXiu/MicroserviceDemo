# Nginx 替代 API Gateway 架构迁移指南

## 概述

本文档详细说明如何将游戏微服务架构从使用 API Gateway 切换到使用 Nginx 作为反向代理和负载均衡器。

---

## 1. 架构对比

### 1.1 原架构（使用 API Gateway）

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Client    │────▶│   API Gateway    │────▶│  Microservices  │
└─────────────┘     │   (8081)         │     │  - user_service │
                    │                  │     │  - auth_service │
                    │ - 服务注册/发现   │     │  - game_data    │
                    │ - JWT 验证       │     │  - gomoku       │
                    │ - 限流熔断       │     └─────────────────┘
                    │ - 负载均衡       │
                    │ - 路由转发       │
                    └──────────────────┘
```

### 1.2 新架构（使用 Nginx）

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Client    │────▶│      Nginx       │────▶│  Microservices  │
└─────────────┘     │   (80)           │     │  - user_service │
                    │                  │     │  - auth_service │
                    │ - 反向代理       │     │  - game_data    │
                    │ - 负载均衡       │     │  - gomoku       │
                    │ - 限流           │     └─────────────────┘
                    │ - WebSocket      │
                    │ - SSL 终止       │
                    └──────────────────┘
```

### 1.3 功能对比

| 功能 | API Gateway | Nginx | 说明 |
|------|-------------|-------|------|
| 路由转发 | ✅ | ✅ | Nginx 通过 location 配置 |
| 负载均衡 | ✅ 动态 | ✅ 静态 | Nginx 需手动配置 upstream |
| 服务发现 | ✅ 自动 | ❌ | 需依赖外部服务发现 |
| JWT 验证 | ✅ 内置 | ❌ | 需在服务内部实现 |
| 限流 | ✅ | ✅ | Nginx limit_req/limit_conn |
| 熔断 | ✅ | ❌ | 需服务实现或使用第三方模块 |
| WebSocket | ✅ | ✅ | Nginx 原生支持 |
| SSL 终止 | ✅ | ✅ | Nginx 性能更优 |

---

## 2. 需要修改的配置文件

### 2.1 配置文件清单

| 服务 | 配置文件 | 需修改项 |
|------|----------|----------|
| user_service | `config/user_service.yml` | `api_gateway.enabled: false` |
| auth_service | `config/auth_service_config.yml` | `service_discovery.enable: false` |
| game_data_service | `config/game_data_service.yml` | `api_gateway.enabled: false` |
| gomoku_server | `config/gomoku_server.yml` | `api_gateway.enabled: false` |

### 2.2 具体修改内容

#### user_service.yml

```yaml
# 修改前
api_gateway:
  enabled: true
  url: "http://172.26.26.199:8081"
  ...

# 修改后
api_gateway:
  enabled: false  # 禁用 API Gateway 注册
  url: "http://172.26.26.199:8081"
  ...
```

#### auth_service_config.yml

```yaml
# 修改前
service_discovery:
  enable: true
  api_gateway:
    host: "172.26.26.199"
    port: 8081
    ...

# 修改后
service_discovery:
  enable: false  # 禁用服务发现
  api_gateway:
    host: "172.26.26.199"
    port: 8081
    ...
```

#### game_data_service.yml

```yaml
# 修改前
api_gateway:
  enabled: true
  ...

# 修改后
api_gateway:
  enabled: false  # 禁用 API Gateway 注册
  ...
```

#### gomoku_server.yml

```yaml
# 修改前
api_gateway:
  enabled: true
  url: "http://172.26.26.199:8081"
  ...

# 修改后
api_gateway:
  enabled: false  # 禁用 API Gateway 注册
  url: "http://172.26.26.199:8081"
  ...
```

---

## 3. Nginx 配置详解

### 3.1 完整配置文件

配置文件位置: `deploy/nginx/nginx.conf`

```nginx
# ================================================================================
# 游戏微服务 Nginx 反向代理配置
# ================================================================================

worker_processes auto;
error_log /var/log/nginx/error.log warn;
pid /var/run/nginx.pid;

events {
    worker_connections 4096;
    use epoll;
    multi_accept on;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;

    # 日志格式（包含上游响应时间）
    log_format main '$remote_addr - $remote_user [$time_local] "$request" '
                    '$status $body_bytes_sent "$http_referer" '
                    '"$http_user_agent" "$http_x_forwarded_for" '
                    'rt=$request_time uct="$upstream_connect_time" '
                    'uht="$upstream_header_time" urt="$upstream_response_time"';

    access_log /var/log/nginx/access.log main;

    # 性能优化
    sendfile on;
    tcp_nopush on;
    tcp_nodelay on;
    keepalive_timeout 65;
    types_hash_max_size 2048;

    # ============================================================
    # 上游服务器定义（Upstream）
    # ============================================================

    upstream user_service {
        server 127.0.0.1:8082 weight=1 max_fails=3 fail_timeout=30s;
        keepalive 32;  # 保持连接池
    }

    upstream auth_service {
        server 127.0.0.1:8083 weight=1 max_fails=3 fail_timeout=30s;
        keepalive 32;
    }

    upstream game_data_service {
        server 127.0.0.1:8084 weight=1 max_fails=3 fail_timeout=30s;
        keepalive 32;
    }

    upstream gomoku_server {
        server 127.0.0.1:8085 weight=1 max_fails=3 fail_timeout=30s;
        keepalive 16;
    }

    # ============================================================
    # 限流配置
    # ============================================================

    # 基于 IP 的请求速率限制
    limit_req_zone $binary_remote_addr zone=api_limit:10m rate=100r/s;

    # 基于 IP 的连接数限制
    limit_conn_zone $binary_remote_addr zone=conn_limit:10m;

    # ============================================================
    # 主服务器配置
    # ============================================================

    server {
        listen 80;
        server_name localhost;

        # 安全头
        add_header X-Frame-Options "SAMEORIGIN" always;
        add_header X-Content-Type-Options "nosniff" always;
        add_header X-XSS-Protection "1; mode=block" always;

        # 全局健康检查
        location = /health {
            access_log off;
            return 200 '{"status":"healthy","timestamp":"$time_iso8601"}';
            add_header Content-Type application/json;
        }

        # 用户服务路由
        location /api/v1/user/ {
            limit_req zone=api_limit burst=50 nodelay;
            limit_conn conn_limit 20;

            proxy_pass http://user_service/api/v1/user/;
            proxy_http_version 1.1;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
            proxy_set_header Connection "";

            proxy_connect_timeout 10s;
            proxy_send_timeout 60s;
            proxy_read_timeout 60s;

            proxy_buffering on;
            proxy_buffer_size 4k;
            proxy_buffers 8 16k;
        }

        # 认证服务路由
        location /api/v1/auth/ {
            limit_req zone=api_limit burst=30 nodelay;
            limit_conn conn_limit 10;

            proxy_pass http://auth_service/api/v1/auth/;
            proxy_http_version 1.1;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
            proxy_set_header Connection "";

            proxy_connect_timeout 10s;
            proxy_send_timeout 30s;
            proxy_read_timeout 30s;
        }

        # 游戏数据服务路由
        location /api/v1/game/ {
            limit_req zone=api_limit burst=100 nodelay;
            limit_conn conn_limit 50;

            proxy_pass http://game_data_service/api/v1/game/;
            proxy_http_version 1.1;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
            proxy_set_header Connection "";

            proxy_connect_timeout 10s;
            proxy_send_timeout 60s;
            proxy_read_timeout 60s;
        }

        # 五子棋 WebSocket 路由
        location /ws/gomoku/ {
            proxy_pass http://gomoku_server/ws/gomoku/;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

            # WebSocket 超时配置（1小时）
            proxy_connect_timeout 60s;
            proxy_send_timeout 3600s;
            proxy_read_timeout 3600s;

            # 禁用缓冲以支持实时通信
            proxy_buffering off;
        }

        # 五子棋 HTTP API
        location /api/v1/gomoku/ {
            proxy_pass http://gomoku_server/api/v1/gomoku/;
            proxy_http_version 1.1;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header Connection "";

            proxy_connect_timeout 10s;
            proxy_send_timeout 30s;
            proxy_read_timeout 30s;
        }

        # 错误页面
        error_page 500 502 503 504 /50x.html;
        location = /50x.html {
            root /usr/share/nginx/html;
            internal;
        }
    }

    # 状态监控端点（仅内网访问）
    server {
        listen 127.0.0.1:8080;
        server_name localhost;

        location /nginx_status {
            stub_status on;
            access_log off;
        }
    }
}
```

### 3.2 关键配置说明

#### Upstream 配置

```nginx
upstream user_service {
    server 127.0.0.1:8082 weight=1 max_fails=3 fail_timeout=30s;
    keepalive 32;
}
```

- `weight`: 权重，用于负载均衡
- `max_fails`: 最大失败次数
- `fail_timeout`: 失败后暂停时间
- `keepalive`: 保持的空闲连接数

#### WebSocket 代理

```nginx
location /ws/gomoku/ {
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_buffering off;
    proxy_read_timeout 3600s;
}
```

关键点：
- 必须设置 `Upgrade` 和 `Connection` 头
- 禁用 `proxy_buffering`
- 设置足够长的超时时间

#### 限流配置

```nginx
limit_req_zone $binary_remote_addr zone=api_limit:10m rate=100r/s;

location /api/v1/user/ {
    limit_req zone=api_limit burst=50 nodelay;
}
```

- `rate=100r/s`: 每秒 100 个请求
- `burst=50`: 允许突发 50 个请求
- `nodelay`: 突发请求不延迟处理

---

## 4. 服务代码修改

### 4.1 使 API Gateway 注册变为可选

需要修改各服务的启动代码，使其在 `api_gateway.enabled: false` 时不尝试注册。

#### 修改位置

以 `user_service` 为例，需要检查以下文件中的服务注册逻辑：

```
src/core_services/user_service/src/user_service.cpp
```

#### 修改示例

```cpp
// 在服务启动时检查配置
void UserService::initialize() {
    // ... 其他初始化 ...

    // 检查是否启用 API Gateway 注册
    auto apiGatewayConfig = config_manager_->getApiGatewayConfig();
    if (apiGatewayConfig.enabled) {
        // 仅在启用时才进行注册
        if (!registerToApiGateway()) {
            LOG_WARN("API Gateway 注册失败，但服务将继续运行");
        }
    } else {
        LOG_INFO("API Gateway 注册已禁用，跳过服务注册");
    }

    // ... 继续其他初始化 ...
}
```

### 4.2 JWT 验证迁移

由于 Nginx 不支持 JWT 验证，需要在各服务内部实现 JWT 验证逻辑。

#### 方案 1: 在服务内部验证

```cpp
// 在请求处理中间件中添加 JWT 验证
bool AuthMiddleware::validateToken(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwt_secret_})
            .with_issuer("game-microservices-auth");

        verifier.verify(decoded);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("JWT 验证失败: {}", e.what());
        return false;
    }
}
```

#### 方案 2: 调用 auth_service 验证

```cpp
// 通过 auth_service 的验证端点验证 token
bool validateTokenViaAuthService(const std::string& token) {
    auto response = httpClient_.get(
        "http://auth_service:8083/api/v1/auth/validate",
        {{"Authorization", "Bearer " + token}}
    );
    return response.statusCode == 200;
}
```

---

## 5. 部署步骤

### 5.1 准备工作

1. **安装 Nginx**

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install nginx

# CentOS/RHEL
sudo yum install epel-release
sudo yum install nginx
```

2. **备份现有配置**

```bash
sudo cp /etc/nginx/nginx.conf /etc/nginx/nginx.conf.backup
```

### 5.2 部署 Nginx 配置

```bash
# 复制配置文件
sudo cp deploy/nginx/nginx.conf /etc/nginx/nginx.conf

# 测试配置语法
sudo nginx -t

# 重载配置
sudo nginx -s reload
```

### 5.3 修改服务配置

修改各服务的配置文件，设置 `api_gateway.enabled: false`：

```bash
# 修改 user_service.yml
sed -i 's/enabled: true/enabled: false/' config/user_service.yml

# 修改 auth_service_config.yml
sed -i 's/enable: true/enable: false/' config/auth_service_config.yml

# 修改 game_data_service.yml
sed -i 's/enabled: true/enabled: false/' config/game_data_service.yml

# 修改 gomoku_server.yml
sed -i 's/enabled: true/enabled: false/' config/gomoku_server.yml
```

### 5.4 重启服务

```bash
# 停止 API Gateway
pkill -f api_gateway

# 重启其他服务
./restart_all_services.sh
```

### 5.5 验证部署

```bash
# 检查 Nginx 状态
sudo systemctl status nginx

# 测试健康检查
curl http://localhost/health

# 测试服务路由
curl http://localhost/api/v1/user/health
curl http://localhost/api/v1/auth/health
curl http://localhost/api/v1/game/health

# 测试 WebSocket（使用 wscat）
wscat -c ws://localhost/ws/gomoku/
```

---

## 6. 监控与运维

### 6.1 Nginx 状态监控

```bash
# 访问 Nginx 状态页面
curl http://127.0.0.1:8080/nginx_status
```

输出示例：
```
Active connections: 10
server accepts handled requests
 1000 1000 5000
Reading: 0 Writing: 5 Waiting: 5
```

### 6.2 日志查看

```bash
# 访问日志
tail -f /var/log/nginx/access.log

# 错误日志
tail -f /var/log/nginx/error.log
```

### 6.3 性能调优

```nginx
# nginx.conf 中的性能参数

worker_processes auto;        # 自动匹配 CPU 核心数
worker_connections 4096;      # 每个 worker 的最大连接数
keepalive_timeout 65;         # Keep-alive 超时
sendfile on;                  # 启用零拷贝
tcp_nopush on;                # 优化数据包发送
tcp_nodelay on;               # 禁用 Nagle 算法
```

---

## 7. 注意事项

### 7.1 功能差异

| 功能 | 原方案 | 新方案 | 处理方式 |
|------|--------|--------|----------|
| 服务发现 | 动态注册 | 静态配置 | 手动维护 upstream |
| JWT 验证 | 网关统一 | 服务内部 | 各服务自行验证 |
| 熔断器 | 网关实现 | 无 | 服务实现或移除 |
| 服务健康检查 | 自动 | 手动 | Nginx passive check |

### 7.2 安全考虑

1. **JWT 验证**: 确保 auth_service 的 JWT 密钥与其他服务共享
2. **限流**: Nginx 限流基于 IP，可能需要调整阈值
3. **CORS**: Nginx 需要配置 CORS 头（如果需要）

### 7.3 扩展性

如需动态服务发现，可考虑：
- **Nginx Plus**: 商业版本支持动态 upstream
- **Consul Template**: 动态生成 Nginx 配置
- **OpenResty**: 使用 Lua 脚本实现动态功能

---

## 8. 回滚方案

如需回滚到 API Gateway：

```bash
# 1. 恢复服务配置
sed -i 's/enabled: false/enabled: true/' config/*.yml
sed -i 's/enable: false/enable: true/' config/*.yml

# 2. 启动 API Gateway
./start_api_gateway.sh

# 3. 重启所有服务
./restart_all_services.sh

# 4. 停止 Nginx（可选）
sudo systemctl stop nginx
```

---

## 9. 总结

| 项目 | 说明 |
|------|------|
| 主要变更 | 禁用服务注册、使用 Nginx 代理 |
| 配置修改 | 4 个配置文件 |
| 代码修改 | 可选（如果服务已支持禁用注册） |
| 新增文件 | `deploy/nginx/nginx.conf` |
| 优点 | 部署简单、性能更好、成熟稳定 |
| 缺点 | 无动态服务发现、需手动维护配置 |
