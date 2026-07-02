# Service Registry 服务新增实施方案

## 文档信息

- **创建日期**: 2025-02-19
- **版本**: 2.0.0
- **状态**: 待确认
- **作者**: Claude Code
- **策略**: 新增服务（非裁剪现有服务）

---

## 一、执行摘要

### 1.1 项目背景

当前架构中，`api_gateway` 服务承担了过多职责：
- 服务注册与发现
- HTTP 请求代理转发
- 负载均衡
- 限流熔断
- 路由管理

**问题**：职责混杂，难以独立扩展，与 Nginx 功能重叠。

### 1.2 解决方案

**新增**一个轻量级的 `service_registry` 服务，专门负责服务注册与发现：

| 对比项 | 现有 api_gateway | 新增 service_registry |
|--------|-----------------|----------------------|
| 策略 | 保留不动 | **全新开发** |
| 职责 | 全功能网关 | 仅服务注册/发现 |
| 存储 | 内存 | Redis（持久化） |
| 状态 | 有状态 | 无状态 |
| 扩展 | 困难 | 容易（水平扩展） |

### 1.3 为什么选择"新增"而非"裁剪"

| 对比维度 | 裁剪现有服务 | 新增独立服务 |
|---------|-------------|-------------|
| **风险** | 高（可能引入回归 Bug） | **低**（不影响现有服务） |
| **代码质量** | 中（历史包袱） | **高**（全新设计） |
| **实际工作量** | 中（需处理隐式依赖） | **中**（可复用部分逻辑） |
| **回滚难度** | 高 | **低**（老服务保留） |
| **迁移策略** | 大爆炸式切换 | **灰度迁移** |

### 1.4 预期收益

1. **零风险迁移**：新老服务并存，可随时回滚
2. **架构清晰**：职责单一，易于理解和维护
3. **高可用**：无状态设计 + Redis 存储，支持水平扩展
4. **渐进式**：灰度迁移，逐步切换流量

---

## 二、整体架构设计

### 2.1 目标架构图

```
                                    ┌─────────────────────────────────────────────┐
                                    │                  客户端                      │
                                    └────────────────────┬────────────────────────┘
                                                         │
                          ┌──────────────────────────────┼──────────────────────────────┐
                          │                              │                              │
                          │ 1. 获取游戏服务列表          │                              │ 2. 游戏业务请求
                          ▼                              │                              ▼
         ┌────────────────────────────────┐              │              ┌────────────────────────────────┐
         │   Service Registry (新增)       │              │              │          Nginx (80/443)        │
         │          端口: 8081             │              │              │                                │
         │                                │              │              │  - 反向代理                     │
         │  ┌──────────────────────────┐  │              │              │  - 负载均衡                     │
         │  │    核心功能（全新实现）    │  │              │              │  - 限流熔断                     │
         │  │  - registerService()     │  │              │              │  - SSL 终结                     │
         │  │  - deregisterService()   │  │              │              │  - WebSocket 代理               │
         │  │  - updateHeartbeat()     │  │              │              └───────────────┬────────────────┘
         │  │  - getServices()         │  │              │                              │
         │  │  - getServicesByType()   │  │              │                              │
         │  └───────────┬──────────────┘  │              │                              │
         │              │                 │              │                              │
         │              ▼                 │              │              ┌──────────────┴──────────────┐
         │  ┌──────────────────────────┐  │              │              │                              │
         │  │   RedisServiceStorage    │  │              │              ▼                              ▼
         │  │   (Redis 存储抽象层)      │  │              │    ┌─────────────────┐          ┌─────────────────┐
         │  └───────────┬──────────────┘  │              │    │  Gomoku Server  │          │  其他游戏服务    │
         │              │                 │              │    │    (8085)       │          │    (808x)       │
         └──────────────┼─────────────────┘              │    └────────┬────────┘          └────────┬────────┘
                        │                                │             │                            │
                        ▼                                │             │ 心跳                       │ 心跳
         ┌────────────────────────────────┐              │             │                            │
         │          Redis 集群            │◄─────────────┴─────────────┘                            │
         │                                │                                                       │
         │  - 服务实例数据 (TTL自动过期)   │◄──────────────────────────────────────────────────────┘
         │  - 索引数据                    │
         │  - Pub/Sub 事件通道            │
         └────────────────────────────────┘
```

### 2.2 服务职责划分

```
┌────────────────────────────────────────────────────────────────────────────────┐
│                              服务职责矩阵                                        │
├─────────────────┬──────────────────┬──────────────────┬────────────────────────┤
│     功能        │  api_gateway     │ service_registry │        Nginx           │
│                 │    (保留)        │    (新增)        │       (保留)           │
├─────────────────┼──────────────────┼──────────────────┼────────────────────────┤
│ 服务注册        │       ✅         │       ✅         │         ❌             │
│ 服务发现        │       ✅         │       ✅         │         ❌             │
│ 心跳处理        │       ✅         │       ✅         │         ❌             │
│ 健康检查        │       ✅         │       ✅         │        被动            │
├─────────────────┼──────────────────┼──────────────────┼────────────────────────┤
│ HTTP 代理       │       ✅         │       ❌         │         ✅             │
│ 负载均衡        │       ✅         │       ❌         │         ✅             │
│ 限流熔断        │       ✅         │       ❌         │         ✅             │
│ SSL 终结        │       ❌         │       ❌         │         ✅             │
├─────────────────┼──────────────────┼──────────────────┼────────────────────────┤
│ 数据存储        │     内存         │     Redis        │         -             │
│ 水平扩展        │      困难        │      容易        │        容易            │
│ 状态            │     有状态       │     无状态       │        无状态          │
└─────────────────┴──────────────────┴──────────────────┴────────────────────────┘
```

