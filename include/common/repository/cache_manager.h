#pragma once

#include "repository_error.h"
#include "common/database/redis_pool.h"
#include <string>
#include <optional>
#include <chrono>
#include <functional>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <list>
#include <nlohmann/json.hpp>

namespace common {
namespace repository {

/**
 * 缓存配置
 */
struct CacheConfig {
    std::string key_prefix;                          // 键前缀
    std::chrono::seconds default_ttl{3600};          // 默认 TTL（1小时）
    bool enabled = true;                             // 是否启用缓存
    bool use_local_cache = true;                     // 使用 L1 本地缓存
    bool use_distributed_cache = true;               // 使用 L2 Redis 缓存
    size_t max_local_cache_size = 1000;              // 本地缓存最大条目数
    std::chrono::seconds local_cache_ttl{300};       // 本地缓存 TTL（5分钟）
};

/**
 * 缓存管理器接口
 */
class ICacheManager {
public:
    virtual ~ICacheManager() = default;

    /**
     * 获取缓存值
     */
    virtual std::optional<std::string> get(const std::string& key) = 0;

    /**
     * 设置缓存值
     */
    virtual bool set(const std::string& key,
                     const std::string& value,
                     std::chrono::seconds ttl = std::chrono::seconds(0)) = 0;

    /**
     * 删除缓存
     */
    virtual bool remove(const std::string& key) = 0;

    /**
     * 按模式删除缓存
     */
    virtual size_t removeByPattern(const std::string& pattern) = 0;

    /**
     * 检查键是否存在
     */
    virtual bool exists(const std::string& key) = 0;

    /**
     * 使实体缓存失效
     */
    virtual void invalidateEntity(const std::string& entity_type,
                                  const std::string& entity_id) = 0;

    /**
     * 获取或加载（Cache-Aside 模式）
     */
    template<typename T, typename Loader>
    std::optional<T> getOrLoad(const std::string& key,
                               Loader&& loader,
                               std::chrono::seconds ttl = std::chrono::seconds(0));
};

/**
 * 本地 LRU 缓存（线程安全）
 */
class LocalLRUCache {
public:
    explicit LocalLRUCache(size_t max_size = 1000,
                           std::chrono::seconds ttl = std::chrono::seconds(300));

    /**
     * 获取缓存值
     */
    std::optional<std::string> get(const std::string& key);

    /**
     * 设置缓存值
     */
    void set(const std::string& key,
             const std::string& value,
             std::chrono::seconds ttl = std::chrono::seconds(0));

    /**
     * 删除缓存
     */
    bool remove(const std::string& key);

    /**
     * 清空缓存
     */
    void clear();

    /**
     * 获取缓存大小
     */
    size_t size() const;

    /**
     * 检查键是否存在
     */
    bool exists(const std::string& key) const;

private:
    struct CacheEntry {
        std::string value;
        std::chrono::steady_clock::time_point expires_at;
    };

    size_t max_size_;
    std::chrono::seconds default_ttl_;

    // LRU 列表：front = 最近使用，back = 最久未使用
    std::list<std::string> lru_list_;

    // 缓存映射：key -> (entry, lru_iterator)
    std::unordered_map<std::string,
                       std::pair<CacheEntry, std::list<std::string>::iterator>> cache_;

    mutable std::shared_mutex mutex_;

    void evictIfNeeded();
    void cleanupExpired();
    bool isExpired(const CacheEntry& entry) const;
};

/**
 * 双层缓存管理器（L1: 本地, L2: Redis）
 */
class TwoLevelCacheManager : public ICacheManager {
public:
    /**
     * 构造函数
     * @param redis_pool Redis 连接池
     * @param config 缓存配置
     */
    TwoLevelCacheManager(
        std::shared_ptr<database::RedisPool> redis_pool,
        const CacheConfig& config = {}
    );

    ~TwoLevelCacheManager() override = default;

    // ICacheManager 实现
    std::optional<std::string> get(const std::string& key) override;
    bool set(const std::string& key,
             const std::string& value,
             std::chrono::seconds ttl = std::chrono::seconds(0)) override;
    bool remove(const std::string& key) override;
    size_t removeByPattern(const std::string& pattern) override;
    bool exists(const std::string& key) override;
    void invalidateEntity(const std::string& entity_type,
                          const std::string& entity_id) override;

    /**
     * 获取配置
     */
    const CacheConfig& getConfig() const { return config_; }

    /**
     * 设置配置
     */
    void setConfig(const CacheConfig& config);

    /**
     * 获取本地缓存
     */
    LocalLRUCache& getLocalCache() { return local_cache_; }

