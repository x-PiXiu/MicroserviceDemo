# Service Registry 服务详细开发计划

## 文档信息

- **创建日期**: 2025-02-19
- **版本**: 1.0.0
- **状态**: 规划中
- **作者**: Claude Code
- **基于**: api_gateway_to_service_registry_refactoring.md

---

## 一、服务概述

### 1.1 服务定位

`service_registry` 是一个**轻量级、高性能、分布式**的服务注册与发现中心，专门负责游戏微服务架构中的服务生命周期管理。

**核心价值**：
- 解耦服务发现逻辑与网关代理逻辑
- 支持 Redis 持久化，实现跨节点数据同步
- 无状态设计，支持水平扩展
- 提供丰富的查询和过滤能力

### 1.2 与现有组件的关系

```
┌─────────────────────────────────────────────────────────────────┐
│                        组件职责边界                               │
├──────────────────┬───────────────────┬──────────────────────────┤
│   组件           │      职责          │       不负责             │
├──────────────────┼───────────────────┼──────────────────────────┤
│ service_registry │ 服务注册/发现       │ 流量代理、负载均衡        │
│                  │ 健康状态管理        │ 限流熔断                  │
│                  │ 元数据存储         │ SSL终结                  │
├──────────────────┼───────────────────┼──────────────────────────┤
│ api_gateway      │ 全功能网关(保留)    │ 可选保留或逐步下线         │
│                  │ 作为过渡期备份       │                          │
├──────────────────┼───────────────────┼──────────────────────────┤
│ nginx            │ 流量入口           │ 服务注册                  │
│                  │ 反向代理/负载均衡    │ 健康检查(主动)            │
│                  │ SSL终结/限流       │                          │
└──────────────────┴───────────────────┴──────────────────────────┘
```

---

## 二、核心业务逻辑模块

### 2.1 模块总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Service Registry 架构                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      API Layer (HTTP)                            │   │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌───────────┐ │   │
│  │  │  Registry   │ │  Query      │ │  Health     │ │  Stats    │ │   │
│  │  │  Handler    │ │  Handler    │ │  Handler    │ │  Handler  │ │   │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └───────────┘ │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                    │                                    │
│  ┌─────────────────────────────────▼───────────────────────────────┐   │
│  │                     Core Service Layer                           │   │
│  │  ┌───────────────────┐  ┌───────────────────┐                  │   │
│  │  │   Registry        │  │   Health          │                  │   │
│  │  │   Manager         │  │   Monitor         │                  │   │
│  │  │  - register()     │  │  - checkHealth()  │                  │   │
│  │  │  - deregister()   │  │  - updateScore()  │                  │   │
│  │  │  - heartbeat()    │  │  - markUnhealthy()│                  │   │
│  │  └───────────────────┘  └───────────────────┘                  │   │
│  │  ┌───────────────────┐  ┌───────────────────┐                  │   │
│  │  │   Query           │  │   Event           │                  │   │
│  │  │   Engine          │  │   Publisher       │                  │   │
│  │  │  - getByType()    │  │  - publishChange()│                  │   │
│  │  │  - getByMeta()    │  │  - notifySub()    │                  │   │
│  │  │  - filter()       │  │                   │                  │   │
│  │  └───────────────────┘  └───────────────────┘                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                    │                                    │
│  ┌─────────────────────────────────▼───────────────────────────────┐   │
│  │                     Storage Layer (Redis)                        │   │
│  │  ┌───────────────────┐  ┌───────────────────┐                  │   │
│  │  │   Service         │  │   Index           │                  │   │
│  │  │   Storage         │  │   Manager         │                  │   │
│  │  │  - CRUD           │  │  - name index     │                  │   │
│  │  │  - TTL mgmt       │  │  - type index     │                  │   │
│  │  │                   │  │  - meta index     │                  │   │
│  │  └───────────────────┘  └───────────────────┘                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 模块详细设计

#### 模块 A: Registry Manager (注册管理器)

**职责**：服务实例的生命周期管理

**核心方法**：

| 方法 | 功能 | 参数 | 返回值 |
|------|------|------|--------|
| `registerService()` | 注册新服务实例 | ServiceInfo | bool |
| `deregisterService()` | 注销服务实例 | name, host, port | bool |
| `updateHeartbeat()` | 更新心跳时间戳 | name, host, port, metadata | bool |
| `renewRegistration()` | 续期服务注册 | name, host, port | bool |

**业务逻辑**：

