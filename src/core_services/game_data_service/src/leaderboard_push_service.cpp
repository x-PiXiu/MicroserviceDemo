/**
 * @file leaderboard_push_service.cpp
 * @brief 排行榜实时推送服务实现
 * @author AI Assistant
 * @date 2026-02-23
 */

#include "leaderboard_push_service.h"
#include <common/logger/logger.h>
#include <algorithm>
#include <sstream>

namespace core_services {
namespace game_service {

LeaderboardPushService::LeaderboardPushService(
    std::shared_ptr<LeaderboardManager> leaderboard_manager,
    const LeaderboardPushConfig& config)
    : leaderboard_manager_(std::move(leaderboard_manager))
    , config_(config) {
}

LeaderboardPushService::~LeaderboardPushService() {
    stop();
}

bool LeaderboardPushService::start() {
    if (running_.exchange(true)) {
        return true; // Already running
    }

    // 设置排行榜管理器回调
    if (leaderboard_manager_) {
        leaderboard_manager_->setUpdateCallback(
            [this](const LeaderboardUpdateNotification& notification) {
                handleLeaderboardUpdate(notification);
            });
    }

    // 启动推送线程
    push_thread_ = std::thread(&LeaderboardPushService::pushLoop, this);

    LOG_INFO("LeaderboardPushService started");
    return true;
}

void LeaderboardPushService::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }

    queue_cv_.notify_all();

    if (push_thread_.joinable()) {
        push_thread_.join();
    }

    // 清除排行榜管理器回调
    if (leaderboard_manager_) {
        leaderboard_manager_->setUpdateCallback(nullptr);
    }

    LOG_INFO("LeaderboardPushService stopped");
}

void LeaderboardPushService::updateConfig(const LeaderboardPushConfig& config) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    config_ = config;
}

void LeaderboardPushService::subscribe(
    const std::string& user_id,
    const std::string& session_id,
    const std::vector<std::string>& types) {

    std::unique_lock<std::shared_mutex> lock(subscription_mutex_);

    // 移除旧的会话映射
    auto old_it = session_to_user_.find(session_id);
    if (old_it != session_to_user_.end()) {
        subscriptions_.erase(old_it->second);
        session_to_user_.erase(old_it);
    }

    // 创建新订阅
    UserSubscription sub;
    sub.user_id = user_id;
    sub.session_id = session_id;
    sub.subscribe_time = std::chrono::system_clock::now();

    if (types.empty()) {
        // 订阅所有类型
        sub.subscribed_types = {"rating", "win_streak", "playtime", "win_rate", "experience", "achievements"};
    } else {
        sub.subscribed_types = std::unordered_set<std::string>(types.begin(), types.end());
    }

    subscriptions_[user_id] = sub;
    session_to_user_[session_id] = user_id;

    LOG_DEBUG("User subscribed to leaderboard updates");
}

void LeaderboardPushService::unsubscribe(
    const std::string& user_id,
    const std::string& session_id) {

    std::unique_lock<std::shared_mutex> lock(subscription_mutex_);

    auto it = subscriptions_.find(user_id);
    if (it != subscriptions_.end() && it->second.session_id == session_id) {
        subscriptions_.erase(it);
        session_to_user_.erase(session_id);
        LOG_DEBUG("User unsubscribed from leaderboard updates");
    }
}

