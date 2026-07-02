/**
 * @file redis_storage.h
 * @brief Redis 存储层接口
 * @details 提供服务实例的 Redis 持久化操作
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "registry_config.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <memory>
#include <vector>
#include <optional>
#include <functional>

namespace core_services {
namespace service_registry {

/**
 * @brief Redis 存储操作结果
 */
template<typename T>
using StorageResult = std::pair<bool, std::optional<T>>;

/**
 * @brief Redis 存储层
 * @details 负责服务实例在 Redis 中的 CRUD 操作
 */
class RedisStorage {
public:
    /**
     * @brief 构造函数
     * @param redis_pool Redis 连接池
     * @param config 注册中心配置
     */
    RedisStorage(
        std::shared_ptr<common::database::RedisPool> redis_pool,
        const RegistryConfig& config
    );

    /**
     * @brief 析构函数
     */
    ~RedisStorage() = default;

    // ==================== 服务实例操作 ====================

    /**
     * @brief 保存服务实例
     * @param service 服务信息
     * @param ttl_seconds TTL（秒），0 表示使用默认值
     * @return 操作是否成功
     */
    bool save(const ServiceInfo& service, int ttl_seconds = 0);

    /**
     * @brief 获取服务实例
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return 服务信息（如果存在）
     */
    std::optional<ServiceInfo> get(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    /**
     * @brief 获取服务实例（通过 Redis Key）
     * @param key Redis Key
     * @return 服务信息（如果存在）
     */
    std::optional<ServiceInfo> getByKey(const std::string& key);

    /**
     * @brief 删除服务实例
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return 操作是否成功
     */
    bool remove(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    /**
     * @brief 检查服务实例是否存在
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return 是否存在
     */
    bool exists(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    /**
     * @brief 更新服务 TTL
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @param ttl_seconds 新的 TTL
     * @return 操作是否成功
     */
    bool updateTTL(
        const std::string& service_name,
        const std::string& host,
        int port,
        int ttl_seconds
    );

    // ==================== 批量操作 ====================

    /**
     * @brief 批量获取服务实例
     * @param keys Redis Key 列表
     * @return 服务信息列表
     */
    std::vector<ServiceInfo> batchGet(const std::vector<std::string>& keys);

    /**
     * @brief 批量保存服务实例
     * @param services 服务信息列表
     * @param ttl_seconds TTL（秒）
     * @return 成功保存的数量
     */
    int batchSave(const std::vector<ServiceInfo>& services, int ttl_seconds = 0);

    /**
     * @brief 批量删除服务实例
     * @param keys Redis Key 列表
     * @return 成功删除的数量
     */
    int batchRemove(const std::vector<std::string>& keys);

    // ==================== 查询操作 ====================

    /**
     * @brief 获取所有服务名称
     * @return 服务名称集合
     */
    std::vector<std::string> getAllServiceNames();

    /**
     * @brief 获取特定服务的所有实例 Key
     * @param service_name 服务名称
     * @return 实例 Key 列表
     */
    std::vector<std::string> getServiceInstanceKeys(const std::string& service_name);

    /**
     * @brief 获取特定服务的所有实例
     * @param service_name 服务名称
     * @return 服务实例列表
     */
    std::vector<ServiceInfo> getServiceInstances(const std::string& service_name);

    /**
     * @brief 获取所有服务实例
     * @return 所有服务实例列表
     */
    std::vector<ServiceInfo> getAllInstances();

    /**
     * @brief 扫描并返回过期的服务实例 Key
     * @param timeout_seconds 超时阈值（秒）
     * @return 过期的服务实例 Key 列表
     */
    std::vector<std::string> scanExpiredInstances(int timeout_seconds);

    // ==================== 辅助方法 ====================

    /**
     * @brief 生成 Redis Key
     * @param service_name 服务名称
     * @param host 主机地址
     * @param port 端口
     * @return Redis Key
     */
    static std::string buildKey(
        const std::string& service_name,
        const std::string& host,
        int port
    );

    /**
     * @brief 解析 Redis Key
     * @param key Redis Key
     * @return (service_name, host, port) 元组
     */
    static std::tuple<std::string, std::string, int> parseKey(const std::string& key);

    /**
     * @brief 获取统计信息
     * @return 统计 JSON
     */
    nlohmann::json getStats();

    /**
     * @brief 测试 Redis 连接
     * @return 是否连接正常
     */
    bool testConnection();

private:
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    RegistryConfig config_;

    // 获取 Redis 连接
    std::shared_ptr<common::database::RedisConnection> getConnection();

    // 序列化服务信息为 Redis Hash 字段
    std::unordered_map<std::string, std::string> serializeToHash(const ServiceInfo& service);

    // 从 Redis Hash 字段反序列化服务信息
    ServiceInfo deserializeFromHash(const std::unordered_map<std::string, std::string>& fields);
};

} // namespace service_registry
} // namespace core_services
