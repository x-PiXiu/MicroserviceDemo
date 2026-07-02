/**
 * @file leaderboard_push_service.h
 * @brief 排行榜实时推送服务 - 通过WebSocket推送排行榜变化通知
 * @author AI Assistant
 * @date 2026-02-23
 * @version 1.0.0
 *
 * 职责：
 * - 订阅排行榜变化事件
 * - 通过WebSocket推送排名变化通知
 * - 支持用户订阅特定排行榜
 * - 批量推送优化
 */

#pragma once

#include "leaderboard_manager.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <chrono>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

// 前向声明
class GameRepository;

/**
 * 排行榜推送通知消息
 */
struct LeaderboardPushMessage {
    std::string message_id;                         // 消息ID
    std::string type;                               // 消息类型: "rank_change", "new_top_player", "milestone"
    std::string leaderboard_type;                   // 排行榜类型
    std::string leaderboard_scope;                  // 排行榜范围
    std::string user_id;                            // 用户ID
    std::string display_name;                       // 显示名称
    int old_rank = 0;                               // 旧排名
    int new_rank = 0;                               // 新排名
    int64_t score = 0;                              // 分数
    int64_t score_change = 0;                       // 分数变化
    std::string tier_name;                          // 段位名称
    std::chrono::system_clock::time_point timestamp;// 时间戳

    /**
     * 转换为JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["message_id"] = message_id;
        j["type"] = type;
        j["leaderboard_type"] = leaderboard_type;
        j["leaderboard_scope"] = leaderboard_scope;
        j["user_id"] = user_id;
        j["display_name"] = display_name;
        j["old_rank"] = old_rank;
        j["new_rank"] = new_rank;
        j["score"] = score;
        j["score_change"] = score_change;
        j["tier_name"] = tier_name;

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()).count();
        j["timestamp"] = ms;
        return j;
    }
};

/**
 * 用户订阅信息
 */
struct UserSubscription {
    std::string user_id;
    std::string session_id;                         // WebSocket会话ID
    std::unordered_set<std::string> subscribed_types; // 订阅的排行榜类型
    std::chrono::system_clock::time_point subscribe_time;

    // 推送配置
    bool enable_rank_change_notify = true;          // 排名变化通知
    bool enable_top_player_notify = true;           // 新榜首通知
    bool enable_milestone_notify = true;            // 里程碑通知
    int min_rank_change_threshold = 10;             // 最小排名变化阈值
};

/**
 * 推送配置
 */
struct LeaderboardPushConfig {
    bool enabled = true;

    // 推送策略
    bool push_rank_changes = true;                  // 推送排名变化
    bool push_new_top_player = true;                // 推送新榜首
    bool push_milestones = true;                    // 推送里程碑

    // 阈值配置
    int min_rank_change_for_notify = 10;            // 触发通知的最小排名变化
    int top_n_for_broadcast = 100;                  // 前N名广播范围
    std::vector<int> milestone_ranks = {1, 10, 50, 100, 500, 1000}; // 里程碑排名

    // 批量推送
    bool enable_batch_push = true;                  // 启用批量推送
    int batch_interval_ms = 1000;                   // 批量推送间隔
    int max_batch_size = 100;                       // 最大批量大小

    // 限流
    int max_pushes_per_user_per_minute = 10;        // 每用户每分钟最大推送数

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LeaderboardPushConfig,
        enabled, push_rank_changes, push_new_top_player, push_milestones,
        min_rank_change_for_notify, top_n_for_broadcast,
        enable_batch_push, batch_interval_ms, max_batch_size)
};

/**
 * WebSocket发送函数类型
 */
using WebSocketSendFunc = std::function<void(const std::string& session_id, const std::string& message)>;
using BroadcastFunc = std::function<void(const std::string& message)>;
using UserBroadcastFunc = std::function<void(const std::string& user_id, const std::string& message)>;

/**
 * 排行榜实时推送服务
 */
class LeaderboardPushService {
public:
    /**
     * 构造函数
     */
    LeaderboardPushService(
        std::shared_ptr<LeaderboardManager> leaderboard_manager,
        const LeaderboardPushConfig& config = LeaderboardPushConfig());

    ~LeaderboardPushService();

    // ========== 生命周期 ==========

    /**
     * 启动服务
     */
    bool start();

    /**
     * 停止服务
     */
    void stop();

    /**
     * 是否运行中
     */
    bool isRunning() const { return running_.load(); }

    // ========== 配置 ==========

    /**
     * 更新配置
     */
    void updateConfig(const LeaderboardPushConfig& config);

    /**
     * 获取配置
     */
    const LeaderboardPushConfig& getConfig() const { return config_; }

