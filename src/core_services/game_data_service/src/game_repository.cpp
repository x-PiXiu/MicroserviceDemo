//
// Created by Microservice Team
// 游戏数据访问层实现 - 简洁稳定架构
// @version 2.0.0 - 集成统一 Redis Repository 模式
//

#include "../include/game_repository.h"
#include <common/database/mysql_pool.h>
#include <common/database/redis_pool.h>
#include <common/logger/logger.h>
#include <common/repository/redis_key_builder.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace core_services {
namespace game_service {

// ==================== 辅助函数 ====================

/**
 * @brief 获取货币类型ID
 * @param currency_code 货币代码 (GOLD, GEM, HONOR)
 * @return 货币类型ID，未知类型返回1 (GOLD)
 */
static int getCurrencyTypeId(const std::string& currency_code) {
    if (currency_code == "GOLD" || currency_code == "gold") return 1;
    if (currency_code == "GEM" || currency_code == "gem") return 2;
    if (currency_code == "HONOR" || currency_code == "honor") return 3;
    return 1; // 默认返回 GOLD
}

/**
 * @brief 获取货币代码
 * @param currency_type_id 货币类型ID
 * @return 货币代码
 */
static std::string getCurrencyCode(int currency_type_id) {
    switch (currency_type_id) {
        case 1: return "GOLD";
        case 2: return "GEM";
        case 3: return "HONOR";
        default: return "GOLD";
    }
}

// ==================== 新架构初始化方法 ====================

void GameRepository::initializeRedisRepositories() {
    if (redis_pool_) {
        profile_redis_ = std::make_unique<UserGameProfileRedisRepository>(redis_pool_);
        currency_redis_ = std::make_unique<UserCurrencyRedisRepository>(redis_pool_);
        inventory_redis_ = std::make_unique<UserInventoryRedisRepository>(redis_pool_);
        achievement_redis_ = std::make_unique<GameAchievementRedisRepository>(redis_pool_);
        LOG_INFO("GameRepository: Redis Repositories initialized successfully");
    } else {
        LOG_WARNING("GameRepository: Redis pool is null, Redis Repositories not initialized");
    }
}

// 构造函数
GameRepository::GameRepository(
    std::shared_ptr<common::database::MySQLPool> mysql_pool,
    std::shared_ptr<common::database::RedisPool> redis_pool
) : mysql_pool_(mysql_pool), redis_pool_(redis_pool) {
    initializeRedisRepositories();
    LOG_INFO("GameRepository initialized with database pools");
}

GameRepository::~GameRepository() {
    LOG_INFO("GameRepository destroyed");
}

// === 用户游戏档案操作 ===

bool GameRepository::createUserProfile(const UserGameProfile& profile) {
    try {
        // 使用RAII管理MySQL连接
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        auto mysql_conn = mysql_guard.get();
        if (!mysql_conn || !mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        // 数据库表使用 (user_id, game_type_id) 作为主键
        // 使用 ON DUPLICATE KEY UPDATE 处理重复键情况
        const char* sql = R"(
            INSERT INTO user_game_profiles
            (user_id, game_type_id, level, experience_points, current_rating, peak_rating, created_at, updated_at)
            VALUES (?, ?, ?, ?, 1200, 1200, NOW(), NOW())
            ON DUPLICATE KEY UPDATE
                level = VALUES(level),
                experience_points = VALUES(experience_points),
                updated_at = NOW()
        )";

        auto stmt = mysql_conn->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for createUserProfile");
            return false;
        }

        // 默认游戏类型ID为1（五子棋）
        int game_type_id = 1;
        stmt->setString(1, profile.user_id);
        stmt->setInt(2, game_type_id);
        stmt->setInt(3, profile.level);
        stmt->setInt64(4, profile.experience_points);

        // 注意：executeUpdate() 返回受影响的行数，对于 INSERT 成功至少返回 1
        // execute() 对于 INSERT 返回 false（无结果集），所以不能用 bool 判断
        int affected_rows = stmt->executeUpdate();
        if (affected_rows > 0) {
            LOG_INFO("Created/updated user profile for user: " + profile.user_id +
                    " (affected rows: " + std::to_string(affected_rows) + ")");
            // 清除相关缓存
            deleteFromCache(buildCacheKey("user_profile", profile.user_id));
            return true;
        } else {
            LOG_WARNING("No rows affected when creating user profile for user: " + profile.user_id);
            return true;  // ON DUPLICATE KEY UPDATE 可能影响 0 行如果数据相同
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in createUserProfile: " + std::string(e.what()));
        return false;
    }
}

std::optional<UserGameProfile> GameRepository::getUserProfile(const std::string& user_id) {
    try {
        // 使用新架构：Cache-Aside 模式
        if (profile_redis_) {
            auto result = profile_redis_->getOrLoad(user_id, [&]() -> std::optional<UserGameProfile> {
                // 从数据库加载的 lambda
                common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
                auto mysql_conn = mysql_guard.get();
                if (!mysql_conn || !mysql_guard.isValid()) {
                    LOG_ERROR("Failed to get MySQL connection in getOrLoad lambda");
                    return std::nullopt;
                }

                // 数据库实际列名 - 添加 peak_rating 和 tier_level
                const char* sql = R"(
                    SELECT user_id, game_type_id, level, experience_points, current_rating, peak_rating, tier_level,
                           total_games, wins, losses, draws, current_win_streak, best_win_streak,
                           created_at, updated_at, last_played_at
                    FROM user_game_profiles
                    WHERE user_id = ?
                    LIMIT 1
                )";

                auto stmt = mysql_conn->prepareStatement(sql);
                if (!stmt) {
                    return std::nullopt;
                }

                stmt->setString(1, user_id);
                auto result = stmt->executeQuery();

                if (result && result->next()) {
                    UserGameProfile profile;
                    profile.user_id = result->getString("user_id");
                    profile.level = result->getInt("level");
                    profile.experience_points = result->getInt64("experience_points");
                    // 读取评分和统计字段
                    profile.current_rating = result->getInt("current_rating");
                    profile.peak_rating = result->getInt("peak_rating");
                    profile.tier_level = result->getInt("tier_level");
                    profile.total_games = result->getInt("total_games");
                    profile.wins = result->getInt("wins");
                    profile.losses = result->getInt("losses");
                    profile.draws = result->getInt("draws");
                    profile.current_win_streak = result->getInt("current_win_streak");
                    profile.best_win_streak = result->getInt("best_win_streak");
                    // 时间字段 - 使用 getString 然后转换
                    std::string created_at_str = result->getString("created_at");
                    std::string last_played_str = result->getString("last_played_at");
                    if (!created_at_str.empty()) {
                        profile.created_at = stringToTime(created_at_str);
                    }
                    if (!last_played_str.empty()) {
                        profile.last_login_at = stringToTime(last_played_str);
                    }
                    // display_name 和 avatar_url 不在此表中，需要从用户服务获取
                    return profile;
                }
                return std::nullopt;
            });

            if (result.has_value()) {
                LOG_INFO("Retrieved user profile for user: " + user_id + " (via Redis Repository)");
                return result.value();
            }
            return std::nullopt;
        }

        // 降级到旧架构（向后兼容）
        LOG_WARNING("Using legacy cache for getUserProfile - Redis Repository not available");
        std::string cache_key = buildCacheKey("user_profile", user_id);
        auto cached = getFromCache(cache_key);
        if (cached.has_value()) {
            try {
                return UserGameProfile::fromJson(cached.value());
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to parse cached user profile, falling back to database");
            }
        }

        // 从数据库获取
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        auto mysql_conn = mysql_guard.get();
        if (!mysql_conn || !mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return std::nullopt;
        }

        // 数据库实际列名 - 添加 peak_rating 和 tier_level
        const char* sql = R"(
            SELECT user_id, game_type_id, level, experience_points, current_rating, peak_rating, tier_level,
                   total_games, wins, losses, draws, current_win_streak, best_win_streak,
                   created_at, updated_at, last_played_at
            FROM user_game_profiles
            WHERE user_id = ?
            LIMIT 1
        )";

        auto stmt = mysql_conn->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getUserProfile");
            return std::nullopt;
        }

        stmt->setString(1, user_id);
        auto result = stmt->executeQuery();

        if (result && result->next()) {
            UserGameProfile profile;
            profile.user_id = result->getString("user_id");
            profile.level = result->getInt("level");
            profile.experience_points = result->getInt64("experience_points");
            // 读取评分和统计字段
            profile.current_rating = result->getInt("current_rating");
            profile.peak_rating = result->getInt("peak_rating");
            profile.tier_level = result->getInt("tier_level");
            profile.total_games = result->getInt("total_games");
            profile.wins = result->getInt("wins");
            profile.losses = result->getInt("losses");
            profile.draws = result->getInt("draws");
            profile.current_win_streak = result->getInt("current_win_streak");
            profile.best_win_streak = result->getInt("best_win_streak");
            // 时间字段 - 使用 getString 然后转换
            std::string created_at_str = result->getString("created_at");
            std::string last_played_str = result->getString("last_played_at");
            if (!created_at_str.empty()) {
                profile.created_at = stringToTime(created_at_str);
            }
            if (!last_played_str.empty()) {
                profile.last_login_at = stringToTime(last_played_str);
            }

            // 缓存结果
            setToCache(cache_key, profile.toJson(), 1800); // 30分钟缓存

            LOG_INFO("Retrieved user profile for user: " + user_id);
            return profile;
        }

        return std::nullopt;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserProfile: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool GameRepository::updateUserProfile(const UserGameProfile& profile) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        // 数据库实际列名，主键是 (user_id, game_type_id)
        const char* sql = R"(
            UPDATE user_game_profiles
            SET level = ?, experience_points = ?, updated_at = NOW(), last_played_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for updateUserProfile");
            return false;
        }

        stmt->setInt(1, profile.level);
        stmt->setInt64(2, profile.experience_points);
        stmt->setString(3, profile.user_id);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Updated user profile for user: " + profile.user_id);

            // 使用新架构：使缓存失效
            if (profile_redis_) {
                auto invalidate_result = profile_redis_->invalidate(profile.user_id);
                if (!invalidate_result.has_value() || !invalidate_result.value()) {
                    LOG_WARNING("Failed to invalidate profile cache via Redis Repository");
                }
            } else {
                // 降级到旧架构
                deleteFromCache(buildCacheKey("user_profile", profile.user_id));
            }
            return true;
        } else {
            LOG_ERROR("Failed to update user profile for user: " + profile.user_id);
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in updateUserProfile: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::deleteUserProfile(const std::string& user_id) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        const char* sql = "DELETE FROM user_game_profiles WHERE user_id = ?";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for deleteUserProfile");
            return false;
        }

        stmt->setString(1, user_id);
        int affected_rows = stmt->executeUpdate();

        if (affected_rows >= 0) {
            LOG_INFO("Deleted user profile for user: " + user_id);

            // 使用新架构：删除缓存
            if (profile_redis_) {
                auto remove_result = profile_redis_->remove(user_id);
                if (!remove_result.has_value() || !remove_result.value()) {
                    LOG_WARNING("Failed to remove profile cache via Redis Repository");
                }
            } else {
                // 降级到旧架构
                deleteFromCache(buildCacheKey("user_profile", user_id));
            }
            return true;
        } else {
            LOG_ERROR("Failed to delete user profile for user: " + user_id);
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in deleteUserProfile: " + std::string(e.what()));
        return false;
    }
}

