#pragma once

#include "gomoku_types.h"
#include "common/config/config_manager.h"
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <set>
#include <atomic>

/**
 * @file match_pool.h
 * @brief 自动匹配系统数据结构定义
 * @details 定义匹配请求、匹配结果、匹配池状态等核心数据结构
 * @author Game Server Team
 * @date 2025-01-23
 * @version 1.0.0
 */

namespace game_services {
namespace gomoku {

/**
 * @brief 匹配模式枚举
 */
enum class MatchMode {
    CASUAL = 0,     // 休闲模式 - 无评分要求
    RANKED = 1      // 竞技模式 - 基于评分匹配
};

/**
 * @brief 匹配状态枚举
 */
enum class MatchStatus {
    WAITING = 0,        // 等待匹配中
    MATCHING = 1,       // 正在匹配
    MATCHED = 2,        // 已匹配成功
    CANCELLED = 3,      // 已取消
    TIMEOUT = 4,        // 超时
    ERROR = 5           // 错误
};

/**
 * @brief 匹配质量等级
 */
enum class MatchQuality {
    EXCELLENT = 0,      // 优秀: 评分差 < 50
    GOOD = 1,           // 良好: 评分差 < 100
    FAIR = 2,           // 一般: 评分差 < 200
    ACCEPTABLE = 3      // 可接受: 评分差 >= 200
};

/**
 * @brief 匹配请求结构
 * @details 表示一个玩家的匹配请求
 */
struct MatchRequest {
    std::string request_id;                             // 请求唯一ID
    std::string user_id;                                // 用户ID
    std::string username;                               // 用户名

    // 玩家属性
    int rating = 1200;                                  // 当前评分
    int tier_level = 5;                                 // 段位等级
    int games_played = 0;                               // 游戏场次

    // 匹配配置
    MatchMode mode = MatchMode::RANKED;                 // 匹配模式
    GameMode game_mode = GameMode::FREESTYLE;           // 游戏规则
    std::vector<std::string> preferred_opponents;       // 首选对手列表（好友等）

    // 评分范围（动态扩展）
    int min_rating = 0;                                 // 最低评分
    int max_rating = 3000;                              // 最高评分

    // 时间信息
    std::chrono::steady_clock::time_point join_time;    // 加入时间
    std::chrono::steady_clock::time_point last_match_attempt; // 最后匹配尝试时间
    int wait_seconds = 0;                               // 等待秒数

    // 状态
    MatchStatus status = MatchStatus::WAITING;          // 当前状态
    int match_attempts = 0;                             // 匹配尝试次数
    int rating_range_expansion = 0;                     // 评分范围扩展量

    // 扩展数据
    nlohmann::json metadata;                            // 扩展元数据

    /**
     * @brief 默认构造函数
     */
    MatchRequest() {
        join_time = std::chrono::steady_clock::now();
        last_match_attempt = join_time;
    }

    /**
     * @brief 构造函数
     */
    MatchRequest(const std::string& uid, const std::string& name, int rt, MatchMode m)
        : user_id(uid), username(name), rating(rt), mode(m) {
        request_id = generateRequestId();
        join_time = std::chrono::steady_clock::now();
        last_match_attempt = join_time;
        min_rating = std::max(0, rating - 100);
        max_rating = rating + 100;
    }

    /**
     * @brief 更新等待时间
     */
    void updateWaitTime() {
        auto now = std::chrono::steady_clock::now();
        wait_seconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - join_time).count());
    }

    /**
     * @brief 扩展评分范围
     * @param expansion 扩展量
     */
    void expandRatingRange(int expansion) {
        rating_range_expansion += expansion;
        min_rating = std::max(0, rating - 100 - rating_range_expansion);
        max_rating = rating + 100 + rating_range_expansion;
    }

    /**
     * @brief 检查评分是否在范围内
     */
    bool isRatingInRange(int other_rating) const {
        return other_rating >= min_rating && other_rating <= max_rating;
    }

    /**
     * @brief 计算与另一个请求的评分差
     */
    int getRatingDifference(const MatchRequest& other) const {
        return std::abs(rating - other.rating);
    }