```cpp
// 服务注册流程
bool RegistryManager::registerService(const ServiceInfo& info) {
    // 1. 参数校验
    if (!validateServiceInfo(info)) {
        return false;
    }

    // 2. 检查是否已存在（幂等性）
    auto existing = storage_->get(info.service_name, info.host, info.port);
    if (existing) {
        // 更新而非重新创建
        return storage_->update(info);
    }

    // 3. 设置初始值
    ServiceInfo new_info = info;
    new_info.register_time = std::chrono::system_clock::now();
    new_info.last_heartbeat = new_info.register_time;
    new_info.healthy = true;
    new_info.health_score = 1.0;

    // 4. 持久化到 Redis
    if (!storage_->save(new_info)) {
        LOG_ERROR("Failed to save service: " + info.service_name);
        return false;
    }

    // 5. 更新索引
    index_manager_->addToIndex(new_info);

    // 6. 发布事件
    event_publisher_->publish(ServiceEvent::REGISTERED, new_info);

    return true;
}
```

**心跳处理逻辑**：

```cpp
bool RegistryManager::updateHeartbeat(
    const std::string& name,
    const std::string& host,
    int port,
    const std::unordered_map<std::string, std::string>& metadata
) {
    // 1. 获取现有服务
    auto service = storage_->get(name, host, port);
    if (!service) {
        // 心跳但服务不存在，可能是过期被清理，触发重新注册
        LOG_WARNING("Heartbeat for unknown service: " + name);
        return false;
    }

    // 2. 更新心跳时间和元数据
    service->last_heartbeat = std::chrono::system_clock::now();
    if (!metadata.empty()) {
        // 合并元数据（保留旧值，更新新值）
        for (const auto& [k, v] : metadata) {
            service->metadata[k] = v;
        }
    }

    // 3. 如果之前不健康，现在恢复
    if (!service->healthy) {
        service->healthy = true;
        service->health_score = std::max(service->health_score, 0.5);
        event_publisher_->publish(ServiceEvent::RECOVERED, *service);
    }

    // 4. 持久化并重置 TTL
    return storage_->saveWithTTL(*service, config_.service_ttl);
}
```

#### 模块 B: Health Monitor (健康监控器)

**职责**：服务健康状态评估和异常检测

**健康检查策略**：

| 策略 | 描述 | 适用场景 |
|------|------|---------|
| 被动检查 | 基于心跳超时判断 | 所有服务 |
| 主动探测 | 定期 HTTP/TCP 探测 | 关键服务 |
| 自适应评分 | 根据历史数据动态调整 | 高可用服务 |

**健康评分算法**：

```cpp
double HealthMonitor::calculateHealthScore(const ServiceInfo& service) {
    double score = 1.0;

    // 1. 心跳延迟因子 (0.0 - 1.0)
    auto heartbeat_age = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - service.last_heartbeat
    ).count();

    if (heartbeat_age > config_.heartbeat_warning_threshold) {
        double delay_factor = 1.0 - std::min(1.0,
            (heartbeat_age - config_.heartbeat_warning_threshold) /
            (config_.heartbeat_timeout - config_.heartbeat_warning_threshold)
        );
        score *= delay_factor;
    }

    // 2. 失败率因子
    if (service.total_requests > 0) {
        double failure_rate = static_cast<double>(service.failed_requests) /
                             service.total_requests;
        double failure_factor = 1.0 - failure_rate;
        score *= failure_factor;
    }

    // 3. 响应时间因子
    if (service.avg_response_time > config_.response_time_threshold) {
        double latency_factor = config_.response_time_threshold.count() /
                               service.avg_response_time.count();
        score *= latency_factor;
    }

    // 4. 连续失败惩罚
    if (service.consecutive_failures > 0) {
        double penalty = std::pow(0.8, service.consecutive_failures);
        score *= penalty;
    }

    return std::max(0.0, std::min(1.0, score));
}
```

**健康状态判定规则**：

```cpp
enum class HealthStatus {
    HEALTHY,      // health_score >= 0.7
    DEGRADED,     // 0.3 <= health_score < 0.7
    UNHEALTHY,    // health_score < 0.3 或心跳超时
    UNKNOWN       // 数据不足
};

HealthStatus HealthMonitor::determineStatus(const ServiceInfo& service) {
    // 心跳超时直接判定不健康
    auto heartbeat_age = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - service.last_heartbeat
    ).count();

    if (heartbeat_age > config_.heartbeat_timeout) {
        return HealthStatus::UNHEALTHY;
    }

    // 根据评分判定
    double score = calculateHealthScore(service);
    if (score >= 0.7) return HealthStatus::HEALTHY;
    if (score >= 0.3) return HealthStatus::DEGRADED;
    return HealthStatus::UNHEALTHY;
}
```

#### 模块 C: Query Engine (查询引擎)

**职责**：多维度服务查询和过滤

**支持的查询维度**：

