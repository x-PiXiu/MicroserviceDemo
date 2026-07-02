#pragma once

#include "common/repository/base_redis_repository.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include "common/logger/logger.h"
#include "user_models.h"
#include <memory>
#include <functional>

namespace core_services {
namespace auth_service {

// 引入 RedisResult 和 RepositoryError 类型别名，方便使用
using common::repository::RedisResult;
using common::repository::RepositoryError;

/**
 * @brief 设备信息结构体
 * @note 从 SessionManager 提取，用于 DeviceRedisRepository
 */
struct DeviceInfo {
    std::string device_id;                                  ///< 设备唯一标识
    std::string device_type;                                ///< 设备类型（web/mobile/desktop）
    std::string device_name;                                ///< 设备名称
    std::string os_name;                                    ///< 操作系统名称
    std::string os_version;                                 ///< 操作系统版本
    std::string browser_name;                               ///< 浏览器名称
    std::string browser_version;                            ///< 浏览器版本
    std::string user_agent;                                 ///< 用户代理字符串
    std::string screen_resolution;                          ///< 屏幕分辨率
    std::string timezone;                                   ///< 时区
    std::chrono::system_clock::time_point first_seen;       ///< 首次见到时间
    std::chrono::system_clock::time_point last_seen;        ///< 最后见到时间
    bool is_trusted = false;                                ///< 是否受信任设备

    DeviceInfo() = default;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["device_id"] = device_id;
        j["device_type"] = device_type;
        j["device_name"] = device_name;
        j["os_name"] = os_name;
        j["os_version"] = os_version;
        j["browser_name"] = browser_name;
        j["browser_version"] = browser_version;
        j["user_agent"] = user_agent;
        j["screen_resolution"] = screen_resolution;
        j["timezone"] = timezone;
        j["is_trusted"] = is_trusted;
        return j;
    }

    static DeviceInfo fromJson(const nlohmann::json& j) {
        DeviceInfo info;
        if (j.contains("device_id")) info.device_id = j["device_id"].get<std::string>();
        if (j.contains("device_type")) info.device_type = j["device_type"].get<std::string>();
        if (j.contains("device_name")) info.device_name = j["device_name"].get<std::string>();
        if (j.contains("os_name")) info.os_name = j["os_name"].get<std::string>();
        if (j.contains("os_version")) info.os_version = j["os_version"].get<std::string>();
        if (j.contains("browser_name")) info.browser_name = j["browser_name"].get<std::string>();
        if (j.contains("browser_version")) info.browser_version = j["browser_version"].get<std::string>();
        if (j.contains("user_agent")) info.user_agent = j["user_agent"].get<std::string>();
        if (j.contains("screen_resolution")) info.screen_resolution = j["screen_resolution"].get<std::string>();
        if (j.contains("timezone")) info.timezone = j["timezone"].get<std::string>();
        if (j.contains("is_trusted")) info.is_trusted = j["is_trusted"].get<bool>();
        return info;
    }

    /**
     * @brief 从用户代理字符串解析设备信息
     */
    static DeviceInfo parseFromUserAgent(const std::string& user_agent, const std::string& device_id) {
        DeviceInfo device_info;
        device_info.device_id = device_id;
        device_info.user_agent = user_agent;
        device_info.first_seen = std::chrono::system_clock::now();
        device_info.last_seen = device_info.first_seen;

        std::string lower_ua = user_agent;
        std::transform(lower_ua.begin(), lower_ua.end(), lower_ua.begin(), ::tolower);

        // 检测设备类型
        if (lower_ua.find("mobile") != std::string::npos ||
            lower_ua.find("android") != std::string::npos ||
            lower_ua.find("iphone") != std::string::npos) {
            device_info.device_type = "mobile";
        } else if (lower_ua.find("tablet") != std::string::npos ||
                  lower_ua.find("ipad") != std::string::npos) {
            device_info.device_type = "tablet";
        } else {
            device_info.device_type = "desktop";
        }

        // 检测操作系统
        if (lower_ua.find("windows") != std::string::npos) {
            device_info.os_name = "Windows";
        } else if (lower_ua.find("mac") != std::string::npos) {
            device_info.os_name = "macOS";
        } else if (lower_ua.find("linux") != std::string::npos) {
            device_info.os_name = "Linux";
        } else if (lower_ua.find("android") != std::string::npos) {
            device_info.os_name = "Android";
        } else if (lower_ua.find("ios") != std::string::npos) {
            device_info.os_name = "iOS";
        }

        // 检测浏览器
        if (lower_ua.find("chrome") != std::string::npos) {
            device_info.browser_name = "Chrome";
        } else if (lower_ua.find("firefox") != std::string::npos) {
            device_info.browser_name = "Firefox";
        } else if (lower_ua.find("safari") != std::string::npos) {
            device_info.browser_name = "Safari";
        } else if (lower_ua.find("edge") != std::string::npos) {
            device_info.browser_name = "Edge";
        }

        return device_info;
    }
};

/**
 * 会话 Redis Repository
 *
 * 管理会话数据的 Redis 缓存，使用统一的键命名和 TTL 管理
 * @version 2.1.0 - 添加加密/解密回调支持和动态 TTL 支持
 */
class SessionRedisRepository : public common::repository::BaseRedisRepository<UserSession> {
public:
    using Base = common::repository::BaseRedisRepository<UserSession>;

