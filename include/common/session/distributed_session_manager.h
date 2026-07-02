#pragma once

#include "distributed_lock.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <string>
#include <optional>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace common {
namespace session {

/**
 * 用户会话信息
 */
struct UserSession {
    std::string session_id;
    std::string user_id;
    std::string username;
    std::string client_ip;
    std::string user_agent;
    std::string device_id;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_active;
    std::chrono::system_clock::time_point expires_at;
    std::unordered_map<std::string, std::string> attributes;

    /**
     * 检查会话是否过期
     */
    bool isExpired() const;

    /**
     * 序列化为 JSON
     */
    std::string toJson() const;

    /**
     * 从 JSON 反序列化
     */
    static UserSession fromJson(const std::string& json);
};

/**
 * 分布式会话管理器配置
 */
struct DistributedSessionConfig {
    std::chrono::seconds session_timeout{3600};         // 会话超时（1小时）
    std::chrono::seconds max_idle_time{1800};          // 最大空闲时间（30分钟）
    int max_concurrent_sessions = 5;                    // 最大并发会话数
    std::string session_prefix = "session:";            // 会话键前缀
    std::string user_sessions_prefix = "user_sessions:";// 用户会话列表前缀
    bool enable_session_encryption = false;             // 是否启用加密
    std::string encryption_key;                         // 加密密钥
};

/**
 * 分布式会话管理器（基于 Redis）
 *
 * 特性：
 * - 支持多实例部署
 * - 会话自动过期
 * - 并发会话限制
 * - 会话属性存储
 */
class DistributedSessionManager {
public:
    /**
     * 构造函数
     */
    DistributedSessionManager(
        std::shared_ptr<database::RedisPool> redis_pool,
        const DistributedSessionConfig& config = {}
    );

    ~DistributedSessionManager() = default;

    // ==================== 会话生命周期 ====================

    /**
     * 创建会话
     * @param user_id 用户 ID
     * @param client_ip 客户端 IP
     * @param user_agent 用户代理
     * @return 会话信息，失败返回 nullopt
     */
    std::optional<UserSession> createSession(
        const std::string& user_id,
        const std::string& client_ip,
        const std::string& user_agent = ""
    );

    /**
     * 获取会话
     * @param session_id 会话 ID
     * @return 会话信息，不存在返回 nullopt
     */
    std::optional<UserSession> getSession(const std::string& session_id);

    /**
     * 更新会话
     * @param session 会话信息
     * @return 是否成功
     */
    bool updateSession(const UserSession& session);

    /**
     * 删除会话
     * @param session_id 会话 ID
     * @return 是否成功
     */
    bool deleteSession(const std::string& session_id);

    /**
     * 删除用户所有会话
     * @param user_id 用户 ID
     * @return 删除的会话数
     */
    int deleteUserSessions(const std::string& user_id);

    // ==================== 会话属性 ====================

    /**
     * 设置会话属性
     */
    bool setAttribute(const std::string& session_id,
                      const std::string& key,
                      const std::string& value);

    /**
     * 获取会话属性
     */
    std::optional<std::string> getAttribute(const std::string& session_id,
                                            const std::string& key);

    /**
     * 删除会话属性
     */
    bool removeAttribute(const std::string& session_id,
                         const std::string& key);

    // ==================== 会话状态 ====================

    /**
     * 检查会话是否有效
     */
    bool isSessionValid(const std::string& session_id);

    /**
     * 刷新会话（延长过期时间）
     */
    bool refreshSession(const std::string& session_id);

    /**
     * 检查用户是否在线
     */
    bool isUserOnline(const std::string& user_id);

    /**
     * 获取用户所有会话
     */
    std::vector<UserSession> getUserSessions(const std::string& user_id);

    /**
     * 获取用户在线会话数
     */
    int getUserSessionCount(const std::string& user_id);

    // ==================== 会话限制 ====================

    /**
     * 检查是否超过并发会话限制
     */
    bool isConcurrentSessionLimitExceeded(const std::string& user_id);

    /**
     * 清理最旧的会话以腾出空间
     */
    bool cleanupOldestSessions(const std::string& user_id, int keep_count);

    // ==================== 统计 ====================

    /**
     * 获取在线用户数
     */
    int getOnlineUserCount();

    /**
     * 获取总会话数
     */
    int getTotalSessionCount();

private:
    std::shared_ptr<database::RedisPool> redis_pool_;
    DistributedSessionConfig config_;
    std::unique_ptr<DistributedLockFactory> lock_factory_;

    // 键构建
    std::string sessionKey(const std::string& session_id) const;
    std::string userSessionsKey(const std::string& user_id) const;

    // 会话存储
    bool storeSession(const UserSession& session);
    bool addToUserSessions(const std::string& user_id, const std::string& session_id);
    bool removeFromUserSessions(const std::string& user_id, const std::string& session_id);

    // 生成会话 ID
    std::string generateSessionId();

    // 加密/解密（如果启用）
    std::string encrypt(const std::string& data) const;
    std::string decrypt(const std::string& data) const;
};

} // namespace session
} // namespace common
