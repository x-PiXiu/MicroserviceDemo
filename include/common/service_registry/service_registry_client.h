/**
 * @file service_registry_client.h
 * @brief 统一的服务注册中心客户端
 * @details 提供服务注册、发现、健康检查等功能
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include "common/http/http_client.h"
#include "common/logger/logger.h"

namespace common {
namespace service_registry {

/**
 * @brief 服务类型枚举
 */
enum class ServiceType {
    CORE,       ///< 核心服务 (user_service, auth_service, etc.)
    GAME,       ///< 游戏服务 (gomoku_server, etc.)
    GATEWAY,    ///< 网关服务 (api_gateway)
    UNKNOWN     ///< 未知类型
};

/**
 * @brief 将 ServiceType 转换为字符串
 */
inline std::string serviceTypeToString(ServiceType type) {
    switch (type) {
        case ServiceType::CORE: return "core";
        case ServiceType::GAME: return "game";
        case ServiceType::GATEWAY: return "gateway";
        default: return "unknown";
    }
}

/**
 * @brief 从字符串解析 ServiceType
 */
inline ServiceType stringToServiceType(const std::string& str) {
    if (str == "core") return ServiceType::CORE;
    if (str == "game") return ServiceType::GAME;
    if (str == "gateway") return ServiceType::GATEWAY;
    return ServiceType::UNKNOWN;
}

/**
 * @brief 服务实例信息（简化版）
 */
struct ServiceInstance {
    std::string service_name;                            ///< 服务名称
    std::string service_version = "1.0.0";               ///< 服务版本
    std::string host;                                    ///< 主机地址
    int port = 0;                                        ///< 端口号
    std::string service_id;                              ///< 服务唯一ID

    bool healthy = true;                                 ///< 健康状态
    double health_score = 1.0;                           ///< 健康分数 (0.0-1.0)
    int weight = 100;                                    ///< 权重

    std::string health_check_endpoint = "/health";       ///< 健康检查端点
    std::vector<std::string> endpoints;                  ///< API端点列表
    std::unordered_map<std::string, std::string> metadata; ///< 元数据

    /**
     * @brief 生成实例ID
     */
    std::string instanceId() const {
        return host + ":" + std::to_string(port);
    }

    /**
     * @brief 从 JSON 解析
     */
    static ServiceInstance fromJson(const nlohmann::json& j) {
        ServiceInstance info;
        info.service_name = j.value("service_name", "");
        info.service_version = j.value("service_version", "1.0.0");
        info.host = j.value("host", "");
        info.port = j.value("port", 0);
        info.service_id = j.value("service_id", "");
        info.healthy = j.value("healthy", true);
        info.health_score = j.value("health_score", 1.0);
        info.weight = j.value("weight", 100);
        info.health_check_endpoint = j.value("health_check_endpoint", "/health");

        if (j.contains("endpoints") && j["endpoints"].is_array()) {
            info.endpoints = j["endpoints"].get<std::vector<std::string>>();
        }
        if (j.contains("metadata") && j["metadata"].is_object()) {
            info.metadata = j["metadata"].get<std::unordered_map<std::string, std::string>>();
        }

        return info;
    }

    /**
     * @brief 转换为 JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["service_name"] = service_name;
        j["service_version"] = service_version;
        j["host"] = host;
        j["port"] = port;
        j["service_id"] = service_id;
        j["health_check_endpoint"] = health_check_endpoint;
        j["weight"] = weight;
        j["endpoints"] = endpoints;
        j["metadata"] = metadata;
        return j;
    }
};

/**
 * @brief 服务查询过滤器
 */
struct ServiceQueryFilter {
    std::optional<std::string> service_name;     ///< 服务名称
    std::optional<std::string> service_type;     ///< 服务类型 (core, game, gateway)
    std::optional<std::string> game_type;        ///< 游戏类型
    std::optional<std::string> region;           ///< 区域
    bool healthy_only = false;                   ///< 仅健康服务
    double min_health_score = 0.0;               ///< 最低健康分数
    int limit = 100;                             ///< 结果数量限制

