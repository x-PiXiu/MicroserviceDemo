/**
 * @file registry_config.h
 * @brief 服务注册中心配置结构定义
 * @details 定义注册中心的各项配置参数，支持从 ConfigManager 加载
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include <string>
#include <chrono>
#include <nlohmann/json.hpp>
#include "common/config/config_manager.h"

namespace core_services {
namespace service_registry {

/**
 * @brief Redis 配置
 */
struct RedisConfig {
    std::string host = "127.0.0.1";             ///< Redis 主机
    int port = 6379;                            ///< Redis 端口
    std::string password;                       ///< Redis 密码
    int database = 0;                           ///< Redis 数据库
    int pool_size = 20;                         ///< 连接池大小
    int connection_timeout_ms = 5000;           ///< 连接超时（毫秒）
    int socket_timeout_ms = 3000;               ///< Socket 超时（毫秒）
    int max_idle_time_ms = 30000;               ///< 最大空闲时间（毫秒）

    /**
     * @brief 从 JSON 解析
     */
    static RedisConfig fromJson(const nlohmann::json& j) {
        RedisConfig config;
        config.host = j.value("host", "127.0.0.1");
        config.port = j.value("port", 6379);
        config.password = j.value("password", "");
        config.database = j.value("database", 0);
        config.pool_size = j.value("pool_size", 20);
        config.connection_timeout_ms = j.value("connection_timeout_ms", 5000);
        config.socket_timeout_ms = j.value("socket_timeout_ms", 3000);
        config.max_idle_time_ms = j.value("max_idle_time_ms", 30000);
        return config;
    }

    /**
     * @brief 从 ConfigManager 加载
     * @details 使用 "redis." 前缀从配置管理器读取配置
     */
    static RedisConfig fromConfigManager() {
        auto& cm = common::config::ConfigManager::getInstance();
        RedisConfig config;

        config.host = cm.get<std::string>("redis.host", "127.0.0.1");
        config.port = cm.get<int>("redis.port", 6379);
        config.password = cm.get<std::string>("redis.password", "");
        config.database = cm.get<int>("redis.database", 0);
        config.pool_size = cm.get<int>("redis.pool_size", 20);
        config.connection_timeout_ms = cm.get<int>("redis.connection_timeout_ms", 5000);
        config.socket_timeout_ms = cm.get<int>("redis.socket_timeout_ms", 3000);
        config.max_idle_time_ms = cm.get<int>("redis.max_idle_time_ms", 30000);

        return config;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["host"] = host;
        j["port"] = port;
        j["password"] = password;
        j["database"] = database;
        j["pool_size"] = pool_size;
        j["connection_timeout_ms"] = connection_timeout_ms;
        j["socket_timeout_ms"] = socket_timeout_ms;
        j["max_idle_time_ms"] = max_idle_time_ms;
        return j;
    }
};

/**
 * @brief HTTP 服务器配置
 */
struct HttpConfig {
    std::string host = "0.0.0.0";               ///< 监听地址
    int port = 8081;                            ///< 监听端口
    int thread_count = 4;                       ///< 工作线程数
    int max_connections = 1000;                 ///< 最大连接数
    int keep_alive_timeout_s = 60;              ///< Keep-Alive 超时（秒）
    int request_timeout_s = 30;                 ///< 请求超时（秒）

    /**
     * @brief 从 JSON 解析
     */
    static HttpConfig fromJson(const nlohmann::json& j) {
        HttpConfig config;
        config.host = j.value("host", "0.0.0.0");
        config.port = j.value("port", 8081);
        config.thread_count = j.value("thread_count", 4);
        config.max_connections = j.value("max_connections", 1000);
        config.keep_alive_timeout_s = j.value("keep_alive_timeout_s", 60);
        config.request_timeout_s = j.value("request_timeout_s", 30);
        return config;
    }

    /**
     * @brief 从 ConfigManager 加载
     * @details 使用 "http." 前缀从配置管理器读取配置
     */
    static HttpConfig fromConfigManager() {
        auto& cm = common::config::ConfigManager::getInstance();
        HttpConfig config;

        config.host = cm.get<std::string>("http.host", "0.0.0.0");
        config.port = cm.get<int>("http.port", 8081);
        config.thread_count = cm.get<int>("http.thread_count", 4);
        config.max_connections = cm.get<int>("http.max_connections", 1000);
        config.keep_alive_timeout_s = cm.get<int>("http.keep_alive_timeout_s", 60);
        config.request_timeout_s = cm.get<int>("http.request_timeout_s", 30);

        return config;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["host"] = host;
        j["port"] = port;
        j["thread_count"] = thread_count;
        j["max_connections"] = max_connections;
        j["keep_alive_timeout_s"] = keep_alive_timeout_s;
        j["request_timeout_s"] = request_timeout_s;
        return j;
    }
};

/**
 * @brief 注册中心配置
 */
struct RegistryConfig {
    // 服务 TTL（秒）- 心跳超时后服务将被自动注销
    int service_ttl_s = 60;

    // 心跳超时（秒）- 超过此时间未收到心跳，标记为不健康
    int heartbeat_timeout_s = 90;

    // 心跳警告阈值（秒）- 超过此时间开始降低健康分数
    int heartbeat_warning_threshold_s = 30;

    // 清理间隔（秒）- 定期清理过期服务
    int cleanup_interval_s = 30;

    // 健康检查间隔（秒）- 定期更新服务健康状态
    int health_check_interval_s = 15;

    // 统计更新间隔（秒）
    int stats_update_interval_s = 60;

    // 响应时间阈值（毫秒）- 超过此阈值开始降低健康分数
    int response_time_threshold_ms = 500;

    // 最大候选服务数（用于推荐）
    int max_recommendation_candidates = 10;