### 2.3 迁移后的最终架构

迁移完成后，`api_gateway` 可选保留（作为备份）或逐步下线：

```
                                    ┌─────────────────────────────────────┐
                                    │              客户端                  │
                                    └─────────────────┬───────────────────┘
                                                      │
                                         ┌────────────┴────────────┐
                                         │                         │
                          ┌──────────────┴──────────┐   ┌─────────┴──────────────┐
                          │   Service Registry      │   │       Nginx            │
                          │   (主力服务发现)         │   │   (流量入口 + 代理)    │
                          │   端口: 8081            │   │   端口: 80/443         │
                          └───────────┬─────────────┘   └───────────┬────────────┘
                                      │                             │
                                      ▼                             ▼
                          ┌───────────────────────┐     ┌───────────────────────┐
                          │      Redis Cluster    │     │    游戏服务集群        │
                          │   (服务注册数据)       │     │  (Gomoku, Snake...)   │
                          └───────────────────────┘     └───────────────────────┘

                          ┌───────────────────────┐
                          │   api_gateway         │  ← 可选保留作为热备份
                          │   (已下线/备份)        │     或完全移除
                          └───────────────────────┘
```

---

## 三、新服务目录结构

### 3.1 目录规划

```
src/core_services/service_registry/           # 全新目录
├── CMakeLists.txt                             # 独立构建配置
├── config/
│   └── service_registry.yml                   # 服务配置文件
├── include/
│   ├── service_registry.h                     # 主服务类（~150行）
│   ├── redis_storage.h                        # Redis 存储层（~100行）
│   ├── registry_handler.h                     # API 处理器（~80行）
│   └── service_info.h                         # 数据结构定义（~60行）
├── src/
│   ├── main.cpp                               # 程序入口（~50行）
│   ├── service_registry.cpp                   # 主服务实现（~250行）
│   ├── redis_storage.cpp                      # Redis 存储实现（~350行）
│   └── registry_handler.cpp                   # API 处理实现（~300行）
└── tests/
    ├── unit/
    │   ├── redis_storage_test.cpp             # Redis 存储单元测试
    │   └── service_registry_test.cpp          # 服务注册单元测试
    └── integration/
        └── full_flow_test.cpp                 # 完整流程集成测试
```

### 3.2 代码量预估

| 模块 | 头文件 | 实现文件 | 总计 |
|------|--------|---------|------|
| 数据结构 | 60行 | - | 60行 |
| Redis 存储层 | 100行 | 350行 | 450行 |
| API 处理器 | 80行 | 300行 | 380行 |
| 主服务类 | 150行 | 250行 | 400行 |
| 入口 | - | 50行 | 50行 |
| **总计** | **390行** | **950行** | **~1340行** |

对比现有 `api_gateway` 约 5000 行，新服务代码量减少 **73%**。

---

## 四、核心接口设计

### 4.1 API 端点

```
# 服务注册相关
POST   /api/v1/services/register           # 注册服务实例
POST   /api/v1/services/heartbeat          # 更新心跳（含 metadata）
DELETE /api/v1/services/deregister         # 注销服务实例

# 服务查询相关
GET    /api/v1/services                    # 获取所有服务列表
GET    /api/v1/services?game_type=xxx      # 按游戏类型筛选
GET    /api/v1/services?healthy=true       # 仅返回健康服务
GET    /api/v1/services/{service_name}     # 获取特定服务详情
GET    /api/v1/services/stats              # 获取统计信息

# 健康检查
GET    /api/v1/services/{service_name}/health  # 服务健康状态
GET    /health                             # Registry 自身健康检查
```

### 4.2 服务注册请求格式

```json
{
    "service_name": "gomoku_server",
    "host": "192.168.1.100",
    "port": 8085,
    "service_version": "1.0.0",
    "health_check_endpoint": "/health",
    "weight": 100,
    "metadata": {
        "game_type": "gomoku",
        "current_players": "5",
        "max_players": "100",
        "active_rooms": "3",
        "region": "cn-east",
        "server_status": "online"
    },
    "endpoints": [
        "/api/v1/gomoku/rooms",
        "/api/v1/gomoku/games",
        "/ws/gomoku/"
    ]
}
```

### 4.3 心跳请求格式

```json
{
    "service_name": "gomoku_server",
    "host": "192.168.1.100",
    "port": 8085,
    "status": "healthy",
    "metadata": {
        "current_players": "8",
        "active_rooms": "5",
        "cpu_usage": "35.5",
        "memory_usage": "42.3"
    }
}
```

### 4.4 服务列表响应格式

```json
{
    "success": true,
    "message": "查询成功",
    "timestamp": 1708123456,
    "data": {
        "services": [
            {
                "service_name": "gomoku_server",
                "host": "192.168.1.100",
                "port": 8085,
                "healthy": true,
                "health_score": 0.95,
                "last_heartbeat": 1708123450,
                "metadata": {
                    "game_type": "gomoku",
                    "current_players": "8",
                    "max_players": "100"
                },
                "endpoints": ["/api/v1/gomoku/rooms", "/ws/gomoku/"]
            }
        ],
        "count": 1
    }
}
```

