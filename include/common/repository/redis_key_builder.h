#pragma once

#include <string>
#include <sstream>

namespace common {
namespace repository {

/**
 * Redis 键名构建器
 *
 * 键命名规范：
 * - {service}:{entity}:{id}           单个实体
 * - {service}:{entity}:{id}:{field}   实体字段
 * - {service}:list:{query_hash}       查询结果列表
 * - {service}:counter:{name}          计数器
 * - {service}:lock:{resource}         分布式锁
 * - session:{session_id}              会话数据（兼容旧格式）
 */
class RedisKeyBuilder {
public:
    // ========================================================================
    // 服务命名空间常量
    // ========================================================================
    static constexpr const char* AUTH_SERVICE = "auth";
    static constexpr const char* USER_SERVICE = "user";
    static constexpr const char* GAME_SERVICE = "game";
    static constexpr const char* GOMOKU_SERVICE = "gomoku";
    static constexpr const char* PAYMENT_SERVICE = "payment";

    // ========================================================================
    // 实体类型常量
    // ========================================================================
    static constexpr const char* ENTITY_USER = "user";
    static constexpr const char* ENTITY_SESSION = "session";
    static constexpr const char* ENTITY_PROFILE = "profile";
    static constexpr const char* ENTITY_PREFERENCES = "prefs";
    static constexpr const char* ENTITY_GAME_RECORD = "record";
    static constexpr const char* ENTITY_LEADERBOARD = "leaderboard";
    static constexpr const char* ENTITY_INVENTORY = "inventory";
    static constexpr const char* ENTITY_ACHIEVEMENT = "achievement";
    static constexpr const char* ENTITY_CURRENCY = "currency";
    static constexpr const char* ENTITY_STATS = "stats";
    static constexpr const char* ENTITY_DEVICE = "device";
    static constexpr const char* ENTITY_ROOM = "room";
    static constexpr const char* ENTITY_ORDER = "order";
    static constexpr const char* ENTITY_TRANSACTION = "tx";

    // ========================================================================
    // 通用键构建方法
    // ========================================================================

    /**
     * 构建实体键: {service}:{entity}:{id}
     */
    static std::string entityKey(
        const std::string& service,
        const std::string& entity,
        const std::string& id
    ) {
        std::ostringstream oss;
        oss << service << ":" << entity << ":" << id;
        return oss.str();
    }

    /**
     * 构建实体字段键: {service}:{entity}:{id}:{field}
     */
    static std::string entityFieldKey(
        const std::string& service,
        const std::string& entity,
        const std::string& id,
        const std::string& field
    ) {
        std::ostringstream oss;
        oss << service << ":" << entity << ":" << id << ":" << field;
        return oss.str();
    }

    /**
     * 构建列表键: {service}:list:{entity}:{query_hash}
     */
    static std::string listKey(
        const std::string& service,
        const std::string& entity,
        const std::string& query_hash
    ) {
        std::ostringstream oss;
        oss << service << ":list:" << entity << ":" << query_hash;
        return oss.str();
    }

    /**
     * 构建计数器键: {service}:counter:{name}
     */
    static std::string counterKey(
        const std::string& service,
        const std::string& name
    ) {
        std::ostringstream oss;
        oss << service << ":counter:" << name;
        return oss.str();
    }

    /**
     * 构建分布式锁键: {service}:lock:{resource}
     */
    static std::string lockKey(
        const std::string& service,
        const std::string& resource
    ) {
        std::ostringstream oss;
        oss << service << ":lock:" << resource;
        return oss.str();
    }

    /**
     * 构建会话键: session:{session_id}
     */
    static std::string sessionKey(const std::string& session_id) {
        return "session:" + session_id;
    }

    /**
     * 构建用户会话列表键: user:sessions:{user_id}
     */
    static std::string userSessionsKey(const std::string& user_id) {
        return "user:sessions:" + user_id;
    }

    // ========================================================================
    // User Service 专用键构建器
    // ========================================================================
    struct UserService {
        static std::string user(const std::string& user_id) {
            return entityKey(USER_SERVICE, ENTITY_USER, user_id);
        }

        static std::string profile(const std::string& user_id) {
            return entityKey(USER_SERVICE, ENTITY_PROFILE, user_id);
        }

        static std::string preferences(const std::string& user_id) {
            return entityKey(USER_SERVICE, ENTITY_PREFERENCES, user_id);
        }

        static std::string usernameIndex(const std::string& username) {
            return entityKey(USER_SERVICE, "idx_username", username);
        }

        static std::string emailIndex(const std::string& email) {
            return entityKey(USER_SERVICE, "idx_email", email);
        }

        static std::string loginAttempts(const std::string& user_id) {
            return counterKey(USER_SERVICE, "login_attempts:" + user_id);
        }
    };

    // ========================================================================
    // Auth Service 专用键构建器
    // ========================================================================
    struct AuthService {
        static std::string session(const std::string& session_id) {
            return sessionKey(session_id);
        }

        static std::string userSessions(const std::string& user_id) {
            return userSessionsKey(user_id);
        }

        static std::string device(const std::string& user_id, const std::string& device_id) {
            return entityFieldKey(AUTH_SERVICE, ENTITY_DEVICE, user_id, device_id);
        }

