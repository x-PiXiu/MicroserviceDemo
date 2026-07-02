#pragma once

#include "common/repository/base_redis_repository.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include "common/logger/logger.h"
#include "game_models.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

// 引入 RedisResult 和 RepositoryError 类型别名，方便使用
using common::repository::RedisResult;
using common::repository::RepositoryError;

/**
 * 用户游戏档案 Redis Repository
 */
class UserGameProfileRedisRepository : public common::repository::BaseRedisRepository<UserGameProfile> {
public:
    using Base = common::repository::BaseRedisRepository<UserGameProfile>;

    explicit UserGameProfileRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::GAME_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_USER,
               common::repository::RedisTTLConfig::GameService::USER_PROFILE) {}

    ~UserGameProfileRedisRepository() override = default;

protected:
    nlohmann::json toJson(const UserGameProfile& profile) const override {
        // 使用 model 的字符串 JSON 方法
        try {
            return nlohmann::json::parse(profile.toJson());
        } catch (const std::exception& e) {
            return nlohmann::json{};
        }
    }

    std::optional<UserGameProfile> fromJson(const nlohmann::json& json) const override {
        try {
            return UserGameProfile::fromJson(json.dump());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserGameProfile from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& user_id) const override {
        return common::repository::RedisKeyBuilder::GameService::userProfile(user_id);
    }
};

/**
 * 用户货币 Redis Repository
 */
class UserCurrencyRedisRepository : public common::repository::BaseRedisRepository<UserCurrency> {
public:
    using Base = common::repository::BaseRedisRepository<UserCurrency>;