---

## 五、Redis 数据结构设计

### 5.1 Key 设计规范

```
# 命名规范
service:{namespace}:{type}:{identifier}

# 示例
service:registry:gomoku_server:192.168.1.100:8085   # 服务实例数据
service:index:names                                  # 服务名称索引
service:index:gomoku_server:instances               # 服务实例列表
service:index:game_type:gomoku                      # 按游戏类型索引
```

### 5.2 详细数据结构

```redis
# ==================== 服务实例存储 ====================
# Key: service:registry:{service_name}:{host}:{port}
# Type: Hash
# TTL: 60秒（心跳更新时重置）

service:registry:gomoku_server:192.168.1.100:8085 = {
    "service_name": "gomoku_server",
    "service_version": "1.0.0",
    "host": "192.168.1.100",
    "port": "8085",
    "health_check_endpoint": "/health",
    "weight": "100",
    "healthy": "true",
    "health_score": "0.95",
    "last_heartbeat": "1708123456789",
    "register_time": "1708120000000",
    "metadata": "{\"game_type\":\"gomoku\",\"current_players\":\"8\",...}",
    "endpoints": "[\"/api/v1/gomoku/rooms\",\"/ws/gomoku/\"]"
}

# ==================== 索引数据 ====================

# 所有服务名称（集合）
service:index:names = {"gomoku_server", "snake_server", "user_service", ...}

# 特定服务的实例列表（集合）
service:index:gomoku_server:instances = {"192.168.1.100:8085", "192.168.1.101:8085"}

# 按游戏类型索引（集合）
service:index:game_type:gomoku = {"gomoku_server:192.168.1.100:8085", ...}
service:index:game_type:snake = {"snake_server:192.168.1.102:8087", ...}

# ==================== 健康检查历史（可选）====================
# Type: Sorted Set (score = timestamp)
service:health:gomoku_server:192.168.1.100:8085 = {
    1708123456789: "healthy:0.95",
    1708123426789: "healthy:0.92",
    ...
}

# ==================== 事件通知（可选）====================
# Channel: service:events
# 用于发布服务变更事件
PUBLISH service:events '{"type":"register","service":"gomoku_server","instance":"192.168.1.100:8085"}'
```

### 5.3 Redis 操作流程

```
服务注册流程：
1. HSET service:registry:{name}:{host}:{port} {fields}
2. EXPIRE service:registry:{name}:{host}:{port} 60
3. SADD service:index:names {name}
4. SADD service:index:{name}:instances {host}:{port}
5. SADD service:index:game_type:{type} {name}:{host}:{port}  (如果有 game_type)
6. PUBLISH service:events {"type":"register",...}

心跳更新流程：
1. EXISTS service:registry:{name}:{host}:{port}
2. HMSET service:registry:{name}:{host}:{port} last_heartbeat {ts} metadata {json}
3. EXPIRE service:registry:{name}:{host}:{port} 60

服务查询流程：
1. SMEMBERS service:index:{name}:instances
2. For each instance: HGETALL service:registry:{name}:{instance}

按游戏类型查询：
1. SMEMBERS service:index:game_type:{type}
2. For each key: HGETALL service:registry:{parsed_name}:{parsed_instance}
```

---

## 六、核心代码设计

### 6.1 数据结构 (service_info.h)

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace core_services {
namespace service_registry {

/**
 * @brief 服务实例信息
 */
struct ServiceInfo {
    // 基本信息
    std::string service_name;
    std::string service_version;
    std::string host;
    int port = 0;

    // 健康检查
    std::string health_check_endpoint = "/health";
    bool healthy = true;
    double health_score = 1.0;

    // 负载均衡
    int weight = 100;

    // 时间戳
    std::chrono::system_clock::time_point last_heartbeat;
    std::chrono::system_clock::time_point register_time;

    // 扩展数据
    std::vector<std::string> endpoints;
    std::unordered_map<std::string, std::string> metadata;

    // 生成实例唯一标识
    std::string instanceId() const {
        return host + ":" + std::to_string(port);
    }

    // 生成 Redis Key
    std::string redisKey() const {
        return "service:registry:" + service_name + ":" + instanceId();
    }
};

/**
 * @brief 服务查询过滤器
 */
struct ServiceFilter {
    std::string service_name;       // 服务名称（可选）
    std::string game_type;          // 游戏类型（可选）
    bool healthy_only = false;      // 仅健康服务
    int min_health_score = 0;       // 最低健康分数
};

} // namespace service_registry
} // namespace core_services
```

### 6.2 Redis 存储层 (redis_storage.h)

```cpp
#pragma once

#include "service_info.h"
#include "common/database/redis_pool.h"
#include <memory>
#include <vector>
#include <chrono>

namespace core_services {
namespace service_registry {

/**
 * @brief Redis 服务存储配置
 */
struct RedisStorageConfig {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::string password;
    int database = 0;
    int pool_size = 10;
    int connection_timeout_ms = 5000;

