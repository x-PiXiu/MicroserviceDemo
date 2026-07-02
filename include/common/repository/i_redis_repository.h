#pragma once

#include "repository_error.h"
#include "redis_key_builder.h"
#include "redis_ttl_config.h"
#include <string>
#include <optional>
#include <vector>
#include <chrono>
#include <functional>

namespace common {
namespace repository {

/**
 * Redis 操作结果类型
 */
template<typename T>
using RedisResult = std::expected<T, RepositoryError>;

/**
 * Redis 仓储接口
 *
 * 提供统一的 Redis 操作抽象，支持泛型实体类型
 *
 * @tparam T 实体类型
 * @tparam IdType ID 类型（默认为 std::string）
 */
template<typename T, typename IdType = std::string>
class IRedisRepository {
public:
    virtual ~IRedisRepository() = default;

    // ========================================================================
    // 基础 CRUD 操作
    // ========================================================================

    /**
     * 根据 ID 查找实体
     * @param id 实体 ID
     * @return 实体（如果存在），失败返回错误
     */
    virtual RedisResult<std::optional<T>> findById(const IdType& id) = 0;

    /**
     * 保存实体到 Redis
     * @param id 实体 ID
     * @param entity 实体数据
     * @param ttl 过期时间（0 表示使用默认 TTL）
     * @return 是否成功
     */
    virtual RedisResult<bool> save(
        const IdType& id,
        const T& entity,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) = 0;

    /**
     * 从 Redis 删除实体
     * @param id 实体 ID
     * @return 是否成功
     */
    virtual RedisResult<bool> remove(const IdType& id) = 0;

    /**
     * 检查实体是否存在
     * @param id 实体 ID
     * @return 是否存在
     */
    virtual RedisResult<bool> exists(const IdType& id) = 0;

    // ========================================================================
    // 批量操作
    // ========================================================================

    /**
     * 批量查找实体
     * @param ids ID 列表
     * @return 找到的实体列表（ID, Entity 对）
     */
    virtual RedisResult<std::vector<std::pair<IdType, T>>> findByIds(
        const std::vector<IdType>& ids
    ) = 0;

    /**
     * 批量保存实体
     * @param entities 实体列表（ID, Entity 对）
     * @param ttl 过期时间
     * @return 是否成功
     */
    virtual RedisResult<bool> saveAll(
        const std::vector<std::pair<IdType, T>>& entities,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) = 0;

    /**
     * 批量删除实体
     * @param ids ID 列表
     * @return 是否成功
     */
    virtual RedisResult<bool> removeAll(const std::vector<IdType>& ids) = 0;

    // ========================================================================
    // 缓存操作
    // ========================================================================

    /**
     * Cache-Aside 模式：获取或加载
     * 如果缓存中存在则返回，否则使用 loader 加载并缓存
     *
     * @param id 实体 ID
     * @param loader 加载函数
     * @param ttl 缓存过期时间
     * @return 实体数据
     */
    virtual RedisResult<T> getOrLoad(
        const IdType& id,
        std::function<std::optional<T>()> loader,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) = 0;

    /**
     * 使指定实体的缓存失效
     * @param id 实体 ID
     * @return 是否成功
     */
    virtual RedisResult<bool> invalidate(const IdType& id) = 0;

    /**
     * 使所有缓存失效（慎用）
     * @return 是否成功
     */
    virtual RedisResult<bool> invalidateAll() = 0;

    /**
     * 刷新实体的 TTL
     * @param id 实体 ID
     * @param ttl 新的过期时间
     * @return 是否成功
     */
    virtual RedisResult<bool> refreshTTL(
        const IdType& id,
        std::chrono::seconds ttl
    ) = 0;

    /**
     * 获取实体的剩余 TTL
     * @param id 实体 ID
     * @return 剩余秒数，-1 表示永不过期，-2 表示不存在
     */
    virtual RedisResult<int64_t> getTTL(const IdType& id) = 0;

    // ========================================================================
    // 索引操作（二级索引支持）
    // ========================================================================

    /**
     * 创建二级索引
     * @param index_name 索引名称
     * @param index_value 索引值
     * @param id 实体 ID
     * @param ttl 索引过期时间
     * @return 是否成功
     */
    virtual RedisResult<bool> createIndex(
        const std::string& index_name,
        const std::string& index_value,
        const IdType& id,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) = 0;

    /**
     * 通过二级索引查找实体 ID
     * @param index_name 索引名称
     * @param index_value 索引值
     * @return 实体 ID（如果存在）
     */
    virtual RedisResult<std::optional<IdType>> findByIndex(
        const std::string& index_name,
        const std::string& index_value
    ) = 0;

    /**
     * 删除二级索引
     * @param index_name 索引名称
     * @param index_value 索引值
     * @return 是否成功
     */
    virtual RedisResult<bool> removeIndex(
        const std::string& index_name,
        const std::string& index_value
    ) = 0;

    // ========================================================================
    // 元数据
    // ========================================================================

    /**
     * 获取服务名称
     */
    virtual std::string getServiceName() const = 0;

    /**
     * 获取实体类型名称
     */
    virtual std::string getEntityName() const = 0;

    /**
     * 获取默认 TTL
     */
    virtual std::chrono::seconds getDefaultTTL() const = 0;

    /**
     * 构建实体键
     */
    virtual std::string buildKey(const IdType& id) const = 0;
};

/**
 * Redis 仓储基础接口（简化版）
 * 仅包含最基本的操作，用于简单场景
 */
template<typename T, typename IdType = std::string>
class ISimpleRedisRepository {
public:
    virtual ~ISimpleRedisRepository() = default;

    virtual RedisResult<std::optional<T>> get(const IdType& id) = 0;
    virtual RedisResult<bool> set(const IdType& id, const T& entity, int ttl_seconds = 0) = 0;
    virtual RedisResult<bool> del(const IdType& id) = 0;
    virtual RedisResult<bool> exists(const IdType& id) = 0;
};

} // namespace repository
} // namespace common