    /**
     * 清空本地缓存
     */
    void clearLocalCache();

    /**
     * 获取缓存统计
     */
    struct Stats {
        size_t local_hits{0};
        size_t local_misses{0};
        size_t remote_hits{0};
        size_t remote_misses{0};
        size_t total_requests{0};

        double localHitRate() const {
            return total_requests > 0 ?
                static_cast<double>(local_hits) / total_requests : 0.0;
        }

        double overallHitRate() const {
            return total_requests > 0 ?
                static_cast<double>(local_hits + remote_hits) / total_requests : 0.0;
        }
    };

    Stats getStats() const;
    void resetStats();

private:
    std::shared_ptr<database::RedisPool> redis_pool_;
    CacheConfig config_;
    LocalLRUCache local_cache_;

    mutable Stats stats_;
    mutable std::mutex stats_mutex_;

    std::string buildKey(const std::string& key) const;
    std::string buildEntityKey(const std::string& entity_type,
                               const std::string& entity_id) const;
};

/**
 * 仅 Redis 缓存管理器
 */
class RedisCacheManager : public ICacheManager {
public:
    explicit RedisCacheManager(
        std::shared_ptr<database::RedisPool> redis_pool,
        const std::string& key_prefix = "cache:"
    );

    std::optional<std::string> get(const std::string& key) override;
    bool set(const std::string& key,
             const std::string& value,
             std::chrono::seconds ttl = std::chrono::seconds(0)) override;
    bool remove(const std::string& key) override;
    size_t removeByPattern(const std::string& pattern) override;
    bool exists(const std::string& key) override;
    void invalidateEntity(const std::string& entity_type,
                          const std::string& entity_id) override;

private:
    std::shared_ptr<database::RedisPool> redis_pool_;
    std::string key_prefix_;

    std::string buildKey(const std::string& key) const;
};

/**
 * 空缓存管理器（用于禁用缓存）
 */
class NullCacheManager : public ICacheManager {
public:
    std::optional<std::string> get(const std::string& /*key*/) override {
        return std::nullopt;
    }

    bool set(const std::string& /*key*/,
             const std::string& /*value*/,
             std::chrono::seconds /*ttl*/) override {
        return true;
    }

    bool remove(const std::string& /*key*/) override {
        return true;
    }

    size_t removeByPattern(const std::string& /*pattern*/) override {
        return 0;
    }

    bool exists(const std::string& /*key*/) override {
        return false;
    }

    void invalidateEntity(const std::string& /*entity_type*/,
                          const std::string& /*entity_id*/) override {
    }
};

/**
 * 缓存助手（模板方法）
 */
class CacheHelper {
public:
    /**
     * 序列化对象到 JSON 字符串
     */
    template<typename T>
    static std::string serialize(const T& obj) {
        return nlohmann::json(obj).dump();
    }

    /**
     * 从 JSON 字符串反序列化对象
     */
    template<typename T>
    static std::optional<T> deserialize(const std::string& data) {
        try {
            auto j = nlohmann::json::parse(data);
            return j.get<T>();
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    /**
     * 获取或加载（带序列化）
     */
    template<typename T, typename Loader>
    static std::optional<T> getOrLoad(
        ICacheManager& cache,
        const std::string& key,
        Loader&& loader,
        std::chrono::seconds ttl = std::chrono::seconds(0)) {

        // 尝试从缓存获取
        auto cached = cache.get(key);
        if (cached.has_value()) {
            auto obj = deserialize<T>(*cached);
            if (obj.has_value()) {
                return obj;
            }
        }

        // 加载数据
        auto obj = loader();
        if (!obj.has_value()) {
            return std::nullopt;
        }

        // 存入缓存
        cache.set(key, serialize(*obj), ttl);

        return obj;
    }

    /**
     * 构建实体缓存键
     */
    static std::string buildEntityKey(
        const std::string& entity_type,
        const std::string& id) {
        return entity_type + ":" + id;
    }

    /**
     * 构建列表缓存键
     */
    static std::string buildListKey(
        const std::string& entity_type,
        const std::string& suffix = "") {
        std::string key = entity_type + ":list";
        if (!suffix.empty()) {
            key += ":" + suffix;
        }
        return key;
    }
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename T, typename Loader>
std::optional<T> ICacheManager::getOrLoad(
    const std::string& key,
    Loader&& loader,
    std::chrono::seconds ttl) {

    return CacheHelper::getOrLoad<T>(*this, key,
        std::forward<Loader>(loader), ttl);
}

} // namespace repository
} // namespace common
