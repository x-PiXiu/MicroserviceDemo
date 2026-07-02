/**
 * @file index_manager.cpp
 * @brief 服务索引管理器实现
 * @details 实现服务实例的多维度索引管理
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "index_manager.h"

namespace core_services {
namespace service_registry {

IndexManager::IndexManager(std::shared_ptr<common::database::RedisPool> redis_pool)
    : redis_pool_(redis_pool) {
    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }
}

// ==================== 索引维护 ====================

bool IndexManager::addToIndex(const ServiceInfo& service) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for addToIndex");
            return false;
        }

        std::string instance_id = service.instanceId();
        std::string full_instance_id = service.service_name + ":" + instance_id;

        // 1. 添加服务名称索引
        conn->sadd(INDEX_NAMES, service.service_name);

        // 2. 添加服务实例索引
        std::string service_instances_key = std::string("service:index:") + service.service_name + ":instances";
        conn->sadd(service_instances_key, instance_id);

        // 3. 添加游戏类型索引
        auto game_type = service.getMetadataString("game_type");
        if (game_type) {
            conn->sadd(INDEX_GAME_TYPES, *game_type);
            std::string game_type_key = std::string("service:index:game_type:") + *game_type;
            conn->sadd(game_type_key, full_instance_id);
        }

        // 4. 添加区域索引
        auto region = service.getMetadataString("region");
        if (region) {
            conn->sadd(INDEX_REGIONS, *region);
            std::string region_key = std::string("service:index:region:") + *region;
            conn->sadd(region_key, full_instance_id);
        }

        // 5. 添加健康状态索引
        if (service.healthy) {
            conn->sadd(INDEX_HEALTHY, full_instance_id);
            conn->srem(INDEX_UNHEALTHY, full_instance_id);
        } else {
            conn->sadd(INDEX_UNHEALTHY, full_instance_id);
            conn->srem(INDEX_HEALTHY, full_instance_id);
        }

        LOG_DEBUG("Added service to index: " + service.service_name + " / " + instance_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("addToIndex failed: " + std::string(e.what()));
        return false;
    }
}

bool IndexManager::removeFromIndex(const ServiceInfo& service) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }

        std::string instance_id = service.instanceId();
        std::string full_instance_id = service.service_name + ":" + instance_id;

        // 1. 从服务实例索引移除
        std::string service_instances_key = std::string("service:index:") + service.service_name + ":instances";
        conn->srem(service_instances_key, instance_id);

        // 检查是否还有其他实例，如果没有则移除服务名称
        auto remaining = conn->smembers(service_instances_key);
        if (remaining.empty()) {
            conn->srem(INDEX_NAMES, service.service_name);
        }

        // 2. 从游戏类型索引移除
        auto game_type = service.getMetadataString("game_type");
        if (game_type) {
            std::string game_type_key = std::string("service:index:game_type:") + *game_type;
            conn->srem(game_type_key, full_instance_id);

            // 检查是否还有其他实例
            auto remaining_game = conn->smembers(game_type_key);
            if (remaining_game.empty()) {
                conn->srem(INDEX_GAME_TYPES, *game_type);
            }
        }

        // 3. 从区域索引移除
        auto region = service.getMetadataString("region");
        if (region) {
            std::string region_key = std::string("service:index:region:") + *region;
            conn->srem(region_key, full_instance_id);

            // 检查是否还有其他实例
            auto remaining_region = conn->smembers(region_key);
            if (remaining_region.empty()) {
                conn->srem(INDEX_REGIONS, *region);
            }
        }

        // 4. 从健康状态索引移除
        conn->srem(INDEX_HEALTHY, full_instance_id);
        conn->srem(INDEX_UNHEALTHY, full_instance_id);

        LOG_DEBUG("Removed service from index: " + service.service_name + " / " + instance_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("removeFromIndex failed: " + std::string(e.what()));
        return false;
    }
}

bool IndexManager::updateIndex(const ServiceInfo& old_service, const ServiceInfo& new_service) {
    // 简单实现：先移除旧索引，再添加新索引
    // 可以优化为只更新变化的部分

    if (old_service.service_name != new_service.service_name ||
        old_service.host != new_service.host ||
        old_service.port != new_service.port) {
        // 实例标识发生变化，需要完全重建
        removeFromIndex(old_service);
        return addToIndex(new_service);
    }

    // 检查元数据是否变化
    auto old_game_type = old_service.getMetadataString("game_type");
    auto new_game_type = new_service.getMetadataString("game_type");
    auto old_region = old_service.getMetadataString("region");
    auto new_region = new_service.getMetadataString("region");

    bool need_reindex = false;

    if (old_game_type != new_game_type) {
        need_reindex = true;
    }
    if (old_region != new_region) {
        need_reindex = true;
    }
    if (old_service.healthy != new_service.healthy) {
        // 只需要更新健康状态索引
        updateHealthIndex(new_service.service_name + ":" + new_service.instanceId(), new_service.healthy);
    }

    if (need_reindex) {
        removeFromIndex(old_service);
        return addToIndex(new_service);
    }

    return true;
}

// ==================== 服务名称索引 ====================

std::vector<std::string> IndexManager::getAllServiceNames() {
    return getSetMembers(INDEX_NAMES);
}

std::vector<std::string> IndexManager::getInstancesByName(const std::string& service_name) {
    std::string key = std::string("service:index:") + service_name + ":instances";
    return getSetMembers(key);
}

bool IndexManager::serviceExists(const std::string& service_name) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }

        return conn->sismember(INDEX_NAMES, service_name);

    } catch (const std::exception& e) {
        LOG_ERROR("serviceExists failed: " + std::string(e.what()));
        return false;
    }
}

// ==================== 游戏类型索引 ====================

std::vector<std::string> IndexManager::getAllGameTypes() {
    return getSetMembers(INDEX_GAME_TYPES);
}

std::vector<std::string> IndexManager::getInstancesByGameType(const std::string& game_type) {
    std::string key = std::string("service:index:game_type:") + game_type;
    return getSetMembers(key);
}

// ==================== 区域索引 ====================

std::vector<std::string> IndexManager::getAllRegions() {
    return getSetMembers(INDEX_REGIONS);
}

std::vector<std::string> IndexManager::getInstancesByRegion(const std::string& region) {
    std::string key = std::string("service:index:region:") + region;
    return getSetMembers(key);
}

// ==================== 健康状态索引 ====================

std::vector<std::string> IndexManager::getHealthyInstances() {
    return getSetMembers(INDEX_HEALTHY);
}

std::vector<std::string> IndexManager::getUnhealthyInstances() {
    return getSetMembers(INDEX_UNHEALTHY);
}

void IndexManager::updateHealthIndex(const std::string& instance_id, bool healthy) {
    if (healthy) {
        addToSet(INDEX_HEALTHY, instance_id);
        removeFromSet(INDEX_UNHEALTHY, instance_id);
    } else {
        addToSet(INDEX_UNHEALTHY, instance_id);
        removeFromSet(INDEX_HEALTHY, instance_id);
    }
}

// ==================== 统计信息 ====================

nlohmann::json IndexManager::getStats() {
    nlohmann::json stats;

    try {
        auto service_names = getAllServiceNames();
        auto game_types = getAllGameTypes();
        auto regions = getAllRegions();
        auto healthy = getHealthyInstances();
        auto unhealthy = getUnhealthyInstances();

        stats["total_services"] = service_names.size();
        stats["total_game_types"] = game_types.size();
        stats["total_regions"] = regions.size();
        stats["healthy_instances"] = healthy.size();
        stats["unhealthy_instances"] = unhealthy.size();

        // 按服务统计实例数
        nlohmann::json by_service;
        for (const auto& name : service_names) {
            auto instances = getInstancesByName(name);
            by_service[name] = instances.size();
        }
        stats["by_service"] = by_service;

        // 按游戏类型统计实例数
        nlohmann::json by_game_type;
        for (const auto& type : game_types) {
            auto instances = getInstancesByGameType(type);
            by_game_type[type] = instances.size();
        }
        stats["by_game_type"] = by_game_type;

        // 按区域统计实例数
        nlohmann::json by_region;
        for (const auto& region : regions) {
            auto instances = getInstancesByRegion(region);
            by_region[region] = instances.size();
        }
        stats["by_region"] = by_region;

    } catch (const std::exception& e) {
        LOG_ERROR("getStats failed: " + std::string(e.what()));
    }

    return stats;
}

int IndexManager::rebuildIndexes(const std::vector<ServiceInfo>& services) {
    // 先清空所有索引
    clearAllIndexes();

    int count = 0;
    for (const auto& service : services) {
        if (addToIndex(service)) {
            count++;
        }
    }

    LOG_INFO("Rebuilt indexes for " + std::to_string(count) + " services");
    return count;
}

bool IndexManager::clearAllIndexes() {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }

        // 删除所有索引 key
        conn->del(INDEX_NAMES);
        conn->del(INDEX_GAME_TYPES);
        conn->del(INDEX_REGIONS);
        conn->del(INDEX_HEALTHY);
        conn->del(INDEX_UNHEALTHY);

        // 获取所有服务名称并删除对应的实例索引
        auto service_names = conn->smembers(INDEX_NAMES);
        for (const auto& name : service_names) {
            std::string key = std::string("service:index:") + name + ":instances";
            conn->del(key);
        }

        LOG_INFO("Cleared all indexes");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("clearAllIndexes failed: " + std::string(e.what()));
        return false;
    }
}

// ==================== 私有方法 ====================

// 🔧 已移除 getConnection() 方法，所有调用改为使用 RedisConnectionGuard
// std::shared_ptr<common::database::RedisConnection> IndexManager::getConnection() {
//     if (!redis_pool_) {
//         return nullptr;
//     }
//     return redis_pool_->getConnection();
// }

bool IndexManager::addToSet(const std::string& key, const std::string& member) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }
        conn->sadd(key, member);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("addToSet failed: " + std::string(e.what()));
        return false;
    }
}

bool IndexManager::removeFromSet(const std::string& key, const std::string& member) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return false;
        }
        conn->srem(key, member);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("removeFromSet failed: " + std::string(e.what()));
        return false;
    }
}

std::vector<std::string> IndexManager::getSetMembers(const std::string& key) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            return {};
        }
        return conn->smembers(key);
    } catch (const std::exception& e) {
        LOG_ERROR("getSetMembers failed: " + std::string(e.what()));
        return {};
    }
}

} // namespace service_registry
} // namespace core_services