    /**
     * @brief 转换为查询参数
     */
    std::string toQueryString() const {
        std::string query;
        auto addParam = [&](const std::string& key, const std::string& value) {
            if (!query.empty()) query += "&";
            query += key + "=" + value;
        };

        if (service_name) addParam("service_name", *service_name);
        if (service_type) addParam("service_type", *service_type);
        if (game_type) addParam("game_type", *game_type);
        if (region) addParam("region", *region);
        if (healthy_only) addParam("healthy_only", "true");
        if (min_health_score > 0.0) addParam("min_health_score", std::to_string(min_health_score));
        if (limit != 100) addParam("limit", std::to_string(limit));

        return query.empty() ? "" : "?" + query;
    }
};

/**
 * @brief 服务推荐结果
 */
struct ServiceRecommendation {
    ServiceInstance recommended;                 ///< 推荐的服务实例
    double load_score = 0.0;                     ///< 负载分数
    std::string reason;                          ///< 推荐原因
    bool valid = false;                          ///< 是否有效
};

/**
 * @brief 推荐策略
 */
enum class RecommendStrategy {
    LEAST_LOAD,      ///< 最少负载
    RANDOM,          ///< 随机
    ROUND_ROBIN,     ///< 轮询
    WEIGHTED         ///< 加权
};

/**
 * @brief 服务注册客户端配置
 */
struct ServiceRegistryClientConfig {
    std::string registry_host = "127.0.0.1";     ///< 注册中心地址
    int registry_port = 8081;                    ///< 注册中心端口
    int heartbeat_interval_s = 30;               ///< 心跳间隔（秒）
    int register_timeout_s = 10;                 ///< 注册超时（秒）
    int request_timeout_ms = 5000;               ///< 请求超时（毫秒）
    bool auto_heartbeat = true;                  ///< 自动发送心跳
    bool auto_reconnect = true;                  ///< 自动重新注册
    int reconnect_delay_s = 5;                   ///< 重连延迟（秒）
};

/**
 * @brief 统一的服务注册中心客户端
 * @details 提供服务注册、发现、健康检查等功能
 *
 * 使用示例:
 * @code
 * // 创建客户端
 * common::service_registry::ServiceRegistryClientConfig config;
 * config.registry_host = "127.0.0.1";
 * config.registry_port = 8081;
 *
 * auto client = std::make_shared<common::service_registry::ServiceRegistryClient>(config);
 *
 * // 注册服务
 * common::service_registry::ServiceInstance instance;
 * instance.service_name = "gomoku_server";
 * instance.host = "192.168.1.100";
 * instance.port = 9001;
 * instance.metadata["service_type"] = "game";
 * instance.metadata["game_type"] = "gomoku";
 *
 * if (client->registerService(instance)) {
 *     // 启动自动心跳
 *     client->startHeartbeat(instance);
 * }
 *
 * // 查询游戏服务
 * common::service_registry::ServiceQueryFilter filter;
 * filter.service_type = "game";
 * filter.healthy_only = true;
 *
 * auto services = client->queryServices(filter);
 * for (const auto& svc : services) {
 *     std::cout << "Found game service: " << svc.service_name
 *               << " at " << svc.host << ":" << svc.port << std::endl;
 * }
 *
 * // 获取推荐的游戏服务
 * auto recommendation = client->recommendService("game", "gomoku");
 * if (recommendation.valid) {
 *     std::cout << "Recommended: " << recommendation.recommended.instanceId() << std::endl;
 * }
 * @endcode
 */
class ServiceRegistryClient {
public:
    /**
     * @brief 构造函数
     * @param config 客户端配置
     */
    explicit ServiceRegistryClient(const ServiceRegistryClientConfig& config = ServiceRegistryClientConfig());

    /**
     * @brief 析构函数
     */
    ~ServiceRegistryClient();

    // ==================== 服务注册 ====================

    /**
     * @brief 注册服务
     * @param instance 服务实例信息
     * @return 是否成功
     */
    bool registerService(const ServiceInstance& instance);

    /**
     * @brief 注销服务
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口号
     * @return 是否成功
     */
    bool deregisterService(const std::string& service_name, const std::string& host, int port);

    /**
     * @brief 发送心跳
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口号
     * @param metadata 更新的元数据（可选）
     * @return 是否成功
     */
    bool sendHeartbeat(const std::string& service_name, const std::string& host, int port,
                       const std::unordered_map<std::string, std::string>& metadata = {});

    // ==================== 心跳管理 ====================

    /**
     * @brief 心跳前回调类型 - 用于动态更新 metadata
     * @return 返回要合并到心跳中的额外 metadata
     */
    using HeartbeatCallback = std::function<std::unordered_map<std::string, std::string>()>;

    /**
     * @brief 启动自动心跳
     * @param instance 服务实例
     */
    void startHeartbeat(const ServiceInstance& instance);

    /**
     * @brief 停止自动心跳
     */
    void stopHeartbeat();

    /**
     * @brief 检查心跳是否运行中
     */
    bool isHeartbeatRunning() const;