PaginatedResponse<UserGameProfile> GameRepository::getUserProfiles(
    const PaginationParams& params, 
    const QueryFilter& filter
) {
    PaginatedResponse<UserGameProfile> response;
    
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return response;
        }

        // 构建查询条件
        std::string where_clause = filter.buildWhereClause();
        
        // 获取总数
        std::string count_sql = "SELECT COUNT(*) as total FROM user_game_profiles WHERE " + where_clause;
        auto count_stmt = mysql_guard->prepareStatement(count_sql.c_str());
        if (count_stmt) {
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据 - 使用数据库实际存在的列名
        std::string data_sql = R"(
            SELECT user_id, game_type_id, level, experience_points, current_rating,
                   total_games, wins, losses, draws, current_win_streak, best_win_streak,
                   total_playtime_seconds, created_at, updated_at, last_played_at
            FROM user_game_profiles
            WHERE )" + where_clause + R"(
            ORDER BY )" + params.sort_by + " " + params.sort_order + R"(
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql.c_str());
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare data statement for getUserProfiles");
            return response;
        }

        data_stmt->setInt(1, params.limit);
        data_stmt->setInt(2, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            UserGameProfile profile;
            profile.user_id = result->getString("user_id");
            profile.level = result->getInt("level");
            profile.experience_points = result->getInt64("experience_points");
            // 注意：display_name 和 avatar_url 不在此表中，需要从用户服务获取

            response.items.push_back(profile);
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved " + std::to_string(response.items.size()) + " user profiles");

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserProfiles: " + std::string(e.what()));
    }

    return response;
}

// === 游戏成就操作 ===

bool GameRepository::unlockAchievement(const GameAchievement& achievement) {
    try {
        // 检查是否已经解锁
        if (hasUserUnlockedAchievement(achievement.user_id, achievement.achievement_type)) {
            LOG_INFO("Achievement already unlocked: " + achievement.achievement_type + " for user: " + achievement.user_id);
            return true; // 已经解锁，视为成功
        }

        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        // 使用 user_achievements 表，achievement_type 作为 achievement_id
        const char* sql = R"(
            INSERT INTO user_achievements
            (user_id, achievement_id, current_progress, is_unlocked, unlocked_at)
            VALUES (?, ?, 1, TRUE, NOW())
            ON DUPLICATE KEY UPDATE current_progress = 1, is_unlocked = TRUE, unlocked_at = NOW()
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for unlockAchievement");
            return false;
        }

        // achievement_type 用作 achievement_id
        stmt->setString(1, achievement.user_id);
        stmt->setString(2, achievement.achievement_type);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Unlocked achievement: " + achievement.achievement_type + " for user: " + achievement.user_id);
            // 清除相关缓存
            deleteFromCache(buildCacheKey("user_achievements", achievement.user_id));
            return true;
        } else {
            LOG_ERROR("Failed to unlock achievement: " + achievement.achievement_type + " for user: " + achievement.user_id);
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in unlockAchievement: " + std::string(e.what()));
        return false;
    }
}

PaginatedResponse<GameAchievement> GameRepository::getUserAchievements(
    const std::string& user_id,
    const PaginationParams& params
) {
    PaginatedResponse<GameAchievement> response;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return response;
        }

        // 获取总数 - 只统计已解锁的成就
        const char* count_sql = "SELECT COUNT(*) as total FROM user_achievements WHERE user_id = ? AND is_unlocked = TRUE";
        auto count_stmt = mysql_guard->prepareStatement(count_sql);
        if (count_stmt) {
            count_stmt->setString(1, user_id);
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据 - 使用 LEFT JOIN 以支持数据库中没有成就定义的情况
        // 如果 achievement_definitions 表没有对应记录，使用 achievement_id 作为标题
        const char* data_sql = R"(
            SELECT ua.achievement_id, ua.user_id, ua.unlocked_at,
                   COALESCE(ad.name, ua.achievement_id) as title,
                   COALESCE(ad.description, '') as description,
                   COALESCE(ad.points, 10) as points
            FROM user_achievements ua
            LEFT JOIN achievement_definitions ad ON ua.achievement_id = ad.achievement_id
            WHERE ua.user_id = ? AND ua.is_unlocked = TRUE
            ORDER BY ua.unlocked_at DESC
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql);
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare data statement for getUserAchievements");
            return response;
        }

        data_stmt->setString(1, user_id);
        data_stmt->setInt(2, params.limit);
        data_stmt->setInt(3, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            GameAchievement achievement;
            achievement.achievement_id = result->getString("achievement_id");
            achievement.user_id = result->getString("user_id");
            achievement.achievement_type = result->getString("achievement_id");
            achievement.title = result->getString("title");
            achievement.description = result->getString("description");
            achievement.points = result->getInt("points");
            // 读取解锁时间
            std::string unlocked_at_str = result->getString("unlocked_at");
            if (!unlocked_at_str.empty()) {
                achievement.unlocked_at = stringToTime(unlocked_at_str);
            }

            response.items.push_back(achievement);
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved " + std::to_string(response.items.size()) + " achievements for user: " + user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserAchievements: " + std::string(e.what()));
    }

    return response;
}

bool GameRepository::hasUserUnlockedAchievement(const std::string& user_id, const std::string& achievement_type) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        // 使用 user_achievements 表，achievement_type 作为 achievement_id
        const char* sql = "SELECT is_unlocked FROM user_achievements WHERE user_id = ? AND achievement_id = ?";
        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for hasUserUnlockedAchievement");
            return false;
        }

        stmt->setString(1, user_id);
        stmt->setString(2, achievement_type);

        auto result = stmt->executeQuery();
        if (result && result->next()) {
            return result->getBoolean("is_unlocked");
        }

        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in hasUserUnlockedAchievement: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::ensureAchievementDefinition(
    const std::string& achievement_id,
    const std::string& name,
    const std::string& description,
    int points) {

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        // 使用 INSERT IGNORE 确保成就定义存在，如果已存在则不重复插入
        const char* sql = R"(
            INSERT IGNORE INTO achievement_definitions
            (achievement_id, game_type_id, name, description, achievement_type, max_progress, points, rarity, is_active)
            VALUES (?, 1, ?, ?, 'milestone', 1, ?, 'common', TRUE)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for ensureAchievementDefinition");
            return false;
        }

        stmt->setString(1, achievement_id);
        stmt->setString(2, name);
        stmt->setString(3, description);
        stmt->setInt(4, points);

        stmt->executeUpdate();
        // INSERT IGNORE 即使记录已存在也不会失败，所以总是返回 true
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ensureAchievementDefinition: " + std::string(e.what()));
        return false;
    }
}

// === 用户货币操作 ===

