#pragma once

#include <chrono>
#include <unordered_map>
#include <string>
#include <mutex>
#include <shared_mutex>

namespace common {
namespace repository {

/**
 * Redis TTL 集中配置管理
 *
 * 提供统一的缓存过期时间配置，避免 TTL 值分散在各服务中
 */
class RedisTTLConfig {
public:
    // ========================================================================
    // 标准 TTL 值
    // ========================================================================
    static constexpr std::chrono::seconds VERY_SHORT_TTL{10};     // 10秒 - 高频变化数据
    static constexpr std::chrono::seconds SHORT_TTL{60};          // 1分钟 - 实时性要求高
    static constexpr std::chrono::seconds MEDIUM_TTL{300};        // 5分钟 - 一般数据
    static constexpr std::chrono::seconds DEFAULT_TTL{3600};      // 1小时 - 默认值
    static constexpr std::chrono::seconds LONG_TTL{86400};        // 24小时 - 索引数据
    static constexpr std::chrono::seconds VERY_LONG_TTL{604800};  // 7天 - 历史数据

    // ========================================================================
    // User Service TTL 配置
    // ========================================================================
    struct UserService {
        static constexpr std::chrono::seconds USER_INFO{3600};           // 1小时
        static constexpr std::chrono::seconds USER_PROFILE{3600};        // 1小时
        static constexpr std::chrono::seconds USER_PREFS{7200};          // 2小时
        static constexpr std::chrono::seconds USERNAME_INDEX{86400};     // 24小时
        static constexpr std::chrono::seconds EMAIL_INDEX{86400};        // 24小时
        static constexpr std::chrono::seconds LOGIN_ATTEMPTS{300};        // 5分钟
    };

    // ========================================================================
    // Auth Service TTL 配置
    // ========================================================================
    struct AuthService {
        static constexpr std::chrono::seconds SESSION{3600};             // 1小时
        static constexpr std::chrono::seconds SESSION_LIST{3600};        // 1小时
        static constexpr std::chrono::seconds DEVICE_INFO{86400};        // 24小时
        static constexpr std::chrono::seconds SECURITY_EVENT{604800};    // 7天
        static constexpr std::chrono::seconds REFRESH_TOKEN{86400};      // 24小时
        static constexpr std::chrono::seconds RATE_LIMIT{60};            // 1分钟
    };

    // ========================================================================
    // Gomoku Service TTL 配置
    // ========================================================================
    struct GomokuService {
        static constexpr std::chrono::seconds USER_STATS{1800};          // 30分钟
        static constexpr std::chrono::seconds GAME_RECORD{3600};         // 1小时
        static constexpr std::chrono::seconds LEADERBOARD{300};          // 5分钟
        static constexpr std::chrono::seconds ROOM_STATE{7200};          // 2小时
        static constexpr std::chrono::seconds MOVE_HISTORY{7200};        // 2小时
        static constexpr std::chrono::seconds WAITING_PLAYERS{30};       // 30秒
    };

    // ========================================================================
    // Game Data Service TTL 配置
    // ========================================================================
    struct GameService {
        static constexpr std::chrono::seconds USER_PROFILE{3600};        // 1小时
        static constexpr std::chrono::seconds ACHIEVEMENT{86400};        // 24小时
        static constexpr std::chrono::seconds INVENTORY{1800};           // 30分钟
        static constexpr std::chrono::seconds CURRENCY{300};             // 5分钟
        static constexpr std::chrono::seconds LEADERBOARD{60};           // 1分钟
        static constexpr std::chrono::seconds DAILY_REWARD{3600};        // 1小时
    };

    // ========================================================================
    // Payment Service TTL 配置
    // ========================================================================
    struct PaymentService {
        static constexpr std::chrono::seconds ORDER{3600};               // 1小时
        static constexpr std::chrono::seconds ORDER_LIST{1800};          // 30分钟
        static constexpr std::chrono::seconds TRANSACTION{86400};        // 24小时
        static constexpr std::chrono::seconds USER_BALANCE{300};         // 5分钟
        static constexpr std::chrono::seconds PENDING_PAYMENT{1800};     // 30分钟
    };

    // ========================================================================
    // 动态配置管理
    // ========================================================================

    /**
     * 获取指定键模式的 TTL
     * @param key_pattern 键模式（如 "user:user:*"）
     * @return TTL 值，未配置返回 DEFAULT_TTL
     */
    static std::chrono::seconds getTTL(const std::string& key_pattern) {
        std::shared_lock<std::shared_mutex> lock(instance().mutex_);
        auto it = instance().custom_ttls_.find(key_pattern);
        if (it != instance().custom_ttls_.end()) {
            return it->second;
        }
        return DEFAULT_TTL;
    }

    /**
     * 设置自定义 TTL
     * @param key_pattern 键模式
     * @param ttl TTL 值
     */
    static void setTTL(const std::string& key_pattern, std::chrono::seconds ttl) {
        std::unique_lock<std::shared_mutex> lock(instance().mutex_);
        instance().custom_ttls_[key_pattern] = ttl;
    }

    /**
     * 移除自定义 TTL 配置
     */
    static void removeTTL(const std::string& key_pattern) {
        std::unique_lock<std::shared_mutex> lock(instance().mutex_);
        instance().custom_ttls_.erase(key_pattern);
    }

    /**
     * 清除所有自定义配置
     */
    static void clearCustomTTLs() {
        std::unique_lock<std::shared_mutex> lock(instance().mutex_);
        instance().custom_ttls_.clear();
    }

    /**
     * 根据服务名获取默认 TTL
     */
    static std::chrono::seconds getDefaultTTLForService(const std::string& service_name) {
        if (service_name == "user") return UserService::USER_INFO;
        if (service_name == "auth") return AuthService::SESSION;
        if (service_name == "gomoku") return GomokuService::USER_STATS;
        if (service_name == "game") return GameService::USER_PROFILE;
        if (service_name == "payment") return PaymentService::ORDER;
        return DEFAULT_TTL;
    }

    /**
     * 将秒数转换为人类可读格式
     */
    static std::string formatTTL(std::chrono::seconds ttl) {
        auto seconds = ttl.count();
        if (seconds < 60) {
            return std::to_string(seconds) + "s";
        } else if (seconds < 3600) {
            return std::to_string(seconds / 60) + "m";
        } else if (seconds < 86400) {
            return std::to_string(seconds / 3600) + "h";
        } else {
            return std::to_string(seconds / 86400) + "d";
        }
    }

private:
    // 单例模式
    RedisTTLConfig() = default;

    static RedisTTLConfig& instance() {
        static RedisTTLConfig inst;
        return inst;
    }

    std::unordered_map<std::string, std::chrono::seconds> custom_ttls_;
    mutable std::shared_mutex mutex_;
};

} // namespace repository
} // namespace common