    // TTL 配置
    int service_ttl_seconds = 60;           // 服务实例 TTL
    int health_history_retention = 3600;    // 健康历史保留时间
};

/**
 * @brief Redis 服务存储层
 * @details 封装所有 Redis 操作，提供服务注册/发现的数据持久化
 */
class RedisStorage {
public:
    explicit RedisStorage(const RedisStorageConfig& config);
    ~RedisStorage();

    // 初始化连接
    bool initialize();
    bool isHealthy() const;

    // ==================== 服务注册 ====================

    /// 注册服务实例
    bool registerService(const ServiceInfo& info);

    /// 注销服务实例
    bool deregisterService(const std::string& service_name,
                          const std::string& host, int port);

    /// 更新心跳（同时更新 metadata）
    bool updateHeartbeat(const std::string& service_name,
                        const std::string& host, int port,
                        const std::unordered_map<std::string, std::string>& metadata);

    // ==================== 服务查询 ====================

    /// 获取所有服务
    std::vector<ServiceInfo> getAllServices(bool healthy_only = false);

    /// 获取特定服务的所有实例
    std::vector<ServiceInfo> getServices(const std::string& service_name,
                                         bool healthy_only = false);

    /// 按过滤器查询
    std::vector<ServiceInfo> queryServices(const ServiceFilter& filter);

    /// 获取单个服务实例
    std::optional<ServiceInfo> getService(const std::string& service_name,
                                          const std::string& host, int port);

    // ==================== 索引维护 ====================

    /// 获取所有服务名称
    std::vector<std::string> getAllServiceNames();

    /// 获取服务的实例 ID 列表
    std::vector<std::string> getServiceInstances(const std::string& service_name);

    // ==================== 统计信息 ====================

    /// 获取服务统计
    struct Stats {
        size_t total_services;
        size_t total_instances;
        size_t healthy_instances;
        size_t unhealthy_instances;
    };
    Stats getStats();

private:
    RedisStorageConfig config_;
    std::shared_ptr<RedisPool> pool_;

    // 内部方法
    bool setServiceData(const ServiceInfo& info);
    bool updateIndexes(const ServiceInfo& info, bool is_register);
    bool removeFromIndexes(const std::string& service_name,
                          const std::string& instance_id);
    ServiceInfo parseServiceData(const std::map<std::string, std::string>& data);
    std::string serializeMetadata(const std::unordered_map<std::string, std::string>& metadata);
    std::unordered_map<std::string, std::string> deserializeMetadata(const std::string& json);
};

} // namespace service_registry
} // namespace core_services
```

### 6.3 API 处理器 (registry_handler.h)

```cpp
#pragma once

#include "service_info.h"
#include "redis_storage.h"
#include "common/http/http_server.h"
#include <memory>

namespace core_services {
namespace service_registry {

/**
 * @brief 服务注册 API 处理器
 */
class RegistryHandler {
public:
    explicit RegistryHandler(std::shared_ptr<RedisStorage> storage);

    /// 注册所有路由到 HTTP 服务器
    void registerRoutes(common::http::HttpServer* server);

private:
    std::shared_ptr<RedisStorage> storage_;

    // 路由处理函数
    void handleRegister(const common::http::HttpRequest& req,
                       common::http::HttpResponse& res);
    void handleDeregister(const common::http::HttpRequest& req,
                         common::http::HttpResponse& res);
    void handleHeartbeat(const common::http::HttpRequest& req,
                        common::http::HttpResponse& res);
    void handleListServices(const common::http::HttpRequest& req,
                           common::http::HttpResponse& res);
    void handleGetService(const common::http::HttpRequest& req,
                         common::http::HttpResponse& res);
    void handleGetStats(const common::http::HttpRequest& req,
                       common::http::HttpResponse& res);
    void handleHealthCheck(const common::http::HttpRequest& req,
                          common::http::HttpResponse& res);

    // 响应构建
    void sendSuccess(common::http::HttpResponse& res,
                     const std::string& message,
                     const nlohmann::json& data = {});
    void sendError(common::http::HttpResponse& res,
                   int status_code,
                   const std::string& error_code,
                   const std::string& message);

    // CORS 处理
    void setCorsHeaders(common::http::HttpResponse& res);

    // JSON 解析
    std::optional<ServiceInfo> parseServiceInfo(const std::string& json_body);
    ServiceInfo toJson(const ServiceInfo& info);
};

} // namespace service_registry
} // namespace core_services
```

### 6.4 主服务类 (service_registry.h)

```cpp
#pragma once

#include "redis_storage.h"
#include "registry_handler.h"
#include "common/http/http_server.h"
#include "common/config/config_manager.h"
#include <memory>
#include <atomic>
#include <thread>

namespace core_services {
namespace service_registry {

/**
 * @brief Service Registry 配置
 */
struct ServiceRegistryConfig {
    // HTTP 服务器配置
    std::string host = "0.0.0.0";
    int port = 8081;
    int thread_count = 4;

    // Redis 配置
    RedisStorageConfig redis;

    // 业务配置
    int heartbeat_timeout_seconds = 60;
    int cleanup_interval_seconds = 30;

    // 从配置文件加载
    static ServiceRegistryConfig fromFile(const std::string& config_path);
    bool validate() const;
};

/**
 * @brief Service Registry 主服务
 * @details 轻量级服务注册中心，提供服务的注册、发现、健康检查功能
 */
class ServiceRegistry {
public:
    explicit ServiceRegistry(const ServiceRegistryConfig& config);
    ~ServiceRegistry();