    /**
     * @brief 设置心跳前回调 - 每次心跳前调用，用于动态更新 metadata
     * @param callback 回调函数，返回要附加到心跳的 metadata
     */
    void setHeartbeatCallback(HeartbeatCallback callback);

    /**
     * @brief 更新心跳实例的 metadata（立即生效）
     * @param key metadata 键
     * @param value metadata 值
     */
    void updateHeartbeatMetadata(const std::string& key, const std::string& value);

    /**
     * @brief 批量更新心跳实例的 metadata
     * @param metadata 要合并的 metadata
     */
    void updateHeartbeatMetadata(const std::unordered_map<std::string, std::string>& metadata);

    // ==================== 服务发现 ====================

    /**
     * @brief 查询服务列表
     * @param filter 查询过滤器
     * @return 服务实例列表
     */
    std::vector<ServiceInstance> queryServices(const ServiceQueryFilter& filter = ServiceQueryFilter());

    /**
     * @brief 获取指定服务的所有实例
     * @param service_name 服务名称
     * @return 服务实例列表
     */
    std::vector<ServiceInstance> getServiceInstances(const std::string& service_name);

    /**
     * @brief 获取指定服务实例
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口号
     * @return 服务实例（如果存在）
     */
    std::optional<ServiceInstance> getServiceInstance(const std::string& service_name,
                                                       const std::string& host, int port);

    /**
     * @brief 查询游戏服务
     * @param game_type 游戏类型（可选，为空则返回所有游戏服务）
     * @param healthy_only 是否仅返回健康服务
     * @return 游戏服务实例列表
     */
    std::vector<ServiceInstance> queryGameServices(const std::string& game_type = "",
                                                    bool healthy_only = true);

    /**
     * @brief 查询核心服务
     * @param service_name 服务名称（可选，为空则返回所有核心服务）
     * @param healthy_only 是否仅返回健康服务
     * @return 核心服务实例列表
     */
    std::vector<ServiceInstance> queryCoreServices(const std::string& service_name = "",
                                                    bool healthy_only = true);

    // ==================== 服务推荐 ====================

    /**
     * @brief 获取推荐服务
     * @param service_type 服务类型 (core, game, gateway)
     * @param game_type 游戏类型（仅当 service_type 为 game 时有效）
     * @param strategy 推荐策略
     * @return 推荐结果
     */
    ServiceRecommendation recommendService(const std::string& service_type,
                                            const std::string& game_type = "",
                                            RecommendStrategy strategy = RecommendStrategy::LEAST_LOAD);

    /**
     * @brief 获取推荐的游戏服务
     * @param game_type 游戏类型
     * @param strategy 推荐策略
     * @return 推荐结果
     */
    ServiceRecommendation recommendGameService(const std::string& game_type,
                                                RecommendStrategy strategy = RecommendStrategy::LEAST_LOAD);

    // ==================== 健康检查 ====================

    /**
     * @brief 检查服务健康状态
     * @param service_name 服务名称
     * @return 健康状态 JSON
     */
    nlohmann::json checkServiceHealth(const std::string& service_name);

    /**
     * @brief 检查注册中心健康状态
     * @return 是否健康
     */
    bool checkRegistryHealth();

    // ==================== 统计信息 ====================

    /**
     * @brief 获取注册中心统计信息
     * @return 统计信息 JSON
     */
    nlohmann::json getStats();

    // ==================== 对局统计 ====================

    /**
     * @brief 报告对局完成
     * @param game_type 游戏类型（如 "gomoku"）
     * @param service_name 报告的服务名称
     * @return 是否成功
     */
    bool reportMatchCompletion(const std::string& game_type, const std::string& service_name);

private:
    ServiceRegistryClientConfig config_;
    http::HttpClient http_client_;
    std::atomic<bool> heartbeat_running_{false};
    std::thread heartbeat_thread_;
    std::mutex mutex_;

    // 当前注册的服务信息（用于心跳）
    ServiceInstance registered_instance_;

    // 心跳前回调 - 用于动态更新 metadata
    HeartbeatCallback heartbeat_callback_;

    /**
     * @brief 构建注册中心 URL
     */
    std::string buildUrl(const std::string& path, const std::string& query = "") const;

    /**
     * @brief 解析 API 响应
     */
    std::optional<nlohmann::json> parseResponse(const http::HttpClientResponse& response);

    /**
     * @brief 心跳线程函数
     */
    void heartbeatLoop();
};

} // namespace service_registry
} // namespace common