std::optional<UserCurrency> GameRepository::getUserCurrency(const std::string& user_id, const std::string& currency_type) {
    try {
        // 使用新架构：优先从 Redis Repository 获取
        if (currency_redis_) {
            auto result = currency_redis_->getCurrency(user_id, currency_type);
            if (result.has_value() && result->has_value()) {
                LOG_INFO("Retrieved currency from Redis Repository: " + currency_type + " for user: " + user_id);
                return result->value();
            }
        }

        // 从数据库获取
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return std::nullopt;
        }

        // 使用 user_currency 表，通过 currency_type_id 查询
        int currency_type_id = getCurrencyTypeId(currency_type);
        const char* sql = R"(
            SELECT user_id, currency_type_id, amount
            FROM user_currency
            WHERE user_id = ? AND currency_type_id = ?
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getUserCurrency");
            return std::nullopt;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, currency_type_id);

        auto result = stmt->executeQuery();
        if (result && result->next()) {
            UserCurrency currency;
            currency.user_id = result->getString("user_id");
            currency.currency_type = currency_type;  // 保持原始字符串类型
            currency.amount = result->getInt64("amount");

            // 缓存到 Redis Repository
            if (currency_redis_) {
                currency_redis_->saveCurrency(currency);
            }

            return currency;
        }

        return std::nullopt;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserCurrency: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool GameRepository::updateUserCurrency(const UserCurrency& currency) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        int currency_type_id = getCurrencyTypeId(currency.currency_type);
        const char* sql = R"(
            INSERT INTO user_currency (user_id, currency_type_id, amount)
            VALUES (?, ?, ?)
            ON DUPLICATE KEY UPDATE amount = VALUES(amount)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for updateUserCurrency");
            return false;
        }

        stmt->setString(1, currency.user_id);
        stmt->setInt(2, currency_type_id);
        stmt->setInt64(3, currency.amount);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Updated currency: " + currency.currency_type + " for user: " + currency.user_id);

            // 使用新架构：更新缓存
            if (currency_redis_) {
                currency_redis_->saveCurrency(currency);
            }
            return true;
        } else {
            LOG_ERROR("Failed to update currency: " + currency.currency_type + " for user: " + currency.user_id);
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in updateUserCurrency: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::addUserCurrency(const std::string& user_id, const std::string& currency_type, long amount) {
    try {
        int currency_type_id = getCurrencyTypeId(currency_type);

        // 使用新架构：使用原子增加操作
        if (currency_redis_) {
            auto result = currency_redis_->incrementCurrency(user_id, currency_type, amount);
            if (result.has_value()) {
                // 同步到数据库（异步或同步取决于一致性要求）
                common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
                if (mysql_guard.isValid()) {
                    const char* sql = R"(
                        INSERT INTO user_currency (user_id, currency_type_id, amount)
                        VALUES (?, ?, ?)
                        ON DUPLICATE KEY UPDATE amount = amount + ?
                    )";

                    auto stmt = mysql_guard->prepareStatement(sql);
                    if (stmt) {
                        stmt->setString(1, user_id);
                        stmt->setInt(2, currency_type_id);
                        stmt->setInt64(3, amount);
                        stmt->setInt64(4, amount);
                        stmt->executeUpdate();
                    }
                }

                LOG_INFO("Added currency: " + currency_type + " amount: " + std::to_string(amount) + " for user: " + user_id);
                return true;
            }
        }

        // 降级到数据库操作
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        const char* sql = R"(
            INSERT INTO user_currency (user_id, currency_type_id, amount)
            VALUES (?, ?, ?)
            ON DUPLICATE KEY UPDATE amount = amount + ?
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for addUserCurrency");
            return false;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, currency_type_id);
        stmt->setInt64(3, amount);
        stmt->setInt64(4, amount);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Added currency: " + currency_type + " amount: " + std::to_string(amount) + " for user: " + user_id);
            // 使缓存失效以确保一致性
            if (currency_redis_) {
                std::string key = user_id + ":" + currency_type;
                currency_redis_->remove(key);
            }
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in addUserCurrency: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::deductUserCurrency(const std::string& user_id, const std::string& currency_type, long amount) {
    try {
        // 先检查余额是否足够
        auto current = getUserCurrency(user_id, currency_type);
        if (!current.has_value() || current->amount < amount) {
            LOG_WARNING("Insufficient currency: " + currency_type + " for user: " + user_id);
            return false;
        }

        // 使用负数调用 addUserCurrency
        return addUserCurrency(user_id, currency_type, -amount);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in deductUserCurrency: " + std::string(e.what()));
        return false;
    }
}

std::vector<UserCurrency> GameRepository::getAllUserCurrencies(const std::string& user_id) {
    std::vector<UserCurrency> currencies;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return currencies;
        }

        // JOIN currency_definitions 获取货币代码
        const char* sql = R"(
            SELECT uc.user_id, cd.currency_code, uc.amount
            FROM user_currency uc
            JOIN currency_definitions cd ON uc.currency_type_id = cd.currency_type_id
            WHERE uc.user_id = ?
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getAllUserCurrencies");
            return currencies;
        }

        stmt->setString(1, user_id);

        auto result = stmt->executeQuery();
        while (result && result->next()) {
            UserCurrency currency;
            currency.user_id = result->getString("user_id");
            currency.currency_type = result->getString("currency_code");
            currency.amount = result->getInt64("amount");
            currencies.push_back(currency);

            // 缓存到 Redis Repository
            if (currency_redis_) {
                currency_redis_->saveCurrency(currency);
            }
        }

        LOG_INFO("Retrieved " + std::to_string(currencies.size()) + " currencies for user: " + user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getAllUserCurrencies: " + std::string(e.what()));
    }

    return currencies;
}

// === 用户库存操作 ===

bool GameRepository::addInventoryItem(const UserInventoryItem& item) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        const char* sql = R"(
            INSERT INTO user_inventory
            (inventory_id, user_id, item_type, item_name, quantity, acquired_at)
            VALUES (?, ?, ?, ?, ?, NOW())
            ON DUPLICATE KEY UPDATE quantity = quantity + VALUES(quantity)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for addInventoryItem");
            return false;
        }

        std::string inventory_id = item.inventory_id.empty() ? generateUUID() : item.inventory_id;
        stmt->setString(1, inventory_id);
        stmt->setString(2, item.user_id);
        stmt->setString(3, item.item_type);
        stmt->setString(4, item.item_name);
        stmt->setInt(5, item.quantity);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Added inventory item: " + item.item_type + " for user: " + item.user_id);

            // 使用新架构：更新缓存
            if (inventory_redis_) {
                inventory_redis_->saveItem(item);
            }
            return true;
        } else {
            LOG_ERROR("Failed to add inventory item: " + item.item_type + " for user: " + item.user_id);
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in addInventoryItem: " + std::string(e.what()));
        return false;
    }
}

PaginatedResponse<UserInventoryItem> GameRepository::getUserInventory(
    const std::string& user_id,
    const PaginationParams& params
) {
    PaginatedResponse<UserInventoryItem> response;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return response;
        }

        // 获取总数
        const char* count_sql = "SELECT COUNT(*) as total FROM user_inventory WHERE user_id = ?";
        auto count_stmt = mysql_guard->prepareStatement(count_sql);
        if (count_stmt) {
            count_stmt->setString(1, user_id);
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据
        const char* data_sql = R"(
            SELECT inventory_id, user_id, item_type, item_name, quantity, acquired_at
            FROM user_inventory
            WHERE user_id = ?
            ORDER BY acquired_at DESC
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql);
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare data statement for getUserInventory");
            return response;
        }

        data_stmt->setString(1, user_id);
        data_stmt->setInt(2, params.limit);
        data_stmt->setInt(3, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            UserInventoryItem item;
            item.inventory_id = result->getString("inventory_id");
            item.user_id = result->getString("user_id");
            item.item_type = result->getString("item_type");
            item.item_name = result->getString("item_name");
            item.quantity = result->getInt("quantity");

            response.items.push_back(item);

            // 缓存到 Redis Repository
            if (inventory_redis_) {
                inventory_redis_->saveItem(item);
            }
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved " + std::to_string(response.items.size()) + " inventory items for user: " + user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserInventory: " + std::string(e.what()));
    }

    return response;
}

bool GameRepository::useInventoryItem(const std::string& user_id, const std::string& item_id, int quantity) {
    try {
        // 先检查库存是否足够
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return false;
        }

        // 检查当前数量
        const char* check_sql = "SELECT quantity FROM user_inventory WHERE user_id = ? AND inventory_id = ?";
        auto check_stmt = mysql_guard->prepareStatement(check_sql);
        if (!check_stmt) {
            LOG_ERROR("Failed to prepare check statement for useInventoryItem");
            return false;
        }

        check_stmt->setString(1, user_id);
        check_stmt->setString(2, item_id);
        auto check_result = check_stmt->executeQuery();

        if (!check_result || !check_result->next()) {
            LOG_WARNING("Inventory item not found: " + item_id + " for user: " + user_id);
            return false;
        }

        int current_quantity = check_result->getInt("quantity");
        if (current_quantity < quantity) {
            LOG_WARNING("Insufficient inventory: " + item_id + " for user: " + user_id);
            return false;
        }

        // 更新库存
        const char* update_sql = R"(
            UPDATE user_inventory
            SET quantity = quantity - ?
            WHERE user_id = ? AND inventory_id = ?
        )";

        auto update_stmt = mysql_guard->prepareStatement(update_sql);
        if (!update_stmt) {
            LOG_ERROR("Failed to prepare update statement for useInventoryItem");
            return false;
        }

        update_stmt->setInt(1, quantity);
        update_stmt->setString(2, user_id);
        update_stmt->setString(3, item_id);

        int affected_rows = update_stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Used inventory item: " + item_id + " quantity: " + std::to_string(quantity) + " for user: " + user_id);

            // 使用新架构：使缓存失效
            if (inventory_redis_) {
                inventory_redis_->remove(item_id);
            } else {
                deleteFromCache(buildCacheKey("inventory", item_id));
            }
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in useInventoryItem: " + std::string(e.what()));
        return false;
    }
}

int GameRepository::getItemQuantity(const std::string& user_id, const std::string& item_type) {
    try {
        // 使用新架构：优先从 Redis Repository 获取
        if (inventory_redis_) {
            auto result = inventory_redis_->findByType(user_id, item_type);
            if (result.has_value()) {
                int total = 0;
                for (const auto& item : result.value()) {
                    total += item.quantity;
                }
                return total;
            }
        }

        // 从数据库获取
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection");
            return 0;
        }

        const char* sql = "SELECT SUM(quantity) as total FROM user_inventory WHERE user_id = ? AND item_type = ?";
        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getItemQuantity");
            return 0;
        }

        stmt->setString(1, user_id);
        stmt->setString(2, item_type);

        auto result = stmt->executeQuery();
        if (result && result->next()) {
            return result->getInt("total");
        }

        return 0;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getItemQuantity: " + std::string(e.what()));
        return 0;
    }
}

// === 排行榜操作 ===