    // 禁止拷贝
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    // 生命周期
    bool initialize();
    void start();
    void stop();

    bool isRunning() const { return running_.load(); }
    int getPort() const { return config_.port; }

private:
    ServiceRegistryConfig config_;
    std::atomic<bool> running_{false};

    // 核心组件
    std::unique_ptr<common::http::HttpServer> http_server_;
    std::shared_ptr<RedisStorage> storage_;
    std::unique_ptr<RegistryHandler> handler_;

    // 清理线程
    std::thread cleanup_thread_;
    void cleanupTask();

    // 健康检查端点
    void handleSelfHealthCheck(const common::http::HttpRequest& req,
                              common::http::HttpResponse& res);
};

} // namespace service_registry
} // namespace core_services
```

---

## 七、灰度迁移计划

### 7.1 迁移阶段总览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           灰度迁移时间线                                      │
└─────────────────────────────────────────────────────────────────────────────┘

阶段0          阶段1           阶段2           阶段3           阶段4
开发测试       灰度上线        流量切换        稳定观察        老服务下线
  │             │               │               │               │
  ▼             ▼               ▼               ▼               ▼
┌─────┐     ┌─────┐        ┌─────┐        ┌─────┐        ┌─────┐
│新建 │────>│10% │────────│50% │────────│100%│────────│下线 │
│服务 │     │流量 │        │流量 │        │流量 │        │老服务│
└─────┘     └─────┘        └─────┘        └─────┘        └─────┘
  2周          1周            1周            1周            随时

              ↑               ↑               ↑               ↑
           新服务         增加流量        全量切换       验证稳定后
           上线测试       观察指标        监控告警       下线老服务
```

### 7.2 详细阶段说明

#### 阶段 0: 开发与测试（2周）

```
目标：完成 service_registry 开发，通过所有测试

任务清单：
├── [ ] 创建项目结构和 CMakeLists.txt
├── [ ] 实现 ServiceInfo 数据结构
├── [ ] 实现 RedisStorage 存储层
│   ├── [ ] registerService
│   ├── [ ] deregisterService
│   ├── [ ] updateHeartbeat
│   ├── [ ] getServices / getAllServices
│   └── [ ] queryServices (按游戏类型等)
├── [ ] 实现 RegistryHandler API 处理器
│   ├── [ ] POST /api/v1/services/register
│   ├── [ ] DELETE /api/v1/services/deregister
│   ├── [ ] POST /api/v1/services/heartbeat
│   └── [ ] GET /api/v1/services
├── [ ] 实现 ServiceRegistry 主服务
├── [ ] 编写单元测试
├── [ ] 编写集成测试
├── [ ] 压力测试（目标：10000 QPS）
└── [ ] 编写部署文档

验收标准：
- 单元测试覆盖率 > 80%
- 所有集成测试通过
- 压力测试满足性能要求
```

#### 阶段 1: 灰度上线（1周）

```
目标：新服务上线，接管 10% 的服务注册流量

部署步骤：
1. 部署 service_registry 到生产环境（端口 8081）
2. 更新 Nginx 配置，添加 service_registry 路由
3. 修改一个游戏服务（如 snake_server）的心跳目标
4. 验证数据正确写入 Redis

Nginx 配置变更：
upstream service_registry {
    server 127.0.0.1:8081;
}

# 新增路由
location /api/v1/services {
    proxy_pass http://service_registry/api/v1/services;
}

监控指标：
- service_registry 的 QPS
- Redis 连接数和响应时间
- 服务注册成功率
- 心跳更新成功率

回滚方案：
- 如果发现问题，立即将 snake_server 的心跳目标改回 api_gateway
- service_registry 可以随时停止，不影响其他服务
```

#### 阶段 2: 增加流量（1周）

```
目标：50% 的服务使用新的 service_registry

任务：
1. 修改更多游戏服务的心跳目标
2. 观察系统稳定性
3. 收集性能指标

流量分配：
- gomoku_server → service_registry
- snake_server → service_registry
- 其他服务 → api_gateway (暂时)

验证检查：
- [ ] 两套系统的数据一致性
- [ ] 客户端能否正确获取服务列表
- [ ] 心跳更新是否正常
- [ ] 无错误日志
```

#### 阶段 3: 全量切换（1周）

```
目标：100% 流量切换到 service_registry

任务：
1. 所有服务切换到 service_registry
2. 更新客户端的服务发现地址
3. 密切监控

切换步骤：
1. 修改所有游戏服务的配置
2. 重启游戏服务（或热更新配置）
3. 验证所有服务正确注册
4. 客户端开始从新地址获取服务列表

此时 api_gateway 仍在运行，但不再接收服务注册流量
```

#### 阶段 4: 老服务下线（随时）

```
目标：确认稳定后，下线或保留 api_gateway 的服务注册功能

选项 A: 完全下线
- 停止 api_gateway 服务
- 移除相关配置

选项 B: 保留作为备份
- api_gateway 保持运行
- 可随时切回

选项 C: 裁剪 api_gateway
- 移除 api_gateway 中的服务注册代码
- 保留其他功能（如果有）
```

### 7.3 回滚方案

每个阶段都有清晰的回滚路径：

