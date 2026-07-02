/**
 * @file match_making_manager.cpp
 * @brief 自动匹配管理器实现
 * @details 实现匹配池管理、匹配循环、匹配策略执行等功能
 * @author Game Server Team
 * @date 2025-01-23
 * @version 1.0.0
 */

#include "match_making_manager.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <climits>

#include "logger/logger.h"

namespace game_services {
namespace gomoku {

// 静态成员初始化
std::atomic<uint64_t> MatchMakingManager::match_counter_{0};

MatchMakingManager::MatchMakingManager(const MatchPoolConfig& config)
    : config_(config)
    , ranked_strategy_(std::make_unique<RatingMatchStrategy>())
    , casual_strategy_(std::make_unique<CasualMatchStrategy>()) {
    LOG_INFO("MatchMakingManager 创建，配置: " + config_.toJson().dump());
}

MatchMakingManager::~MatchMakingManager() {
    stop();
    LOG_INFO("MatchMakingManager 销毁");
}

bool MatchMakingManager::start() {
    if (running_.exchange(true)) {
        LOG_WARNING("MatchMakingManager 已经在运行");
        return true;
    }

    stop_requested_ = false;

    // 启动匹配线程
    match_thread_ = std::make_unique<std::thread>(&MatchMakingManager::matchLoop, this);

    LOG_INFO("MatchMakingManager 启动成功");
    return true;
}

void MatchMakingManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    stop_requested_ = true;
    match_cv_.notify_all();

    if (match_thread_ && match_thread_->joinable()) {
        match_thread_->join();
    }
    match_thread_.reset();

    LOG_INFO("MatchMakingManager 已停止");
}

bool MatchMakingManager::addRequest(const MatchRequest& request) {
    if (request.user_id.empty()) {
        LOG_ERROR("添加匹配请求失败：用户ID为空");
        return false;
    }

    // 检查池容量
    if (static_cast<int>(requests_.size()) >= config_.max_pool_size) {
        LOG_ERROR("添加匹配请求失败：匹配池已满");
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(pool_mutex_);

    // 检查是否已在池中
    if (requests_.find(request.user_id) != requests_.end()) {
        LOG_WARNING("用户已在匹配池中: " + request.user_id);
        return false;
    }

    // 添加请求
    MatchRequest new_request = request;
    new_request.status = MatchStatus::WAITING;
    new_request.join_time = std::chrono::steady_clock::now();
    new_request.last_match_attempt = new_request.join_time;

    // 设置初始评分范围
    new_request.min_rating = std::max(0, new_request.rating - config_.initial_rating_range);
    new_request.max_rating = new_request.rating + config_.initial_rating_range;

    requests_[new_request.user_id] = new_request;
    waiting_users_.insert(new_request.user_id);

    // 按模式分组
    if (new_request.mode == MatchMode::RANKED) {
        ranked_requests_.insert(new_request.user_id);
    } else {
        casual_requests_.insert(new_request.user_id);
    }

    lock.unlock();

    // 触发事件
    emitEvent(MatchEventType::PLAYER_JOINED, new_request.request_id,
              new_request.user_id, new_request.toJson());

    // 通知匹配线程
    match_cv_.notify_one();

    LOG_INFO("用户加入匹配池: " + new_request.user_id +
             ", 评分: " + std::to_string(new_request.rating) +
             ", 模式: " + (new_request.mode == MatchMode::RANKED ? "ranked" : "casual"));

    return true;
}

bool MatchMakingManager::cancelRequest(const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);

    auto it = requests_.find(user_id);
    if (it == requests_.end()) {
        LOG_WARNING("取消匹配失败：用户不在匹配池中: " + user_id);
        return false;
    }

    MatchRequest request = it->second;
    request.status = MatchStatus::CANCELLED;

    // 从池中移除
    waiting_users_.erase(user_id);
    ranked_requests_.erase(user_id);
    casual_requests_.erase(user_id);
    requests_.erase(it);

    lock.unlock();

    // 更新统计
    requests_cancelled_.fetch_add(1);

    // 触发事件
    emitEvent(MatchEventType::PLAYER_CANCELLED, request.request_id, user_id);

    // 状态回调
    if (status_callback_) {
        status_callback_(user_id, MatchStatus::WAITING, MatchStatus::CANCELLED);
    }

    LOG_INFO("用户取消匹配: " + user_id);
    return true;
}

std::optional<MatchRequest> MatchMakingManager::getRequest(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);

