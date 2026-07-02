/**
 * @file registry_manager.h
 * @brief 服务注册管理器接口
 * @details 管理服务实例的注册、注销、心跳等核心业务
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "registry_config.h"
#include "redis_storage.h"
#include "index_manager.h"
#include "common/logger/logger.h"
#include "common/database/redis_pool.h"
#include <memory>
#include <vector>
#include <functional>

namespace core_services {
namespace service_registry {

// 前向声明
class EventPublisher;

/**
 * @brief 服务注册管理器
 * @details 核心业务逻辑，管理服务生命周期
 */
class RegistryManager {
public:
    /**
     * @brief 构造函数
     * @param storage Redis 存储层
     * @param index_manager 索引管理器
     * @param redis_pool Redis 连接池（用于对局统计）
     * @param config 注册中心配置
     */
    RegistryManager(
        std::shared_ptr<RedisStorage> storage,
        std::shared_ptr<IndexManager> index_manager,
        std::shared_ptr<common::database::RedisPool> redis_pool,
        const RegistryConfig& config
    );

    /**
     * @brief 析构函数
     */
    ~RegistryManager() = default;

    // ==================== 服务注册/注销 ====================

    /**
     * @brief 注册服务实例
     * @param service 服务信息
     * @return 操作是否成功
     */
    bool registerService(const ServiceInfo& service);

    /**
     * @brief 注销服务实例
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return 操作是否成功
     */
    bool deregisterService(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    /**
     * @brief 批量注册服务
     * @param services 服务列表
     * @return 成功注册的数量
     */
    int batchRegister(const std::vector<ServiceInfo>& services);

    // ==================== 心跳管理 ====================

    /**
     * @brief 更新心跳
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @param metadata 更新的元数据（可选）
     * @return 操作是否成功
     */
    bool updateHeartbeat(
        const std::string& service_name,
        const std::string& host,
        int port,
        const std::unordered_map<std::string, std::string>& metadata = {}
    );

    /**
     * @brief 续期服务注册
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return 操作是否成功
     */
    bool renewRegistration(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    // ==================== 服务查询 ====================

    /**
     * @brief 获取服务实例
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return 服务信息（如果存在）
     */
    std::optional<ServiceInfo> getService(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    /**
     * @brief 获取服务的所有实例
     * @param service_name 服务名称
     * @return 实例列表
     */
    std::vector<ServiceInfo> getServiceInstances(const std::string& service_name);

    /**
     * @brief 获取所有服务实例
     * @return 所有服务实例列表
     */
    std::vector<ServiceInfo> getAllInstances();

    /**
     * @brief 获取所有服务名称
     * @return 服务名称列表
     */
    std::vector<std::string> getAllServiceNames();

    // ==================== 清理操作 ====================

    /**
     * @brief 清理过期服务
     * @return 清理的实例数量
     */
    int cleanupExpiredServices();

    /**
     * @brief 清理指定服务的过期实例
     * @param service_name 服务名称
     * @return 清理的实例数量
     */
    int cleanupServiceExpiredInstances(const std::string& service_name);

    /**
     * @brief 重建索引
     * @param services 需要重建索引的服务列表
     * @return 重建索引的服务数量
     */
    int rebuildIndexes(const std::vector<ServiceInfo>& services);

    // ==================== 统计信息 ====================

    /**
     * @brief 获取统计信息
     * @return 统计 JSON
     */
    nlohmann::json getStats();

    // ==================== 对局统计 ====================

    /**
     * @brief 报告对局完成
     * @param game_type 游戏类型（如 "gomoku"）
     * @param service_name 报告的服务名称
     * @return 操作是否成功
     */
    bool reportMatchCompletion(const std::string& game_type, const std::string& service_name);

    /**
     * @brief 获取今日对局数
     * @return 今日对局总数
     */
    int64_t getTodayMatches();

    /**
     * @brief 设置事件发布器
     * @param publisher 事件发布器
     */
    void setEventPublisher(std::shared_ptr<EventPublisher> publisher) {
        event_publisher_ = publisher;
    }

private:
    std::shared_ptr<RedisStorage> storage_;
    std::shared_ptr<IndexManager> index_manager_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    std::shared_ptr<EventPublisher> event_publisher_;
    RegistryConfig config_;

    // 对局统计 Redis 键前缀
    static constexpr const char* MATCH_STATS_KEY_PREFIX = "registry:matches:";

    // 获取今日日期键（格式：YYYY-MM-DD）
    std::string getTodayDateKey();

    // 验证服务信息
    bool validateServiceInfo(const ServiceInfo& service);

    // 发布事件
    void publishEvent(ServiceEventType type, const ServiceInfo& service);
};

} // namespace service_registry
} // namespace core_services