    /**
     * @brief 评估匹配质量
     */
    MatchQuality evaluateMatchQuality(const MatchRequest& other) const {
        int diff = getRatingDifference(other);
        if (diff < 50) return MatchQuality::EXCELLENT;
        if (diff < 100) return MatchQuality::GOOD;
        if (diff < 200) return MatchQuality::FAIR;
        return MatchQuality::ACCEPTABLE;
    }

    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const {
        return {
            {"request_id", request_id},
            {"user_id", user_id},
            {"username", username},
            {"rating", rating},
            {"tier_level", tier_level},
            {"mode", mode == MatchMode::RANKED ? "ranked" : "casual"},
            {"game_mode", gameModeToString(game_mode)},
            {"min_rating", min_rating},
            {"max_rating", max_rating},
            {"wait_seconds", wait_seconds},
            {"status", static_cast<int>(status)},
            {"match_attempts", match_attempts},
            {"rating_range_expansion", rating_range_expansion}
        };
    }

private:
    /**
     * @brief 生成请求ID
     */
    static std::string generateRequestId() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return "match_" + std::to_string(ms) + "_" + std::to_string(++counter);
    }
};

/**
 * @brief 匹配结果结构
 * @details 表示一次成功匹配的结果
 */
struct MatchResult {
    std::string match_id;                               // 匹配ID
    std::vector<MatchRequest> players;                  // 匹配的玩家列表
    MatchQuality quality = MatchQuality::ACCEPTABLE;    // 匹配质量
    int rating_difference = 0;                          // 评分差异
    int total_wait_seconds = 0;                         // 总等待时间

    std::chrono::steady_clock::time_point matched_time; // 匹配完成时间

    // 房间创建信息
    std::string room_id;                                // 分配的房间ID
    GameMode game_mode = GameMode::FREESTYLE;          // 游戏规则
    bool room_created = false;                          // 房间是否已创建

    /**
     * @brief 检查结果是否有效
     */
    bool isValid() const {
        return players.size() >= 2 && !match_id.empty();
    }

    /**
     * @brief 获取对手ID（对于二人游戏）
     */
    std::string getOpponentId(const std::string& my_user_id) const {
        for (const auto& player : players) {
            if (player.user_id != my_user_id) {
                return player.user_id;
            }
        }
        return "";
    }

    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const {
        nlohmann::json players_json = nlohmann::json::array();
        for (const auto& player : players) {
            players_json.push_back(player.toJson());
        }

        std::string quality_str;
        switch (quality) {
            case MatchQuality::EXCELLENT: quality_str = "excellent"; break;
            case MatchQuality::GOOD: quality_str = "good"; break;
            case MatchQuality::FAIR: quality_str = "fair"; break;
            default: quality_str = "acceptable"; break;
        }

        return {
            {"match_id", match_id},
            {"players", players_json},
            {"quality", quality_str},
            {"rating_difference", rating_difference},
            {"total_wait_seconds", total_wait_seconds},
            {"room_id", room_id},
            {"game_mode", gameModeToString(game_mode)},
            {"room_created", room_created}
        };
    }
};

/**
 * @brief 匹配池状态
 * @details 表示当前匹配池的整体状态
 */
struct MatchPoolStatus {
    int total_requests = 0;                 // 总请求数
    int waiting_requests = 0;               // 等待中的请求数
    int matched_requests = 0;               // 已匹配的请求数
    int cancelled_requests = 0;             // 已取消的请求数

    // 按模式统计
    int ranked_count = 0;                   // 竞技模式数量
    int casual_count = 0;                   // 休闲模式数量

    // 评分分布
    int rating_min = 0;
    int rating_max = 0;
    double rating_avg = 0.0;

    // 等待时间统计
    int avg_wait_seconds = 0;
    int max_wait_seconds = 0;

    // 匹配效率
    int64_t total_matches_today = 0;        // 今日匹配总数
    double avg_match_quality = 0.0;         // 平均匹配质量评分

    std::chrono::steady_clock::time_point last_update;  // 最后更新时间

    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const {
        return {
            {"total_requests", total_requests},
            {"waiting_requests", waiting_requests},
            {"matched_requests", matched_requests},
            {"cancelled_requests", cancelled_requests},
            {"ranked_count", ranked_count},
            {"casual_count", casual_count},
            {"rating_distribution", {
                {"min", rating_min},
                {"max", rating_max},
                {"avg", rating_avg}
            }},
            {"wait_time", {
                {"avg_seconds", avg_wait_seconds},
                {"max_seconds", max_wait_seconds}
            }},
            {"statistics", {
                {"total_matches_today", total_matches_today},
                {"avg_match_quality", avg_match_quality}
            }}
        };
    }
};

/**
 * @brief 匹配池配置
 */
