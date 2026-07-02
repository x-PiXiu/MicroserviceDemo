#include "common/repository/cache_manager.h"
#include "common/logger/logger.h"
#include <algorithm>
#include <stdexcept>

namespace common {
namespace repository {

// ============================================================================
// LocalLRUCache Implementation
// ============================================================================

LocalLRUCache::LocalLRUCache(size_t max_size, std::chrono::seconds ttl)
    : max_size_(max_size)
    , default_ttl_(ttl) {
}

std::optional<std::string> LocalLRUCache::get(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return std::nullopt;
    }

    // 检查是否过期
    if (isExpired(it->second.first)) {
        // 移除过期条目
        lru_list_.erase(it->second.second);
        cache_.erase(it);
        return std::nullopt;
    }

    // 更新 LRU 顺序（移到最前面）
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second.second);

    return it->second.first.value;
}

void LocalLRUCache::set(const std::string& key,
                         const std::string& value,
                         std::chrono::seconds ttl) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto actual_ttl = ttl.count() > 0 ? ttl : default_ttl_;
    auto expires_at = std::chrono::steady_clock::now() + actual_ttl;

    // 检查是否已存在
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // 更新现有条目
        it->second.first.value = value;
        it->second.first.expires_at = expires_at;
        // 移到最前面
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.second);
        return;
    }

    // 清理过期条目和淘汰
    evictIfNeeded();

    // 添加新条目
    lru_list_.push_front(key);
    cache_[key] = {CacheEntry{value, expires_at}, lru_list_.begin()};
}

bool LocalLRUCache::remove(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }

    lru_list_.erase(it->second.second);
    cache_.erase(it);
    return true;
}

void LocalLRUCache::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
    lru_list_.clear();
}

size_t LocalLRUCache::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_.size();
}

bool LocalLRUCache::exists(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }

    return !isExpired(it->second.first);
}

void LocalLRUCache::evictIfNeeded() {
    // 先清理过期条目
    cleanupExpired();

    // 如果仍然超过最大大小，淘汰最久未使用的
    while (cache_.size() >= max_size_ && !lru_list_.empty()) {
        const std::string& oldest_key = lru_list_.back();
        cache_.erase(oldest_key);
        lru_list_.pop_back();
    }
}