    explicit UserCurrencyRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::GAME_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_CURRENCY,
               common::repository::RedisTTLConfig::GameService::CURRENCY) {}

    ~UserCurrencyRedisRepository() override = default;

    /**
     * 获取用户指定类型的货币
     */
    RedisResult<std::optional<UserCurrency>> getCurrency(
        const std::string& user_id,
        const std::string& currency_type
    ) {
        std::string key = common::repository::RedisKeyBuilder::GameService::currency(user_id, currency_type);

        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string value = conn->get(key);
            if (value.empty()) {
                return std::nullopt;
            }

            auto json = nlohmann::json::parse(value);
            return UserCurrency::fromJson(json.dump());

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get currency: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 保存用户货币
     */
    RedisResult<bool> saveCurrency(const UserCurrency& currency) {
        std::string key = common::repository::RedisKeyBuilder::GameService::currency(
            currency.user_id, currency.currency_type);

        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            nlohmann::json json = nlohmann::json::parse(currency.toJson());
            std::string value = json.dump();

            auto ttl = common::repository::RedisTTLConfig::GameService::CURRENCY;
            bool success = conn->set(key, value, static_cast<int>(ttl.count()));

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save currency: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 增加货币数量
     */
    RedisResult<long> incrementCurrency(
        const std::string& user_id,
        const std::string& currency_type,
        long amount
    ) {
        std::string key = common::repository::RedisKeyBuilder::GameService::currency(user_id, currency_type);

        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            // 使用 Redis INCRBY 原子操作
            // 但需要先确保键存在
            auto existing = getCurrency(user_id, currency_type);
            if (!existing.has_value()) {
                return common::utils::unexpected(existing.error());
            }

            UserCurrency currency;
            if (existing->has_value()) {
                currency = existing->value();
                currency.amount += amount;
            } else {
                currency.user_id = user_id;
                currency.currency_type = currency_type;
                currency.amount = amount;
                currency.last_updated_at = std::chrono::system_clock::now();
            }

            auto save_result = saveCurrency(currency);
            if (!save_result.has_value() || !save_result.value()) {
                return common::utils::unexpected(save_result.has_value() ?
                    common::repository::RepositoryError::cacheError("Failed to save currency") :
                    save_result.error());
            }

            return currency.amount;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to increment currency: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

protected:
    nlohmann::json toJson(const UserCurrency& currency) const override {
        try {
            return nlohmann::json::parse(currency.toJson());
        } catch (const std::exception&) {
            return nlohmann::json{};
        }
    }

    std::optional<UserCurrency> fromJson(const nlohmann::json& json) const override {
        try {
            return UserCurrency::fromJson(json.dump());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserCurrency from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }
};

/**
 * 用户库存 Redis Repository
 */
class UserInventoryRedisRepository : public common::repository::BaseRedisRepository<UserInventoryItem> {
public:
    using Base = common::repository::BaseRedisRepository<UserInventoryItem>;

    explicit UserInventoryRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::GAME_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_INVENTORY,
               common::repository::RedisTTLConfig::GameService::INVENTORY) {}

    ~UserInventoryRedisRepository() override = default;

    /**
     * 获取用户库存
     */
    RedisResult<std::vector<UserInventoryItem>> getUserInventory(const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string inventory_key = common::repository::RedisKeyBuilder::GameService::inventory(user_id);

            // 获取库存中的所有物品 ID
            auto item_ids = conn->smembers(inventory_key + ":items");

            std::vector<UserInventoryItem> items;
            for (const auto& item_id : item_ids) {
                auto item = findById(user_id + ":" + item_id);
                if (item.has_value() && item->has_value()) {
                    items.push_back(item->value());
                }
            }

            return items;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get user inventory: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 保存库存物品
     */
    RedisResult<bool> saveItem(const UserInventoryItem& item) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string item_key = buildKey(item.user_id + ":" + item.inventory_id);
            nlohmann::json json = toJson(item);

            auto ttl = common::repository::RedisTTLConfig::GameService::INVENTORY;
            bool success = conn->set(item_key, json.dump(), static_cast<int>(ttl.count()));

            if (success) {
                // 添加到用户的库存集合
                std::string inventory_key = common::repository::RedisKeyBuilder::GameService::inventory(item.user_id);
                conn->sadd(inventory_key + ":items", item.inventory_id);

                // 添加到类型索引
                std::string type_index_key = inventory_key + ":type:" + item.item_type;
                conn->sadd(type_index_key, item.inventory_id);
            }

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save inventory item: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 按类型查找库存物品
     */
    RedisResult<std::vector<UserInventoryItem>> findByType(const std::string& user_id, const std::string& item_type) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string inventory_key = common::repository::RedisKeyBuilder::GameService::inventory(user_id);
            std::string type_index_key = inventory_key + ":type:" + item_type;

            // 获取该类型的所有物品 ID
            auto item_ids = conn->smembers(type_index_key);

            std::vector<UserInventoryItem> items;
            for (const auto& item_id : item_ids) {
                auto item = findById(user_id + ":" + item_id);
                if (item.has_value() && item->has_value()) {
                    items.push_back(item->value());
                }
            }

            return items;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to find items by type: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

protected:
    nlohmann::json toJson(const UserInventoryItem& item) const override {
        try {
            return nlohmann::json::parse(item.toJson());
        } catch (const std::exception&) {
            return nlohmann::json{};
        }
    }

    std::optional<UserInventoryItem> fromJson(const nlohmann::json& json) const override {
        try {
            return UserInventoryItem::fromJson(json.dump());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserInventoryItem from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& id) const override {
        // id 格式为 "user_id:item_id"
        return common::repository::RedisKeyBuilder::entityKey(
            common::repository::RedisKeyBuilder::GAME_SERVICE,
            common::repository::RedisKeyBuilder::ENTITY_INVENTORY,
            id);
    }
};

/**
 * 游戏成就 Redis Repository
 */
class GameAchievementRedisRepository : public common::repository::BaseRedisRepository<GameAchievement> {
public:
    using Base = common::repository::BaseRedisRepository<GameAchievement>;

    explicit GameAchievementRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::GAME_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_ACHIEVEMENT,
               common::repository::RedisTTLConfig::GameService::ACHIEVEMENT) {}

    ~GameAchievementRedisRepository() override = default;

    /**
     * 获取用户所有成就
     */
    RedisResult<std::vector<GameAchievement>> getUserAchievements(const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string achievements_key = common::repository::RedisKeyBuilder::GameService::userAchievements(user_id);

            // 获取成就列表
            auto achievement_ids = conn->smembers(achievements_key);

            std::vector<GameAchievement> achievements;
            for (const auto& achievement_id : achievement_ids) {
                auto achievement = findById(user_id + ":" + achievement_id);
                if (achievement.has_value() && achievement->has_value()) {
                    achievements.push_back(achievement->value());
                }
            }

            return achievements;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get user achievements: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

protected:
    nlohmann::json toJson(const GameAchievement& achievement) const override {
        try {
            return nlohmann::json::parse(achievement.toJson());
        } catch (const std::exception&) {
            return nlohmann::json{};
        }
    }

    std::optional<GameAchievement> fromJson(const nlohmann::json& json) const override {
        try {
            return GameAchievement::fromJson(json.dump());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse GameAchievement from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& id) const override {
        return common::repository::RedisKeyBuilder::GameService::achievement(id, "");
    }
};

} // namespace game_service
} // namespace core_services