struct MatchPoolConfig {
    // 基础配置
    int max_pool_size = 1000;                       // 最大池容量
    int max_wait_seconds = 300;                     // 最大等待时间（5分钟）
    int match_interval_ms = 100;                    // 匹配间隔（毫秒）

    // 评分配置
    int initial_rating_range = 100;                 // 初始评分范围
    int rating_expansion_per_interval = 25;         // 每次扩展的评分量
    int max_rating_expansion = 500;                 // 最大评分扩展

    // 优先级配置
    int priority_wait_threshold_seconds = 30;       // 优先级等待阈值
    int high_priority_expansion_bonus = 50;         // 高优先级额外扩展

    // 质量配置
    int excellent_quality_threshold = 50;           // 优秀质量阈值
    int good_quality_threshold = 100;               // 良好质量阈值
    int fair_quality_threshold = 200;               // 一般质量阈值

    // 评分权重（用于排序）
    double rating_weight = 0.6;                     // 评分权重
    double wait_time_weight = 0.3;                  // 等待时间权重
    double tier_weight = 0.1;                       // 段位权重

    /**
     * @brief 从配置管理器加载
     */
    static MatchPoolConfig fromConfig() {
        auto& config = common::config::ConfigManager::getInstance();
        MatchPoolConfig cfg;

        cfg.max_pool_size = config.get<int>("match.max_pool_size", 1000);
        cfg.max_wait_seconds = config.get<int>("match.max_wait_seconds", 300);
        cfg.match_interval_ms = config.get<int>("match.match_interval_ms", 100);
        cfg.initial_rating_range = config.get<int>("match.initial_rating_range", 100);
        cfg.rating_expansion_per_interval = config.get<int>("match.rating_expansion_per_interval", 25);
        cfg.max_rating_expansion = config.get<int>("match.max_rating_expansion", 500);
        cfg.priority_wait_threshold_seconds = config.get<int>("match.priority_wait_threshold_seconds", 30);
        cfg.high_priority_expansion_bonus = config.get<int>("match.high_priority_expansion_bonus", 50);
        cfg.excellent_quality_threshold = config.get<int>("match.excellent_quality_threshold", 50);
        cfg.good_quality_threshold = config.get<int>("match.good_quality_threshold", 100);
        cfg.fair_quality_threshold = config.get<int>("match.fair_quality_threshold", 200);
        cfg.rating_weight = config.get<double>("match.rating_weight", 0.6);
        cfg.wait_time_weight = config.get<double>("match.wait_time_weight", 0.3);
        cfg.tier_weight = config.get<double>("match.tier_weight", 0.1);

        return cfg;
    }

    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const {
        return {
            {"max_pool_size", max_pool_size},
            {"max_wait_seconds", max_wait_seconds},
            {"match_interval_ms", match_interval_ms},
            {"initial_rating_range", initial_rating_range},
            {"rating_expansion_per_interval", rating_expansion_per_interval},
            {"max_rating_expansion", max_rating_expansion},
            {"priority_wait_threshold_seconds", priority_wait_threshold_seconds},
            {"high_priority_expansion_bonus", high_priority_expansion_bonus},
            {"quality_thresholds", {
                {"excellent", excellent_quality_threshold},
                {"good", good_quality_threshold},
                {"fair", fair_quality_threshold}
            }},
            {"weights", {
                {"rating", rating_weight},
                {"wait_time", wait_time_weight},
                {"tier", tier_weight}
            }}
        };
    }
};

/**
 * @brief 匹配事件类型
 */
enum class MatchEventType {
    PLAYER_JOINED,         // 玩家加入匹配池
    PLAYER_MATCHED,        // 玩家匹配成功
    PLAYER_CANCELLED,      // 玩家取消匹配
    PLAYER_TIMEOUT,        // 玩家匹配超时
    MATCH_COMPLETED,       // 匹配完成
    POOL_STATUS_UPDATE     // 池状态更新
};

/**
 * @brief 匹配事件
 */
struct MatchEvent {
    MatchEventType type;
    std::string request_id;
    std::string user_id;
    std::string match_id;
    nlohmann::json data;
    std::chrono::steady_clock::time_point timestamp;

    MatchEvent(MatchEventType t, const std::string& req_id, const std::string& uid)
        : type(t), request_id(req_id), user_id(uid) {
        timestamp = std::chrono::steady_clock::now();
    }
};

/**
 * @brief 匹配事件回调类型
 */
using MatchEventCallback = std::function<void(const MatchEvent&)>;

} // namespace gomoku
} // namespace game_services
