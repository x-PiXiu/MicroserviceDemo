#pragma once

#include "common/repository/base_redis_repository.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include "common/logger/logger.h"
#include "user_models.h"
#include <memory>

namespace core_services {
namespace user_service {

// 引入 RedisResult 和 RepositoryError 类型别名，方便使用
using common::repository::RedisResult;
using common::repository::RepositoryError;

/**
 * 用户信息 Redis Repository
 *
 * 管理用户基础信息的 Redis 缓存
 */
class UserRedisRepository : public common::repository::BaseRedisRepository<UserInfo> {
public:
    using Base = common::repository::BaseRedisRepository<UserInfo>;

    explicit UserRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::USER_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_USER,
               common::repository::RedisTTLConfig::UserService::USER_INFO) {}

    ~UserRedisRepository() override = default;

    // ========================================================================
    // 用户专用操作
    // ========================================================================

    /**
     * 通过用户名查找用户（使用二级索引）
     */
    RedisResult<std::optional<UserInfo>> findByUsername(const std::string& username) {
        // 先通过索引查找 user_id
        auto index_result = findByIndex("username", username);
        if (!index_result.has_value()) {
            return common::utils::unexpected(index_result.error());
        }

        if (!index_result->has_value()) {
            return std::nullopt;
        }

        // 再通过 user_id 查找用户
        return findById(index_result.value().value());
    }

    /**
     * 通过邮箱查找用户（使用二级索引）
     */
    RedisResult<std::optional<UserInfo>> findByEmail(const std::string& email) {
        auto index_result = findByIndex("email", email);
        if (!index_result.has_value()) {
            return common::utils::unexpected(index_result.error());
        }

        if (!index_result->has_value()) {
            return std::nullopt;
        }

        return findById(index_result.value().value());
    }

    /**
     * 保存用户并创建索引
     */
    RedisResult<bool> saveWithIndex(const UserInfo& user) {
        // 保存主实体
        auto save_result = save(user.user_id, user);
        if (!save_result.has_value() || !save_result.value()) {
            return save_result;
        }

        // 创建用户名索引
        auto username_idx = createIndex(
            "username", user.username, user.user_id,
            common::repository::RedisTTLConfig::UserService::USERNAME_INDEX
        );
        if (!username_idx.has_value() || !username_idx.value()) {
            LOG_WARNING("Failed to create username index for user: " + user.user_id);
        }

        // 创建邮箱索引
        auto email_idx = createIndex(
            "email", user.email, user.user_id,
            common::repository::RedisTTLConfig::UserService::EMAIL_INDEX
        );
        if (!email_idx.has_value() || !email_idx.value()) {
            LOG_WARNING("Failed to create email index for user: " + user.user_id);
        }

        return true;
    }

    /**
     * 使所有用户相关缓存失效
     */
    RedisResult<bool> invalidateUserCache(
        const std::string& user_id,
        const std::string& username,
        const std::string& email
    ) {
        // 删除主实体
        auto result = invalidate(user_id);
        if (!result.has_value() || !result.value()) {
            LOG_WARNING("Failed to invalidate user cache: " + user_id);
        }

        // 删除用户名索引
        removeIndex("username", username);

        // 删除邮箱索引
        removeIndex("email", email);

        return true;
    }

protected:
    nlohmann::json toJson(const UserInfo& user) const override {
        // 🔧 修复：缓存时包含敏感字段（password_hash, salt）
        // user_service 是内部服务，缓存数据用于内部认证流程
        return user.toJson(true);
    }

    std::optional<UserInfo> fromJson(const nlohmann::json& json) const override {
        try {
            return UserInfo::fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserInfo from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }
};

/**
 * 用户档案 Redis Repository
 */
class UserProfileRedisRepository : public common::repository::BaseRedisRepository<UserProfile> {
public:
    using Base = common::repository::BaseRedisRepository<UserProfile>;

    explicit UserProfileRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::USER_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_PROFILE,
               common::repository::RedisTTLConfig::UserService::USER_PROFILE) {}

    ~UserProfileRedisRepository() override = default;

protected:
    nlohmann::json toJson(const UserProfile& profile) const override {
        return profile.toJson();
    }

    std::optional<UserProfile> fromJson(const nlohmann::json& json) const override {
        try {
            return UserProfile::fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserProfile from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& user_id) const override {
        return common::repository::RedisKeyBuilder::UserService::profile(user_id);
    }
};

/**
 * 用户偏好设置 Redis Repository
 */
class UserPreferencesRedisRepository : public common::repository::BaseRedisRepository<UserPreferences> {
public:
    using Base = common::repository::BaseRedisRepository<UserPreferences>;

    explicit UserPreferencesRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::USER_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_PREFERENCES,
               common::repository::RedisTTLConfig::UserService::USER_PREFS) {}

    ~UserPreferencesRedisRepository() override = default;

protected:
    nlohmann::json toJson(const UserPreferences& prefs) const override {
        return prefs.toJson();
    }

    std::optional<UserPreferences> fromJson(const nlohmann::json& json) const override {
        try {
            return UserPreferences::fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserPreferences from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& user_id) const override {
        return common::repository::RedisKeyBuilder::UserService::preferences(user_id);
    }
};

/**
 * 用户会话 Redis Repository
 */
class UserSessionRedisRepository : public common::repository::BaseRedisRepository<UserSession> {
public:
    using Base = common::repository::BaseRedisRepository<UserSession>;

    explicit UserSessionRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::USER_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_SESSION,
               common::repository::RedisTTLConfig::AuthService::SESSION) {}

    ~UserSessionRedisRepository() override = default;

    /**
     * 获取用户的所有活跃会话
     */
    RedisResult<std::vector<UserSession>> getActiveSessions(const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            // 获取用户会话列表
            std::string sessions_key = common::repository::RedisKeyBuilder::userSessionsKey(user_id);
            auto session_ids = conn->smembers(sessions_key);

            std::vector<UserSession> sessions;
            for (const auto& session_id : session_ids) {
                auto session = findById(session_id);
                if (session.has_value() && session->has_value()) {
                    auto& s = session->value();
                    if (!s.isExpired()) {
                        sessions.push_back(s);
                    }
                }
            }

            return sessions;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get active sessions: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 添加会话到用户会话列表
     */
    RedisResult<bool> addToUserSessions(const std::string& user_id, const std::string& session_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string sessions_key = common::repository::RedisKeyBuilder::userSessionsKey(user_id);
            bool success = conn->sadd(sessions_key, session_id);

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to add session to user list: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

protected:
    nlohmann::json toJson(const UserSession& session) const override {
        return session.toJson();
    }

    std::optional<UserSession> fromJson(const nlohmann::json& json) const override {
        try {
            return UserSession::fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserSession from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }
};

} // namespace user_service
} // namespace core_services