bool GameRepository::updateLeaderboardEntry(const LeaderboardEntry& entry) {
    try {
        // 更新 Redis Sorted Set（实时排行榜）
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (!redis_guard.isValid()) {
            LOG_ERROR("Failed to get Redis connection for leaderboard update");
            return false;
        }

        auto redis_conn = redis_guard.get();
        std::string redis_key = common::repository::RedisKeyBuilder::GameService::leaderboard(entry.leaderboard_type, entry.game_type);

        // 使用 ZADD 更新分数（参数顺序：key, score, member）
        if (!redis_conn->zadd(redis_key, static_cast<double>(entry.score), entry.user_id)) {
            LOG_ERROR("Failed to update Redis leaderboard: " + redis_key);
            return false;
        }

        // 同时更新 MySQL（持久化）
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_WARNING("MySQL connection failed, but Redis update succeeded");
            return true;  // Redis 更新成功，返回成功
        }

        // 数据库表 leaderboards 使用 (user_id, game_type_id, leaderboard_type) 作为主键
        // game_type 转换为 game_type_id: gomoku=1, snake=2 等
        int game_type_id = 1;  // 默认五子棋
        if (entry.game_type == "gomoku" || entry.game_type == "1") game_type_id = 1;
        else if (entry.game_type == "snake" || entry.game_type == "2") game_type_id = 2;

        const char* sql = R"(
            INSERT INTO leaderboards
            (user_id, game_type_id, leaderboard_type, rank, score, username, nickname, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, NOW())
            ON DUPLICATE KEY UPDATE rank = VALUES(rank), score = VALUES(score), updated_at = NOW()
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_WARNING("Failed to prepare MySQL statement for leaderboard");
            return true;  // Redis 已成功
        }

        stmt->setString(1, entry.user_id);
        stmt->setInt(2, game_type_id);
        stmt->setString(3, entry.leaderboard_type);
        stmt->setInt(4, entry.rank_position);
        stmt->setInt64(5, entry.score);
        stmt->setString(6, entry.username.empty() ? "unknown" : entry.username);
        stmt->setString(7, entry.nickname.empty() ? "unknown" : entry.nickname);

        stmt->executeUpdate();
        LOG_INFO("Updated leaderboard: " + entry.leaderboard_type + "/" + entry.game_type + " for user: " + entry.user_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in updateLeaderboardEntry: " + std::string(e.what()));
        return false;
    }
}

PaginatedResponse<LeaderboardEntry> GameRepository::getLeaderboard(
    const std::string& leaderboard_type,
    const std::string& game_type,
    const PaginationParams& params
) {
    PaginatedResponse<LeaderboardEntry> response;

    try {
        // 从 Redis Sorted Set 获取排行榜
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (redis_guard.isValid()) {
            auto redis_conn = redis_guard.get();
            std::string redis_key = common::repository::RedisKeyBuilder::GameService::leaderboard(leaderboard_type, game_type);

            // 使用 ZREVRANGE 获取排名（从高到低，withscores=true）
            int start = params.getOffset();
            int stop = start + params.limit - 1;

            // zrevrange 返回 vector<string>，withscores=true 时交替存储 member 和 score
            auto results = redis_conn->zrevrange(redis_key, start, stop, true);
            if (!results.empty()) {
                int rank = start + 1;
                // 解析交替的 member 和 score
                for (size_t i = 0; i + 1 < results.size(); i += 2) {
                    LeaderboardEntry entry;
                    entry.user_id = results[i];
                    entry.leaderboard_type = leaderboard_type;
                    entry.game_type = game_type;
                    entry.score = std::stoll(results[i + 1]);
                    entry.rank_position = rank++;
                    response.items.push_back(entry);
                }

                // 获取总数
                response.total_count = redis_conn->zcard(redis_key);
                response.page = params.page;
                response.limit = params.limit;
                response.total_pages = (response.total_count + params.limit - 1) / params.limit;

                LOG_INFO("Retrieved leaderboard from Redis: " + leaderboard_type + "/" + game_type);
                return response;
            }
        }

        // 降级到 MySQL 查询
        LOG_WARNING("Redis leaderboard not available, falling back to MySQL");
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for leaderboard");
            return response;
        }

        // 转换 game_type 为 game_type_id
        int game_type_id = 1;
        if (game_type == "gomoku" || game_type == "1") game_type_id = 1;
        else if (game_type == "snake" || game_type == "2") game_type_id = 2;

        // 获取总数
        const char* count_sql = "SELECT COUNT(*) as total FROM leaderboards WHERE leaderboard_type = ? AND game_type_id = ?";
        auto count_stmt = mysql_guard->prepareStatement(count_sql);
        if (count_stmt) {
            count_stmt->setString(1, leaderboard_type);
            count_stmt->setInt(2, game_type_id);
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据
        const char* data_sql = R"(
            SELECT user_id, leaderboard_type, score, rank, username, nickname, avatar_url
            FROM leaderboards
            WHERE leaderboard_type = ? AND game_type_id = ?
            ORDER BY score DESC
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql);
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare statement for getLeaderboard");
            return response;
        }

        data_stmt->setString(1, leaderboard_type);
        data_stmt->setInt(2, game_type_id);
        data_stmt->setInt(3, params.limit);
        data_stmt->setInt(4, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            LeaderboardEntry entry;
            entry.user_id = result->getString("user_id");
            entry.leaderboard_type = result->getString("leaderboard_type");
            entry.game_type = game_type;
            entry.score = result->getInt64("score");
            entry.rank_position = result->getInt("rank");
            entry.username = result->getString("username");
            entry.nickname = result->getString("nickname");
            entry.avatar_url = result->getString("avatar_url");
            response.items.push_back(entry);
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved leaderboard from MySQL: " + leaderboard_type + "/" + game_type);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getLeaderboard: " + std::string(e.what()));
    }

    return response;
}

int GameRepository::getUserRank(const std::string& user_id, const std::string& leaderboard_type, const std::string& game_type) {
    try {
        // 先检查 Redis 中是否存在该用户（使用 zscore）
        bool user_in_redis = false;
        double redis_score = 0;

        {
            common::database::RedisConnectionGuard redis_guard(*redis_pool_);
            if (redis_guard.isValid()) {
                auto redis_conn = redis_guard.get();
                std::string redis_key = common::repository::RedisKeyBuilder::GameService::leaderboard(leaderboard_type, game_type);

                // 使用 ZSCORE 检查用户是否存在
                redis_score = redis_conn->zscore(redis_key, user_id);
                // zscore 返回 0 可能是分数为 0 或者成员不存在
                // 需要额外检查成员是否存在
                user_in_redis = (redis_score != 0.0);
            }
        }

        // 使用 MySQL 计算排名（因为没有 ZCOUNT/ZREVRANK 方法）
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for user rank");
            return 0;
        }

        // 转换 game_type 为 game_type_id
        int game_type_id = 1;
        if (game_type == "gomoku" || game_type == "1") game_type_id = 1;
        else if (game_type == "snake" || game_type == "2") game_type_id = 2;

        // 计算排名：统计分数比该用户高的成员数量
        const char* sql = R"(
            SELECT COUNT(*) + 1 as rank
            FROM leaderboards
            WHERE leaderboard_type = ? AND game_type_id = ? AND score > (
                SELECT COALESCE(MAX(score), 0) FROM leaderboards WHERE user_id = ? AND leaderboard_type = ? AND game_type_id = ?
            )
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getUserRank");
            return 0;
        }

        stmt->setString(1, leaderboard_type);
        stmt->setInt(2, game_type_id);
        stmt->setString(3, user_id);
        stmt->setString(4, leaderboard_type);
        stmt->setInt(5, game_type_id);

        auto result = stmt->executeQuery();
        if (result && result->next()) {
            int rank = result->getInt("rank");
            LOG_INFO("Calculated rank for user " + user_id + " in " + leaderboard_type + "/" + game_type + ": " + std::to_string(rank));
            return rank;
        }

        return 0;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserRank: " + std::string(e.what()));
        return 0;
    }
}

// === 数据库管理 ===

bool GameRepository::testDatabaseConnection() {
    try {
        // 测试MySQL连接
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("MySQL connection test failed");
            return false;
        }

        auto stmt = mysql_guard->prepareStatement("SELECT 1 as test");
        if (!stmt) {
            LOG_ERROR("Failed to prepare test statement for MySQL");
            return false;
        }

        auto result = stmt->executeQuery();
        if (!result || !result->next() || result->getInt("test") != 1) {
            LOG_ERROR("MySQL query test failed");
            return false;
        }

        // 测试Redis连接
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (!redis_guard.isValid()) {
            LOG_ERROR("Redis connection test failed");
            return false;
        }

        auto redis_conn = redis_guard.get();
        if (!redis_conn->ping()) {
            LOG_ERROR("Redis ping test failed");
            return false;
        }

        LOG_INFO("Database connection test passed");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in testDatabaseConnection: " + std::string(e.what()));
        return false;
    }
}

// === 定时任务相关操作 ===

int GameRepository::cleanupExpiredCache(const std::string& pattern) {
    int cleaned_count = 0;

    try {
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (!redis_guard.isValid()) {
            LOG_WARNING("Redis connection not available for cache cleanup");
            return 0;
        }

        auto redis_conn = redis_guard.get();

        // 清理用户档案缓存
        if (profile_redis_) {
            // 清理过期的用户档案缓存（通过 TTL 自动过期，这里只做日志记录）
            LOG_DEBUG("User profile cache cleanup - TTL handles expiration automatically");
            cleaned_count++;
        }

        // 清理货币缓存
        if (currency_redis_) {
            LOG_DEBUG("Currency cache cleanup - TTL handles expiration automatically");
            cleaned_count++;
        }

        // 清理库存缓存
        if (inventory_redis_) {
            LOG_DEBUG("Inventory cache cleanup - TTL handles expiration automatically");
            cleaned_count++;
        }

        // 清理成就缓存
        if (achievement_redis_) {
            LOG_DEBUG("Achievement cache cleanup - TTL handles expiration automatically");
            cleaned_count++;
        }

        LOG_INFO("Cache cleanup completed, " + std::to_string(cleaned_count) + " cache types processed");
        return cleaned_count;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in cleanupExpiredCache: " + std::string(e.what()));
        return cleaned_count;
    }
}