    auto it = requests_.find(user_id);
    if (it == requests_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool MatchMakingManager::isInPool(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);
    return requests_.find(user_id) != requests_.end();
}

int MatchMakingManager::getWaitTime(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);

    auto it = requests_.find(user_id);
    if (it == requests_.end()) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - it->second.join_time);
    return static_cast<int>(duration.count());
}

MatchPoolStatus MatchMakingManager::getPoolStatus() const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);

    MatchPoolStatus status;
    status.total_requests = static_cast<int>(requests_.size());
    status.waiting_requests = static_cast<int>(waiting_users_.size());
    status.ranked_count = static_cast<int>(ranked_requests_.size());
    status.casual_count = static_cast<int>(casual_requests_.size());

    // 计算评分分布
    if (!requests_.empty()) {
        status.rating_min = INT_MAX;
        status.rating_max = 0;
        int64_t rating_sum = 0;
        int max_wait = 0;
        int64_t wait_sum = 0;

        for (const auto& [uid, req] : requests_) {
            status.rating_min = std::min(status.rating_min, req.rating);
            status.rating_max = std::max(status.rating_max, req.rating);
            rating_sum += req.rating;

            // 更新等待时间
            auto wait = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - req.join_time).count());
            max_wait = std::max(max_wait, wait);
            wait_sum += wait;
        }

        status.rating_avg = static_cast<double>(rating_sum) / requests_.size();
        status.max_wait_seconds = max_wait;
        status.avg_wait_seconds = static_cast<int>(wait_sum / requests_.size());
    }

    status.total_matches_today = total_matches_.load();
    status.last_update = std::chrono::steady_clock::now();

    return status;
}

size_t MatchMakingManager::getPoolSize() const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);
    return requests_.size();
}

std::vector<MatchRequest> MatchMakingManager::getWaitingRequests() const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);

    std::vector<MatchRequest> result;
    result.reserve(waiting_users_.size());

    for (const auto& user_id : waiting_users_) {
        auto it = requests_.find(user_id);
        if (it != requests_.end()) {
            result.push_back(it->second);
        }
    }

    return result;
}

void MatchMakingManager::updateConfig(const MatchPoolConfig& config) {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    config_ = config;
    LOG_INFO("匹配池配置已更新: " + config_.toJson().dump());
}

nlohmann::json MatchMakingManager::getStatistics() const {
    return {
        {"total_matches", total_matches_.load()},
        {"quality_distribution", {
            {"excellent", total_excellent_matches_.load()},
            {"good", total_good_matches_.load()},
            {"fair", total_fair_matches_.load()},
            {"acceptable", total_acceptable_matches_.load()}
        }},
        {"requests", {
            {"processed", requests_processed_.load()},
            {"cancelled", requests_cancelled_.load()},
            {"timeout", requests_timeout_.load()}
        }},
        {"total_wait_time_ms", total_wait_time_ms_.load()},
        {"pool_status", getPoolStatus().toJson()}
    };
}

void MatchMakingManager::resetStatistics() {
    total_matches_ = 0;
    total_excellent_matches_ = 0;
    total_good_matches_ = 0;
    total_fair_matches_ = 0;
    total_acceptable_matches_ = 0;
    total_wait_time_ms_ = 0;
    requests_processed_ = 0;
    requests_cancelled_ = 0;
    requests_timeout_ = 0;
    LOG_INFO("匹配统计数据已重置");
}

void MatchMakingManager::matchLoop() {
    LOG_INFO("匹配循环线程启动");

    while (!stop_requested_) {
        // 等待或超时
        std::unique_lock<std::mutex> lock(match_mutex_);
        match_cv_.wait_for(lock,
            std::chrono::milliseconds(config_.match_interval_ms));
        lock.unlock();

        if (stop_requested_) {
            break;
        }

        // 执行匹配
        int matched = performMatching();

        // 更新等待时间和处理超时
        updateAllWaitTimes();
        handleTimeoutRequests();

        if (matched > 0) {
            LOG_DEBUG("本轮匹配完成，成功配对: " + std::to_string(matched));
        }
    }

    LOG_INFO("匹配循环线程退出");
}

