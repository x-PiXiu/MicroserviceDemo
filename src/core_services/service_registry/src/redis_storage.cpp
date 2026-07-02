/**
 * @file redis_storage.cpp
 * @brief Redis 存储层实现
 * @details 实现服务实例的 Redis 持久化操作
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "redis_storage.h"
#include <sstream>

namespace core_services {
namespace service_registry {

// Redis Key 前缀
static const std::string KEY_PREFIX = "service:registry:";
static const std::string INDEX_PREFIX = "service:index:";

RedisStorage::RedisStorage(
    std::shared_ptr<common::database::RedisPool> redis_pool,
    const RegistryConfig& config
) : redis_pool_(redis_pool)
  , config_(config) {
    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }
}

// ==================== 服务实例操作 ====================

bool RedisStorage::save(const ServiceInfo& service, int ttl_seconds) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for save");
            return false;
        }

        std::string key = buildKey(service.service_name, service.host, service.port);
        auto fields = serializeToHash(service);

        // 使用 HMSET 保存 Hash
        // 由于 RedisConnection 可能不直接支持 HMSET，我们使用多个 SET
        nlohmann::json json_data = service.toJson();
        std::string value = json_data.dump();

        int ttl = ttl_seconds > 0 ? ttl_seconds : config_.service_ttl_s;
        bool success = conn->set(key, value, ttl);

        if (success) {
            LOG_DEBUG("Saved service to Redis: " + key);
        } else {
            LOG_ERROR("Failed to save service to Redis: " + key);
        }

        return success;

    } catch (const std::exception& e) {
        LOG_ERROR("Redis save failed: " + std::string(e.what()));
        return false;
    }
}

std::optional<ServiceInfo> RedisStorage::get(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    std::string key = buildKey(service_name, host, port);
    return getByKey(key);
}

std::optional<ServiceInfo> RedisStorage::getByKey(const std::string& key) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for get");
            return std::nullopt;
        }

        std::string value = conn->get(key);
        if (value.empty()) {
            return std::nullopt;
        }

        auto json = nlohmann::json::parse(value);
        return ServiceInfo::fromJson(json);

    } catch (const std::exception& e) {
        LOG_ERROR("Redis get failed for key " + key + ": " + std::string(e.what()));
        return std::nullopt;
    }
}

bool RedisStorage::remove(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for remove");
            return false;
        }

        std::string key = buildKey(service_name, host, port);
        bool success = conn->del(key);

        if (success) {
            LOG_DEBUG("Removed service from Redis: " + key);
        }

        return success;

    } catch (const std::exception& e) {
        LOG_ERROR("Redis remove failed: " + std::string(e.what()));
        return false;
    }
}

bool RedisStorage::exists(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }

        std::string key = buildKey(service_name, host, port);
        return conn->exists(key);

    } catch (const std::exception& e) {
        LOG_ERROR("Redis exists failed: " + std::string(e.what()));
        return false;
    }
}

bool RedisStorage::updateTTL(
    const std::string& service_name,
    const std::string& host,
    int port,
    int ttl_seconds
) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }

        std::string key = buildKey(service_name, host, port);
        return conn->expire(key, ttl_seconds);

    } catch (const std::exception& e) {
        LOG_ERROR("Redis updateTTL failed: " + std::string(e.what()));
        return false;
    }
}

// ==================== 批量操作 ====================

std::vector<ServiceInfo> RedisStorage::batchGet(const std::vector<std::string>& keys) {
    std::vector<ServiceInfo> results;

    if (keys.empty()) {
        return results;
    }

    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for batchGet");
            return results;
        }

        // 由于 RedisConnection 可能不支持 MGET，逐个获取
        for (const auto& key : keys) {
            std::string value = conn->get(key);
            if (!value.empty()) {
                try {
                    auto json = nlohmann::json::parse(value);
                    results.push_back(ServiceInfo::fromJson(json));
                } catch (const std::exception& e) {
                    LOG_WARNING("Failed to parse service info for key " + key);
                }
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Redis batchGet failed: " + std::string(e.what()));
    }

    return results;
}

int RedisStorage::batchSave(const std::vector<ServiceInfo>& services, int ttl_seconds) {
    int saved = 0;

    for (const auto& service : services) {
        if (save(service, ttl_seconds)) {
            saved++;
        }
    }

    return saved;
}

int RedisStorage::batchRemove(const std::vector<std::string>& keys) {
    int removed = 0;

    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return 0;
        }

        for (const auto& key : keys) {
            if (conn->del(key)) {
                removed++;
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Redis batchRemove failed: " + std::string(e.what()));
    }

    return removed;
}

// ==================== 查询操作 ====================

std::vector<std::string> RedisStorage::getAllServiceNames() {
    std::vector<std::string> names;

    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return names;
        }

        // 获取服务名称索引集合
        std::string index_key = INDEX_PREFIX + "names";
        auto members = conn->smembers(index_key);

        for (const auto& member : members) {
            names.push_back(member);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Redis getAllServiceNames failed: " + std::string(e.what()));
    }

    return names;
}

std::vector<std::string> RedisStorage::getServiceInstanceKeys(const std::string& service_name) {
    std::vector<std::string> keys;

    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return keys;
        }

        // 获取服务实例索引集合
        std::string index_key = INDEX_PREFIX + service_name + ":instances";
        auto members = conn->smembers(index_key);

        for (const auto& member : members) {
            // 成员格式是 "host:port"，需要构建完整 key
            keys.push_back(KEY_PREFIX + service_name + ":" + member);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Redis getServiceInstanceKeys failed: " + std::string(e.what()));
    }

    return keys;
}

std::vector<ServiceInfo> RedisStorage::getServiceInstances(const std::string& service_name) {
    auto keys = getServiceInstanceKeys(service_name);
    return batchGet(keys);
}

std::vector<ServiceInfo> RedisStorage::getAllInstances() {
    std::vector<ServiceInfo> all_instances;

    auto service_names = getAllServiceNames();
    for (const auto& name : service_names) {
        auto instances = getServiceInstances(name);
        all_instances.insert(all_instances.end(), instances.begin(), instances.end());
    }

    return all_instances;
}

std::vector<std::string> RedisStorage::scanExpiredInstances(int timeout_seconds) {
    std::vector<std::string> expired_keys;

    try {
        auto all_instances = getAllInstances();

        for (const auto& service : all_instances) {
            if (service.heartbeatAgeSeconds() > timeout_seconds) {
                expired_keys.push_back(service.redisKey());
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Redis scanExpiredInstances failed: " + std::string(e.what()));
    }

    return expired_keys;
}

// ==================== 辅助方法 ====================

std::string RedisStorage::buildKey(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    return KEY_PREFIX + service_name + ":" + host + ":" + std::to_string(port);
}

std::tuple<std::string, std::string, int> RedisStorage::parseKey(const std::string& key) {
    // Key 格式: service:registry:{service_name}:{host}:{port}

    std::string service_name;
    std::string host;
    int port = 0;

    // 移除前缀
    std::string rest = key;
    if (rest.find(KEY_PREFIX) == 0) {
        rest = rest.substr(KEY_PREFIX.length());
    }

    // 解析剩余部分
    size_t first_colon = rest.find(':');
    if (first_colon != std::string::npos) {
        service_name = rest.substr(0, first_colon);
        rest = rest.substr(first_colon + 1);

        size_t last_colon = rest.rfind(':');
        if (last_colon != std::string::npos) {
            host = rest.substr(0, last_colon);
            try {
                port = std::stoi(rest.substr(last_colon + 1));
            } catch (...) {
                port = 0;
            }
        }
    }

    return {service_name, host, port};
}

nlohmann::json RedisStorage::getStats() {
    nlohmann::json stats;

    try {
        auto service_names = getAllServiceNames();
        stats["total_services"] = service_names.size();

        int total_instances = 0;
        int healthy_instances = 0;

        for (const auto& name : service_names) {
            auto instances = getServiceInstances(name);
            total_instances += instances.size();

            for (const auto& instance : instances) {
                if (instance.healthy) {
                    healthy_instances++;
                }
            }
        }

        stats["total_instances"] = total_instances;
        stats["healthy_instances"] = healthy_instances;
        stats["unhealthy_instances"] = total_instances - healthy_instances;

    } catch (const std::exception& e) {
        LOG_ERROR("Redis getStats failed: " + std::string(e.what()));
    }

    return stats;
}

bool RedisStorage::testConnection() {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }

        // 执行 PING 命令
        std::string result = conn->get("__test__");  // 简单测试
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Redis testConnection failed: " + std::string(e.what()));
        return false;
    }
}

// ==================== 私有方法 ====================

// 🔧 已移除 getConnection() 方法，所有调用改为使用 RedisConnectionGuard
// std::shared_ptr<common::database::RedisConnection> RedisStorage::getConnection() {
//     if (!redis_pool_) {
//         return nullptr;
//     }
//     return redis_pool_->getConnection();
// }

std::unordered_map<std::string, std::string> RedisStorage::serializeToHash(const ServiceInfo& service) {
    std::unordered_map<std::string, std::string> fields;

    fields["service_name"] = service.service_name;
    fields["service_version"] = service.service_version;
    fields["host"] = service.host;
    fields["port"] = std::to_string(service.port);
    fields["service_id"] = service.service_id;
    fields["health_check_endpoint"] = service.health_check_endpoint;
    fields["healthy"] = service.healthy ? "1" : "0";
    fields["health_score"] = std::to_string(service.health_score);
    fields["weight"] = std::to_string(service.weight);

    // 时间戳
    fields["register_time"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            service.register_time.time_since_epoch()
        ).count()
    );
    fields["last_heartbeat"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            service.last_heartbeat.time_since_epoch()
        ).count()
    );

    // 统计
    fields["total_requests"] = std::to_string(service.total_requests);
    fields["failed_requests"] = std::to_string(service.failed_requests);
    fields["consecutive_failures"] = std::to_string(service.consecutive_failures);
    fields["consecutive_successes"] = std::to_string(service.consecutive_successes);
    fields["avg_response_time_ms"] = std::to_string(service.avg_response_time_ms);

    // 元数据
    nlohmann::json metadata_json = service.metadata;
    fields["metadata"] = metadata_json.dump();

    // 端点
    nlohmann::json endpoints_json = service.endpoints;
    fields["endpoints"] = endpoints_json.dump();

    return fields;
}

ServiceInfo RedisStorage::deserializeFromHash(
    const std::unordered_map<std::string, std::string>& fields
) {
    ServiceInfo info;

    auto get_field = [&fields](const std::string& key, const std::string& default_val = "") {
        auto it = fields.find(key);
        return it != fields.end() ? it->second : default_val;
    };

    info.service_name = get_field("service_name");
    info.service_version = get_field("service_version", "1.0.0");
    info.host = get_field("host");
    info.port = std::stoi(get_field("port", "0"));
    info.service_id = get_field("service_id");
    info.health_check_endpoint = get_field("health_check_endpoint", "/health");
    info.healthy = get_field("healthy", "1") == "1";
    info.health_score = std::stod(get_field("health_score", "1.0"));
    info.weight = std::stoi(get_field("weight", "100"));

    // 时间戳
    int64_t register_ms = std::stoll(get_field("register_time", "0"));
    info.register_time = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(register_ms)
    );

    int64_t heartbeat_ms = std::stoll(get_field("last_heartbeat", "0"));
    info.last_heartbeat = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(heartbeat_ms)
    );

    // 统计
    info.total_requests = std::stoll(get_field("total_requests", "0"));
    info.failed_requests = std::stoll(get_field("failed_requests", "0"));
    info.consecutive_failures = std::stoi(get_field("consecutive_failures", "0"));
    info.consecutive_successes = std::stoi(get_field("consecutive_successes", "0"));
    info.avg_response_time_ms = std::stoll(get_field("avg_response_time_ms", "0"));

    // 元数据
    try {
        std::string metadata_str = get_field("metadata", "{}");
        auto metadata_json = nlohmann::json::parse(metadata_str);
        info.metadata = metadata_json.get<std::unordered_map<std::string, std::string>>();
    } catch (...) {}

    // 端点
    try {
        std::string endpoints_str = get_field("endpoints", "[]");
        auto endpoints_json = nlohmann::json::parse(endpoints_str);
        info.endpoints = endpoints_json.get<std::vector<std::string>>();
    } catch (...) {}

    return info;
}

} // namespace service_registry
} // namespace core_services