void LocalLRUCache::cleanupExpired() {
    auto now = std::chrono::steady_clock::now();

    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (isExpired(it->second.first)) {
            lru_list_.erase(it->second.second);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

bool LocalLRUCache::isExpired(const CacheEntry& entry) const {
    return std::chrono::steady_clock::now() >= entry.expires_at;
}

// ============================================================================
// TwoLevelCacheManager Implementation
// ============================================================================

TwoLevelCacheManager::TwoLevelCacheManager(
    std::shared_ptr<database::RedisPool> redis_pool,
    const CacheConfig& config)
    : redis_pool_(redis_pool)
    , config_(config)
    , local_cache_(config.max_local_cache_size, config.local_cache_ttl) {

    if (!redis_pool_ && config.use_distributed_cache) {
        LOG_WARNING("Redis pool is null, distributed cache will be disabled");
        config_.use_distributed_cache = false;
    }
}

std::optional<std::string> TwoLevelCacheManager::get(const std::string& key) {
    if (!config_.enabled) {
        return std::nullopt;
    }

    std::string full_key = buildKey(key);

    // 尝试 L1 缓存
    if (config_.use_local_cache) {
        auto local_result = local_cache_.get(full_key);
        if (local_result.has_value()) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.local_hits++;
            stats_.total_requests++;
            return local_result;
        }
    }

    // 尝试 L2 缓存（Redis）
    if (config_.use_distributed_cache && redis_pool_) {
        try {
            auto redis_result = redis_pool_->get(full_key);
            if (redis_result.has_value()) {
                // 回填 L1 缓存
                if (config_.use_local_cache) {
                    local_cache_.set(full_key, *redis_result, config_.local_cache_ttl);
                }

                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.remote_hits++;
                stats_.total_requests++;
                return redis_result;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Redis get failed: " + std::string(e.what()));
        }
    }

    // 缓存未命中
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.local_misses++;
    stats_.remote_misses++;
    stats_.total_requests++;

    return std::nullopt;
}

bool TwoLevelCacheManager::set(const std::string& key,
                                const std::string& value,
                                std::chrono::seconds ttl) {
    if (!config_.enabled) {
        return false;
    }

    std::string full_key = buildKey(key);
    auto actual_ttl = ttl.count() > 0 ? ttl : config_.default_ttl;
    bool success = true;

    // 设置 L1 缓存
    if (config_.use_local_cache) {
        local_cache_.set(full_key, value,
            std::min(actual_ttl, config_.local_cache_ttl));
    }

    // 设置 L2 缓存
    if (config_.use_distributed_cache && redis_pool_) {
        try {
            redis_pool_->set(full_key, value, actual_ttl);
        } catch (const std::exception& e) {
            LOG_ERROR("Redis set failed: " + std::string(e.what()));
            success = false;
        }
    }

    return success;
}

bool TwoLevelCacheManager::remove(const std::string& key) {
    std::string full_key = buildKey(key);
    bool success = true;

    // 移除 L1
    if (config_.use_local_cache) {
        local_cache_.remove(full_key);
    }

    // 移除 L2
    if (config_.use_distributed_cache && redis_pool_) {
        try {
            redis_pool_->del(full_key);
        } catch (const std::exception& e) {
            LOG_ERROR("Redis del failed: " + std::string(e.what()));
            success = false;
        }
    }

    return success;
}

size_t TwoLevelCacheManager::removeByPattern(const std::string& pattern) {
    size_t count = 0;

    // 清空本地缓存（简单处理，不精确匹配模式）
    if (config_.use_local_cache) {
        local_cache_.clear();
    }

    // 从 Redis 删除匹配的键
    if (config_.use_distributed_cache && redis_pool_) {
        try {
            std::string full_pattern = config_.key_prefix + pattern;
            auto keys = redis_pool_->keys(full_pattern);
            for (const auto& key : keys) {
                redis_pool_->del(key);
                count++;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Redis keys/del by pattern failed: " + std::string(e.what()));
        }
    }

    return count;
}

bool TwoLevelCacheManager::exists(const std::string& key) {
    std::string full_key = buildKey(key);

    // 检查 L1
    if (config_.use_local_cache && local_cache_.exists(full_key)) {
        return true;
    }

    // 检查 L2
    if (config_.use_distributed_cache && redis_pool_) {
        try {
            return redis_pool_->exists(full_key);
        } catch (const std::exception& e) {
            LOG_ERROR("Redis exists failed: " + std::string(e.what()));
        }
    }

    return false;
}

void TwoLevelCacheManager::invalidateEntity(const std::string& entity_type,
                                             const std::string& entity_id) {
    std::string key = buildEntityKey(entity_type, entity_id);
    remove(key);
}

void TwoLevelCacheManager::setConfig(const CacheConfig& config) {
    config_ = config;
}

void TwoLevelCacheManager::clearLocalCache() {
    local_cache_.clear();
}

TwoLevelCacheManager::Stats TwoLevelCacheManager::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void TwoLevelCacheManager::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Stats{};
}

std::string TwoLevelCacheManager::buildKey(const std::string& key) const {
    if (config_.key_prefix.empty()) {
        return key;
    }
    return config_.key_prefix + key;
}

std::string TwoLevelCacheManager::buildEntityKey(const std::string& entity_type,
                                                  const std::string& entity_id) const {
    return entity_type + ":" + entity_id;
}

// ============================================================================
// RedisCacheManager Implementation
// ============================================================================

RedisCacheManager::RedisCacheManager(
    std::shared_ptr<database::RedisPool> redis_pool,
    const std::string& key_prefix)
    : redis_pool_(redis_pool)
    , key_prefix_(key_prefix) {

    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }
}

std::string RedisCacheManager::buildKey(const std::string& key) const {
    return key_prefix_ + key;
}

std::optional<std::string> RedisCacheManager::get(const std::string& key) {
    try {
        return redis_pool_->get(buildKey(key));
    } catch (const std::exception& e) {
        LOG_ERROR("RedisCacheManager get failed: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool RedisCacheManager::set(const std::string& key,
                             const std::string& value,
                             std::chrono::seconds ttl) {
    try {
        if (ttl.count() > 0) {
            redis_pool_->set(buildKey(key), value, ttl);
        } else {
            redis_pool_->set(buildKey(key), value);
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("RedisCacheManager set failed: " + std::string(e.what()));
        return false;
    }
}

bool RedisCacheManager::remove(const std::string& key) {
    try {
        redis_pool_->del(buildKey(key));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("RedisCacheManager remove failed: " + std::string(e.what()));
        return false;
    }
}

size_t RedisCacheManager::removeByPattern(const std::string& pattern) {
    try {
        auto keys = redis_pool_->keys(key_prefix_ + pattern);
        for (const auto& key : keys) {
            redis_pool_->del(key);
        }
        return keys.size();
    } catch (const std::exception& e) {
        LOG_ERROR("RedisCacheManager removeByPattern failed: " + std::string(e.what()));
        return 0;
    }
}

bool RedisCacheManager::exists(const std::string& key) {
    try {
        return redis_pool_->exists(buildKey(key));
    } catch (const std::exception& e) {
        LOG_ERROR("RedisCacheManager exists failed: " + std::string(e.what()));
        return false;
    }
}

void RedisCacheManager::invalidateEntity(const std::string& entity_type,
                                          const std::string& entity_id) {
    remove(entity_type + ":" + entity_id);
}

} // namespace repository
} // namespace common