        static std::string securityEvent(const std::string& user_id) {
            return entityKey(AUTH_SERVICE, "security_events", user_id);
        }

        static std::string refreshToken(const std::string& user_id) {
            return entityKey(AUTH_SERVICE, "refresh_token", user_id);
        }

        static std::string rateLimit(const std::string& client_ip) {
            return counterKey(AUTH_SERVICE, "rate_limit:" + client_ip);
        }
    };

    // ========================================================================
    // Gomoku Service 专用键构建器
    // ========================================================================
    struct GomokuService {
        static std::string userStats(const std::string& user_id) {
            return entityKey(GOMOKU_SERVICE, ENTITY_STATS, user_id);
        }

        static std::string gameRecord(int64_t game_id) {
            return entityKey(GOMOKU_SERVICE, ENTITY_GAME_RECORD, std::to_string(game_id));
        }

        static std::string gameRecordByRoom(const std::string& room_id) {
            return entityKey(GOMOKU_SERVICE, "game_by_room", room_id);
        }

        static std::string leaderboard(const std::string& game_mode) {
            return entityKey(GOMOKU_SERVICE, ENTITY_LEADERBOARD, game_mode);
        }

        static std::string roomState(const std::string& room_id) {
            return entityKey(GOMOKU_SERVICE, ENTITY_ROOM, room_id);
        }

        static std::string roomLock(const std::string& room_id) {
            return lockKey(GOMOKU_SERVICE, "room:" + room_id);
        }

        static std::string userLock(const std::string& user_id) {
            return lockKey(GOMOKU_SERVICE, "user:" + user_id);
        }

        static std::string moveHistory(const std::string& room_id) {
            return entityFieldKey(GOMOKU_SERVICE, ENTITY_ROOM, room_id, "moves");
        }

        static std::string waitingPlayers(const std::string& game_mode) {
            return listKey(GOMOKU_SERVICE, "waiting", game_mode);
        }
    };

    // ========================================================================
    // Game Data Service 专用键构建器
    // ========================================================================
    struct GameService {
        static std::string userProfile(const std::string& user_id) {
            return entityKey(GAME_SERVICE, ENTITY_USER, user_id);
        }

        static std::string achievement(const std::string& user_id, const std::string& achievement_type) {
            return entityFieldKey(GAME_SERVICE, ENTITY_ACHIEVEMENT, user_id, achievement_type);
        }

        static std::string userAchievements(const std::string& user_id) {
            return entityKey(GAME_SERVICE, ENTITY_ACHIEVEMENT, user_id);
        }

        static std::string inventory(const std::string& user_id) {
            return entityKey(GAME_SERVICE, ENTITY_INVENTORY, user_id);
        }

        static std::string currency(const std::string& user_id, const std::string& currency_type) {
            return entityFieldKey(GAME_SERVICE, ENTITY_CURRENCY, user_id, currency_type);
        }

        static std::string leaderboard(const std::string& type, const std::string& game_type) {
            return entityFieldKey(GAME_SERVICE, ENTITY_LEADERBOARD, type, game_type);
        }

        static std::string dailyReward(const std::string& user_id) {
            return entityKey(GAME_SERVICE, "daily_reward", user_id);
        }
    };

    // ========================================================================
    // Payment Service 专用键构建器
    // ========================================================================
    struct PaymentService {
        static std::string order(const std::string& order_id) {
            return entityKey(PAYMENT_SERVICE, ENTITY_ORDER, order_id);
        }

        static std::string orderByUser(const std::string& user_id) {
            return entityKey(PAYMENT_SERVICE, "orders_by_user", user_id);
        }

        static std::string transaction(const std::string& tx_id) {
            return entityKey(PAYMENT_SERVICE, ENTITY_TRANSACTION, tx_id);
        }

        static std::string userBalance(const std::string& user_id) {
            return entityKey(PAYMENT_SERVICE, "balance", user_id);
        }

        static std::string paymentLock(const std::string& order_id) {
            return lockKey(PAYMENT_SERVICE, "order:" + order_id);
        }

        static std::string pendingPayment(const std::string& order_id) {
            return entityKey(PAYMENT_SERVICE, "pending", order_id);
        }
    };

    // ========================================================================
    // 工具方法
    // ========================================================================

    /**
     * 解析键中的实体 ID
     * @param key Redis 键
     * @return 实体 ID，如果解析失败返回空字符串
     */
    static std::string parseEntityId(const std::string& key) {
        // 查找最后一个冒号后的内容
        size_t last_colon = key.rfind(':');
        if (last_colon != std::string::npos && last_colon + 1 < key.length()) {
            return key.substr(last_colon + 1);
        }
        return "";
    }

    /**
     * 检查键是否属于指定服务
     */
    static bool belongsToService(const std::string& key, const std::string& service) {
        return key.find(service + ":") == 0;
    }

    /**
     * 构建模式匹配键（用于 SCAN 命令）
     */
    static std::string patternKey(
        const std::string& service,
        const std::string& entity = "*"
    ) {
        return service + ":" + entity + ":*";
    }
};

} // namespace repository
} // namespace common