    // 是否启用事件发布
    bool enable_events = true;

    // 是否启用本地缓存
    bool enable_local_cache = true;

    // 本地缓存大小
    int local_cache_size = 1000;

    // 本地缓存 TTL（秒）
    int local_cache_ttl_s = 5;

    /**
     * @brief 从 JSON 解析
     */
    static RegistryConfig fromJson(const nlohmann::json& j) {
        RegistryConfig config;
        config.service_ttl_s = j.value("service_ttl_s", 60);
        config.heartbeat_timeout_s = j.value("heartbeat_timeout_s", 90);
        config.heartbeat_warning_threshold_s = j.value("heartbeat_warning_threshold_s", 30);
        config.cleanup_interval_s = j.value("cleanup_interval_s", 30);
        config.health_check_interval_s = j.value("health_check_interval_s", 15);
        config.stats_update_interval_s = j.value("stats_update_interval_s", 60);
        config.response_time_threshold_ms = j.value("response_time_threshold_ms", 500);
        config.max_recommendation_candidates = j.value("max_recommendation_candidates", 10);
        config.enable_events = j.value("enable_events", true);
        config.enable_local_cache = j.value("enable_local_cache", true);
        config.local_cache_size = j.value("local_cache_size", 1000);
        config.local_cache_ttl_s = j.value("local_cache_ttl_s", 5);
        return config;
    }

    /**
     * @brief 从 ConfigManager 加载
     * @details 使用 "registry." 前缀从配置管理器读取配置
     */
    static RegistryConfig fromConfigManager() {
        auto& cm = common::config::ConfigManager::getInstance();
        RegistryConfig config;

        config.service_ttl_s = cm.get<int>("registry.service_ttl_s", 60);
        config.heartbeat_timeout_s = cm.get<int>("registry.heartbeat_timeout_s", 90);
        config.heartbeat_warning_threshold_s = cm.get<int>("registry.heartbeat_warning_threshold_s", 30);
        config.cleanup_interval_s = cm.get<int>("registry.cleanup_interval_s", 30);
        config.health_check_interval_s = cm.get<int>("registry.health_check_interval_s", 15);
        config.stats_update_interval_s = cm.get<int>("registry.stats_update_interval_s", 60);
        config.response_time_threshold_ms = cm.get<int>("registry.response_time_threshold_ms", 500);
        config.max_recommendation_candidates = cm.get<int>("registry.max_recommendation_candidates", 10);
        config.enable_events = cm.get<bool>("registry.enable_events", true);
        config.enable_local_cache = cm.get<bool>("registry.enable_local_cache", true);
        config.local_cache_size = cm.get<int>("registry.local_cache_size", 1000);
        config.local_cache_ttl_s = cm.get<int>("registry.local_cache_ttl_s", 5);

        return config;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["service_ttl_s"] = service_ttl_s;
        j["heartbeat_timeout_s"] = heartbeat_timeout_s;
        j["heartbeat_warning_threshold_s"] = heartbeat_warning_threshold_s;
        j["cleanup_interval_s"] = cleanup_interval_s;
        j["health_check_interval_s"] = health_check_interval_s;
        j["stats_update_interval_s"] = stats_update_interval_s;
        j["response_time_threshold_ms"] = response_time_threshold_ms;
        j["max_recommendation_candidates"] = max_recommendation_candidates;
        j["enable_events"] = enable_events;
        j["enable_local_cache"] = enable_local_cache;
        j["local_cache_size"] = local_cache_size;
        j["local_cache_ttl_s"] = local_cache_ttl_s;
        return j;
    }
};

/**
 * @brief 服务注册中心完整配置
 */
struct ServiceRegistryConfig {
    std::string name = "service_registry";      ///< 服务名称
    std::string version = "1.0.0";              ///< 服务版本
    std::string environment = "development";    ///< 运行环境

    RedisConfig redis;                          ///< Redis 配置
    HttpConfig http;                            ///< HTTP 配置
    RegistryConfig registry;                    ///< 注册中心配置

    /**
     * @brief 从 JSON 解析
     */
    static ServiceRegistryConfig fromJson(const nlohmann::json& j) {
        ServiceRegistryConfig config;
        config.name = j.value("name", "service_registry");
        config.version = j.value("version", "1.0.0");
        config.environment = j.value("environment", "development");

        if (j.contains("redis")) {
            config.redis = RedisConfig::fromJson(j["redis"]);
        }
        if (j.contains("http")) {
            config.http = HttpConfig::fromJson(j["http"]);
        }
        if (j.contains("registry")) {
            config.registry = RegistryConfig::fromJson(j["registry"]);
        }

        return config;
    }

    /**
     * @brief 从 ConfigManager 加载
     * @details 从已加载的配置管理器中读取所有配置
     * @note 调用此方法前需要先调用 ConfigManager::loadFromYaml() 或 loadFromFile()
     */
    static ServiceRegistryConfig fromConfigManager() {
        auto& cm = common::config::ConfigManager::getInstance();
        ServiceRegistryConfig config;

        // 加载基本配置
        config.name = cm.get<std::string>("name", "service_registry");
        config.version = cm.get<std::string>("version", "1.0.0");
        config.environment = cm.get<std::string>("environment", "development");

        // 加载子配置
        config.redis = RedisConfig::fromConfigManager();
        config.http = HttpConfig::fromConfigManager();
        config.registry = RegistryConfig::fromConfigManager();

        return config;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["name"] = name;
        j["version"] = version;
        j["environment"] = environment;
        j["redis"] = redis.toJson();
        j["http"] = http.toJson();
        j["registry"] = registry.toJson();
        return j;
    }
};

} // namespace service_registry
} // namespace core_services
