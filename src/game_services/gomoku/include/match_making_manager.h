#pragma once

#include "match_pool.h"
#include "match_strategy.h"
#include "gomoku_types.h"
#include "common/thread_pool/thread_pool.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>

/**
 * @file match_making_manager.h
 * @brief 自动匹配管理器
 * @details 管理匹配池、执行匹配逻辑、处理匹配事件
 * @author Game Server Team
 * @date 2025-01-23
 * @version 1.0.0
 */

namespace game_services {
namespace gomoku {

// 前向声明
class GomokuServer;

/**
 * @brief 匹配结果回调类型
 */
using MatchResultCallback = std::function<void(const MatchResult& result)>;

/**
 * @brief 匹配状态变更回调类型
 */
using MatchStatusCallback = std::function<void(const std::string& user_id, MatchStatus old_status, MatchStatus new_status)>;

/**
 * @brief 自动匹配管理器
 * @details 核心匹配管理类，负责：
 * - 管理匹配池
 * - 执行匹配循环
 * - 处理匹配请求
 * - 通知匹配结果
 */
class MatchMakingManager {
public:
    /**
     * @brief 构造函数
     * @param config 匹配池配置
     */
    explicit MatchMakingManager(const MatchPoolConfig& config = MatchPoolConfig());

    /**
     * @brief 析构函数
     */
    ~MatchMakingManager();

    // ========== 生命周期管理 ==========

    /**
     * @brief 启动匹配管理器
     * @return 是否启动成功
     */
    bool start();

    /**
     * @brief 停止匹配管理器
     */
    void stop();

    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_.load(); }

    // ========== 匹配请求管理 ==========

    /**
     * @brief 添加匹配请求
     * @param request 匹配请求
     * @return 是否添加成功
     */
    bool addRequest(const MatchRequest& request);

    /**
     * @brief 取消匹配请求
     * @param user_id 用户ID
     * @return 是否取消成功
     */
    bool cancelRequest(const std::string& user_id);

    /**
     * @brief 获取用户的匹配请求
     * @param user_id 用户ID
     * @return 匹配请求，不存在返回空
     */
    std::optional<MatchRequest> getRequest(const std::string& user_id) const;

    /**
     * @brief 检查用户是否在匹配池中
     * @param user_id 用户ID
     */
    bool isInPool(const std::string& user_id) const;

    /**
     * @brief 获取用户在匹配池中的等待时间
     * @param user_id 用户ID
     * @return 等待秒数，不在池中返回0
     */
    int getWaitTime(const std::string& user_id) const;

    // ========== 匹配池状态 ==========

    /**
     * @brief 获取匹配池状态
     */
    MatchPoolStatus getPoolStatus() const;

    /**
     * @brief 获取匹配池大小
     */
    size_t getPoolSize() const;

    /**
     * @brief 获取所有等待中的请求
     */
    std::vector<MatchRequest> getWaitingRequests() const;

    // ========== 回调设置 ==========

    /**
     * @brief 设置匹配结果回调
     * @param callback 回调函数
     */
    void setMatchResultCallback(MatchResultCallback callback) {
        match_result_callback_ = std::move(callback);
    }

    /**
     * @brief 设置状态变更回调
     * @param callback 回调函数
     */
    void setStatusCallback(MatchStatusCallback callback) {
        status_callback_ = std::move(callback);
    }

    /**
     * @brief 设置事件回调
     * @param callback 回调函数
     */
    void setEventCallback(MatchEventCallback callback) {
        event_callback_ = std::move(callback);
    }

    // ========== 配置管理 ==========

    /**
     * @brief 更新匹配池配置
     * @param config 新配置
     */
    void updateConfig(const MatchPoolConfig& config);

    /**
     * @brief 获取当前配置
     */
    const MatchPoolConfig& getConfig() const { return config_; }

    // ========== 统计信息 ==========

    /**
     * @brief 获取统计信息
     */
    nlohmann::json getStatistics() const;

    /**
     * @brief 重置统计数据
     */
    void resetStatistics();

private:
    // ========== 内部方法 ==========

    /**
     * @brief 匹配循环主函数
     */
    void matchLoop();

    /**
     * @brief 执行一轮匹配
     * @return 匹配成功的对数
     */
    int performMatching();

    /**
     * @brief 尝试匹配单个请求
     * @param request 待匹配请求
     * @param candidates 候选者列表
     * @return 匹配结果，失败返回空
     */
    std::optional<MatchResult> tryMatchRequest(
        const MatchRequest& request,
        const std::vector<MatchRequest>& candidates);

    /**
     * @brief 创建匹配结果
     */
    MatchResult createMatchResult(
        const MatchRequest& request1,
        const MatchRequest& request2,
        MatchQuality quality);

    /**
     * @brief 更新请求的评分范围（动态扩展）
     * @param request 请求
     */
    void expandRatingRange(MatchRequest& request);

    /**
     * @brief 更新所有请求的等待时间
     */
    void updateAllWaitTimes();

    /**
     * @brief 处理超时请求
     */
    void handleTimeoutRequests();

    /**
     * @brief 从匹配池移除请求
     */
    bool removeRequestInternal(const std::string& user_id);

    /**
     * @brief 生成匹配ID
     */
    std::string generateMatchId();

    /**
     * @brief 触发事件
     */
    void emitEvent(MatchEventType type, const std::string& request_id,
                   const std::string& user_id, const nlohmann::json& data = {});

    /**
     * @brief 更新统计数据
     */
    void updateStatistics(const MatchResult& result);

private:
    // 配置
    MatchPoolConfig config_;

    // 匹配策略
    std::unique_ptr<IMatchStrategy> ranked_strategy_;
    std::unique_ptr<IMatchStrategy> casual_strategy_;

    // 匹配池存储
    mutable std::shared_mutex pool_mutex_;
    std::unordered_map<std::string, MatchRequest> requests_;    // user_id -> request
    std::set<std::string> waiting_users_;                       // 等待中的用户ID（按加入时间排序）

    // 按模式分组的索引
    std::set<std::string> ranked_requests_;
    std::set<std::string> casual_requests_;

    // 运行状态
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::unique_ptr<std::thread> match_thread_;
    std::condition_variable match_cv_;
    mutable std::mutex match_mutex_;

    // 回调函数
    MatchResultCallback match_result_callback_;
    MatchStatusCallback status_callback_;
    MatchEventCallback event_callback_;

    // 统计数据
    mutable std::mutex stats_mutex_;
    std::atomic<int64_t> total_matches_{0};
    std::atomic<int64_t> total_excellent_matches_{0};
    std::atomic<int64_t> total_good_matches_{0};
    std::atomic<int64_t> total_fair_matches_{0};
    std::atomic<int64_t> total_acceptable_matches_{0};
    std::atomic<int64_t> total_wait_time_ms_{0};
    std::atomic<int64_t> requests_processed_{0};
    std::atomic<int64_t> requests_cancelled_{0};
    std::atomic<int64_t> requests_timeout_{0};

    // 匹配ID生成
    static std::atomic<uint64_t> match_counter_;
};

/**
 * @brief 匹配管理器工厂
 */
class MatchMakingManagerFactory {
public:
    /**
     * @brief 创建匹配管理器
     * @param config 配置
     */
    static std::unique_ptr<MatchMakingManager> create(const MatchPoolConfig& config = MatchPoolConfig()) {
        return std::make_unique<MatchMakingManager>(config);
    }
};

} // namespace gomoku
} // namespace game_services