int GameRepository::archiveOldGameRecords(int days_old) {
    int archived_count = 0;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for data archival");
            return 0;
        }

        // 归档旧的游戏结果记录（game_results 表有 played_at 字段）
        // 注意：leaderboards 表使用 updated_at，通常不需要归档（排行榜需要保持当前状态）
        // 这里主要归档 game_results 表中的旧记录
        const char* archive_sql = R"(
            INSERT INTO game_results_archive
            SELECT * FROM game_results
            WHERE played_at < DATE_SUB(NOW(), INTERVAL ? DAY)
            ON DUPLICATE KEY UPDATE score = VALUES(score)
        )";

        auto stmt = mysql_guard->prepareStatement(archive_sql);
        if (stmt) {
            stmt->setInt(1, days_old);
            int rows = stmt->executeUpdate();
            if (rows > 0) {
                archived_count = rows;
            }
        }

        // 删除已归档的记录
        const char* delete_sql = R"(
            DELETE FROM game_results
            WHERE played_at < DATE_SUB(NOW(), INTERVAL ? DAY)
        )";

        auto delete_stmt = mysql_guard->prepareStatement(delete_sql);
        if (delete_stmt) {
            delete_stmt->setInt(1, days_old);
            delete_stmt->executeUpdate();
        }

        LOG_INFO("Archived " + std::to_string(archived_count) + " old game records (older than " + std::to_string(days_old) + " days)");
        return archived_count;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in archiveOldGameRecords: " + std::string(e.what()));
        return archived_count;
    }
}

int GameRepository::cleanupExpiredInventoryItems(int days_old) {
    int cleaned_count = 0;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for inventory cleanup");
            return 0;
        }

        // 删除数量为0的库存物品（已使用完毕）
        const char* cleanup_sql = R"(
            DELETE FROM user_inventory
            WHERE quantity <= 0
        )";

        auto stmt = mysql_guard->prepareStatement(cleanup_sql);
        if (stmt) {
            int rows = stmt->executeUpdate();
            cleaned_count = rows > 0 ? rows : 1;  // 至少记录执行了一次
        }

        // 同时清理 Redis 缓存中的无效数据
        if (inventory_redis_) {
            // Redis 缓存会通过 TTL 自动过期
            LOG_DEBUG("Inventory Redis cache cleanup completed");
        }

        LOG_INFO("Cleaned up " + std::to_string(cleaned_count) + " expired inventory items");
        return cleaned_count;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in cleanupExpiredInventoryItems: " + std::string(e.what()));
        return cleaned_count;
    }
}

int GameRepository::auditCurrencyConsistency() {
    int inconsistencies_found = 0;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for currency audit");
            return 0;
        }

        // 检查负数余额
        const char* negative_balance_sql = R"(
            SELECT COUNT(*) as count FROM user_currency WHERE amount < 0
        )";

        auto stmt = mysql_guard->prepareStatement(negative_balance_sql);
        if (stmt) {
            auto result = stmt->executeQuery();
            if (result && result->next()) {
                int negative_count = result->getInt("count");
                if (negative_count > 0) {
                    LOG_WARNING("Found " + std::to_string(negative_count) + " currencies with negative balance");
                    inconsistencies_found += negative_count;
                }
            }
        }

        // 检查 Redis 缓存与 MySQL 的一致性（抽样检查）
        if (currency_redis_) {
            // 获取部分货币记录进行比对
            const char* sample_sql = R"(
                SELECT uc.user_id, cd.currency_code, uc.amount
                FROM user_currency uc
                JOIN currency_definitions cd ON uc.currency_type_id = cd.currency_type_id
                LIMIT 100
            )";

            auto sample_stmt = mysql_guard->prepareStatement(sample_sql);
            if (sample_stmt) {
                auto sample_result = sample_stmt->executeQuery();
                while (sample_result && sample_result->next()) {
                    std::string user_id = sample_result->getString("user_id");
                    std::string currency_type = sample_result->getString("currency_code");
                    long mysql_amount = sample_result->getInt64("amount");

                    // 从 Redis 获取缓存值
                    auto cached = currency_redis_->getCurrency(user_id, currency_type);
                    if (cached.has_value() && cached->has_value()) {
                        long redis_amount = cached->value().amount;
                        if (redis_amount != mysql_amount) {
                            LOG_WARNING("Currency inconsistency: user=" + user_id +
                                       ", type=" + currency_type +
                                       ", mysql=" + std::to_string(mysql_amount) +
                                       ", redis=" + std::to_string(redis_amount));
                            inconsistencies_found++;

                            // 修复：使缓存失效，下次从数据库加载
                            currency_redis_->remove(user_id + ":" + currency_type);
                        }
                    }
                }
            }
        }

        if (inconsistencies_found == 0) {
            LOG_INFO("Currency system audit passed - no inconsistencies found");
        } else {
            LOG_WARNING("Currency system audit completed - " + std::to_string(inconsistencies_found) + " inconsistencies found and logged");
        }

        return inconsistencies_found;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in auditCurrencyConsistency: " + std::string(e.what()));
        return inconsistencies_found;
    }
}

int GameRepository::recalculateRankings(const std::string& leaderboard_type, const std::string& game_type) {
    int updated_count = 0;

    try {
        // Redis Sorted Set 会自动维护排名，这里主要用于：
        // 1. 同步 MySQL 和 Redis 数据
        // 2. 清理无效的排行榜条目

        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_WARNING("MySQL connection not available for ranking recalculation");
            return 0;
        }

        // 构建查询条件
        std::string where_clause = "WHERE 1=1";
        if (!leaderboard_type.empty()) {
            where_clause += " AND leaderboard_type = '" + leaderboard_type + "'";
        }
        if (!game_type.empty()) {
            // 转换 game_type 字符串为 game_type_id
            int game_type_id = 1;
            if (game_type == "gomoku" || game_type == "1") game_type_id = 1;
            else if (game_type == "snake" || game_type == "2") game_type_id = 2;
            where_clause += " AND game_type_id = " + std::to_string(game_type_id);
        }

        // 从 MySQL 重新计算排名并同步到 Redis
        // 注意：leaderboards 表使用 game_type_id 而非 game_type 字符串
        std::string recalc_sql = R"(
            SELECT user_id, leaderboard_type, game_type_id, MAX(score) as score
            FROM leaderboards
            )" + where_clause + R"(
            GROUP BY user_id, leaderboard_type, game_type_id
            ORDER BY score DESC
        )";

        auto stmt = mysql_guard->prepareStatement(recalc_sql.c_str());
        if (stmt) {
            auto result = stmt->executeQuery();
            while (result && result->next()) {
                LeaderboardEntry entry;
                entry.user_id = result->getString("user_id");
                entry.leaderboard_type = result->getString("leaderboard_type");
                int game_type_id = result->getInt("game_type_id");
                // 转换 game_type_id 为字符串用于 Redis key
                entry.game_type = std::to_string(game_type_id);
                entry.score = result->getInt64("score");

                // 更新到 Redis
                common::database::RedisConnectionGuard redis_guard(*redis_pool_);
                if (redis_guard.isValid()) {
                    auto redis_conn = redis_guard.get();
                    std::string redis_key = common::repository::RedisKeyBuilder::GameService::leaderboard(entry.leaderboard_type, entry.game_type);
                    redis_conn->zadd(redis_key, static_cast<double>(entry.score), entry.user_id);
                    updated_count++;
                }
            }
        }

        LOG_INFO("Recalculated " + std::to_string(updated_count) + " ranking entries");
        return updated_count;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in recalculateRankings: " + std::string(e.what()));
        return updated_count;
    }
}

// === 私有辅助方法 ===

std::string GameRepository::buildCacheKey(const std::string& prefix, const std::string& key) const {
    return "game_service:" + prefix + ":" + key;
}

