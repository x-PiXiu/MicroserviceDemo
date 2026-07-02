/**
 * @file registry_manager.cpp
 * @brief 服务注册管理器实现
 * @details 实现服务实例的注册、注销、心跳等核心业务
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "registry_manager.h"
#include "event_publisher.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace core_services {
namespace service_registry {

RegistryManager::RegistryManager(
    std::shared_ptr<RedisStorage> storage,
    std::shared_ptr<IndexManager> index_manager,
    std::shared_ptr<common::database::RedisPool> redis_pool,
    const RegistryConfig& config
) : storage_(storage)
  , index_manager_(index_manager)
  , redis_pool_(redis_pool)
  , config_(config) {
    if (!storage_) {
        throw std::invalid_argument("Storage cannot be null");
    }
    if (!index_manager_) {
        throw std::invalid_argument("IndexManager cannot be null");
    }
}

// ==================== 服务注册/注销 ====================

bool RegistryManager::registerService(const ServiceInfo& service) {
    // 1. 参数校验
    if (!validateServiceInfo(service)) {
        LOG_ERROR("Invalid service info for registration");
        return false;
    }

    // 2. 检查是否已存在（幂等性）
    auto existing = storage_->get(service.service_name, service.host, service.port);
    if (existing) {
        // 更新而非重新创建
        ServiceInfo updated = service;
        updated.register_time = existing->register_time;  // 保留原注册时间
        updated.total_requests = existing->total_requests;
        updated.failed_requests = existing->failed_requests;

        if (!storage_->save(updated, config_.service_ttl_s)) {
            LOG_ERROR("Failed to update existing service: " + service.service_name);
            return false;
        }

        // 更新索引
        index_manager_->updateIndex(*existing, updated);

        LOG_DEBUG("Updated existing service: " + service.service_name + " / " + service.instanceId());
        return true;
    }

    // 3. 设置初始值
    ServiceInfo new_service = service;
    new_service.register_time = std::chrono::system_clock::now();
    new_service.last_heartbeat = new_service.register_time;
    new_service.healthy = true;
    new_service.health_score = 1.0;

    // 4. 持久化到 Redis
    if (!storage_->save(new_service, config_.service_ttl_s)) {
        LOG_ERROR("Failed to save service: " + service.service_name);
        return false;
    }

    // 5. 更新索引
    index_manager_->addToIndex(new_service);

    // 6. 发布事件
    publishEvent(ServiceEventType::REGISTERED, new_service);

    LOG_INFO("Registered service: " + service.service_name + " / " + service.instanceId());
    return true;
}

bool RegistryManager::deregisterService(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    // 1. 获取现有服务
    auto service = storage_->get(service_name, host, port);
    if (!service) {
        LOG_WARNING("Attempted to deregister unknown service: " + service_name);
        return false;  // 服务不存在
    }

    // 2. 从存储中删除
    if (!storage_->remove(service_name, host, port)) {
        LOG_ERROR("Failed to remove service from storage: " + service_name);
        return false;
    }

    // 3. 从索引中移除
    index_manager_->removeFromIndex(*service);

    // 4. 发布事件
    publishEvent(ServiceEventType::DEREGISTERED, *service);

    LOG_INFO("Deregistered service: " + service_name + " / " + host + ":" + std::to_string(port));
    return true;
}

int RegistryManager::batchRegister(const std::vector<ServiceInfo>& services) {
    int registered = 0;
    for (const auto& service : services) {
        if (registerService(service)) {
            registered++;
        }
    }
    return registered;
}

// ==================== 心跳管理 ====================

bool RegistryManager::updateHeartbeat(
    const std::string& service_name,
    const std::string& host,
    int port,
    const std::unordered_map<std::string, std::string>& metadata
) {
    // 1. 获取现有服务
    auto service = storage_->get(service_name, host, port);
    if (!service) {
        LOG_WARNING("Heartbeat for unknown service: " + service_name + " / " + host + ":" + std::to_string(port));
        return false;
    }

    // 2. 更新心跳时间和元数据
    service->last_heartbeat = std::chrono::system_clock::now();

    bool metadata_changed = false;
    if (!metadata.empty()) {
        // 合并元数据（保留旧值，更新新值）
        for (const auto& [k, v] : metadata) {
            if (service->metadata[k] != v) {
                service->metadata[k] = v;
                metadata_changed = true;
            }
        }
    }

    // 3. 如果之前不健康，现在恢复
    bool was_unhealthy = !service->healthy;
    if (was_unhealthy) {
        service->healthy = true;
        service->health_score = std::max(service->health_score, 0.5);
        index_manager_->updateHealthIndex(
            service_name + ":" + service->instanceId(),
            true
        );
    }

    // 4. 持久化并重置 TTL
    if (!storage_->save(*service, config_.service_ttl_s)) {
        LOG_ERROR("Failed to save heartbeat: " + service_name);
        return false;
    }

    // 5. 更新索引（如果元数据变化）
    if (metadata_changed) {
        index_manager_->addToIndex(*service);
    }

    // 6. 发布事件
    if (was_unhealthy) {
        publishEvent(ServiceEventType::RECOVERED, *service);
    } else if (metadata_changed) {
        publishEvent(ServiceEventType::METADATA_UPDATED, *service);
    }

    LOG_DEBUG("Updated heartbeat: " + service_name + " / " + service->instanceId());
    return true;
}

bool RegistryManager::renewRegistration(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    // 简单的重置 TTL
    return storage_->updateTTL(service_name, host, port, config_.service_ttl_s);
}

// ==================== 服务查询 ====================

std::optional<ServiceInfo> RegistryManager::getService(
    const std::string& service_name,
    const std::string& host,
    int port
) {
    return storage_->get(service_name, host, port);
}

std::vector<ServiceInfo> RegistryManager::getServiceInstances(const std::string& service_name) {
    return storage_->getServiceInstances(service_name);
}

std::vector<ServiceInfo> RegistryManager::getAllInstances() {
    return storage_->getAllInstances();
}

std::vector<std::string> RegistryManager::getAllServiceNames() {
    return index_manager_->getAllServiceNames();
}

// ==================== 清理操作 ====================

int RegistryManager::cleanupExpiredServices() {
    int cleaned = 0;

    try {
        // 扫描过期实例
        auto expired_keys = storage_->scanExpiredInstances(config_.heartbeat_timeout_s);

        for (const auto& key : expired_keys) {
            auto [service_name, host, port] = RedisStorage::parseKey(key);

            // 获取服务信息并发布事件
            auto service = storage_->getByKey(key);
            if (service) {
                publishEvent(ServiceEventType::HEARTBEAT_MISSED, *service);

                // 从存储中删除
                if (storage_->remove(service_name, host, port)) {
                    // 从索引中移除
                    index_manager_->removeFromIndex(*service);
                    cleaned++;

                    LOG_INFO("Cleaned up expired service: " + service_name + " / " + host + ":" + std::to_string(port));
                }
            }
        }

        if (cleaned > 0) {
            LOG_INFO("Cleaned up " + std::to_string(cleaned) + " expired services");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("cleanupExpiredServices failed: " + std::string(e.what()));
    }

    return cleaned;
}

int RegistryManager::cleanupServiceExpiredInstances(const std::string& service_name) {
    int cleaned = 0;

    auto instances = storage_->getServiceInstances(service_name);
    for (const auto& instance : instances) {
        if (instance.heartbeatAgeSeconds() > config_.heartbeat_timeout_s) {
            if (deregisterService(instance.service_name, instance.host, instance.port)) {
                cleaned++;
            }
        }
    }

    return cleaned;
}

int RegistryManager::rebuildIndexes(const std::vector<ServiceInfo>& services) {
    return index_manager_->rebuildIndexes(services);
}

// ==================== 统计信息 ====================

nlohmann::json RegistryManager::getStats() {
    nlohmann::json stats;

    try {
        auto all_instances = getAllInstances();

        size_t total_instances = all_instances.size();
        size_t healthy_instances = 0;
        size_t unhealthy_instances = 0;
        size_t degraded_instances = 0;

        double total_health_score = 0.0;
        double total_cpu = 0.0;
        double total_memory = 0.0;
        int64_t total_players = 0;

        int cpu_count = 0;
        int memory_count = 0;

        std::unordered_map<std::string, size_t> by_service;
        std::unordered_map<std::string, size_t> by_game_type;
        std::unordered_map<std::string, size_t> by_region;

        for (const auto& instance : all_instances) {
            // 健康状态统计
            if (instance.healthy && instance.health_score >= 0.7) {
                healthy_instances++;
            } else if (instance.health_score >= 0.3) {
                degraded_instances++;
            } else {
                unhealthy_instances++;
            }

            total_health_score += instance.health_score;

            // 按服务统计
            by_service[instance.service_name]++;

            // 按游戏类型统计
            auto game_type = instance.getMetadataString("game_type");
            if (game_type) {
                by_game_type[*game_type]++;
            }

            // 按区域统计
            auto region = instance.getMetadataString("region");
            if (region) {
                by_region[*region]++;
            }

            // 资源统计
            auto cpu = instance.getMetadataDouble("cpu_usage");
            if (cpu) {
                total_cpu += *cpu;
                cpu_count++;
            }

            auto memory = instance.getMetadataDouble("memory_usage");
            if (memory) {
                total_memory += *memory;
                memory_count++;
            }

            auto players = instance.getMetadataInt("current_players");
            if (players) {
                total_players += *players;
            }
        }

        stats["total_services"] = by_service.size();
        stats["total_instances"] = total_instances;
        stats["healthy_instances"] = healthy_instances;
        stats["unhealthy_instances"] = unhealthy_instances;
        stats["degraded_instances"] = degraded_instances;

        stats["instances_by_service"] = by_service;
        stats["instances_by_game_type"] = by_game_type;
        stats["instances_by_region"] = by_region;

        stats["avg_health_score"] = total_instances > 0 ? total_health_score / total_instances : 0.0;
        stats["avg_cpu_usage"] = cpu_count > 0 ? total_cpu / cpu_count : 0.0;
        stats["avg_memory_usage"] = memory_count > 0 ? total_memory / memory_count : 0.0;
        stats["total_current_players"] = total_players;

        // 添加今日对局统计
        stats["today_matches"] = getTodayMatches();

        stats["updated_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

    } catch (const std::exception& e) {
        LOG_ERROR("getStats failed: " + std::string(e.what()));
    }

    return stats;
}

// ==================== 私有方法 ====================

bool RegistryManager::validateServiceInfo(const ServiceInfo& service) {
    // 检查必填字段
    if (service.service_name.empty()) {
        LOG_ERROR("Service name is required");
        return false;
    }

    if (service.host.empty()) {
        LOG_ERROR("Host is required");
        return false;
    }

    if (service.port <= 0 || service.port > 65535) {
        LOG_ERROR("Invalid port: " + std::to_string(service.port));
        return false;
    }

    // 检查服务名称格式
    for (char c : service.service_name) {
        if (!std::isalnum(c) && c != '_' && c != '-') {
            LOG_ERROR("Invalid service name format: " + service.service_name);
            return false;
        }
    }

    return true;
}

void RegistryManager::publishEvent(ServiceEventType type, const ServiceInfo& service) {
    if (event_publisher_ && config_.enable_events) {
        event_publisher_->publish(type, service);
    }
}

// ==================== 对局统计 ====================

std::string RegistryManager::getTodayDateKey() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time_t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

bool RegistryManager::reportMatchCompletion(const std::string& game_type, const std::string& service_name) {
    if (!redis_pool_) {
        LOG_WARNING("Redis pool not available for match statistics");
        return false;
    }

    try {
        std::string date_key = getTodayDateKey();
        std::string redis_key = std::string(MATCH_STATS_KEY_PREFIX) + date_key;

        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for match statistics");
            return false;
        }

        // 增加今日对局计数
        long new_count = conn->incr(redis_key);

        // 设置过期时间为 48 小时（给一些缓冲时间）
        if (new_count == 1) {
            conn->expire(redis_key, 48 * 60 * 60);
        }

        LOG_INFO("Match reported: game_type=" + game_type +
                ", service=" + service_name +
                ", today_total=" + std::to_string(new_count));

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to report match completion: " + std::string(e.what()));
        return false;
    }
}

int64_t RegistryManager::getTodayMatches() {
    if (!redis_pool_) {
        return 0;
    }

    try {
        std::string date_key = getTodayDateKey();
        std::string redis_key = std::string(MATCH_STATS_KEY_PREFIX) + date_key;

        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for match statistics");
            return 0;
        }

        std::string value = conn->get(redis_key);
        if (value.empty()) {
            return 0;
        }

        return std::stoll(value);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get today matches: " + std::string(e.what()));
        return 0;
    }
}

} // namespace service_registry
} // namespace core_services