```cpp
struct ServiceQuery {
    // 基础过滤
    std::optional<std::string> service_name;     // 服务名称
    std::optional<std::string> game_type;        // 游戏类型
    std::optional<std::string> region;           // 区域

    // 健康过滤
    bool healthy_only = false;                   // 仅健康服务
    double min_health_score = 0.0;               // 最低健康分数

    // 负载过滤
    int max_current_players = INT_MAX;           // 最大当前玩家数
    double max_cpu_usage = 100.0;                // 最大CPU使用率
    double max_memory_usage = 100.0;             // 最大内存使用率

    // 排序
    enum class SortBy {
        HEALTH_SCORE,    // 按健康分数
        LOAD_ASC,        // 按负载升序
        RANDOM           // 随机
    } sort_by = SortBy::HEALTH_SCORE;

    // 分页
    int limit = 100;
    int offset = 0;
};
```

**查询执行逻辑**：

```cpp
std::vector<ServiceInfo> QueryEngine::query(const ServiceQuery& query) {
    std::vector<ServiceInfo> results;

    // 1. 确定查询范围（使用索引优化）
    std::vector<std::string> candidate_keys;

    if (query.service_name) {
        // 从服务名称索引获取
        candidate_keys = index_manager_->getInstancesByName(*query.service_name);
    } else if (query.game_type) {
        // 从游戏类型索引获取
        candidate_keys = index_manager_->getInstancesByGameType(*query.game_type);
    } else {
        // 全量扫描（性能较低）
        candidate_keys = index_manager_->getAllInstanceKeys();
    }

    // 2. 批量获取服务数据
    auto services = storage_->batchGet(candidate_keys);

    // 3. 应用过滤条件
    for (const auto& service : services) {
        if (!matchesFilter(service, query)) {
            continue;
        }
        results.push_back(service);
    }

    // 4. 排序
    sortResults(results, query.sort_by);

    // 5. 分页
    if (query.offset > 0 || query.limit < results.size()) {
        int end = std::min(query.offset + query.limit, (int)results.size());
        results = std::vector<ServiceInfo>(
            results.begin() + query.offset,
            results.begin() + end
        );
    }

    return results;
}
```

#### 模块 D: Event Publisher (事件发布器)

**职责**：服务变更事件通知

**事件类型**：

```cpp
enum class ServiceEventType {
    REGISTERED,        // 服务注册
    DEREGISTERED,      // 服务注销
    HEARTBEAT_MISSED,  // 心跳丢失
    HEALTH_CHANGED,    // 健康状态变化
    RECOVERED,         // 服务恢复
    METADATA_UPDATED   // 元数据更新
};

struct ServiceEvent {
    ServiceEventType type;
    std::string service_name;
    std::string instance_id;
    std::chrono::system_clock::time_point timestamp;
    nlohmann::json payload;
};
```

**事件发布机制**：

```cpp
class EventPublisher {
public:
    // 发布到 Redis Pub/Sub
    void publish(ServiceEventType type, const ServiceInfo& service) {
        ServiceEvent event;
        event.type = type;
        event.service_name = service.service_name;
        event.instance_id = service.instanceId();
        event.timestamp = std::chrono::system_clock::now();
        event.payload = serializeServiceInfo(service);

        std::string channel = "service:events:" + service.service_name;
        std::string message = serializeEvent(event);

        redis_->publish(channel, message);

        // 同时发布到全局事件通道
        redis_->publish("service:events:all", message);
    }

    // 订阅事件
    void subscribe(const std::string& service_name,
                   std::function<void(const ServiceEvent&)> callback) {
        std::string channel = "service:events:" + service_name;
        redis_->subscribe(channel, [callback](const std::string& message) {
            auto event = deserializeEvent(message);
            callback(event);
        });
    }
};
```

---

## 三、数据结构设计