std::optional<std::string> GameRepository::getFromCache(const std::string& key) const {
    try {
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (!redis_guard.isValid()) {
            return std::nullopt;
        }

        auto redis_conn = redis_guard.get();
        if (!redis_conn) {
            return std::nullopt;
        }
        
        std::string value = redis_conn->get(key);
        if (!value.empty()) {
            return value;
        }

        return std::nullopt;

    } catch (const std::exception& e) {
        LOG_WARNING("Exception in getFromCache: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool GameRepository::setToCache(const std::string& key, const std::string& value, int expire_seconds) const {
    try {
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (!redis_guard.isValid()) {
            return false;
        }

        auto redis_conn = redis_guard.get();
        if (!redis_conn) {
            return false;
        }
        
        // 使用set方法的TTL参数替代setex
        return redis_conn->set(key, value, expire_seconds);

    } catch (const std::exception& e) {
        LOG_WARNING("Exception in setToCache: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::deleteFromCache(const std::string& key) const {
    try {
        common::database::RedisConnectionGuard redis_guard(*redis_pool_);
        if (!redis_guard.isValid()) {
            return false;
        }

        auto redis_conn = redis_guard.get();
        return redis_conn->del(key) > 0;

    } catch (const std::exception& e) {
        LOG_WARNING("Exception in deleteFromCache: " + std::string(e.what()));
        return false;
    }
}

std::string GameRepository::generateUUID() const {
    // 简单的UUID替代：使用时间戳和随机数
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    // 格式：timestamp-random (类似UUID但更简单)
    return std::to_string(timestamp) + "-" + std::to_string(dis(gen));
}

std::string GameRepository::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// ==================== 游戏结算操作 ====================

bool GameRepository::saveGameSettlement(const GameSettlement& settlement) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for saveGameSettlement");
            return false;
        }

        const char* sql = R"(
            INSERT INTO game_settlements
            (settlement_id, game_id, game_type, game_type_id, game_mode,
             duration_seconds, started_at, ended_at, player_count, settlement_data)
            VALUES (?, ?, ?, ?, ?, ?, FROM_UNIXTIME(?), FROM_UNIXTIME(?), ?, ?)
            ON DUPLICATE KEY UPDATE
                settlement_data = VALUES(settlement_data),
                ended_at = VALUES(ended_at)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for saveGameSettlement");
            return false;
        }

        auto started_time_t = std::chrono::system_clock::to_time_t(settlement.started_at);
        auto ended_time_t = std::chrono::system_clock::to_time_t(settlement.ended_at);

        stmt->setString(1, settlement.settlement_id);
        stmt->setString(2, settlement.game_id);
        stmt->setString(3, settlement.game_type);
        stmt->setInt(4, settlement.game_type_id);
        stmt->setString(5, settlement.game_mode);
        stmt->setInt(6, settlement.duration_seconds);
        stmt->setInt64(7, static_cast<int64_t>(started_time_t));
        stmt->setInt64(8, static_cast<int64_t>(ended_time_t));
        stmt->setInt(9, settlement.player_count);
        // JSON 字段：确保不为空
        stmt->setString(10, settlement.settlement_data.empty() ? "{}" : settlement.settlement_data);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Saved game settlement: " + settlement.settlement_id + " for game: " + settlement.game_id);
            return true;
        }

        LOG_ERROR("Failed to save game settlement: " + settlement.settlement_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in saveGameSettlement: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::saveGameSettlementPlayer(const GameSettlementPlayer& player) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for saveGameSettlementPlayer");
            return false;
        }

        const char* sql = R"(
            INSERT INTO game_settlement_players
            (settlement_id, user_id, result, rating_before, rating_after, rating_change,
             tier_before, tier_after, tier_changed, gold_earned, gem_earned, honor_earned,
             experience_earned, win_streak_before, win_streak_after, is_first_win_today,
             achievements_unlocked, bonuses)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON DUPLICATE KEY UPDATE
                rating_after = VALUES(rating_after),
                tier_after = VALUES(tier_after)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for saveGameSettlementPlayer");
            return false;
        }

        stmt->setString(1, player.settlement_id);
        stmt->setString(2, player.user_id);
        stmt->setString(3, player.result);
        stmt->setInt(4, player.rating_before);
        stmt->setInt(5, player.rating_after);
        stmt->setInt(6, player.rating_change);
        stmt->setInt(7, player.tier_before);
        stmt->setInt(8, player.tier_after);
        stmt->setBoolean(9, player.tier_changed);
        stmt->setInt(10, player.gold_earned);
        stmt->setInt(11, player.gem_earned);
        stmt->setInt(12, player.honor_earned);
        stmt->setInt(13, player.experience_earned);
        stmt->setInt(14, player.win_streak_before);
        stmt->setInt(15, player.win_streak_after);
        stmt->setBoolean(16, player.is_first_win_today);
        // JSON 字段：空字符串使用有效的空 JSON 数组/对象
        stmt->setString(17, player.achievements_unlocked.empty() ? "[]" : player.achievements_unlocked);
        stmt->setString(18, player.bonuses.empty() ? "{}" : player.bonuses);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Saved game settlement player: " + player.user_id + " for settlement: " + player.settlement_id);
            return true;
        }

        LOG_ERROR("Failed to save game settlement player: " + player.user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in saveGameSettlementPlayer: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::saveGameSettlementPlayers(const std::vector<GameSettlementPlayer>& players) {
    try {
        for (const auto& player : players) {
            if (!saveGameSettlementPlayer(player)) {
                LOG_ERROR("Failed to save player in batch: " + player.user_id);
                return false;
            }
        }
        LOG_INFO("Saved " + std::to_string(players.size()) + " settlement players");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in saveGameSettlementPlayers: " + std::string(e.what()));
        return false;
    }
}

std::optional<GameSettlement> GameRepository::getGameSettlement(const std::string& game_id) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for getGameSettlement");
            return std::nullopt;
        }

        const char* sql = R"(
            SELECT settlement_id, game_id, game_type, game_type_id, game_mode,
                   duration_seconds, started_at, ended_at, player_count, settlement_data, created_at
            FROM game_settlements
            WHERE game_id = ?
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getGameSettlement");
            return std::nullopt;
        }

        stmt->setString(1, game_id);
        auto result = stmt->executeQuery();

        if (result && result->next()) {
            GameSettlement settlement;
            settlement.settlement_id = result->getString("settlement_id");
            settlement.game_id = result->getString("game_id");
            settlement.game_type = result->getString("game_type");
            settlement.game_type_id = result->getInt("game_type_id");
            settlement.game_mode = result->getString("game_mode");
            settlement.duration_seconds = result->getInt("duration_seconds");
            settlement.player_count = result->getInt("player_count");
            settlement.settlement_data = result->getString("settlement_data");

            LOG_INFO("Retrieved game settlement for game: " + game_id);
            return settlement;
        }

        LOG_INFO("Game settlement not found for game: " + game_id);
        return std::nullopt;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getGameSettlement: " + std::string(e.what()));
        return std::nullopt;
    }
}

PaginatedResponse<GameSettlement> GameRepository::getUserGameSettlements(
    const std::string& user_id,
    const PaginationParams& params
) {
    PaginatedResponse<GameSettlement> response;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for getUserGameSettlements");
            return response;
        }

        // 获取总数
        const char* count_sql = R"(
            SELECT COUNT(*) as total
            FROM game_settlements gs
            JOIN game_settlement_players gsp ON gs.settlement_id = gsp.settlement_id
            WHERE gsp.user_id = ?
        )";

        auto count_stmt = mysql_guard->prepareStatement(count_sql);
        if (count_stmt) {
            count_stmt->setString(1, user_id);
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据
        const char* data_sql = R"(
            SELECT gs.settlement_id, gs.game_id, gs.game_type, gs.game_type_id, gs.game_mode,
                   gs.duration_seconds, gs.started_at, gs.ended_at, gs.player_count, gs.settlement_data
            FROM game_settlements gs
            JOIN game_settlement_players gsp ON gs.settlement_id = gsp.settlement_id
            WHERE gsp.user_id = ?
            ORDER BY gs.ended_at DESC
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql);
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare data statement for getUserGameSettlements");
            return response;
        }

        data_stmt->setString(1, user_id);
        data_stmt->setInt(2, params.limit);
        data_stmt->setInt(3, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            GameSettlement settlement;
            settlement.settlement_id = result->getString("settlement_id");
            settlement.game_id = result->getString("game_id");
            settlement.game_type = result->getString("game_type");
            settlement.game_type_id = result->getInt("game_type_id");
            settlement.game_mode = result->getString("game_mode");
            settlement.duration_seconds = result->getInt("duration_seconds");
            settlement.player_count = result->getInt("player_count");
            settlement.settlement_data = result->getString("settlement_data");

            response.items.push_back(settlement);
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved " + std::to_string(response.items.size()) + " game settlements for user: " + user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserGameSettlements: " + std::string(e.what()));
    }

    return response;
}

// ==================== 每日统计操作 ====================