int MatchMakingManager::performMatching() {
    int match_count = 0;
    std::vector<std::pair<std::string, std::string>> matched_pairs;

    // 获取等待中的请求快照
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);

    // 先处理竞技模式（基于评分）
    std::vector<MatchRequest> ranked_candidates;
    for (const auto& user_id : ranked_requests_) {
        auto it = requests_.find(user_id);
        if (it != requests_.end() && it->second.status == MatchStatus::WAITING) {
            ranked_candidates.push_back(it->second);
        }
    }

    // 按等待时间排序（等待久的优先）
    std::sort(ranked_candidates.begin(), ranked_candidates.end(),
        [](const MatchRequest& a, const MatchRequest& b) {
            return a.wait_seconds > b.wait_seconds;
        });

    lock.unlock();

    // 尝试竞技模式匹配
    std::set<std::string> matched_users;
    for (const auto& request : ranked_candidates) {
        if (matched_users.find(request.user_id) != matched_users.end()) {
            continue;
        }

        // 获取候选者（排除已匹配的）
        std::vector<MatchRequest> available;
        {
            std::shared_lock<std::shared_mutex> inner_lock(pool_mutex_);
            for (const auto& user_id : ranked_requests_) {
                if (matched_users.find(user_id) != matched_users.end()) {
                    continue;
                }
                auto it = requests_.find(user_id);
                if (it != requests_.end() &&
                    it->second.status == MatchStatus::WAITING &&
                    it->second.user_id != request.user_id) {
                    available.push_back(it->second);
                }
            }
        }

        auto result = tryMatchRequest(request, available);
        if (result) {
            matched_users.insert(request.user_id);
            matched_users.insert(result->getOpponentId(request.user_id));
            matched_pairs.emplace_back(request.user_id, result->getOpponentId(request.user_id));

            // 触发回调
            if (match_result_callback_) {
                match_result_callback_(*result);
            }

            updateStatistics(*result);
            match_count++;

            LOG_INFO("匹配成功: " + request.user_id + " vs " +
                     result->getOpponentId(request.user_id) +
                     ", 质量: " + std::to_string(static_cast<int>(result->quality)));
        }
    }

    // 处理休闲模式
    {
        std::shared_lock<std::shared_mutex> lock(pool_mutex_);

        std::vector<MatchRequest> casual_candidates;
        for (const auto& user_id : casual_requests_) {
            if (matched_users.find(user_id) != matched_users.end()) {
                continue;
            }
            auto it = requests_.find(user_id);
            if (it != requests_.end() && it->second.status == MatchStatus::WAITING) {
                casual_candidates.push_back(it->second);
            }
        }

        lock.unlock();

        // 按等待时间排序
        std::sort(casual_candidates.begin(), casual_candidates.end(),
            [](const MatchRequest& a, const MatchRequest& b) {
                return a.wait_seconds > b.wait_seconds;
            });

        // 休闲模式：简单配对
        for (size_t i = 0; i + 1 < casual_candidates.size(); i += 2) {
            if (matched_users.find(casual_candidates[i].user_id) != matched_users.end() ||
                matched_users.find(casual_candidates[i + 1].user_id) != matched_users.end()) {
                continue;
            }

            auto result = createMatchResult(
                casual_candidates[i], casual_candidates[i + 1], MatchQuality::ACCEPTABLE);

            matched_users.insert(casual_candidates[i].user_id);
            matched_users.insert(casual_candidates[i + 1].user_id);

            if (match_result_callback_) {
                match_result_callback_(result);
            }

            updateStatistics(result);
            match_count++;
        }
    }

    // 从池中移除已匹配的用户
    {
        std::unique_lock<std::shared_mutex> lock(pool_mutex_);
        for (const auto& user_id : matched_users) {
            requests_.erase(user_id);
            waiting_users_.erase(user_id);
            ranked_requests_.erase(user_id);
            casual_requests_.erase(user_id);
        }
    }

    return match_count;
}

std::optional<MatchResult> MatchMakingManager::tryMatchRequest(
    const MatchRequest& request,
    const std::vector<MatchRequest>& candidates) {

    if (candidates.empty()) {
        return std::nullopt;
    }

    // 选择匹配策略
    IMatchStrategy* strategy = (request.mode == MatchMode::RANKED)
        ? ranked_strategy_.get()
        : casual_strategy_.get();

    // 找到最佳匹配
    auto best_match = strategy->findBestMatch(request, candidates, config_);
    if (!best_match) {
        // 尝试扩展评分范围后再匹配
        {
            std::unique_lock<std::shared_mutex> lock(pool_mutex_);
            auto it = requests_.find(request.user_id);
            if (it != requests_.end()) {
                expandRatingRange(it->second);

                // 重新获取候选者
                std::vector<MatchRequest> expanded_candidates;
                for (const auto& cand : candidates) {
                    if (it->second.isRatingInRange(cand.rating) &&
                        cand.isRatingInRange(it->second.rating)) {
                        expanded_candidates.push_back(cand);
                    }
                }

                if (!expanded_candidates.empty()) {
                    best_match = strategy->findBestMatch(it->second, expanded_candidates, config_);
                }
            }
        }

        if (!best_match) {
            return std::nullopt;
        }
    }

    // 创建匹配结果
    int rating_diff = request.getRatingDifference(*best_match);
    MatchQuality quality = RatingMatchStrategy::evaluateQuality(rating_diff, config_);

    return createMatchResult(request, *best_match, quality);
}