### 3.1 ServiceInfo 完整定义

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace core_services {
namespace service_registry {

/**
 * @brief 服务实例完整信息
 */
struct ServiceInfo {
    // ==================== 基础信息 ====================
    std::string service_name;                    ///< 服务名称（如 gomoku_server）
    std::string service_version;                 ///< 服务版本（如 1.0.0）
    std::string host;                            ///< 主机地址
    int port = 0;                                ///< 端口号
    std::string service_id;                      ///< 服务唯一ID（可选，自动生成）

    // ==================== 健康检查 ====================
    std::string health_check_endpoint = "/health";  ///< 健康检查端点
    bool healthy = true;                         ///< 健康状态
    double health_score = 1.0;                   ///< 健康分数 (0.0-1.0)

    // ==================== 负载均衡 ====================
    int weight = 100;                            ///< 权重 (1-1000)

    // ==================== 时间戳 ====================
    std::chrono::system_clock::time_point register_time;   ///< 注册时间
    std::chrono::system_clock::time_point last_heartbeat;  ///< 最后心跳时间

    // ==================== 统计数据 ====================
    int total_requests = 0;                      ///< 总请求数
    int failed_requests = 0;                     ///< 失败请求数
    int consecutive_failures = 0;                ///< 连续失败次数
    int consecutive_successes = 0;               ///< 连续成功次数
    std::chrono::milliseconds avg_response_time{0};  ///< 平均响应时间

    // ==================== 扩展数据 ====================
    std::vector<std::string> endpoints;          ///< API端点列表
    std::unordered_map<std::string, std::string> metadata;  ///< 元数据

    // ==================== 预定义元数据键 ====================
    // game_type: 游戏类型 (gomoku, snake, tetris)
    // region: 区域 (cn-east, cn-west)
    // current_players: 当前玩家数
    // max_players: 最大玩家数
    // active_rooms: 活跃房间数
    // cpu_usage: CPU使用率
    // memory_usage: 内存使用率
    // server_status: 服务器状态 (online, maintenance, draining)

    // ==================== 辅助方法 ====================

    /// 生成实例唯一标识
    std::string instanceId() const {
        return host + ":" + std::to_string(port);
    }

    /// 生成 Redis Key
    std::string redisKey() const {
        return "service:registry:" + service_name + ":" + instanceId();
    }

    /// 计算运行时长（秒）
    int64_t uptimeSeconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - register_time
        ).count();
    }

    /// 计算心跳延迟（秒）
    int64_t heartbeatAgeSeconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - last_heartbeat
        ).count();
    }

    /// 从元数据获取值
    template<typename T>
    std::optional<T> getMetadata(const std::string& key) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) return std::nullopt;

        if constexpr (std::is_same_v<T, std::string>) {
            return it->second;
        } else if constexpr (std::is_same_v<T, int>) {
            try { return std::stoi(it->second); }
            catch (...) { return std::nullopt; }
        } else if constexpr (std::is_same_v<T, double>) {
            try { return std::stod(it->second); }
            catch (...) { return std::nullopt; }
        } else if constexpr (std::is_same_v<T, bool>) {
            return it->second == "true" || it->second == "1";
        }
        return std::nullopt;
    }
};

/**
 * @brief 服务查询过滤器
 */
struct ServiceFilter {
    // 基础过滤
    std::optional<std::string> service_name;     ///< 服务名称
    std::optional<std::string> game_type;        ///< 游戏类型
    std::optional<std::string> region;           ///< 区域

    // 健康过滤
    bool healthy_only = false;                   ///< 仅健康服务
    double min_health_score = 0.0;               ///< 最低健康分数

    // 负载过滤
    std::optional<int> max_current_players;      ///< 最大当前玩家数
    std::optional<double> max_cpu_usage;         ///< 最大CPU使用率
    std::optional<double> max_memory_usage;      ///< 最大内存使用率

    // 排序和分页
    enum class SortBy { HEALTH_SCORE, LOAD, RANDOM };
    SortBy sort_by = SortBy::HEALTH_SCORE;
    int limit = 100;
    int offset = 0;
};

/**
 * @brief 服务统计信息
 */
struct ServiceStats {
    size_t total_services = 0;                   ///< 总服务数
    size_t total_instances = 0;                  ///< 总实例数
    size_t healthy_instances = 0;                ///< 健康实例数
    size_t unhealthy_instances = 0;              ///< 不健康实例数
    size_t degraded_instances = 0;               ///< 降级实例数

    // 按游戏类型统计
    std::unordered_map<std::string, size_t> instances_by_game_type;

    // 按区域统计
    std::unordered_map<std::string, size_t> instances_by_region;

    // 平均指标
    double avg_health_score = 0.0;
    double avg_cpu_usage = 0.0;
    double avg_memory_usage = 0.0;
    int64_t total_current_players = 0;
};

} // namespace service_registry
} // namespace core_services
```

### 3.2 Redis 数据结构

```
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
    "total_requests": "1523",
    "failed_requests": "12",
    "avg_response_time_ms": "45",
    "metadata": "{\"game_type\":\"gomoku\",\"current_players\":\"8\",...}",
    "endpoints": "[\"/api/v1/gomoku/rooms\",\"/ws/gomoku/\"]"
}

# ==================== 索引数据 ====================

# 所有服务名称（Set）
service:index:names = {"gomoku_server", "snake_server", "user_service"}

# 特定服务的实例列表（Set）
service:index:gomoku_server:instances = {"192.168.1.100:8085", "192.168.1.101:8085"}

# 按游戏类型索引（Set）
service:index:game_type:gomoku = {"gomoku_server:192.168.1.100:8085"}
service:index:game_type:snake = {"snake_server:192.168.1.102:8087"}