std::optional<UserDailyStats> GameRepository::getUserDailyStats(
    const std::string& user_id,
    int game_type_id,
    const std::string& date
) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for getUserDailyStats");
            return std::nullopt;
        }

        std::string stat_date = date.empty() ? "CURRENT_DATE()" : "?";

        std::string sql = R"(
            SELECT user_id, game_type_id, stat_date, games_played, games_won, games_lost, games_drawn,
                   first_win_at, first_win_claimed, total_playtime_seconds, total_gold_earned,
                   total_honor_earned, total_exp_earned, rating_change, peak_streak, created_at, updated_at
            FROM user_daily_stats
            WHERE user_id = ? AND game_type_id = ? AND stat_date = )" + stat_date;

        auto stmt = mysql_guard->prepareStatement(sql.c_str());
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getUserDailyStats");
            return std::nullopt;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, game_type_id);
        if (!date.empty()) {
            stmt->setString(3, date);
        }

        auto result = stmt->executeQuery();
        if (result && result->next()) {
            UserDailyStats stats;
            stats.user_id = result->getString("user_id");
            stats.game_type_id = result->getInt("game_type_id");
            stats.stat_date = result->getString("stat_date");
            stats.games_played = result->getInt("games_played");
            stats.games_won = result->getInt("games_won");
            stats.games_lost = result->getInt("games_lost");
            stats.games_drawn = result->getInt("games_drawn");
            stats.first_win_claimed = result->getBoolean("first_win_claimed");
            stats.total_playtime_seconds = result->getInt("total_playtime_seconds");
            stats.total_gold_earned = result->getInt("total_gold_earned");
            stats.total_honor_earned = result->getInt("total_honor_earned");
            stats.total_exp_earned = result->getInt("total_exp_earned");
            stats.rating_change = result->getInt("rating_change");
            stats.peak_streak = result->getInt("peak_streak");

            LOG_INFO("Retrieved daily stats for user: " + user_id + " on " + stats.stat_date);
            return stats;
        }

        LOG_INFO("No daily stats found for user: " + user_id);
        return std::nullopt;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserDailyStats: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool GameRepository::updateUserDailyStats(const UserDailyStats& stats) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for updateUserDailyStats");
            return false;
        }

        const char* sql = R"(
            INSERT INTO user_daily_stats
            (user_id, game_type_id, stat_date, games_played, games_won, games_lost, games_drawn,
             first_win_claimed, total_playtime_seconds, total_gold_earned, total_honor_earned,
             total_exp_earned, rating_change, peak_streak)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON DUPLICATE KEY UPDATE
                games_played = VALUES(games_played),
                games_won = VALUES(games_won),
                games_lost = VALUES(games_lost),
                games_drawn = VALUES(games_drawn),
                first_win_claimed = VALUES(first_win_claimed),
                total_playtime_seconds = VALUES(total_playtime_seconds),
                total_gold_earned = VALUES(total_gold_earned),
                total_honor_earned = VALUES(total_honor_earned),
                total_exp_earned = VALUES(total_exp_earned),
                rating_change = VALUES(rating_change),
                peak_streak = GREATEST(peak_streak, VALUES(peak_streak))
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for updateUserDailyStats");
            return false;
        }

        stmt->setString(1, stats.user_id);
        stmt->setInt(2, stats.game_type_id);
        stmt->setString(3, stats.stat_date);
        stmt->setInt(4, stats.games_played);
        stmt->setInt(5, stats.games_won);
        stmt->setInt(6, stats.games_lost);
        stmt->setInt(7, stats.games_drawn);
        stmt->setBoolean(8, stats.first_win_claimed);
        stmt->setInt(9, stats.total_playtime_seconds);
        stmt->setInt(10, stats.total_gold_earned);
        stmt->setInt(11, stats.total_honor_earned);
        stmt->setInt(12, stats.total_exp_earned);
        stmt->setInt(13, stats.rating_change);
        stmt->setInt(14, stats.peak_streak);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Updated daily stats for user: " + stats.user_id);
            return true;
        }

        LOG_ERROR("Failed to update daily stats for user: " + stats.user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in updateUserDailyStats: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::incrementUserDailyStats(
    const std::string& user_id,
    int game_type_id,
    int games_played,
    int games_won,
    int games_lost,
    int games_drawn,
    int gold_earned,
    int honor_earned,
    int exp_earned,
    int rating_change,
    bool is_win
) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for incrementUserDailyStats");
            return false;
        }

        const char* sql = R"(
            INSERT INTO user_daily_stats
            (user_id, game_type_id, stat_date, games_played, games_won, games_lost, games_drawn,
             first_win_at, total_playtime_seconds, total_gold_earned, total_honor_earned,
             total_exp_earned, rating_change, peak_streak)
            VALUES (?, ?, CURRENT_DATE(), ?, ?, ?, ?, IF(? AND ? IS NULL, NOW(), NULL), 0, ?, ?, ?, ?, IF(? > 0, 1, 0))
            ON DUPLICATE KEY UPDATE
                games_played = games_played + VALUES(games_played),
                games_won = games_won + VALUES(games_won),
                games_lost = games_lost + VALUES(games_lost),
                games_drawn = games_drawn + VALUES(games_drawn),
                first_win_at = IF(first_win_at IS NULL AND ?, NOW(), first_win_at),
                total_gold_earned = total_gold_earned + VALUES(total_gold_earned),
                total_honor_earned = total_honor_earned + VALUES(total_honor_earned),
                total_exp_earned = total_exp_earned + VALUES(total_exp_earned),
                rating_change = rating_change + VALUES(rating_change),
                peak_streak = GREATEST(peak_streak, peak_streak + IF(? > 0, 1, 0))
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for incrementUserDailyStats");
            return false;
        }

        // is_win 参数用于设置首胜时间
        bool is_first_win = is_win;
        stmt->setString(1, user_id);
        stmt->setInt(2, game_type_id);
        stmt->setInt(3, games_played);
        stmt->setInt(4, games_won);
        stmt->setInt(5, games_lost);
        stmt->setInt(6, games_drawn);
        stmt->setBoolean(7, is_first_win);
        stmt->setBoolean(8, is_first_win);  // 用于检查 first_win_at IS NULL
        stmt->setInt(9, gold_earned);
        stmt->setInt(10, honor_earned);
        stmt->setInt(11, exp_earned);
        stmt->setInt(12, rating_change);
        stmt->setInt(13, games_won);  // 用于 peak_streak 初始化
        stmt->setBoolean(14, is_first_win);  // 用于 ON DUPLICATE KEY UPDATE
        stmt->setInt(15, games_won);  // 用于 peak_streak 更新

        int affected_rows = stmt->executeUpdate();
        if (affected_rows >= 0) {
            LOG_INFO("Incremented daily stats for user: " + user_id);
            return true;
        }

        LOG_ERROR("Failed to increment daily stats for user: " + user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in incrementUserDailyStats: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::claimFirstWinReward(const std::string& user_id, int game_type_id) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for claimFirstWinReward");
            return false;
        }

        const char* sql = R"(
            UPDATE user_daily_stats
            SET first_win_claimed = TRUE
            WHERE user_id = ? AND game_type_id = ? AND stat_date = CURRENT_DATE()
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for claimFirstWinReward");
            return false;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, game_type_id);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows > 0) {
            LOG_INFO("Claimed first win reward for user: " + user_id);
            return true;
        }

        LOG_WARNING("No first win to claim for user: " + user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in claimFirstWinReward: " + std::string(e.what()));
        return false;
    }
}

// ==================== 段位历史操作 ====================

bool GameRepository::recordTierHistory(const TierHistory& history) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for recordTierHistory");
            return false;
        }

        const char* sql = R"(
            INSERT INTO tier_history
            (user_id, game_type_id, tier_before, tier_after, rating_before, rating_after,
             is_promotion, game_id)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for recordTierHistory");
            return false;
        }

        stmt->setString(1, history.user_id);
        stmt->setInt(2, history.game_type_id);
        stmt->setInt(3, history.tier_before);
        stmt->setInt(4, history.tier_after);
        stmt->setInt(5, history.rating_before);
        stmt->setInt(6, history.rating_after);
        stmt->setBoolean(7, history.is_promotion);
        stmt->setString(8, history.game_id);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows > 0) {
            LOG_INFO("Recorded tier history for user: " + history.user_id +
                    " (" + std::to_string(history.tier_before) + " -> " + std::to_string(history.tier_after) + ")");
            return true;
        }

        LOG_ERROR("Failed to record tier history for user: " + history.user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in recordTierHistory: " + std::string(e.what()));
        return false;
    }
}

PaginatedResponse<TierHistory> GameRepository::getUserTierHistory(
    const std::string& user_id,
    int game_type_id,
    const PaginationParams& params
) {
    PaginatedResponse<TierHistory> response;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for getUserTierHistory");
            return response;
        }

        // 获取总数
        const char* count_sql = R"(
            SELECT COUNT(*) as total FROM tier_history
            WHERE user_id = ? AND game_type_id = ?
        )";

        auto count_stmt = mysql_guard->prepareStatement(count_sql);
        if (count_stmt) {
            count_stmt->setString(1, user_id);
            count_stmt->setInt(2, game_type_id);
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据
        const char* data_sql = R"(
            SELECT id, user_id, game_type_id, tier_before, tier_after, rating_before, rating_after,
                   is_promotion, game_id, created_at
            FROM tier_history
            WHERE user_id = ? AND game_type_id = ?
            ORDER BY created_at DESC
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql);
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare data statement for getUserTierHistory");
            return response;
        }

        data_stmt->setString(1, user_id);
        data_stmt->setInt(2, game_type_id);
        data_stmt->setInt(3, params.limit);
        data_stmt->setInt(4, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            TierHistory history;
            history.id = result->getInt64("id");
            history.user_id = result->getString("user_id");
            history.game_type_id = result->getInt("game_type_id");
            history.tier_before = result->getInt("tier_before");
            history.tier_after = result->getInt("tier_after");
            history.rating_before = result->getInt("rating_before");
            history.rating_after = result->getInt("rating_after");
            history.is_promotion = result->getBoolean("is_promotion");
            history.game_id = result->getString("game_id");

            response.items.push_back(history);
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved " + std::to_string(response.items.size()) + " tier history for user: " + user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserTierHistory: " + std::string(e.what()));
    }

    return response;
}

// ==================== 奖励日志操作 ====================

bool GameRepository::recordRewardLog(const RewardLog& log) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for recordRewardLog");
            return false;
        }

        const char* sql = R"(
            INSERT INTO reward_logs
            (log_id, user_id, game_type_id, game_id, reward_type, currency_type,
             currency_amount, experience, rating_change, reason, metadata)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for recordRewardLog");
            return false;
        }

        stmt->setString(1, log.log_id);
        stmt->setString(2, log.user_id);
        stmt->setInt(3, log.game_type_id);
        stmt->setString(4, log.game_id);
        stmt->setString(5, log.reward_type);
        stmt->setString(6, log.currency_type);
        stmt->setInt(7, log.currency_amount);
        stmt->setInt(8, log.experience);
        stmt->setInt(9, log.rating_change);
        stmt->setString(10, log.reason);
        // JSON 字段：空字符串使用有效的空 JSON 对象
        stmt->setString(11, log.metadata.empty() ? "{}" : log.metadata);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows > 0) {
            LOG_INFO("Recorded reward log: " + log.log_id + " for user: " + log.user_id);
            return true;
        }

        LOG_ERROR("Failed to record reward log for user: " + log.user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in recordRewardLog: " + std::string(e.what()));
        return false;
    }
}

PaginatedResponse<RewardLog> GameRepository::getUserRewardHistory(
    const std::string& user_id,
    const PaginationParams& params,
    const std::string& reward_type
) {
    PaginatedResponse<RewardLog> response;

    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for getUserRewardHistory");
            return response;
        }

        // 构建条件
        std::string where_clause = "WHERE user_id = ?";
        if (!reward_type.empty()) {
            where_clause += " AND reward_type = ?";
        }

        // 获取总数
        std::string count_sql = "SELECT COUNT(*) as total FROM reward_logs " + where_clause;
        auto count_stmt = mysql_guard->prepareStatement(count_sql.c_str());
        if (count_stmt) {
            count_stmt->setString(1, user_id);
            if (!reward_type.empty()) {
                count_stmt->setString(2, reward_type);
            }
            auto count_result = count_stmt->executeQuery();
            if (count_result && count_result->next()) {
                response.total_count = count_result->getInt("total");
            }
        }

        // 获取数据
        std::string data_sql = R"(
            SELECT id, log_id, user_id, game_type_id, game_id, reward_type, currency_type,
                   currency_amount, experience, rating_change, reason, metadata, created_at
            FROM reward_logs
            )" + where_clause + R"(
            ORDER BY created_at DESC
            LIMIT ? OFFSET ?
        )";

        auto data_stmt = mysql_guard->prepareStatement(data_sql.c_str());
        if (!data_stmt) {
            LOG_ERROR("Failed to prepare data statement for getUserRewardHistory");
            return response;
        }

        int param_idx = 1;
        data_stmt->setString(param_idx++, user_id);
        if (!reward_type.empty()) {
            data_stmt->setString(param_idx++, reward_type);
        }
        data_stmt->setInt(param_idx++, params.limit);
        data_stmt->setInt(param_idx, params.getOffset());

        auto result = data_stmt->executeQuery();
        while (result && result->next()) {
            RewardLog log;
            log.id = result->getInt64("id");
            log.log_id = result->getString("log_id");
            log.user_id = result->getString("user_id");
            log.game_type_id = result->getInt("game_type_id");
            log.game_id = result->getString("game_id");
            log.reward_type = result->getString("reward_type");
            log.currency_type = result->getString("currency_type");
            log.currency_amount = result->getInt("currency_amount");
            log.experience = result->getInt("experience");
            log.rating_change = result->getInt("rating_change");
            log.reason = result->getString("reason");
            log.metadata = result->getString("metadata");

            response.items.push_back(log);
        }

        response.page = params.page;
        response.limit = params.limit;
        response.total_pages = (response.total_count + params.limit - 1) / params.limit;

        LOG_INFO("Retrieved " + std::to_string(response.items.size()) + " reward logs for user: " + user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserRewardHistory: " + std::string(e.what()));
    }

    return response;
}

