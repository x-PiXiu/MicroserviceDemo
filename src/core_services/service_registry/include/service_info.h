/**
 * @file service_info.h
 * @brief 服务注册中心数据结构定义
 * @details 定义服务实例、查询过滤器、统计信息等核心数据结构
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace core_services {
namespace service_registry {

/**
 * @brief 服务健康状态枚举
 */
enum class HealthStatus {
    HEALTHY,      ///< 健康: health_score >= 0.7
    DEGRADED,     ///< 降级: 0.3 <= health_score < 0.7
    UNHEALTHY,    ///< 不健康: health_score < 0.3 或心跳超时
    UNKNOWN       ///< 未知: 数据不足
};

/**
 * @brief 服务事件类型枚举
 */
enum class ServiceEventType {
    REGISTERED,        ///< 服务注册
    DEREGISTERED,      ///< 服务注销
    HEARTBEAT_MISSED,  ///< 心跳丢失
    HEALTH_CHANGED,    ///< 健康状态变化
    RECOVERED,         ///< 服务恢复
    METADATA_UPDATED   ///< 元数据更新
};

/**
 * @brief 服务实例完整信息
 * @details 存储服务实例的所有相关信息，包括基础信息、健康状态、统计数据等
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
    int64_t total_requests = 0;                  ///< 总请求数
    int64_t failed_requests = 0;                 ///< 失败请求数
    int consecutive_failures = 0;                ///< 连续失败次数
    int consecutive_successes = 0;               ///< 连续成功次数
    int64_t avg_response_time_ms = 0;            ///< 平均响应时间（毫秒）

    // ==================== 扩展数据 ====================
    std::vector<std::string> endpoints;          ///< API端点列表
    std::unordered_map<std::string, std::string> metadata;  ///< 元数据

    // ==================== 辅助方法 ====================

    /**
     * @brief 生成实例唯一标识
     * @return 格式: "host:port"
     */
    std::string instanceId() const {
        return host + ":" + std::to_string(port);
    }

    /**
     * @brief 生成 Redis Key
     * @return 格式: "service:registry:{service_name}:{host}:{port}"
     */
    std::string redisKey() const {
        return "service:registry:" + service_name + ":" + instanceId();
    }

    /**
     * @brief 计算运行时长（秒）
     * @return 运行时长秒数
     */
    int64_t uptimeSeconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - register_time
        ).count();
    }

    /**
     * @brief 计算心跳延迟（秒）
     * @return 距离上次心跳的秒数
     */
    int64_t heartbeatAgeSeconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - last_heartbeat
        ).count();
    }

    /**
     * @brief 从元数据获取字符串值
     * @param key 元数据键
     * @return 元数据值（如果不存在返回 nullopt）
     */
    std::optional<std::string> getMetadataString(const std::string& key) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) return std::nullopt;
        return it->second;
    }

    /**
     * @brief 从元数据获取整数值
     * @param key 元数据键
     * @return 元数据值（如果不存在或转换失败返回 nullopt）
     */
    std::optional<int> getMetadataInt(const std::string& key) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) return std::nullopt;
        try {
            return std::stoi(it->second);
        } catch (...) {
            return std::nullopt;
        }
    }

    /**
     * @brief 从元数据获取双精度值
     * @param key 元数据键
     * @return 元数据值（如果不存在或转换失败返回 nullopt）
     */
    std::optional<double> getMetadataDouble(const std::string& key) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) return std::nullopt;
        try {
            return std::stod(it->second);
        } catch (...) {
            return std::nullopt;
        }
    }

    /**
     * @brief 从元数据获取布尔值
     * @param key 元数据键
     * @return 元数据值（如果不存在返回 nullopt）
     */
    std::optional<bool> getMetadataBool(const std::string& key) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) return std::nullopt;
        return it->second == "true" || it->second == "1";
    }

    // ==================== 序列化 ====================

    /**
     * @brief 转换为 JSON
     * @return JSON 对象
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["service_name"] = service_name;
        j["service_version"] = service_version;
        j["host"] = host;
        j["port"] = port;
        j["service_id"] = service_id;
        j["health_check_endpoint"] = health_check_endpoint;
        j["healthy"] = healthy;
        j["health_score"] = health_score;
        j["weight"] = weight;

        // 时间戳转为毫秒
        j["register_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            register_time.time_since_epoch()
        ).count();
        j["last_heartbeat"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            last_heartbeat.time_since_epoch()
        ).count();

        j["total_requests"] = total_requests;
        j["failed_requests"] = failed_requests;
        j["consecutive_failures"] = consecutive_failures;
        j["consecutive_successes"] = consecutive_successes;
        j["avg_response_time_ms"] = avg_response_time_ms;

        j["endpoints"] = endpoints;
        j["metadata"] = metadata;

        // 计算字段
        j["instance_id"] = instanceId();
        j["heartbeat_age_seconds"] = heartbeatAgeSeconds();
        j["uptime_seconds"] = uptimeSeconds();

        return j;
    }

    /**
     * @brief 从 JSON 解析
     * @param j JSON 对象
     * @return ServiceInfo 对象
     */
    static ServiceInfo fromJson(const nlohmann::json& j) {
        ServiceInfo info;

        info.service_name = j.value("service_name", "");
        info.service_version = j.value("service_version", "1.0.0");
        info.host = j.value("host", "");
        info.port = j.value("port", 0);
        info.service_id = j.value("service_id", "");

        info.health_check_endpoint = j.value("health_check_endpoint", "/health");
        info.healthy = j.value("healthy", true);
        info.health_score = j.value("health_score", 1.0);
        info.weight = j.value("weight", 100);

        // 解析时间戳
        if (j.contains("register_time")) {
            auto ms = j["register_time"].get<int64_t>();
            info.register_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(ms)
            );
        }
        if (j.contains("last_heartbeat")) {
            auto ms = j["last_heartbeat"].get<int64_t>();
            info.last_heartbeat = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(ms)
            );
        }

        info.total_requests = j.value("total_requests", 0);
        info.failed_requests = j.value("failed_requests", 0);
        info.consecutive_failures = j.value("consecutive_failures", 0);
        info.consecutive_successes = j.value("consecutive_successes", 0);
        info.avg_response_time_ms = j.value("avg_response_time_ms", 0);

        if (j.contains("endpoints")) {
            info.endpoints = j["endpoints"].get<std::vector<std::string>>();
        }
        if (j.contains("metadata")) {
            info.metadata = j["metadata"].get<std::unordered_map<std::string, std::string>>();
        }

        return info;
    }
};