# 按区域索引（Set）
service:index:region:cn-east = {"gomoku_server:192.168.1.100:8085"}
service:index:region:cn-west = {"gomoku_server:192.168.1.101:8085"}

# ==================== 统计缓存 ====================
# 缓存统计结果，减少计算开销
service:stats:summary = {
    "total_services": "5",
    "total_instances": "12",
    "healthy_instances": "10",
    "updated_at": "1708123456"
}

# ==================== 事件通道 ====================
# Pub/Sub 频道
service:events:all                    # 全局事件
service:events:gomoku_server          # 特定服务事件
```

---

## 四、API 接口设计

### 4.1 完整 API 列表

#### 服务注册相关

```
POST   /api/v1/services/register
       注册新服务实例

POST   /api/v1/services/heartbeat
       更新心跳（同时更新 metadata）

DELETE /api/v1/services/deregister
       注销服务实例

POST   /api/v1/services/batch-register
       批量注册（用于服务启动时）
```

#### 服务查询相关

```
GET    /api/v1/services
       获取所有服务列表
       ?healthy=true              仅健康服务
       ?game_type=gomoku          按游戏类型
       ?region=cn-east            按区域
       ?min_health_score=0.7      最低健康分数
       ?limit=100&offset=0        分页

GET    /api/v1/services/{service_name}
       获取特定服务的所有实例

GET    /api/v1/services/{service_name}/{host}:{port}
       获取特定实例详情

GET    /api/v1/services/{service_name}/health
       获取服务健康状态

GET    /api/v1/services/stats
       获取统计信息

GET    /api/v1/services/recommend
       获取推荐的服务实例（用于负载均衡）
       ?game_type=gomoku
       ?strategy=least_load       策略：least_load/random/round_robin
```

#### 系统管理相关

```
GET    /health
       Registry 自身健康检查

GET    /api/v1/services/admin/cleanup
       手动触发清理过期服务（管理员）

POST   /api/v1/services/admin/rebuild-index
       重建索引（管理员）
```

### 4.2 请求/响应示例

#### 注册服务

```http
POST /api/v1/services/register
Content-Type: application/json

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

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
    "success": true,
    "message": "服务注册成功",
    "timestamp": 1708123456789,
    "data": {
        "service_name": "gomoku_server",
        "instance_id": "192.168.1.100:8085",
        "redis_key": "service:registry:gomoku_server:192.168.1.100:8085",
        "ttl_seconds": 60
    }
}
```

#### 心跳更新

```http
POST /api/v1/services/heartbeat
Content-Type: application/json

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

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
    "success": true,
    "message": "心跳更新成功",
    "timestamp": 1708123516789,
    "data": {
        "next_heartbeat_deadline": 1708123576789,
        "health_score": 0.92
    }
}
```

#### 查询服务列表

```http
GET /api/v1/services?game_type=gomoku&healthy=true&limit=10
```

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
    "success": true,
    "message": "查询成功",
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

#### 获取推荐实例

```http
GET /api/v1/services/recommend?game_type=gomoku&strategy=least_load
```

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
    "success": true,
    "message": "推荐成功",
    "timestamp": 1708123530000,
    "data": {
        "recommended": {
            "host": "192.168.1.101",
            "port": 8085,
            "load_score": 0.35,
            "reason": "最低负载"
        },
        "candidates": [
            {
                "host": "192.168.1.101",
                "port": 8085,
                "load_score": 0.35
            },
            {
                "host": "192.168.1.100",
                "port": 8085,
                "load_score": 0.55
            }
        ]
    }
}
```

---

## 五、扩展性设计

### 5.1 近期扩展（1-3 个月）

#### 扩展 1: 服务分组管理

**场景**：支持按项目/租户隔离服务

```cpp
struct ServiceGroup {
    std::string group_id;                        ///< 分组ID
    std::string group_name;                      ///< 分组名称
    std::unordered_map<std::string, std::string> config;  ///< 分组配置
};

// 服务信息增加分组字段
struct ServiceInfo {
    // ... 现有字段
    std::string group_id;                        ///< 所属分组
};

// API 扩展
GET /api/v1/groups/{group_id}/services
```

#### 扩展 2: 服务依赖关系

**场景**：定义服务间的依赖，用于故障传播分析

```cpp
struct ServiceDependency {
    std::string service_name;                    ///< 服务名称
    std::vector<std::string> depends_on;         ///< 依赖的服务列表
    bool required;                               ///< 是否必需依赖
};

// API 扩展
POST /api/v1/services/{service_name}/dependencies
GET  /api/v1/services/{service_name}/dependencies
GET  /api/v1/services/{service_name}/dependents   // 被哪些服务依赖
```

#### 扩展 3: 服务配置中心

**场景**：结合服务发现，提供配置分发能力