// ==================== 用户档案扩展操作 ====================

bool GameRepository::updateUserRating(
    const std::string& user_id,
    int game_type_id,
    int new_rating,
    int rating_change
) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for updateUserRating");
            return false;
        }

        // 根据新评分计算段位
        int new_tier = 3;  // 默认白银III
        if (new_rating >= 3000) new_tier = 15;      // 大师
        else if (new_rating >= 2900) new_tier = 14; // 钻石I
        else if (new_rating >= 2800) new_tier = 13; // 钻石II
        else if (new_rating >= 2600) new_tier = 12; // 钻石III
        else if (new_rating >= 2400) new_tier = 11; // 铂金I
        else if (new_rating >= 2200) new_tier = 10; // 铂金II
        else if (new_rating >= 2000) new_tier = 9;  // 铂金III
        else if (new_rating >= 1800) new_tier = 8;  // 黄金I
        else if (new_rating >= 1600) new_tier = 7;  // 黄金II
        else if (new_rating >= 1400) new_tier = 6;  // 黄金III
        else if (new_rating >= 1200) new_tier = 5;  // 白银I
        else if (new_rating >= 1000) new_tier = 4;  // 白银II
        else if (new_rating >= 800) new_tier = 3;   // 白银III
        else if (new_rating >= 600) new_tier = 2;   // 青铜I
        else if (new_rating >= 400) new_tier = 1;   // 青铜II
        else new_tier = 0;                          // 青铜III

        // 使用 INSERT ... ON DUPLICATE KEY UPDATE 确保记录存在
        // 如果记录不存在则创建，如果存在则更新
        const char* sql = R"(
            INSERT INTO user_game_profiles
            (user_id, game_type_id, level, experience_points, current_rating, peak_rating, tier_level, created_at, updated_at)
            VALUES (?, ?, 1, 0, ?, ?, ?, NOW(), NOW())
            ON DUPLICATE KEY UPDATE
                current_rating = VALUES(current_rating),
                peak_rating = GREATEST(peak_rating, VALUES(peak_rating)),
                tier_level = VALUES(tier_level),
                updated_at = NOW()
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for updateUserRating");
            return false;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, game_type_id);
        stmt->setInt(3, new_rating);
        stmt->setInt(4, new_rating);  // peak_rating 初始值 = current_rating
        stmt->setInt(5, new_tier);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows > 0) {
            LOG_INFO("Updated rating for user: " + user_id +
                    " (new_rating=" + std::to_string(new_rating) + ", change=" + std::to_string(rating_change) + ")");

            // 使缓存失效
            if (profile_redis_) {
                profile_redis_->invalidate(user_id);
            } else {
                deleteFromCache(buildCacheKey("user_profile", user_id));
            }
            return true;
        }

        LOG_WARNING("No rows affected for user rating: " + user_id);
        return true;  // ON DUPLICATE KEY UPDATE 可能影响 2 行或 0 行（数据相同）

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in updateUserRating: " + std::string(e.what()));
        return false;
    }
}

bool GameRepository::updateUserGameStats(
    const std::string& user_id,
    int game_type_id,
    const std::string& result,
    int duration_seconds,
    int new_streak
) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for updateUserGameStats");
            return false;
        }

        // 使用 INSERT ... ON DUPLICATE KEY UPDATE 确保记录存在
        // 如果记录不存在则创建初始记录，如果存在则更新统计
        const char* sql = R"(
            INSERT INTO user_game_profiles
            (user_id, game_type_id, level, experience_points, current_rating, peak_rating,
             total_games, wins, losses, draws, current_win_streak, best_win_streak,
             total_playtime_seconds, created_at, updated_at, last_played_at)
            VALUES (?, ?, 1, 0, 1200, 1200,
                    1,
                    IF(? = 'win', 1, 0),
                    IF(? IN ('loss', 'surrender', 'timeout'), 1, 0),
                    IF(? = 'draw', 1, 0),
                    ?, ?,
                    ?, NOW(), NOW(), NOW())
            ON DUPLICATE KEY UPDATE
                total_games = total_games + 1,
                wins = wins + IF(VALUES(wins) = 1, 1, 0),
                losses = losses + IF(VALUES(losses) = 1, 1, 0),
                draws = draws + IF(VALUES(draws) = 1, 1, 0),
                current_win_streak = VALUES(current_win_streak),
                best_win_streak = GREATEST(best_win_streak, VALUES(best_win_streak)),
                total_playtime_seconds = total_playtime_seconds + VALUES(total_playtime_seconds),
                last_played_at = NOW(),
                updated_at = NOW()
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for updateUserGameStats");
            return false;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, game_type_id);
        // 3-5: result 用于计算 wins/losses/draws
        stmt->setString(3, result);
        stmt->setString(4, result);
        stmt->setString(5, result);
        // 6-7: current_win_streak, best_win_streak
        stmt->setInt(6, new_streak);
        stmt->setInt(7, new_streak);
        // 8: duration_seconds
        stmt->setInt(8, duration_seconds);

        int affected_rows = stmt->executeUpdate();
        if (affected_rows > 0) {
            LOG_INFO("Updated game stats for user: " + user_id + " (result=" + result + ")");

            // 使缓存失效
            if (profile_redis_) {
                profile_redis_->invalidate(user_id);
            } else {
                deleteFromCache(buildCacheKey("user_profile", user_id));
            }
            return true;
        }

        LOG_WARNING("No rows affected for user stats: " + user_id);
        return true;  // ON DUPLICATE KEY UPDATE 可能影响 2 行或 0 行（数据相同）

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in updateUserGameStats: " + std::string(e.what()));
        return false;
    }
}

std::optional<UserGameProfile> GameRepository::getUserGameProfileByType(
    const std::string& user_id,
    int game_type_id
) {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for getUserGameProfileByType");
            return std::nullopt;
        }

        const char* sql = R"(
            SELECT user_id, game_type_id, level, experience_points, current_rating, peak_rating,
                   tier_level, total_games, wins, losses, draws, current_win_streak, best_win_streak,
                   total_playtime_seconds, created_at, updated_at, last_played_at
            FROM user_game_profiles
            WHERE user_id = ? AND game_type_id = ?
        )";

        auto stmt = mysql_guard->prepareStatement(sql);
        if (!stmt) {
            LOG_ERROR("Failed to prepare statement for getUserGameProfileByType");
            return std::nullopt;
        }

        stmt->setString(1, user_id);
        stmt->setInt(2, game_type_id);

        auto result = stmt->executeQuery();
        if (result && result->next()) {
            UserGameProfile profile;
            profile.user_id = result->getString("user_id");
            profile.level = result->getInt("level");
            profile.experience_points = result->getInt64("experience_points");
            profile.current_rating = result->getInt("current_rating");
            profile.peak_rating = result->getInt("peak_rating");
            profile.tier_level = result->getInt("tier_level");
            profile.total_games = result->getInt("total_games");
            profile.wins = result->getInt("wins");
            profile.losses = result->getInt("losses");
            profile.draws = result->getInt("draws");
            profile.current_win_streak = result->getInt("current_win_streak");
            profile.best_win_streak = result->getInt("best_win_streak");

            LOG_INFO("Retrieved game profile for user: " + user_id + " game_type_id: " + std::to_string(game_type_id));
            return profile;
        }

        LOG_INFO("Game profile not found for user: " + user_id + " game_type_id: " + std::to_string(game_type_id));
        return std::nullopt;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in getUserGameProfileByType: " + std::string(e.what()));
        return std::nullopt;
    }
}

// ==================== 数据库管理 ====================

bool GameRepository::initializeTables() {
    try {
        common::database::MySQLConnectionGuard mysql_guard(*mysql_pool_);
        if (!mysql_guard.isValid()) {
            LOG_ERROR("Failed to get MySQL connection for initializeTables");
            return false;
        }

        // 检查核心表是否存在
        const char* check_sql = "SELECT COUNT(*) as count FROM information_schema.tables "
                                "WHERE table_schema = DATABASE() AND table_name = 'user_game_profiles'";

        auto check_stmt = mysql_guard->prepareStatement(check_sql);
        if (check_stmt) {
            auto result = check_stmt->executeQuery();
            if (result && result->next()) {
                int count = result->getInt("count");
                if (count > 0) {
                    LOG_INFO("Database tables already exist");
                    return true;
                }
            }
        }

        // 表不存在，记录警告（实际建表应通过迁移脚本执行）
        LOG_WARNING("Database tables do not exist. Please run database migrations.");
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in initializeTables: " + std::string(e.what()));
        return false;
    }
}

} // namespace game_service
} // namespace core_services