| 阶段 | 回滚操作 | 预计时间 |
|------|---------|---------|
| 阶段1 | 修改服务配置，指向 api_gateway | < 5分钟 |
| 阶段2 | 批量修改服务配置 | < 15分钟 |
| 阶段3 | 批量修改 + 客户端配置 | < 30分钟 |
| 阶段4 | 重启 api_gateway | < 5分钟 |

---

## 八、Nginx 配置更新

### 8.1 完整配置示例

```nginx
# deploy/nginx/nginx.conf

# ==================== 上游服务定义 ====================

# Service Registry（新增）
upstream service_registry {
    server 127.0.0.1:8081 weight=1 max_fails=3 fail_timeout=30s;
    keepalive 16;
}

# 现有游戏服务（保持不变）
upstream gomoku_server {
    server 127.0.0.1:8085 weight=1 max_fails=3 fail_timeout=30s;
    keepalive 16;
}

upstream snake_server {
    server 127.0.0.1:8087 weight=1 max_fails=3 fail_timeout=30s;
    keepalive 16;
}

# ==================== 服务发现路由（新增）====================

# Service Registry API - 服务注册和发现
location /api/v1/services {
    proxy_pass http://service_registry/api/v1/services;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;

    # 连接设置
    proxy_connect_timeout 5s;
    proxy_send_timeout 10s;
    proxy_read_timeout 10s;

    # 缓存策略（服务列表可短期缓存）
    proxy_cache_valid 200 5s;
}

# ==================== 游戏服务路由（保持不变）====================

# 五子棋服务
location /api/v1/gomoku/ {
    proxy_pass http://gomoku_server/api/v1/gomoku/;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

location /ws/gomoku/ {
    proxy_pass http://gomoku_server/ws/gomoku/;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_read_timeout 3600s;
}

# 贪吃蛇服务
location /api/v1/snake/ {
    proxy_pass http://snake_server/api/v1/snake/;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}

location /ws/snake/ {
    proxy_pass http://snake_server/ws/snake/;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_read_timeout 3600s;
}

# ==================== 限流配置 ====================

# 限流区域
limit_req_zone $binary_remote_addr zone=registry_limit:10m rate=50r/s;

# Service Registry 限流
location /api/v1/services {
    limit_req zone=registry_limit burst=100 nodelay;
    # ... 其他配置
}
```

---

## 九、游戏服务改造

### 9.1 配置变更

```yaml
# config/gomoku_service.yml

service:
  name: gomoku_server
  host: 0.0.0.0
  port: 8085

# 原配置（迁移前）
# api_gateway:
#   url: http://localhost:8081
#   heartbeat_interval: 30

# 新配置（迁移后）
service_registry:
  url: http://localhost:8081
  heartbeat_interval: 30
  registration_retry_count: 5
  heartbeat_timeout_ms: 10000
```

### 9.2 心跳逻辑修改

```cpp
// gomoku_server.cpp

void GomokuServer::startHeartbeat() {
    heartbeat_thread_ = std::thread([this]() {
        common::http::HttpClient client;

        while (running_) {
            try {
                // 构建心跳请求
                nlohmann::json heartbeat = {
                    {"service_name", config_.service_name},
                    {"host", config_.host},
                    {"port", config_.port},
                    {"status", "healthy"},
                    {"metadata", {
                        {"game_type", "gomoku"},
                        {"current_players", std::to_string(getCurrentPlayers())},
                        {"max_players", std::to_string(config_.max_players)},
                        {"active_rooms", std::to_string(getActiveRooms())},
                        {"cpu_usage", formatCpuUsage()},
                        {"memory_usage", formatMemoryUsage()}
                    }}
                };

                // 发送到 Service Registry
                std::string url = config_.service_registry_url + "/api/v1/services/heartbeat";
                std::map<std::string, std::string> headers = {
                    {"Content-Type", "application/json"}
                };

                auto response = client.post(url, heartbeat.dump(), headers, 10000);

                if (response.status_code == 200) {
                    LOG_DEBUG("心跳发送成功");
                } else {
                    LOG_WARNING("心跳发送失败: HTTP " + std::to_string(response.status_code));
                }

            } catch (const std::exception& e) {
                LOG_ERROR("心跳异常: " + std::string(e.what()));
            }

            std::this_thread::sleep_for(std::chrono::seconds(config_.heartbeat_interval));
        }
    });
}
```

---

## 十、测试计划

### 10.1 单元测试