```cpp
struct ServiceConfig {
    std::string config_key;                      ///< 配置键
    std::string config_value;                    ///< 配置值
    std::string version;                         ///< 版本号
    std::chrono::system_clock::time_point updated_at;
};

// API 扩展
GET    /api/v1/services/{service_name}/config
POST   /api/v1/services/{service_name}/config
DELETE /api/v1/services/{service_name}/config/{key}
```

### 5.2 中期扩展（3-6 个月）

#### 扩展 4: 服务网格集成

**场景**：与 Istio/Envoy 集成，提供服务发现 API

```cpp
// 实现 Envoy xDS 协议
class EnvoyXdsService {
public:
    // SDS (Service Discovery Service)
    void handleDiscoveryRequest(const DiscoveryRequest& request);

    // EDS (Endpoint Discovery Service)
    void handleEndpointDiscovery(const DiscoveryRequest& request);
};

// API 扩展
POST /v3/discovery:endpoints
POST /v3/discovery:clusters
```

#### 扩展 5: 多数据中心支持

**场景**：跨机房服务发现

```cpp
struct DataCenter {
    std::string dc_id;                           ///< 数据中心ID
    std::string region;                          ///< 区域
    std::string endpoint;                        ///< Registry 端点
    int priority;                                ///< 优先级
    bool active;                                 ///< 是否活跃
};

// 服务信息增加数据中心字段
struct ServiceInfo {
    // ... 现有字段
    std::string datacenter;                      ///< 所属数据中心
};

// 跨数据中心同步
class CrossDcSync {
    void syncToRemote(const ServiceInfo& info);
    void syncFromRemote(const std::string& dc_id);
};
```

#### 扩展 6: 服务降级与熔断规则

**场景**：定义服务级别的降级策略

```cpp
struct CircuitBreakerRule {
    std::string service_name;
    int failure_threshold = 5;                   ///< 失败阈值
    std::chrono::seconds timeout{30};            ///< 熔断超时
    int success_threshold = 2;                   ///< 恢复阈值
};

struct DegradationRule {
    std::string service_name;
    double cpu_threshold = 80.0;                 ///< CPU阈值
    double memory_threshold = 85.0;              ///< 内存阈值
    std::string action;                          ///< 降级动作
};

// API 扩展
POST /api/v1/services/{service_name}/rules/circuit-breaker
POST /api/v1/services/{service_name}/rules/degradation
```

### 5.3 远期扩展（6-12 个月）

#### 扩展 7: 智能负载预测

**场景**：基于历史数据预测服务负载

```cpp
class LoadPredictor {
public:
    // 预测未来 N 分钟的负载
    LoadPrediction predict(const std::string& service_name,
                          std::chrono::minutes horizon);

    // 基于预测的自动扩缩容建议
    ScalingRecommendation recommend(const std::string& service_name);

private:
    // 使用时序数据存储
    std::unique_ptr<TimeSeriesStore> ts_store_;
    // 机器学习模型
    std::unique_ptr<PredictionModel> model_;
};
```

#### 扩展 8: 服务拓扑图

**场景**：可视化服务依赖和调用关系

```cpp
struct ServiceTopology {
    std::vector<ServiceNode> nodes;
    std::vector<ServiceEdge> edges;

    // 分析功能
    std::vector<std::string> findCriticalPath();
    std::vector<std::string> findCycles();
    ImpactAnalysis analyzeFailureImpact(const std::string& service_name);
};

// API 扩展
GET /api/v1/topology
GET /api/v1/topology/impact/{service_name}
```

---

## 六、性能优化策略

### 6.1 Redis 操作优化

```cpp
// 1. 使用 Pipeline 批量操作
class OptimizedStorage {
    std::vector<ServiceInfo> batchGet(const std::vector<std::string>& keys) {
        auto conn = pool_->getConnection();
        conn->pipelineStart();

        for (const auto& key : keys) {
            conn->pipelineAddGet(key);
        }

        auto results = conn->pipelineExec();
        // ... 解析结果
    }

    // 2. 使用 Lua 脚本保证原子性
    bool registerWithIndex(const ServiceInfo& info) {
        static const char* SCRIPT = R"(
            local service_key = KEYS[1]
            local name_index_key = KEYS[2]
            local type_index_key = KEYS[3]
            local ttl = ARGV[1]
            local service_data = ARGV[2]
            local instance_id = ARGV[3]
            local game_type = ARGV[4]

            -- 保存服务数据
            redis.call('HSET', service_key, unpack(cjson.decode(service_data)))
            redis.call('EXPIRE', service_key, ttl)

            -- 更新索引
            redis.call('SADD', name_index_key, instance_id)
            if game_type and game_type ~= '' then
                redis.call('SADD', type_index_key, instance_id)
            end

            return 1
        )";

        return redis_->evalScript(SCRIPT, keys, args);
    }
};
```

