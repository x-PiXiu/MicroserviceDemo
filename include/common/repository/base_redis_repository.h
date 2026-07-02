#pragma once

#include "i_redis_repository.h"
#include "redis_key_builder.h"
#include "redis_ttl_config.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <type_traits>
#include <sstream>

namespace common {
namespace repository {

/**
 * Redis 仓储基类实现
 *
 * 提供通用的 Redis 操作实现，子类只需实现序列化/反序列化方法
 *
 * 使用示例：
 * ```cpp
 * class UserRedisRepository : public BaseRedisRepository<UserInfo> {
 * public:
 *     UserRedisRepository(std::shared_ptr<RedisPool> pool)
 *         : BaseRedisRepository(pool, "user", "user", RedisTTLConfig::UserService::USER_INFO) {}
 *
 * protected:
 *     nlohmann::json toJson(const UserInfo& user) const override {
 *         return {{"id", user.id}, {"name", user.name}};
 *     }
 *
 *     std::optional<UserInfo> fromJson(const nlohmann::json& j) const override {
 *         UserInfo user;
 *         user.id = j.value("id", "");
 *         user.name = j.value("name", "");
 *         return user;
 *     }
 * };
 * ```
 */
template<typename T, typename IdType = std::string>
class BaseRedisRepository : public IRedisRepository<T, IdType> {
public:
    /**
     * 构造函数
     * @param redis_pool Redis 连接池
     * @param service_name 服务名称（用于键前缀）
     * @param entity_name 实体名称（用于键前缀）
     * @param default_ttl 默认过期时间
     */
    BaseRedisRepository(
        std::shared_ptr<database::RedisPool> redis_pool,
        const std::string& service_name,
        const std::string& entity_name,
        std::chrono::seconds default_ttl = RedisTTLConfig::DEFAULT_TTL
    ) : redis_pool_(redis_pool)
      , service_name_(service_name)
      , entity_name_(entity_name)
      , default_ttl_(default_ttl) {

        if (!redis_pool_) {
            throw std::invalid_argument("Redis pool cannot be null");
        }
    }

    virtual ~BaseRedisRepository() = default;

    // ========================================================================
    // IRedisRepository 实现
    // ========================================================================

    RedisResult<std::optional<T>> findById(const IdType& id) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(id);
            std::string value = conn->get(key);

            if (value.empty()) {
                return std::nullopt;
            }

            auto entity = deserialize(value);
            if (!entity.has_value()) {
                LOG_WARNING("Failed to deserialize entity for key: " + key);
                return std::nullopt;
            }