MatchResult MatchMakingManager::createMatchResult(
    const MatchRequest& request1,
    const MatchRequest& request2,
    MatchQuality quality) {

    MatchResult result;
    result.match_id = generateMatchId();
    result.players = {request1, request2};
    result.quality = quality;
    result.rating_difference = request1.getRatingDifference(request2);
    result.total_wait_seconds = request1.wait_seconds + request2.wait_seconds;
    result.matched_time = std::chrono::steady_clock::now();
    result.game_mode = request1.game_mode;

    return result;
}

void MatchMakingManager::expandRatingRange(MatchRequest& request) {
    request.match_attempts++;

    // 根据匹配尝试次数和等待时间扩展评分范围
    int expansion = config_.rating_expansion_per_interval * request.match_attempts;

    // 等待时间超过阈值时，额外扩展
    if (request.wait_seconds > config_.priority_wait_threshold_seconds) {
        expansion += config_.high_priority_expansion_bonus;
    }

    // 限制最大扩展
    expansion = std::min(expansion, config_.max_rating_expansion);

    request.expandRatingRange(expansion);
    request.last_match_attempt = std::chrono::steady_clock::now();

    LOG_DEBUG("扩展评分范围: 用户=" + request.user_id +
              ", 扩展量=" + std::to_string(expansion) +
              ", 新范围=[" + std::to_string(request.min_rating) +
              ", " + std::to_string(request.max_rating) + "]");
}

void MatchMakingManager::updateAllWaitTimes() {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto& [user_id, request] : requests_) {
        request.wait_seconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now - request.join_time).count());
    }
}

void MatchMakingManager::handleTimeoutRequests() {
    std::vector<std::string> timeout_users;

    {
        std::shared_lock<std::shared_mutex> lock(pool_mutex_);
        for (const auto& [user_id, request] : requests_) {
            if (request.wait_seconds >= config_.max_wait_seconds) {
                timeout_users.push_back(user_id);
            }
        }
    }

    for (const auto& user_id : timeout_users) {
        std::unique_lock<std::shared_mutex> lock(pool_mutex_);

        auto it = requests_.find(user_id);
        if (it != requests_.end()) {
            MatchRequest request = it->second;
            request.status = MatchStatus::TIMEOUT;

            emitEvent(MatchEventType::PLAYER_TIMEOUT, request.request_id, user_id);

            if (status_callback_) {
                status_callback_(user_id, MatchStatus::WAITING, MatchStatus::TIMEOUT);
            }

            requests_.erase(it);
            waiting_users_.erase(user_id);
            ranked_requests_.erase(user_id);
            casual_requests_.erase(user_id);

            requests_timeout_.fetch_add(1);
            LOG_INFO("用户匹配超时: " + user_id);
        }
    }
}

std::string MatchMakingManager::generateMatchId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "match_" << ms << "_" << std::setfill('0') << std::setw(6)
       << (++match_counter_ % 1000000);
    return ss.str();
}

void MatchMakingManager::emitEvent(MatchEventType type, const std::string& request_id,
                                    const std::string& user_id, const nlohmann::json& data) {
    if (event_callback_) {
        MatchEvent event(type, request_id, user_id);
        event.data = data;
        event_callback_(event);
    }
}

void MatchMakingManager::updateStatistics(const MatchResult& result) {
    total_matches_.fetch_add(1);
    total_wait_time_ms_.fetch_add(result.total_wait_seconds * 1000);

    switch (result.quality) {
        case MatchQuality::EXCELLENT:
            total_excellent_matches_.fetch_add(1);
            break;
        case MatchQuality::GOOD:
            total_good_matches_.fetch_add(1);
            break;
        case MatchQuality::FAIR:
            total_fair_matches_.fetch_add(1);
            break;
        case MatchQuality::ACCEPTABLE:
            total_acceptable_matches_.fetch_add(1);
            break;
    }

    requests_processed_.fetch_add(2);  // 每次匹配处理2个请求
}

} // namespace gomoku
} // namespace game_services