```cpp
// tests/unit/redis_storage_test.cpp

class RedisStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        RedisStorageConfig config;
        config.host = "127.0.0.1";
        config.port = 6379;
        config.database = 15;  // 使用测试数据库
        storage_ = std::make_unique<RedisStorage>(config);
        ASSERT_TRUE(storage_->initialize());
    }

    void TearDown() override {
        // 清理测试数据
    }

    std::unique_ptr<RedisStorage> storage_;
};

TEST_F(RedisStorageTest, RegisterService) {
    ServiceInfo info;
    info.service_name = "test_service";
    info.host = "127.0.0.1";
    info.port = 8080;
    info.metadata["game_type"] = "test";

    ASSERT_TRUE(storage_->registerService(info));

    auto services = storage_->getServices("test_service");
    ASSERT_EQ(services.size(), 1);
    EXPECT_EQ(services[0].host, "127.0.0.1");
    EXPECT_EQ(services[0].port, 8080);
}

TEST_F(RedisStorageTest, HeartbeatUpdatesMetadata) {
    // 先注册
    ServiceInfo info;
    info.service_name = "test_service";
    info.host = "127.0.0.1";
    info.port = 8080;
    storage_->registerService(info);

    // 更新心跳
    std::unordered_map<std::string, std::string> new_metadata = {
        {"current_players", "10"},
        {"game_type", "test"}
    };
    ASSERT_TRUE(storage_->updateHeartbeat("test_service", "127.0.0.1", 8080, new_metadata));

    // 验证
    auto services = storage_->getServices("test_service");
    ASSERT_EQ(services.size(), 1);
    EXPECT_EQ(services[0].metadata["current_players"], "10");
}

TEST_F(RedisStorageTest, QueryByGameType) {
    // 注册多个服务
    ServiceInfo gomoku;
    gomoku.service_name = "gomoku_server";
    gomoku.host = "127.0.0.1";
    gomoku.port = 8085;
    gomoku.metadata["game_type"] = "gomoku";
    storage_->registerService(gomoku);

    ServiceInfo snake;
    snake.service_name = "snake_server";
    snake.host = "127.0.0.1";
    snake.port = 8087;
    snake.metadata["game_type"] = "snake";
    storage_->registerService(snake);

    // 按游戏类型查询
    ServiceFilter filter;
    filter.game_type = "gomoku";

    auto services = storage_->queryServices(filter);
    ASSERT_EQ(services.size(), 1);
    EXPECT_EQ(services[0].service_name, "gomoku_server");
}

TEST_F(RedisStorageTest, DeregisterService) {
    ServiceInfo info;
    info.service_name = "test_service";
    info.host = "127.0.0.1";
    info.port = 8080;
    storage_->registerService(info);

    ASSERT_TRUE(storage_->deregisterService("test_service", "127.0.0.1", 8080));

    auto services = storage_->getServices("test_service");
    EXPECT_EQ(services.size(), 0);
}
```

### 10.2 集成测试

```cpp
// tests/integration/full_flow_test.cpp

class ServiceRegistryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 19081;  // 测试端口
        config_.redis.port = 6379;
        config_.redis.database = 15;

        registry_ = std::make_unique<ServiceRegistry>(config_);
        ASSERT_TRUE(registry_->initialize());
        registry_->start();

        // 等待服务启动
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override {
        registry_->stop();
    }

    ServiceRegistryConfig config_;
    std::unique_ptr<ServiceRegistry> registry_;
};

TEST_F(ServiceRegistryIntegrationTest, FullServiceLifecycle) {
    common::http::HttpClient client;
    std::string base_url = "http://127.0.0.1:19081/api/v1/services";

    // 1. 注册服务
    nlohmann::json register_req = {
        {"service_name", "integration_test"},
        {"host", "127.0.0.1"},
        {"port", 9999},
        {"metadata", {{"game_type", "test"}}}
    };

    auto response = client.post(base_url + "/register", register_req.dump());
    EXPECT_EQ(response.status_code, 200);

    // 2. 查询服务列表
    response = client.get(base_url);
    EXPECT_EQ(response.status_code, 200);

    auto result = nlohmann::json::parse(response.body);
    EXPECT_TRUE(result["success"].get<bool>());
    EXPECT_GT(result["data"]["services"].size(), 0);

    // 3. 发送心跳
    nlohmann::json heartbeat_req = {
        {"service_name", "integration_test"},
        {"host", "127.0.0.1"},
        {"port", 9999},
        {"metadata", {{"current_players", "5"}}}
    };

    response = client.post(base_url + "/heartbeat", heartbeat_req.dump());
    EXPECT_EQ(response.status_code, 200);

    // 4. 验证心跳更新
    response = client.get(base_url + "?service=integration_test");
    result = nlohmann::json::parse(response.body);
    EXPECT_EQ(result["data"]["services"][0]["metadata"]["current_players"], "5");

    // 5. 注销服务
    response = client.del(base_url + "/deregister", register_req.dump());
    EXPECT_EQ(response.status_code, 200);

    // 6. 验证注销
    response = client.get(base_url + "?service=integration_test");
    result = nlohmann::json::parse(response.body);
    EXPECT_EQ(result["data"]["services"].size(), 0);
}
```

### 10.3 压力测试

```cpp
// tests/stress/stress_test.cpp

TEST(StressTest, HighConcurrencyRegistration) {
    const int NUM_THREADS = 10;
    const int REGISTRATIONS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            common::http::HttpClient client;
            for (int i = 0; i < REGISTRATIONS_PER_THREAD; ++i) {
                nlohmann::json req = {
                    {"service_name", "stress_test_" + std::to_string(t)},
                    {"host", "127.0.0.1"},
                    {"port", 8000 + i}
                };

                auto response = client.post(
                    "http://127.0.0.1:8081/api/v1/services/register",
                    req.dump()
                );

                if (response.status_code == 200) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int total_requests = NUM_THREADS * REGISTRATIONS_PER_THREAD;
    double qps = (total_requests * 1000.0) / duration.count();
    double success_rate = (success_count * 100.0) / total_requests;

    std::cout << "总请求: " << total_requests << std::endl;
    std::cout << "成功数: " << success_count << std::endl;
    std::cout << "QPS: " << qps << std::endl;
    std::cout << "成功率: " << success_rate << "%" << std::endl;

    // 验收标准
    EXPECT_GT(qps, 1000);  // QPS > 1000
    EXPECT_GT(success_rate, 99.0);  // 成功率 > 99%
}
```

