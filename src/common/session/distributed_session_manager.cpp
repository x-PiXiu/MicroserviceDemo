#include "common/session/distributed_session_manager.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace common {
namespace session {

// ============================================================================
// UserSession Implementation
// ============================================================================

bool UserSession::isExpired() const {
    return std::chrono::system_clock::now() >= expires_at;
}

std::string UserSession::toJson() const {
    nlohmann::json j;
    j["session_id"] = session_id;
    j["user_id"] = user_id;
    j["username"] = username;
    j["client_ip"] = client_ip;
    j["user_agent"] = user_agent;
    j["device_id"] = device_id;

    // 时间戳转换
    j["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        created_at.time_since_epoch()).count();
    j["last_active"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        last_active.time_since_epoch()).count();
    j["expires_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        expires_at.time_since_epoch()).count();

    j["attributes"] = attributes;

    return j.dump();
}

UserSession UserSession::fromJson(const std::string& json) {
    UserSession session;
    try {
        auto j = nlohmann::json::parse(json);

        if (j.contains("session_id")) session.session_id = j["session_id"];
        if (j.contains("user_id")) session.user_id = j["user_id"];
        if (j.contains("username")) session.username = j["username"];
        if (j.contains("client_ip")) session.client_ip = j["client_ip"];
        if (j.contains("user_agent")) session.user_agent = j["user_agent"];
        if (j.contains("device_id")) session.device_id = j["device_id"];

        if (j.contains("created_at")) {
            auto ms = j["created_at"].get<int64_t>();
            session.created_at = std::chrono::system_clock::time_point{
                std::chrono::milliseconds{ms}
            };
        }
        if (j.contains("last_active")) {
            auto ms = j["last_active"].get<int64_t>();
            session.last_active = std::chrono::system_clock::time_point{
                std::chrono::milliseconds{ms}
            };
        }
        if (j.contains("expires_at")) {
            auto ms = j["expires_at"].get<int64_t>();
            session.expires_at = std::chrono::system_clock::time_point{
                std::chrono::milliseconds{ms}
            };
        }

        if (j.contains("attributes")) {
            session.attributes = j["attributes"].get<std::unordered_map<std::string, std::string>>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse session JSON: " + std::string(e.what()));
    }

    return session;
}

// ============================================================================
// DistributedSessionManager Implementation
// ============================================================================

DistributedSessionManager::DistributedSessionManager(
    std::shared_ptr<database::RedisPool> redis_pool,
    const DistributedSessionConfig& config)
    : redis_pool_(redis_pool)
    , config_(config) {

    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }

    // 创建锁工厂
    lock_factory_ = std::make_unique<DistributedLockFactory>(redis_pool_);
}

std::string DistributedSessionManager::sessionKey(const std::string& session_id) const {
    return config_.session_prefix + session_id;
}

std::string DistributedSessionManager::userSessionsKey(const std::string& user_id) const {
    return config_.user_sessions_prefix + user_id;
}

std::string DistributedSessionManager::generateSessionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    // 生成 32 字符的会话 ID
    for (int i = 0; i < 32; i++) {
        ss << std::setw(1) << dis(gen);
    }

    // 添加时间戳
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    ss << "_" << std::dec << ms;

    return ss.str();
}

std::optional<UserSession> DistributedSessionManager::createSession(
    const std::string& user_id,
    const std::string& client_ip,
    const std::string& user_agent) {

    try {
        // 检查并发会话限制
        if (isConcurrentSessionLimitExceeded(user_id)) {
            // 清理最旧的会话
            cleanupOldestSessions(user_id, config_.max_concurrent_sessions - 1);
        }

        // 创建会话
        UserSession session;
        session.session_id = generateSessionId();
        session.user_id = user_id;
        session.client_ip = client_ip;
        session.user_agent = user_agent;
        session.created_at = std::chrono::system_clock::now();
        session.last_active = session.created_at;
        session.expires_at = session.created_at + config_.session_timeout;

        // 存储会话
        if (!storeSession(session)) {
            LOG_ERROR("Failed to store session for user: " + user_id);
            return std::nullopt;
        }

        // 添加到用户会话列表
        if (!addToUserSessions(user_id, session.session_id)) {
            LOG_ERROR("Failed to add session to user list: " + user_id);
            // 回滚
            redis_pool_->del(sessionKey(session.session_id));
            return std::nullopt;
        }

        LOG_INFO("Session created: " + session.session_id + " for user: " + user_id);
        return session;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create session: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<UserSession> DistributedSessionManager::getSession(const std::string& session_id) {
    try {
        auto data = redis_pool_->get(sessionKey(session_id));
        if (!data.has_value()) {
            return std::nullopt;
        }

        auto session = UserSession::fromJson(encrypt(data.value()));

        // 检查是否过期
        if (session.isExpired()) {
            deleteSession(session_id);
            return std::nullopt;
        }

        return session;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get session: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool DistributedSessionManager::updateSession(const UserSession& session) {
    try {
        // 更新最后活跃时间
        UserSession updated = session;
        updated.last_active = std::chrono::system_clock::now();
        updated.expires_at = updated.last_active + config_.session_timeout;

        return storeSession(updated);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to update session: " + std::string(e.what()));
        return false;
    }
}

bool DistributedSessionManager::deleteSession(const std::string& session_id) {
    try {
        // 先获取会话以获取 user_id
        auto session = getSession(session_id);

        // 删除会话
        redis_pool_->del(sessionKey(session_id));

        // 从用户会话列表中移除
        if (session.has_value()) {
            removeFromUserSessions(session->user_id, session_id);
        }

        LOG_INFO("Session deleted: " + session_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to delete session: " + std::string(e.what()));
        return false;
    }
}

int DistributedSessionManager::deleteUserSessions(const std::string& user_id) {
    try {
        auto sessions = getUserSessions(user_id);
        int count = 0;

        for (const auto& session : sessions) {
            if (deleteSession(session.session_id)) {
                count++;
            }
        }

        // 清空用户会话列表
        redis_pool_->del(userSessionsKey(user_id));

        LOG_INFO("Deleted " + std::to_string(count) + " sessions for user: " + user_id);
        return count;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to delete user sessions: " + std::string(e.what()));
        return 0;
    }
}

bool DistributedSessionManager::setAttribute(const std::string& session_id,
                                              const std::string& key,
                                              const std::string& value) {
    auto session = getSession(session_id);
    if (!session.has_value()) {
        return false;
    }

    session->attributes[key] = value;
    return updateSession(*session);
}

std::optional<std::string> DistributedSessionManager::getAttribute(
    const std::string& session_id,
    const std::string& key) {

    auto session = getSession(session_id);
    if (!session.has_value()) {
        return std::nullopt;
    }

    auto it = session->attributes.find(key);
    if (it == session->attributes.end()) {
        return std::nullopt;
    }

    return it->second;
}

bool DistributedSessionManager::removeAttribute(const std::string& session_id,
                                                 const std::string& key) {
    auto session = getSession(session_id);
    if (!session.has_value()) {
        return false;
    }

    session->attributes.erase(key);
    return updateSession(*session);
}

bool DistributedSessionManager::isSessionValid(const std::string& session_id) {
    auto session = getSession(session_id);
    return session.has_value() && !session->isExpired();
}

bool DistributedSessionManager::refreshSession(const std::string& session_id) {
    auto session = getSession(session_id);
    if (!session.has_value()) {
        return false;
    }

    return updateSession(*session);
}

bool DistributedSessionManager::isUserOnline(const std::string& user_id) {
    try {
        auto sessions = getUserSessions(user_id);
        return !sessions.empty();
    } catch (...) {
        return false;
    }
}

std::vector<UserSession> DistributedSessionManager::getUserSessions(const std::string& user_id) {
    std::vector<UserSession> result;

    try {
        // 获取用户的所有会话 ID
        auto session_ids = redis_pool_->smembers(userSessionsKey(user_id));

        for (const auto& sid : session_ids) {
            auto session = getSession(sid);
            if (session.has_value() && !session->isExpired()) {
                result.push_back(*session);
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get user sessions: " + std::string(e.what()));
    }

    return result;
}

int DistributedSessionManager::getUserSessionCount(const std::string& user_id) {
    try {
        return static_cast<int>(redis_pool_->scard(userSessionsKey(user_id)));
    } catch (...) {
        return 0;
    }
}

bool DistributedSessionManager::isConcurrentSessionLimitExceeded(const std::string& user_id) {
    return getUserSessionCount(user_id) >= config_.max_concurrent_sessions;
}

bool DistributedSessionManager::cleanupOldestSessions(const std::string& user_id, int keep_count) {
    try {
        auto sessions = getUserSessions(user_id);

        // 按最后活跃时间排序
        std::sort(sessions.begin(), sessions.end(),
            [](const UserSession& a, const UserSession& b) {
                return a.last_active > b.last_active;
            });

        // 删除超出数量的会话
        int deleted = 0;
        for (size_t i = keep_count; i < sessions.size(); i++) {
            if (deleteSession(sessions[i].session_id)) {
                deleted++;
            }
        }

        if (deleted > 0) {
            LOG_INFO("Cleaned up " + std::to_string(deleted) +
                    " oldest sessions for user: " + user_id);
        }

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to cleanup sessions: " + std::string(e.what()));
        return false;
    }
}

int DistributedSessionManager::getOnlineUserCount() {
    // 需要扫描所有用户会话键，这在生产环境中可能不高效
    // 建议使用单独的计数器
    LOG_WARNING("getOnlineUserCount() is not optimized for production use");
    return 0;
}

int DistributedSessionManager::getTotalSessionCount() {
    // 同上
    LOG_WARNING("getTotalSessionCount() is not optimized for production use");
    return 0;
}

bool DistributedSessionManager::storeSession(const UserSession& session) {
    try {
        auto json = session.toJson();
        auto data = decrypt(json);  // 加密（如果启用）

        // 存储会话，设置过期时间
        auto ttl = std::chrono::duration_cast<std::chrono::seconds>(
            session.expires_at - std::chrono::system_clock::now());

        if (ttl.count() <= 0) {
            ttl = config_.session_timeout;
        }

        redis_pool_->set(sessionKey(session.session_id), data, ttl);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to store session: " + std::string(e.what()));
        return false;
    }
}

bool DistributedSessionManager::addToUserSessions(const std::string& user_id,
                                                   const std::string& session_id) {
    try {
        redis_pool_->sadd(userSessionsKey(user_id), session_id);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to add to user sessions: " + std::string(e.what()));
        return false;
    }
}

bool DistributedSessionManager::removeFromUserSessions(const std::string& user_id,
                                                        const std::string& session_id) {
    try {
        redis_pool_->srem(userSessionsKey(user_id), session_id);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to remove from user sessions: " + std::string(e.what()));
        return false;
    }
}

std::string DistributedSessionManager::encrypt(const std::string& data) const {
    if (!config_.enable_session_encryption || config_.encryption_key.empty()) {
        return data;
    }

    // TODO: 实现加密逻辑（使用 AES 或其他算法）
    // 这里暂时返回原数据
    return data;
}

std::string DistributedSessionManager::decrypt(const std::string& data) const {
    if (!config_.enable_session_encryption || config_.encryption_key.empty()) {
        return data;
    }

    // TODO: 实现解密逻辑
    return data;
}

} // namespace session
} // namespace common