std::optional<UserSubscription> LeaderboardPushService::getUserSubscription(
    const std::string& user_id) const {

    std::shared_lock<std::shared_mutex> lock(subscription_mutex_);
    auto it = subscriptions_.find(user_id);
    if (it != subscriptions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<std::string> LeaderboardPushService::getSubscribedUsers() const {
    std::shared_lock<std::shared_mutex> lock(subscription_mutex_);
    std::vector<std::string> users;
    users.reserve(subscriptions_.size());
    for (const auto& [user_id, _] : subscriptions_) {
        users.push_back(user_id);
    }
    return users;
}

void LeaderboardPushService::handleLeaderboardUpdate(
    const LeaderboardUpdateNotification& notification) {

    if (!config_.enabled || !running_) {
        return;
    }

    int rank_change = notification.old_rank - notification.new_rank;

    // 检查是否应该推送
    if (!shouldPush(notification.user_id, rank_change)) {
        return;
    }

    // 检查限流
    if (!checkRateLimit(notification.user_id)) {
        return;
    }

    // 创建推送消息
    LeaderboardPushMessage message;
    message.message_id = generateMessageId();
    message.type = "rank_change";
    message.leaderboard_type = typeToString(notification.type);
    message.leaderboard_scope = scopeToString(notification.scope);
    message.user_id = notification.user_id;
    message.old_rank = notification.old_rank;
    message.new_rank = notification.new_rank;
    message.score = notification.score;
    message.score_change = 0; // 需要额外信息
    message.timestamp = std::chrono::system_clock::now();

    // 添加到推送队列
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        push_queue_.push(message);
    }
    queue_cv_.notify_one();

    // 检查里程碑
    if (config_.push_milestones) {
        auto milestone = checkMilestone(notification.new_rank);
        if (milestone.has_value()) {
            pushMilestone(notification.user_id, "", notification.type, milestone.value());
        }
    }

    // 检查新榜首
    if (config_.push_new_top_player && notification.new_rank == 1 && notification.old_rank != 1) {
        broadcastNewTopPlayer(notification.user_id, "", notification.type, notification.score);
    }

    // 更新统计
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        rank_change_pushes_++;
        total_pushes_++;
    }
}

void LeaderboardPushService::pushRankChange(
    const std::string& user_id,
    LeaderboardType type,
    LeaderboardScope scope,
    int old_rank,
    int new_rank,
    int64_t score) {

    LeaderboardUpdateNotification notification;
    notification.user_id = user_id;
    notification.type = type;
    notification.scope = scope;
    notification.old_rank = old_rank;
    notification.new_rank = new_rank;
    notification.score = score;

    handleLeaderboardUpdate(notification);
}

void LeaderboardPushService::broadcastNewTopPlayer(
    const std::string& user_id,
    const std::string& display_name,
    LeaderboardType type,
    int64_t score) {

    if (!config_.enabled || !broadcast_func_) {
        return;
    }

    LeaderboardPushMessage message;
    message.message_id = generateMessageId();
    message.type = "new_top_player";
    message.leaderboard_type = typeToString(type);
    message.leaderboard_scope = "global";
    message.user_id = user_id;
    message.display_name = display_name;
    message.new_rank = 1;
    message.score = score;
    message.timestamp = std::chrono::system_clock::now();

    // 广播给所有订阅用户
    nlohmann::json json_msg = message.toJson();
    json_msg["message"] = display_name.empty()
        ? "New leaderboard leader!"
        : display_name + " is now #1!";

    broadcast_func_(json_msg.dump());

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        top_player_broadcasts_++;
        total_pushes_++;
    }

    LOG_INFO("Broadcast new top player in leaderboard");
}

void LeaderboardPushService::pushMilestone(
    const std::string& user_id,
    const std::string& display_name,
    LeaderboardType type,
    int milestone_rank) {

    if (!config_.enabled) {
        return;
    }

    // 检查用户是否订阅
    auto subscription = getUserSubscription(user_id);
    if (!subscription.has_value()) {
        return;
    }

    LeaderboardPushMessage message;
    message.message_id = generateMessageId();
    message.type = "milestone";
    message.leaderboard_type = typeToString(type);
    message.leaderboard_scope = "global";
    message.user_id = user_id;
    message.display_name = display_name;
    message.new_rank = milestone_rank;
    message.timestamp = std::chrono::system_clock::now();

    // 发送给特定用户
    if (user_broadcast_func_) {
        nlohmann::json json_msg = message.toJson();
        json_msg["message"] = "Congratulations! You reached rank #" + std::to_string(milestone_rank) + "!";
        user_broadcast_func_(user_id, json_msg.dump());
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        milestone_pushes_++;
        total_pushes_++;
    }

    LOG_INFO("Pushed milestone: user reached new rank");
}

void LeaderboardPushService::pushLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        queue_cv_.wait_for(lock, std::chrono::milliseconds(config_.batch_interval_ms), [this] {
            return !running_ || push_queue_.size() >= static_cast<size_t>(config_.max_batch_size);
        });

        if (!running_) {
            break;
        }

        processBatchPush();
    }
}