    // 加密/解密回调类型定义
    using EncryptFunc = std::function<std::string(const std::string&)>;
    using DecryptFunc = std::function<std::string(const std::string&)>;

    explicit SessionRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::AUTH_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_SESSION,
               common::repository::RedisTTLConfig::AuthService::SESSION) {}

    ~SessionRedisRepository() override = default;

    // ========================================================================
    // 加密/解密回调设置
    // ========================================================================

    /**
     * @brief 设置加密/解密回调函数
     * @param encrypt 加密函数
     * @param decrypt 解密函数
     */
    void setEncryptionCallbacks(EncryptFunc encrypt, DecryptFunc decrypt) {
        encrypt_func_ = std::move(encrypt);
        decrypt_func_ = std::move(decrypt);
        encryption_enabled_ = true;
        LOG_INFO("SessionRedisRepository: 加密回调已设置");
    }

    /**
     * @brief 清除加密/解密回调
     */
    void clearEncryptionCallbacks() {
        encrypt_func_ = nullptr;
        decrypt_func_ = nullptr;
        encryption_enabled_ = false;
    }

    /**
     * @brief 检查是否启用加密
     */
    bool isEncryptionEnabled() const {
        return encryption_enabled_ && encrypt_func_ && decrypt_func_;
    }

    // ========================================================================
    // 重写基类方法以支持加密和动态 TTL
    // ========================================================================

    /**
     * @brief 保存会话（支持动态 TTL）
     * @param session_id 会话 ID
     * @param session 会话数据
     * @param ttl 自定义 TTL（0 表示使用默认值）
     */
    RedisResult<bool> saveWithTTL(const std::string& session_id,
                                   const UserSession& session,
                                   std::chrono::seconds ttl) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(session_id);
            std::string data = serializeSession(session);

            // 使用传入的 TTL 或默认 TTL
            auto actual_ttl = ttl.count() > 0 ? ttl : default_ttl_;
            bool success = conn->set(key, data, static_cast<int>(actual_ttl.count()));

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save session: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * @brief 保存会话（使用默认 TTL）
     */
    RedisResult<bool> save(const std::string& session_id,
                           const UserSession& session,
                           std::chrono::seconds ttl = std::chrono::seconds(0)) override {
        return saveWithTTL(session_id, session, ttl);
    }