/**
 * @brief 服务查询过滤器
 * @details 支持多维度服务查询和过滤
 */
struct ServiceFilter {
    // ==================== 基础过滤 ====================
    std::optional<std::string> service_name;     ///< 服务名称
    std::optional<std::string> service_type;     ///< 服务类型 (core, game, gateway)
    std::optional<std::string> game_type;        ///< 游戏类型
    std::optional<std::string> region;           ///< 区域

    // ==================== 健康过滤 ====================
    bool healthy_only = false;                   ///< 仅健康服务
    double min_health_score = 0.0;               ///< 最低健康分数
    double max_health_score = 1.0;               ///< 最高健康分数

    // ==================== 负载过滤 ====================
    std::optional<int> max_current_players;      ///< 最大当前玩家数
    std::optional<double> max_cpu_usage;         ///< 最大CPU使用率
    std::optional<double> max_memory_usage;      ///< 最大内存使用率

    // ==================== 排序选项 ====================
    enum class SortBy {
        HEALTH_SCORE,   ///< 按健康分数排序
        LOAD_ASC,       ///< 按负载升序
        LOAD_DESC,      ///< 按负载降序
        RANDOM,         ///< 随机排序
        REGISTER_TIME   ///< 按注册时间
    };
    SortBy sort_by = SortBy::HEALTH_SCORE;

    // ==================== 分页 ====================
    int limit = 100;
    int offset = 0;