void LeaderboardPushService::processBatchPush() {
    std::vector<LeaderboardPushMessage> batch;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!push_queue_.empty() && batch.size() < static_cast<size_t>(config_.max_batch_size)) {
            batch.push_back(push_queue_.front());
            push_queue_.pop();
        }
    }

    for (const auto& message : batch) {
        sendPush(message);
    }
}

void LeaderboardPushService::sendPush(const LeaderboardPushMessage& message) {
    // 获取用户订阅信息
    auto subscription = getUserSubscription(message.user_id);
    if (!subscription.has_value()) {
        return;
    }

    // 检查是否订阅了该类型
    if (!subscription->subscribed_types.empty() &&
        subscription->subscribed_types.find(message.leaderboard_type) == subscription->subscribed_types.end()) {
        return;
    }

    // 检查用户是否启用排名变化通知
    if (message.type == "rank_change" && !subscription->enable_rank_change_notify) {
        return;
    }

    // 发送WebSocket消息
    if (ws_send_func_) {
        nlohmann::json json_msg = message.toJson();
        ws_send_func_(subscription->session_id, json_msg.dump());
    }

    // 也通过用户广播发送
    if (user_broadcast_func_) {
        nlohmann::json json_msg = message.toJson();
        user_broadcast_func_(message.user_id, json_msg.dump());
    }
}

bool LeaderboardPushService::shouldPush(const std::string& user_id, int rank_change) const {
    // 排名没有变化
    if (rank_change == 0) {
        return false;
    }

    // 检查是否达到最小变化阈值
    if (std::abs(rank_change) < config_.min_rank_change_for_notify) {
        // 如果用户进入前N名，仍然推送
        // 这里需要额外信息，简化处理
        return false;
    }

    // 检查用户是否订阅
    auto subscription = getUserSubscription(user_id);
    if (!subscription.has_value()) {
        return false;
    }

    return true;
}

bool LeaderboardPushService::checkRateLimit(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);

    auto now = std::chrono::system_clock::now();
    auto& history = user_push_history_[user_id];

    // 清理超过1分钟的记录
    auto one_minute_ago = now - std::chrono::minutes(1);
    history.erase(
        std::remove_if(history.begin(), history.end(),
            [one_minute_ago](const auto& t) { return t < one_minute_ago; }),
        history.end());

    // 检查是否超过限制
    if (static_cast<int>(history.size()) >= config_.max_pushes_per_user_per_minute) {
        return false;
    }

    // 记录本次推送
    history.push_back(now);
    return true;
}

std::optional<int> LeaderboardPushService::checkMilestone(int rank) const {
    for (int milestone : config_.milestone_ranks) {
        if (rank == milestone) {
            return milestone;
        }
    }
    return std::nullopt;
}

std::string LeaderboardPushService::generateMessageId() const {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return "lb_push_" + std::to_string(ms) + "_" + std::to_string(++counter);
}

std::string LeaderboardPushService::typeToString(LeaderboardType type) {
    switch (type) {
        case LeaderboardType::RATING: return "rating";
        case LeaderboardType::WIN_STREAK: return "win_streak";
        case LeaderboardType::PLAYTIME: return "playtime";
        case LeaderboardType::WIN_RATE: return "win_rate";
        case LeaderboardType::EXPERIENCE: return "experience";
        case LeaderboardType::ACHIEVEMENTS: return "achievements";
        default: return "unknown";
    }
}

std::string LeaderboardPushService::scopeToString(LeaderboardScope scope) {
    switch (scope) {
        case LeaderboardScope::GLOBAL: return "global";
        case LeaderboardScope::WEEKLY: return "weekly";
        case LeaderboardScope::MONTHLY: return "monthly";
        case LeaderboardScope::SEASONAL: return "seasonal";
        default: return "unknown";
    }
}

nlohmann::json LeaderboardPushService::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    std::shared_lock<std::shared_mutex> sub_lock(subscription_mutex_);

    return {
        {"enabled", config_.enabled},
        {"running", running_.load()},
        {"total_subscriptions", subscriptions_.size()},
        {"queue_size", push_queue_.size()},
        {"statistics", {
            {"total_pushes", total_pushes_},
            {"rank_change_pushes", rank_change_pushes_},
            {"milestone_pushes", milestone_pushes_},
            {"top_player_broadcasts", top_player_broadcasts_}
        }}
    };
}

} // namespace game_service
} // namespace core_services