### 6.2 缓存策略

```cpp
// 本地缓存 + Redis 双层架构
class CachedStorage {
    // L1: 本地缓存（热点数据）
    std::unique_ptr<LruCache<std::string, ServiceInfo>> local_cache_;

    // L2: Redis 持久化
    std::unique_ptr<RedisStorage> redis_storage_;

public:
    std::optional<ServiceInfo> get(const std::string& key) {
        // 1. 先查本地缓存
        auto cached = local_cache_->get(key);
        if (cached && !isExpired(*cached)) {
            return cached;
        }

        // 2. 查 Redis
        auto from_redis = redis_storage_->get(key);
        if (from_redis) {
            local_cache_->put(key, *from_redis);
        }

        return from_redis;
    }
};
```

### 6.3 连接池配置

```yaml
# config/service_registry.yml
redis:
  host: 127.0.0.1
  port: 6379
  pool_size: 20                # 连接池大小
  max_idle_time: 30000         # 最大空闲时间（ms）
  connection_timeout: 5000     # 连接超时（ms）
  socket_timeout: 3000         # Socket 超时（ms）

  # 重试配置
  retry:
    max_attempts: 3
    initial_delay: 100
    max_delay: 1000

http:
  port: 8081
  thread_count: 4
  max_connections: 1000
  keep_alive_timeout: 60

cache:
  local_cache_size: 1000
  local_cache_ttl: 5           # 秒
```

---

## 七、可观测性设计

### 7.1 指标暴露

```cpp
// Prometheus 指标定义
class Metrics {
public:
    // 计数器
    prometheus::Counter& registrations_total;
    prometheus::Counter& registrations_failed;
    prometheus::Counter& heartbeats_total;
    prometheus::Counter& heartbeats_failed;
    prometheus::Counter& queries_total;

    // 直方图
    prometheus::Histogram& query_latency;
    prometheus::Histogram& redis_latency;

    // 仪表盘
    prometheus::Gauge& services_count;
    prometheus::Gauge& instances_count;
    prometheus::Gauge& healthy_instances;
    prometheus::Gauge& unhealthy_instances;
    prometheus::Gauge& redis_connections;
};

// 暴露端点
GET /metrics    // Prometheus 格式
```

### 7.2 日志规范

```cpp
// 结构化日志
LOG_INFO("Service registered", {
    {"service_name", info.service_name},
    {"instance_id", info.instanceId()},
    {"source_ip", request.remote_addr}
});

LOG_WARNING("Heartbeat timeout", {
    {"service_name", name},
    {"instance_id", host + ":" + std::to_string(port)},
    {"heartbeat_age", std::to_string(age.count()) + "s"}
});

LOG_ERROR("Redis operation failed", {
    {"operation", "registerService"},
    {"error", e.what()},
    {"service_name", info.service_name}
});
```

### 7.3 健康检查端点

```http
GET /health

{
    "status": "healthy",
    "timestamp": 1708123600000,
    "components": {
        "redis": {
            "status": "healthy",
            "latency_ms": 2,
            "connections": 15
        },
        "storage": {
            "status": "healthy",
            "total_services": 5,
            "total_instances": 12
        }
    },
    "version": "1.0.0",
    "uptime_seconds": 86400
}
```

---

## 八、安全设计

### 8.1 认证机制

```cpp
// API Key 认证
class AuthMiddleware {
public:
    bool authenticate(const HttpRequest& req) {
        auto api_key = req.getHeader("X-API-Key");
        if (!api_key) return false;

        return key_validator_->validate(*api_key);
    }
};

// 服务注册需要认证
POST /api/v1/services/register
Header: X-API-Key: <service-api-key>
```

### 8.2 访问控制

```cpp
// RBAC 权限模型
enum class Permission {
    SERVICE_READ,
    SERVICE_WRITE,
    SERVICE_ADMIN,
    SYSTEM_ADMIN
};

struct Role {
    std::string name;
    std::vector<Permission> permissions;
};

// 权限检查
bool checkPermission(const std::string& api_key, Permission perm);
```

### 8.3 数据保护

```cpp
// 敏感元数据加密
class MetadataProtector {
public:
    std::string encrypt(const std::string& plaintext);
    std::string decrypt(const std::string& ciphertext);

private:
    std::unique_ptr<AES256> cipher_;
    std::string key_;
};

// 自动处理敏感字段
static const std::set<std::string> SENSITIVE_FIELDS = {
    "db_password", "api_secret", "private_key"
};
```

---

## 九、部署与运维

### 9.1 Docker 部署