    /**
     * @brief 根据 ID 查找会话（支持解密）
     */
    RedisResult<std::optional<UserSession>> findById(const std::string& session_id) override {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = buildKey(session_id);
            std::string data = conn->get(key);

            if (data.empty()) {
                return std::nullopt;
            }

            auto session = deserializeSession(data);
            return session;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to find session: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    // ========================================================================
    // 会话专用操作
    // ========================================================================

    /**
     * 获取用户的所有会话 ID
     */
    RedisResult<std::vector<std::string>> getUserSessionIds(const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::AuthService::userSessions(user_id);
            auto session_ids = conn->smembers(key);

            return session_ids;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get user session ids: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 获取用户的所有活跃会话
     */
    RedisResult<std::vector<UserSession>> getUserSessions(const std::string& user_id) {
        auto ids_result = getUserSessionIds(user_id);
        if (!ids_result.has_value()) {
            return common::utils::unexpected(ids_result.error());
        }

        std::vector<UserSession> sessions;
        for (const auto& session_id : ids_result.value()) {
            auto session = findById(session_id);
            if (session.has_value() && session->has_value()) {
                auto& s = session->value();
                // 检查是否过期
                if (!s.isExpired()) {
                    sessions.push_back(s);
                } else {
                    // 清理过期会话
                    removeFromUserSessions(user_id, session_id);
                }
            }
        }

        return sessions;
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

            std::string key = common::repository::RedisKeyBuilder::AuthService::userSessions(user_id);
            bool success = conn->sadd(key, session_id);

            // 设置会话列表的过期时间
            conn->expire(key, static_cast<int>(
                common::repository::RedisTTLConfig::AuthService::SESSION_LIST.count()));

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to add session to user list: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 从用户会话列表中移除会话
     */
    RedisResult<bool> removeFromUserSessions(const std::string& user_id, const std::string& session_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::AuthService::userSessions(user_id);
            bool success = conn->srem(key, session_id);

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to remove session from user list: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 获取用户会话数量
     */
    RedisResult<int64_t> getUserSessionCount(const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::AuthService::userSessions(user_id);
            int64_t count = conn->scard(key);

            return count;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get user session count: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 删除用户所有会话
     */
    RedisResult<int> deleteAllUserSessions(const std::string& user_id) {
        auto sessions_result = getUserSessions(user_id);
        if (!sessions_result.has_value()) {
            return common::utils::unexpected(sessions_result.error());
        }

        int deleted_count = 0;
        for (const auto& session : sessions_result.value()) {
            auto remove_result = remove(session.session_id);
            if (remove_result.has_value() && remove_result.value()) {
                deleted_count++;
            }
        }

        // 清空用户会话列表
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (conn) {
                std::string key = common::repository::RedisKeyBuilder::AuthService::userSessions(user_id);
                conn->del(key);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to clear user sessions list: " + std::string(e.what()));
        }

        return deleted_count;
    }

    /**
     * 检查是否超过并发会话限制
     */
    RedisResult<bool> isConcurrentLimitExceeded(const std::string& user_id, int max_sessions) {
        auto count_result = getUserSessionCount(user_id);
        if (!count_result.has_value()) {
            return common::utils::unexpected(count_result.error());
        }

        return count_result.value() >= max_sessions;
    }

    /**
     * 更新会话最后活跃时间
     */
    RedisResult<bool> updateActivity(const std::string& session_id) {
        auto session_result = findById(session_id);
        if (!session_result.has_value()) {
            return common::utils::unexpected(session_result.error());
        }

        if (!session_result->has_value()) {
            return common::utils::unexpected(common::repository::RepositoryError::notFound(
                "Session not found: " + session_id));
        }

        UserSession session = session_result.value().value();
        session.last_activity_at = std::chrono::system_clock::now();

        return save(session_id, session);
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

    std::string buildKey(const std::string& session_id) const override {
        return common::repository::RedisKeyBuilder::AuthService::session(session_id);
    }

private:
    // 加密/解密回调
    EncryptFunc encrypt_func_;
    DecryptFunc decrypt_func_;
    bool encryption_enabled_ = false;

    /**
     * @brief 序列化会话数据（可选加密）
     */
    std::string serializeSession(const UserSession& session) const {
        std::string data = toJson(session).dump();

        // 如果启用加密且有加密回调，则加密数据
        if (encryption_enabled_ && encrypt_func_) {
            return encrypt_func_(data);
        }

        return data;
    }

    /**
     * @brief 反序列化会话数据（可选解密）
     */
    std::optional<UserSession> deserializeSession(const std::string& data) const {
        std::string json_str = data;

        // 如果启用解密且有解密回调，则解密数据
        if (encryption_enabled_ && decrypt_func_) {
            json_str = decrypt_func_(data);
        }

        try {
            auto json = nlohmann::json::parse(json_str);
            return fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to deserialize session: " + std::string(e.what()));
            return std::nullopt;
        }
    }
};

/**
 * 设备信息 Redis Repository
 */
class DeviceRedisRepository : public common::repository::BaseRedisRepository<DeviceInfo> {
public:
    using Base = common::repository::BaseRedisRepository<DeviceInfo>;

    explicit DeviceRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::AUTH_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_DEVICE,
               common::repository::RedisTTLConfig::AuthService::DEVICE_INFO) {}

    ~DeviceRedisRepository() override = default;

    /**
     * 获取用户的所有设备
     */
    RedisResult<std::vector<DeviceInfo>> getUserDevices(const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            // 获取用户设备列表
            std::string devices_key = userDevicesKey(user_id);
            auto device_ids = conn->smembers(devices_key);

            std::vector<DeviceInfo> devices;
            for (const auto& device_id : device_ids) {
                auto device = findById(user_id + ":" + device_id);
                if (device.has_value() && device->has_value()) {
                    devices.push_back(device.value().value());
                }
            }

            return devices;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get user devices: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 保存设备并添加到用户设备列表
     */
    RedisResult<bool> saveDevice(const std::string& user_id, const DeviceInfo& device) {
        // 保存设备信息
        auto save_result = save(user_id + ":" + device.device_id, device);
        if (!save_result.has_value() || !save_result.value()) {
            return save_result;
        }

        // 添加到用户设备列表
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string devices_key = userDevicesKey(user_id);
            conn->sadd(devices_key, device.device_id);

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to add device to user list: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 设置设备信任状态
     */
    RedisResult<bool> setDeviceTrusted(const std::string& user_id,
                                        const std::string& device_id,
                                        bool trusted) {
        auto device_result = findById(user_id + ":" + device_id);
        if (!device_result.has_value()) {
            return common::utils::unexpected(device_result.error());
        }

        if (!device_result->has_value()) {
            return common::utils::unexpected(common::repository::RepositoryError::notFound(
                "Device not found: " + device_id));
        }

        DeviceInfo device = device_result.value().value();
        device.is_trusted = trusted;

        return save(user_id + ":" + device_id, device);
    }

    /**
     * 移除设备
     */
    RedisResult<bool> removeDevice(const std::string& user_id, const std::string& device_id) {
        // 删除设备信息
        auto remove_result = remove(user_id + ":" + device_id);
        if (!remove_result.has_value() || !remove_result.value()) {
            return remove_result;
        }

        // 从用户设备列表中移除
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (conn) {
                std::string devices_key = userDevicesKey(user_id);
                conn->srem(devices_key, device_id);
            }

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to remove device from user list: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

protected:
    nlohmann::json toJson(const DeviceInfo& device) const override {
        return device.toJson();
    }

    std::optional<DeviceInfo> fromJson(const nlohmann::json& json) const override {
        try {
            return DeviceInfo::fromJson(json);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse DeviceInfo from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& id) const override {
        // id 格式为 "user_id:device_id"
        size_t pos = id.find(':');
        if (pos != std::string::npos) {
            std::string user_id = id.substr(0, pos);
            std::string device_id = id.substr(pos + 1);
            return common::repository::RedisKeyBuilder::AuthService::device(user_id, device_id);
        }
        return common::repository::RedisKeyBuilder::entityKey(
            common::repository::RedisKeyBuilder::AUTH_SERVICE,
            common::repository::RedisKeyBuilder::ENTITY_DEVICE,
            id);
    }

private:
    std::string userDevicesKey(const std::string& user_id) const {
        return common::repository::RedisKeyBuilder::entityKey(
            common::repository::RedisKeyBuilder::AUTH_SERVICE,
            "user_devices",
            user_id);
    }
};

/**
 * 安全事件 Redis Repository
 *
 * 使用 Redis List 存储安全事件
 */
class SecurityEventRedisRepository {
public:
    explicit SecurityEventRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : redis_pool_(redis_pool) {}

    /**
     * 记录安全事件
     */
    RedisResult<bool> recordEvent(const std::string& user_id,
                                   const nlohmann::json& event) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::AuthService::securityEvent(user_id);
            std::string event_str = event.dump();

            // 添加到列表头部
            conn->lpush(key, event_str);

            // 设置过期时间
            conn->expire(key, static_cast<int>(
                common::repository::RedisTTLConfig::AuthService::SECURITY_EVENT.count()));

            // 限制列表长度（保留最近 100 条）
            conn->lpop(key);  // 简化处理，实际应使用 LTRIM

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to record security event: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 获取用户安全事件
     */
    RedisResult<std::vector<nlohmann::json>> getEvents(
        const std::string& user_id,
        int limit = 50
    ) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::AuthService::securityEvent(user_id);

            // 注意: 当前 RedisConnection 不支持 lrange，暂时返回空列表
            // TODO: 后续可以添加 lrange 支持或使用其他数据结构
            LOG_WARNING("SecurityEventRedisRepository::getEvents - lrange not implemented, returning empty list");
            return std::vector<nlohmann::json>();

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get security events: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

private:
    std::shared_ptr<common::database::RedisPool> redis_pool_;

    // 🔧 已移除 getConnection() 方法，所有调用改为使用 RedisConnectionGuard
    // std::shared_ptr<common::database::RedisConnection> getConnection() {
    //     if (!redis_pool_) return nullptr;
    //     return redis_pool_->getConnection();
    // }
};

} // namespace auth_service
} // namespace core_services