---

## 十一、监控与告警

### 11.1 关键指标

```
# Prometheus 指标示例

# 服务注册相关
service_registry_registrations_total           # 注册总数
service_registry_registrations_failed_total    # 注册失败数
service_registry_heartbeats_total              # 心跳总数
service_registry_heartbeats_failed_total       # 心跳失败数

# 服务查询相关
service_registry_queries_total                 # 查询总数
service_registry_query_duration_seconds        # 查询延迟

# Redis 相关
service_registry_redis_connections_active      # 活跃连接数
service_registry_redis_commands_total          # Redis 命令数
service_registry_redis_errors_total            # Redis 错误数

# 存储相关
service_registry_services_count                # 已注册服务数
service_registry_instances_count               # 已注册实例数
```

### 11.2 告警规则

```yaml
# alerting_rules.yml

groups:
  - name: service_registry
    rules:
      # 服务注册失败率
      - alert: HighRegistrationFailureRate
        expr: |
          rate(service_registry_registrations_failed_total[5m]) /
          rate(service_registry_registrations_total[5m]) > 0.01
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "服务注册失败率过高"

      # 查询延迟
      - alert: HighQueryLatency
        expr: |
          histogram_quantile(0.99,
            rate(service_registry_query_duration_seconds_bucket[5m])
          ) > 0.1
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "服务查询延迟过高"

      # Redis 连接问题
      - alert: RedisConnectionIssues
        expr: |
          rate(service_registry_redis_errors_total[5m]) > 1
        for: 2m
        labels:
          severity: critical
        annotations:
          summary: "Redis 连接异常"

      # 实例数异常下降
      - alert: InstanceCountDropped
        expr: |
          (service_registry_instances_count - service_registry_instances_count offset 5m) < -10
        for: 2m
        labels:
          severity: critical
        annotations:
          summary: "服务实例数异常下降"
```

---

## 十二、风险评估与缓解

### 12.1 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| Redis 连接池耗尽 | 中 | 高 | 增加连接池，实现连接复用，设置超时 |
| 心跳风暴 | 低 | 中 | 心跳抖动（jitter），错峰发送 |
| 服务列表缓存不一致 | 中 | 低 | 客户端后台刷新，短 TTL |
| 新服务 Bug | 中 | 中 | 灰度发布，保留老服务 |

### 12.2 运维风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| 迁移期间数据不一致 | 中 | 中 | 双写期间保证数据同步 |
| 配置错误 | 中 | 中 | 配置校验，变更审批 |
| 网络分区 | 低 | 高 | 多可用区部署 |

### 12.3 回滚流程

```
发现问题后的回滚步骤：

1. 评估问题严重程度
   - P0: 立即回滚
   - P1: 30分钟内回滚
   - P2: 计划性回滚

2. 执行回滚
   a. 修改游戏服务配置，指向 api_gateway
   b. 重启游戏服务
   c. 停止 service_registry（可选）
   d. 更新 Nginx 配置

3. 验证回滚
   - 检查服务注册状态
   - 验证客户端功能
   - 监控错误日志

4. 问题分析
   - 收集日志和监控数据
   - 定位根因
   - 制定修复方案
```

---

## 十三、项目时间表

### 13.1 里程碑

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           项目时间表                                          │
└─────────────────────────────────────────────────────────────────────────────┘

Week 1-2: 开发阶段
├── Day 1-3:  项目结构搭建、数据结构定义
├── Day 4-7:  RedisStorage 实现
├── Day 8-10: RegistryHandler 实现
└── Day 11-14: ServiceRegistry 主服务、单元测试

Week 3: 测试与部署准备
├── Day 1-3:  集成测试
├── Day 4-5:  压力测试
└── Day 6-7:  部署文档、监控配置

Week 4: 灰度上线
├── Day 1:    部署到生产环境
├── Day 2-3:  10% 流量测试
├── Day 4-5:  50% 流量
└── Day 6-7:  100% 流量切换

Week 5+: 稳定观察
├── 监控告警
├── 性能调优
└── 老服务下线（可选）
```

### 13.2 资源需求

| 资源 | 需求 | 说明 |
|------|------|------|
| 开发人员 | 1人 | 全职 2 周 |
| 测试环境 | Redis + 应用服务器 | 用于开发和测试 |
| 生产环境 | 新增 service_registry 实例 | 可复用现有 Redis |

---

## 十四、总结

### 14.1 方案优势

1. **零风险**：新增服务，不影响现有系统
2. **可回滚**：任何阶段都可快速回滚
3. **灰度迁移**：逐步切换，降低影响
4. **架构清晰**：职责单一，易于维护
5. **高可用**：无状态设计，支持水平扩展

### 14.2 预期收益

1. **性能提升**：业务流量走 Nginx，延迟降低
2. **可维护性**：代码量减少 70%，职责清晰
3. **扩展性**：支持水平扩展，应对高并发
4. **成本降低**：资源占用减少，运维简化

### 14.3 待确认事项

- [ ] Redis 部署方案确认（单机/Sentinel/Cluster）
- [ ] 迁移时间窗口确认
- [ ] 是否需要保留 api_gateway 作为备份
- [ ] 监控系统集成方案

---

**等待确认**: 请审阅此方案，如有疑问或需要调整，请告知。