```dockerfile
# Dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libhiredis-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY build/service_registry /app/
COPY config/service_registry.yml /app/config/

EXPOSE 8081

HEALTHCHECK --interval=30s --timeout=5s \
    CMD curl -f http://localhost:8081/health || exit 1

CMD ["./service_registry", "--config", "config/service_registry.yml"]
```

### 9.2 Kubernetes 部署

```yaml
# k8s/deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: service-registry
  labels:
    app: service-registry
spec:
  replicas: 3
  selector:
    matchLabels:
      app: service-registry
  template:
    metadata:
      labels:
        app: service-registry
    spec:
      containers:
      - name: service-registry
        image: registry.example.com/service-registry:1.0.0
        ports:
        - containerPort: 8081
        env:
        - name: REDIS_HOST
          valueFrom:
            configMapKeyRef:
              name: service-registry-config
              key: redis_host
        resources:
          requests:
            memory: "256Mi"
            cpu: "250m"
          limits:
            memory: "512Mi"
            cpu: "500m"
        livenessProbe:
          httpGet:
            path: /health
            port: 8081
          initialDelaySeconds: 10
          periodSeconds: 30
        readinessProbe:
          httpGet:
            path: /health
            port: 8081
          initialDelaySeconds: 5
          periodSeconds: 10
---
apiVersion: v1
kind: Service
metadata:
  name: service-registry
spec:
  selector:
    app: service-registry
  ports:
  - port: 8081
    targetPort: 8081
  type: ClusterIP
```

### 9.3 运维手册

#### 启动服务

```bash
# 直接运行
./service_registry --config config/service_registry.yml

# Docker 运行
docker run -d \
    --name service-registry \
    -p 8081:8081 \
    -v $(pwd)/config:/app/config \
    registry.example.com/service-registry:1.0.0
```

#### 健康检查

```bash
# 检查服务状态
curl http://localhost:8081/health

# 检查服务列表
curl http://localhost:8081/api/v1/services/stats
```

#### 故障排查

```bash
# 查看日志
docker logs service-registry -f

# 检查 Redis 连接
redis-cli -h 127.0.0.1 -p 6379 ping

# 手动触发清理
curl -X POST http://localhost:8081/api/v1/services/admin/cleanup
```

---

## 十、开发任务清单

### 10.1 Phase 1: 核心功能（第 1-2 周）

| 任务 | 优先级 | 预估时间 | 依赖 |
|------|--------|---------|------|
| 创建项目结构和 CMakeLists.txt | P0 | 4h | - |
| 实现 ServiceInfo 数据结构 | P0 | 4h | - |
| 实现 RedisStorage 基础 CRUD | P0 | 8h | ServiceInfo |
| 实现索引管理器 | P0 | 6h | RedisStorage |
| 实现 RegistryManager | P0 | 8h | RedisStorage |
| 实现 RegistryHandler HTTP 处理 | P0 | 8h | RegistryManager |
| 实现 ServiceRegistry 主服务 | P0 | 6h | 所有组件 |
| 编写配置加载 | P1 | 4h | - |

### 10.2 Phase 2: 测试与优化（第 3 周）

| 任务 | 优先级 | 预估时间 | 依赖 |
|------|--------|---------|------|
| 编写单元测试 | P0 | 12h | Phase 1 |
| 编写集成测试 | P0 | 8h | Phase 1 |
| 性能测试和优化 | P1 | 8h | Phase 1 |
| 压力测试（10000 QPS） | P1 | 8h | 优化完成 |

### 10.3 Phase 3: 部署与文档（第 4 周）

| 任务 | 优先级 | 预估时间 | 依赖 |
|------|--------|---------|------|
| 编写 Docker 镜像 | P1 | 4h | Phase 2 |
| 编写 K8s 部署文件 | P1 | 4h | Docker |
| 编写运维手册 | P2 | 4h | - |
| 编写 API 文档 | P2 | 4h | - |
| 灰度上线准备 | P0 | 8h | 全部 |

---

## 十一、总结

### 11.1 核心价值

1. **职责单一**：专注于服务注册与发现，代码量减少 70%
2. **高性能**：Redis 持久化 + 本地缓存，支持 10000+ QPS
3. **高可用**：无状态设计，支持水平扩展
4. **易扩展**：模块化设计，支持丰富的扩展点

### 11.2 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 存储 | Redis | 支持跨节点同步，自带 TTL |
| 架构 | 无状态 | 简化部署和扩展 |
| 索引 | Redis Set | 查询效率高，维护简单 |
| 事件 | Redis Pub/Sub | 原生支持，延迟低 |

### 11.3 后续规划

- **近期**：完成核心功能，灰度上线
- **中期**：服务网格集成，多数据中心支持
- **远期**：智能负载预测，服务拓扑分析

---

**文档状态**：待审核
**下一步**：确认方案后开始 Phase 1 开发
