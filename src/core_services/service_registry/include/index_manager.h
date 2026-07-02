/**
 * @file index_manager.h
 * @brief 服务索引管理器接口
 * @details 管理服务实例的多维度索引
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace core_services {
namespace service_registry {

/**
 * @brief 服务索引管理器
 * @details 管理服务名称索引、游戏类型索引、区域索引等
 */
class IndexManager {
public:
    /**
     * @brief 构造函数
     * @param redis_pool Redis 连接池
     */
    explicit IndexManager(std::shared_ptr<common::database::RedisPool> redis_pool);

    /**
     * @brief 析构函数
     */
    ~IndexManager() = default;

    // ==================== 索引维护 ====================

    /**
     * @brief 添加服务到索引
     * @param service 服务信息
     * @return 操作是否成功
     */
    bool addToIndex(const ServiceInfo& service);

    /**
     * @brief 从索引中移除服务
     * @param service 服务信息
     * @return 操作是否成功
     */
    bool removeFromIndex(const ServiceInfo& service);

    /**
     * @brief 更新服务索引
     * @param old_service 旧服务信息（用于移除旧索引）
     * @param new_service 新服务信息（用于添加新索引）
     * @return 操作是否成功
     */
    bool updateIndex(const ServiceInfo& old_service, const ServiceInfo& new_service);

    // ==================== 服务名称索引 ====================

    /**
     * @brief 获取所有服务名称
     * @return 服务名称列表
     */
    std::vector<std::string> getAllServiceNames();

    /**
     * @brief 获取服务的所有实例 ID
     * @param service_name 服务名称
     * @return 实例 ID 列表（格式: host:port）
     */
    std::vector<std::string> getInstancesByName(const std::string& service_name);

    /**
     * @brief 检查服务名称是否存在
     * @param service_name 服务名称
     * @return 是否存在
     */
    bool serviceExists(const std::string& service_name);

    // ==================== 游戏类型索引 ====================

    /**
     * @brief 获取所有游戏类型
     * @return 游戏类型列表
     */
    std::vector<std::string> getAllGameTypes();

    /**
     * @brief 获取指定游戏类型的所有实例
     * @param game_type 游戏类型
     * @return 实例列表（格式: service_name:host:port）
     */
    std::vector<std::string> getInstancesByGameType(const std::string& game_type);

    // ==================== 区域索引 ====================

    /**
     * @brief 获取所有区域
     * @return 区域列表
     */
    std::vector<std::string> getAllRegions();

    /**
     * @brief 获取指定区域的所有实例
     * @param region 区域
     * @return 实例列表（格式: service_name:host:port）
     */
    std::vector<std::string> getInstancesByRegion(const std::string& region);

    // ==================== 健康状态索引 ====================

    /**
     * @brief 获取健康实例列表
     * @return 实例列表
     */
    std::vector<std::string> getHealthyInstances();

    /**
     * @brief 获取不健康实例列表
     * @return 实例列表
     */
    std::vector<std::string> getUnhealthyInstances();

    /**
     * @brief 更新实例健康状态索引
     * @param instance_id 实例 ID
     * @param healthy 是否健康
     */
    void updateHealthIndex(const std::string& instance_id, bool healthy);

    // ==================== 统计信息 ====================

    /**
     * @brief 获取索引统计信息
     * @return 统计 JSON
     */
    nlohmann::json getStats();

    /**
     * @brief 重建所有索引
     * @param services 所有服务实例
     * @return 重建的索引数量
     */
    int rebuildIndexes(const std::vector<ServiceInfo>& services);

    /**
     * @brief 清空所有索引
     * @return 操作是否成功
     */
    bool clearAllIndexes();

private:
    std::shared_ptr<common::database::RedisPool> redis_pool_;

    // 索引 Key 常量
    static constexpr const char* INDEX_NAMES = "service:index:names";
    static constexpr const char* INDEX_GAME_TYPES = "service:index:game_types";
    static constexpr const char* INDEX_REGIONS = "service:index:regions";
    static constexpr const char* INDEX_HEALTHY = "service:index:healthy";
    static constexpr const char* INDEX_UNHEALTHY = "service:index:unhealthy";

    // 获取 Redis 连接
    std::shared_ptr<common::database::RedisConnection> getConnection();

    // 添加到集合索引
    bool addToSet(const std::string& key, const std::string& member);

    // 从集合索引移除
    bool removeFromSet(const std::string& key, const std::string& member);

    // 获取集合所有成员
    std::vector<std::string> getSetMembers(const std::string& key);
};

} // namespace service_registry
} // namespace core_services