    // ========== WebSocket回调设置 ==========

    /**
     * 设置WebSocket发送函数
     * @param send_func 发送函数 (session_id, message) -> void
     */
    void setWebSocketSendFunc(WebSocketSendFunc send_func) {
        ws_send_func_ = std::move(send_func);
    }

    /**
     * 设置广播函数
     * @param broadcast_func 广播函数 (message) -> void
     */
    void setBroadcastFunc(BroadcastFunc broadcast_func) {
        broadcast_func_ = std::move(broadcast_func);
    }

    /**
     * 设置用户广播函数
     * @param user_broadcast_func 用户广播函数 (user_id, message) -> void
     */
    void setUserBroadcastFunc(UserBroadcastFunc user_broadcast_func) {
        user_broadcast_func_ = std::move(user_broadcast_func);
    }

    // ========== 订阅管理 ==========

    /**
     * 用户订阅排行榜
     * @param user_id 用户ID
     * @param session_id WebSocket会话ID
     * @param types 订阅的排行榜类型（空表示全部）
     */
    void subscribe(
        const std::string& user_id,
        const std::string& session_id,
        const std::vector<std::string>& types = {});

    /**
     * 用户取消订阅
     * @param user_id 用户ID
     * @param session_id WebSocket会话ID
     */
    void unsubscribe(const std::string& user_id, const std::string& session_id);

    /**
     * 获取用户订阅信息
     */
    std::optional<UserSubscription> getUserSubscription(const std::string& user_id) const;

    /**
     * 获取所有订阅用户
     */
    std::vector<std::string> getSubscribedUsers() const;

    // ========== 推送接口 ==========

    /**
     * 处理排行榜更新通知
     * 从LeaderboardManager回调调用
     */
    void handleLeaderboardUpdate(const LeaderboardUpdateNotification& notification);

    /**
     * 手动推送排名变化
     */
    void pushRankChange(
        const std::string& user_id,
        LeaderboardType type,
        LeaderboardScope scope,
        int old_rank,
        int new_rank,
        int64_t score);

    /**
     * 广播新榜首
     */
    void broadcastNewTopPlayer(
        const std::string& user_id,
        const std::string& display_name,
        LeaderboardType type,
        int64_t score);

    /**
     * 推送里程碑通知
     */
    void pushMilestone(
        const std::string& user_id,
        const std::string& display_name,
        LeaderboardType type,
        int milestone_rank);

    // ========== 统计 ==========

    /**
     * 获取统计信息
     */
    nlohmann::json getStatistics() const;

private:
    std::shared_ptr<LeaderboardManager> leaderboard_manager_;
    LeaderboardPushConfig config_;

    // WebSocket回调
    WebSocketSendFunc ws_send_func_;
    BroadcastFunc broadcast_func_;
    UserBroadcastFunc user_broadcast_func_;

    // 订阅管理
    mutable std::shared_mutex subscription_mutex_;
    std::unordered_map<std::string, UserSubscription> subscriptions_; // user_id -> subscription
    std::unordered_map<std::string, std::string> session_to_user_;   // session_id -> user_id

    // 推送队列
    mutable std::mutex queue_mutex_;
    std::queue<LeaderboardPushMessage> push_queue_;
    std::condition_variable queue_cv_;

    // 限流
    mutable std::mutex rate_limit_mutex_;
    std::unordered_map<std::string, std::vector<std::chrono::system_clock::time_point>>
        user_push_history_; // user_id -> push timestamps

    // 运行状态
    std::atomic<bool> running_{false};
    std::thread push_thread_;

    // 统计
    mutable std::mutex stats_mutex_;
    int64_t total_pushes_{0};
    int64_t rank_change_pushes_{0};
    int64_t milestone_pushes_{0};
    int64_t top_player_broadcasts_{0};

    // ========== 内部方法 ==========

    /**
     * 推送线程主循环
     */
    void pushLoop();

    /**
     * 处理批量推送
     */
    void processBatchPush();

    /**
     * 发送推送消息
     */
    void sendPush(const LeaderboardPushMessage& message);

    /**
     * 检查是否应该推送
     */
    bool shouldPush(const std::string& user_id, int rank_change) const;

    /**
     * 检查限流
     */
    bool checkRateLimit(const std::string& user_id);

    /**
     * 检查是否为里程碑
     */
    std::optional<int> checkMilestone(int rank) const;

    /**
     * 生成消息ID
     */
    std::string generateMessageId() const;

    /**
     * 类型/范围转字符串
     */
    static std::string typeToString(LeaderboardType type);
    static std::string scopeToString(LeaderboardScope scope);
};

} // namespace game_service
} // namespace core_services