    /**
     * @brief 检查服务是否匹配过滤器
     * @param service 服务信息
     * @return 是否匹配
     */
    bool matches(const ServiceInfo& service) const {
        // 服务名称过滤
        if (service_name && service.service_name != *service_name) {
            return false;
        }

        // 服务类型过滤
        if (service_type) {
            auto type = service.getMetadataString("service_type");
            if (!type || *type != *service_type) {
                return false;
            }
        }

        // 游戏类型过滤
        if (game_type) {
            auto type = service.getMetadataString("game_type");
            if (!type || *type != *game_type) {
                return false;
            }
        }

        // 区域过滤
        if (region) {
            auto r = service.getMetadataString("region");
            if (!r || *r != *region) {
                return false;
            }
        }

        // 健康状态过滤
        if (healthy_only && !service.healthy) {
            return false;
        }

        // 健康分数过滤
        if (service.health_score < min_health_score ||
            service.health_score > max_health_score) {
            return false;
        }

        // 玩家数过滤
        if (max_current_players) {
            auto players = service.getMetadataInt("current_players");
            if (players && *players > *max_current_players) {
                return false;
            }
        }

        // CPU 使用率过滤
        if (max_cpu_usage) {
            auto cpu = service.getMetadataDouble("cpu_usage");
            if (cpu && *cpu > *max_cpu_usage) {
                return false;
            }
        }

        // 内存使用率过滤
        if (max_memory_usage) {
            auto mem = service.getMetadataDouble("memory_usage");
            if (mem && *mem > *max_memory_usage) {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 转换为 JSON
     * @return JSON 对象
     */
    nlohmann::json toJson() const {
        nlohmann::json j;

        if (service_name) j["service_name"] = *service_name;
        if (service_type) j["service_type"] = *service_type;
        if (game_type) j["game_type"] = *game_type;
        if (region) j["region"] = *region;

        j["healthy_only"] = healthy_only;
        j["min_health_score"] = min_health_score;
        j["max_health_score"] = max_health_score;

        if (max_current_players) j["max_current_players"] = *max_current_players;
        if (max_cpu_usage) j["max_cpu_usage"] = *max_cpu_usage;
        if (max_memory_usage) j["max_memory_usage"] = *max_memory_usage;

        switch (sort_by) {
            case SortBy::HEALTH_SCORE: j["sort_by"] = "health_score"; break;
            case SortBy::LOAD_ASC: j["sort_by"] = "load_asc"; break;
            case SortBy::LOAD_DESC: j["sort_by"] = "load_desc"; break;
            case SortBy::RANDOM: j["sort_by"] = "random"; break;
            case SortBy::REGISTER_TIME: j["sort_by"] = "register_time"; break;
        }

        j["limit"] = limit;
        j["offset"] = offset;

        return j;
    }
};

/**
 * @brief 服务统计信息
 * @details 聚合统计所有服务实例的状态
 */
struct ServiceStats {
    // ==================== 基础统计 ====================
    size_t total_services = 0;                   ///< 总服务数
    size_t total_instances = 0;                  ///< 总实例数
    size_t healthy_instances = 0;                ///< 健康实例数
    size_t unhealthy_instances = 0;              ///< 不健康实例数
    size_t degraded_instances = 0;               ///< 降级实例数

    // ==================== 分组统计 ====================
    std::unordered_map<std::string, size_t> instances_by_service;   ///< 按服务名统计
    std::unordered_map<std::string, size_t> instances_by_game_type; ///< 按游戏类型统计
    std::unordered_map<std::string, size_t> instances_by_region;    ///< 按区域统计

    // ==================== 平均指标 ====================
    double avg_health_score = 0.0;               ///< 平均健康分数
    double avg_cpu_usage = 0.0;                  ///< 平均CPU使用率
    double avg_memory_usage = 0.0;               ///< 平均内存使用率
    int64_t total_current_players = 0;           ///< 总当前玩家数

    // ==================== 时间戳 ====================
    std::chrono::system_clock::time_point updated_at;  ///< 更新时间

    /**
     * @brief 转换为 JSON
     * @return JSON 对象
     */
    nlohmann::json toJson() const {
        nlohmann::json j;

        j["total_services"] = total_services;
        j["total_instances"] = total_instances;
        j["healthy_instances"] = healthy_instances;
        j["unhealthy_instances"] = unhealthy_instances;
        j["degraded_instances"] = degraded_instances;

        j["instances_by_service"] = instances_by_service;
        j["instances_by_game_type"] = instances_by_game_type;
        j["instances_by_region"] = instances_by_region;

        j["avg_health_score"] = avg_health_score;
        j["avg_cpu_usage"] = avg_cpu_usage;
        j["avg_memory_usage"] = avg_memory_usage;
        j["total_current_players"] = total_current_players;

        j["updated_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            updated_at.time_since_epoch()
        ).count();

        return j;
    }
};

/**
 * @brief 服务事件
 * @details 用于事件发布/订阅
 */
struct ServiceEvent {
    ServiceEventType type;                       ///< 事件类型
    std::string service_name;                    ///< 服务名称
    std::string instance_id;                     ///< 实例ID
    std::chrono::system_clock::time_point timestamp;  ///< 时间戳
    nlohmann::json payload;                      ///< 事件数据

    /**
     * @brief 转换为 JSON
     * @return JSON 对象
     */
    nlohmann::json toJson() const {
        nlohmann::json j;

        switch (type) {
            case ServiceEventType::REGISTERED: j["type"] = "registered"; break;
            case ServiceEventType::DEREGISTERED: j["type"] = "deregistered"; break;
            case ServiceEventType::HEARTBEAT_MISSED: j["type"] = "heartbeat_missed"; break;
            case ServiceEventType::HEALTH_CHANGED: j["type"] = "health_changed"; break;
            case ServiceEventType::RECOVERED: j["type"] = "recovered"; break;
            case ServiceEventType::METADATA_UPDATED: j["type"] = "metadata_updated"; break;
        }

        j["service_name"] = service_name;
        j["instance_id"] = instance_id;
        j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()
        ).count();
        j["payload"] = payload;

        return j;
    }

    /**
     * @brief 从 JSON 解析
     * @param j JSON 对象
     * @return ServiceEvent 对象
     */
    static ServiceEvent fromJson(const nlohmann::json& j) {
        ServiceEvent event;

        std::string type_str = j.value("type", "");
        if (type_str == "registered") event.type = ServiceEventType::REGISTERED;
        else if (type_str == "deregistered") event.type = ServiceEventType::DEREGISTERED;
        else if (type_str == "heartbeat_missed") event.type = ServiceEventType::HEARTBEAT_MISSED;
        else if (type_str == "health_changed") event.type = ServiceEventType::HEALTH_CHANGED;
        else if (type_str == "recovered") event.type = ServiceEventType::RECOVERED;
        else if (type_str == "metadata_updated") event.type = ServiceEventType::METADATA_UPDATED;

        event.service_name = j.value("service_name", "");
        event.instance_id = j.value("instance_id", "");

        if (j.contains("timestamp")) {
            auto ms = j["timestamp"].get<int64_t>();
            event.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(ms)
            );
        }

        event.payload = j.value("payload", nlohmann::json::object());

        return event;
    }
};

/**
 * @brief 服务推荐结果
 * @details 用于负载均衡推荐
 */
struct ServiceRecommendation {
    ServiceInfo recommended;                     ///< 推荐的服务实例
    double load_score = 0.0;                     ///< 负载分数
    std::string reason;                          ///< 推荐原因
    std::vector<std::pair<ServiceInfo, double>> candidates;  ///< 候选列表

    /**
     * @brief 转换为 JSON
     * @return JSON 对象
     */
    nlohmann::json toJson() const {
        nlohmann::json j;

        j["recommended"] = recommended.toJson();
        j["load_score"] = load_score;
        j["reason"] = reason;

        nlohmann::json candidates_json = nlohmann::json::array();
        for (const auto& [info, score] : candidates) {
            nlohmann::json candidate;
            candidate["service"] = info.toJson();
            candidate["load_score"] = score;
            candidates_json.push_back(candidate);
        }
        j["candidates"] = candidates_json;

        return j;
    }
};

} // namespace service_registry
} // namespace core_services