            return entity.value();

        } catch (const std::exception& e) {
            LOG_ERROR("Redis findById failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> save(
        const IdType& id,
        const T& entity,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(id);
            std::string value = serialize(entity);

            auto actual_ttl = ttl.count() > 0 ? ttl : default_ttl_;
            bool success = conn->set(key, value, static_cast<int>(actual_ttl.count()));

            if (success) {
                LOG_DEBUG("Saved entity to Redis: " + key);
            }

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis save failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> remove(const IdType& id) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(id);
            bool success = conn->del(key);

            if (success) {
                LOG_DEBUG("Removed entity from Redis: " + key);
            }

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis remove failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> exists(const IdType& id) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(id);
            return conn->exists(key);

        } catch (const std::exception& e) {
            LOG_ERROR("Redis exists failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<std::vector<std::pair<IdType, T>>> findByIds(
        const std::vector<IdType>& ids
    ) override {
        std::vector<std::pair<IdType, T>> result;

        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            for (const auto& id : ids) {
                std::string key = buildKey(id);
                std::string value = conn->get(key);

                if (!value.empty()) {
                    auto entity = deserialize(value);
                    if (entity.has_value()) {
                        result.emplace_back(id, entity.value());
                    }
                }
            }

            return result;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis findByIds failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> saveAll(
        const std::vector<std::pair<IdType, T>>& entities,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            auto actual_ttl = ttl.count() > 0 ? ttl : default_ttl_;

            // 使用管道批量写入
            conn->pipelineStart();
            for (const auto& [id, entity] : entities) {
                std::string key = buildKey(id);
                std::string value = serialize(entity);
                conn->pipelineAddSet(key, value);
                // 注意：管道模式下无法单独设置 TTL，需要在后续单独设置
            }
            conn->pipelineExec();

            // 单独设置 TTL
            for (const auto& [id, entity] : entities) {
                std::string key = buildKey(id);
                conn->expire(key, static_cast<int>(actual_ttl.count()));
            }

            LOG_DEBUG("Saved " + std::to_string(entities.size()) + " entities to Redis");
            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis saveAll failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> removeAll(const std::vector<IdType>& ids) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            for (const auto& id : ids) {
                std::string key = buildKey(id);
                conn->del(key);
            }

            LOG_DEBUG("Removed " + std::to_string(ids.size()) + " entities from Redis");
            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis removeAll failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<T> getOrLoad(
        const IdType& id,
        std::function<std::optional<T>()> loader,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) override {
        // 先尝试从缓存获取
        auto cached = findById(id);
        if (!cached.has_value()) {
            return common::utils::unexpected(cached.error());
        }

        if (cached->has_value()) {
            return cached->value();
        }

        // 缓存未命中，使用 loader 加载
        auto loaded = loader();
        if (!loaded.has_value()) {
            return common::utils::unexpected(RepositoryError::notFound(
                "Entity not found and loader returned null"));
        }

        // 保存到缓存
        auto save_result = save(id, loaded.value(), ttl);
        if (!save_result.has_value() || !save_result.value()) {
            LOG_WARNING("Failed to cache loaded entity");
        }

        return loaded.value();
    }

    RedisResult<bool> invalidate(const IdType& id) override {
        return remove(id);
    }

    RedisResult<bool> invalidateAll() override {
        // 注意：这是一个危险操作，慎用
        LOG_WARNING("invalidateAll() called - this clears all cache for: " +
                    service_name_ + ":" + entity_name_);
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            // 构建模式匹配
            std::string pattern = service_name_ + ":" + entity_name_ + ":*";
            auto keys = conn->zrange(pattern, 0, -1, false);

            // 注意：这里需要使用 SCAN 命令，但当前 RedisConnection 未提供
            // 这是一个简化实现
            LOG_WARNING("invalidateAll() not fully implemented - pattern: " + pattern);
            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis invalidateAll failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> refreshTTL(
        const IdType& id,
        std::chrono::seconds ttl
    ) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(id);
            bool success = conn->expire(key, static_cast<int>(ttl.count()));

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis refreshTTL failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<int64_t> getTTL(const IdType& id) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(id);
            int64_t ttl = conn->ttl(key);

            return ttl;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis getTTL failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> createIndex(
        const std::string& index_name,
        const std::string& index_value,
        const IdType& id,
        std::chrono::seconds ttl = std::chrono::seconds(0)
    ) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            // 索引键格式：{service}:idx:{index_name}:{index_value}
            std::string index_key = service_name_ + ":idx:" + index_name + ":" + index_value;
            std::string id_str = convertIdToString(id);

            auto actual_ttl = ttl.count() > 0 ? ttl : default_ttl_;
            bool success = conn->set(index_key, id_str, static_cast<int>(actual_ttl.count()));

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis createIndex failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<std::optional<IdType>> findByIndex(
        const std::string& index_name,
        const std::string& index_value
    ) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string index_key = service_name_ + ":idx:" + index_name + ":" + index_value;
            std::string id_str = conn->get(index_key);

            if (id_str.empty()) {
                return std::nullopt;
            }

            return convertStringToId(id_str);

        } catch (const std::exception& e) {
            LOG_ERROR("Redis findByIndex failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    RedisResult<bool> removeIndex(
        const std::string& index_name,
        const std::string& index_value
    ) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string index_key = service_name_ + ":idx:" + index_name + ":" + index_value;
            bool success = conn->del(index_key);

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Redis removeIndex failed: " + std::string(e.what()));
            return common::utils::unexpected(RepositoryError::cacheError(e.what()));
        }
    }

    // ========================================================================
    // 元数据
    // ========================================================================

    std::string getServiceName() const override { return service_name_; }
    std::string getEntityName() const override { return entity_name_; }
    std::chrono::seconds getDefaultTTL() const override { return default_ttl_; }

    std::string buildKey(const IdType& id) const override {
        return RedisKeyBuilder::entityKey(service_name_, entity_name_, convertIdToString(id));
    }

protected:
    // ========================================================================
    // 子类需要重写的方法
    // ========================================================================

    /**
     * 序列化实体为字符串（默认使用 JSON）
     */
    virtual std::string serialize(const T& entity) const {
        return toJson(entity).dump();
    }

    /**
     * 反序列化字符串为实体（默认使用 JSON）
     */
    virtual std::optional<T> deserialize(const std::string& data) const {
        try {
            auto json = nlohmann::json::parse(data);
            return fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to deserialize JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    /**
     * 将实体转换为 JSON（子类必须实现）
     */
    virtual nlohmann::json toJson(const T& entity) const = 0;

    /**
     * 从 JSON 解析实体（子类必须实现）
     */
    virtual std::optional<T> fromJson(const nlohmann::json& json) const = 0;

    // ========================================================================
    // 辅助方法
    // ========================================================================

    /**
     * 将 ID 转换为字符串
     */
    virtual std::string convertIdToString(const IdType& id) const {
        if constexpr (std::is_same_v<IdType, std::string>) {
            return id;
        } else if constexpr (std::is_integral_v<IdType>) {
            return std::to_string(id);
        } else {
            // 尝试使用流转换
            std::ostringstream oss;
            oss << id;
            return oss.str();
        }
    }

    /**
     * 将字符串转换为 ID
     */
    virtual IdType convertStringToId(const std::string& str) const {
        if constexpr (std::is_same_v<IdType, std::string>) {
            return str;
        } else if constexpr (std::is_same_v<IdType, int>) {
            return std::stoi(str);
        } else if constexpr (std::is_same_v<IdType, int64_t>) {
            return std::stoll(str);
        } else {
            // 尝试使用流转换
            std::istringstream iss(str);
            IdType id;
            iss >> id;
            return id;
        }
    }

    /**
     * 获取 Redis 连接
     */
    std::shared_ptr<database::RedisConnection> getConnection() {
        if (!redis_pool_) {
            return nullptr;
        }
        return redis_pool_->getConnection();
    }

    /**
     * 返回 Redis 连接
     */
    void returnConnection(std::shared_ptr<database::RedisConnection> conn) {
        if (redis_pool_ && conn) {
            redis_pool_->returnConnection(conn);
        }
    }

protected:
    std::shared_ptr<database::RedisPool> redis_pool_;
    std::string service_name_;
    std::string entity_name_;
    std::chrono::seconds default_ttl_;
};

} // namespace repository
} // namespace common
