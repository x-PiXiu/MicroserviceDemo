#pragma once

#include "match_pool.h"
#include "gomoku_types.h"
#include <memory>
#include <vector>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

/**
 * @file match_strategy.h
 * @brief 匹配策略接口和实现
 * @details 定义匹配策略的抽象接口和基于评分的匹配策略实现
 * @author Game Server Team
 * @date 2025-01-23
 * @version 1.0.0
 */

namespace game_services {
namespace gomoku {

/**
 * @brief 匹配候选者评分结果
 */
struct MatchScore {
    double total_score = 0.0;           // 总评分
    double rating_score = 0.0;          // 评分匹配分
    double wait_time_score = 0.0;       // 等待时间加成分
    double preference_score = 0.0;      // 偏好加成分
    double tier_score = 0.0;            // 段位匹配分

    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const {
        return {
            {"total", total_score},
            {"rating", rating_score},
            {"wait_time", wait_time_score},
            {"preference", preference_score},
            {"tier", tier_score}
        };
    }
};

/**
 * @brief 匹配策略接口
 * @details 定义匹配策略的抽象接口，支持不同的匹配算法
 */
class IMatchStrategy {
public:
    virtual ~IMatchStrategy() = default;

    /**
     * @brief 从候选者中找到最佳匹配
     * @param request 待匹配的请求
     * @param candidates 候选者列表
     * @param config 匹配池配置
     * @return 最佳匹配的候选者，如果没有合适的则返回空
     */
    virtual std::optional<MatchRequest> findBestMatch(
        const MatchRequest& request,
        const std::vector<MatchRequest>& candidates,
        const MatchPoolConfig& config) = 0;

    /**
     * @brief 计算两个请求之间的匹配评分
     * @param request1 请求1
     * @param request2 请求2
     * @param config 匹配池配置
     * @return 匹配评分
     */
    virtual MatchScore calculateMatchScore(
        const MatchRequest& request1,
        const MatchRequest& request2,
        const MatchPoolConfig& config) const = 0;

    /**
     * @brief 检查两个请求是否可以匹配
     * @param request1 请求1
     * @param request2 请求2
     * @param config 匹配池配置
     * @return 是否可以匹配
     */
    virtual bool canMatch(
        const MatchRequest& request1,
        const MatchRequest& request2,
        const MatchPoolConfig& config) const = 0;

    /**
     * @brief 获取策略名称
     */
    virtual std::string getName() const = 0;

    /**
     * @brief 获取策略描述
     */
    virtual std::string getDescription() const = 0;
};

/**
 * @brief 基于评分的匹配策略
 * @details 实现基于ELO评分的匹配算法，支持动态评分范围扩展
 */
class RatingMatchStrategy : public IMatchStrategy {
public:
    /**
     * @brief 构造函数
     */
    RatingMatchStrategy() = default;

    /**
     * @brief 析构函数
     */
    ~RatingMatchStrategy() override = default;

    /**
     * @brief 从候选者中找到最佳匹配
     * @details 基于评分差、等待时间、偏好等因素综合评估
     */
    std::optional<MatchRequest> findBestMatch(
        const MatchRequest& request,
        const std::vector<MatchRequest>& candidates,
        const MatchPoolConfig& config) override {

        std::optional<MatchRequest> best_match;
        double best_score = -1.0;

        for (const auto& candidate : candidates) {
            // 跳过自己
            if (candidate.user_id == request.user_id) {
                continue;
            }

            // 检查是否可以匹配
            if (!canMatch(request, candidate, config)) {
                continue;
            }

            // 计算匹配评分
            MatchScore score = calculateMatchScore(request, candidate, config);

            // 更新最佳匹配
            if (score.total_score > best_score) {
                best_score = score.total_score;
                best_match = candidate;
            }
        }

        return best_match;
    }

    /**
     * @brief 计算两个请求之间的匹配评分
     * @details 综合评分差、等待时间、段位等因素
     */
    MatchScore calculateMatchScore(
        const MatchRequest& request1,
        const MatchRequest& request2,
        const MatchPoolConfig& config) const override {

        MatchScore score;

        // 1. 评分匹配分（评分差越小，分数越高）
        int rating_diff = request1.getRatingDifference(request2);
        score.rating_score = calculateRatingScore(rating_diff, config);

        // 2. 等待时间加成分（等待时间越长，分数越高）
        int total_wait = request1.wait_seconds + request2.wait_seconds;
        score.wait_time_score = calculateWaitTimeScore(total_wait, config);

        // 3. 偏好加成分（如果是首选对手）
        score.preference_score = calculatePreferenceScore(request1, request2);

        // 4. 段位匹配分（段位差距越小，分数越高）
        int tier_diff = std::abs(request1.tier_level - request2.tier_level);
        score.tier_score = calculateTierScore(tier_diff, config);

        // 计算总分
        score.total_score =
            score.rating_score * config.rating_weight +
            score.wait_time_score * config.wait_time_weight +
            score.preference_score * 0.2 +
            score.tier_score * config.tier_weight;

        return score;
    }

    /**
     * @brief 检查两个请求是否可以匹配
     */
    bool canMatch(
        const MatchRequest& request1,
        const MatchRequest& request2,
        const MatchPoolConfig& config) const override {

        // 不能匹配自己
        if (request1.user_id == request2.user_id) {
            return false;
        }

        // 检查匹配模式是否兼容
        if (request1.mode != request2.mode) {
            return false;
        }

        // 检查游戏规则是否相同
        if (request1.game_mode != request2.game_mode) {
            return false;
        }

        // 检查双方评分范围是否互相包含
        bool in_range1 = request1.isRatingInRange(request2.rating);
        bool in_range2 = request2.isRatingInRange(request1.rating);

        // 至少一方的评分在另一方的范围内
        if (!in_range1 && !in_range2) {
            return false;
        }

        // 检查双方状态是否为等待中
        if (request1.status != MatchStatus::WAITING ||
            request2.status != MatchStatus::WAITING) {
            return false;
        }

        return true;
    }

    /**
     * @brief 获取策略名称
     */
    std::string getName() const override {
        return "RatingMatchStrategy";
    }

    /**
     * @brief 获取策略描述
     */
    std::string getDescription() const override {
        return "基于ELO评分的匹配策略，支持动态评分范围扩展和等待时间优先级";
    }

    /**
     * @brief 评估匹配质量
     */
    static MatchQuality evaluateQuality(int rating_diff, const MatchPoolConfig& config) {
        if (rating_diff < config.excellent_quality_threshold) {
            return MatchQuality::EXCELLENT;
        } else if (rating_diff < config.good_quality_threshold) {
            return MatchQuality::GOOD;
        } else if (rating_diff < config.fair_quality_threshold) {
            return MatchQuality::FAIR;
        }
        return MatchQuality::ACCEPTABLE;
    }

private:
    /**
     * @brief 计算评分匹配分
     * @details 评分差越小，分数越高，范围 0-1
     */
    double calculateRatingScore(int rating_diff, const MatchPoolConfig& config) const {
        // 评分差为0时得1分，评分差达到最大扩展时得0分
        double max_diff = static_cast<double>(config.initial_rating_range + config.max_rating_expansion);
        double score = 1.0 - (static_cast<double>(rating_diff) / max_diff);
        return std::max(0.0, std::min(1.0, score));
    }

    /**
     * @brief 计算等待时间加成分
     * @details 等待时间越长，分数越高，范围 0-1
     */
    double calculateWaitTimeScore(int total_wait_seconds, const MatchPoolConfig& config) const {
        // 等待时间达到阈值时得满分
        double threshold = static_cast<double>(config.priority_wait_threshold_seconds * 2);
        double score = static_cast<double>(total_wait_seconds) / threshold;
        return std::min(1.0, score);
    }

    /**
     * @brief 计算偏好加成分
     * @details 如果是首选对手，得高分
     */
    double calculatePreferenceScore(
        const MatchRequest& request1,
        const MatchRequest& request2) const {

        // 检查是否在首选对手列表中
        for (const auto& preferred : request1.preferred_opponents) {
            if (preferred == request2.user_id) {
                return 1.0;
            }
        }
        for (const auto& preferred : request2.preferred_opponents) {
            if (preferred == request1.user_id) {
                return 1.0;
            }
        }
        return 0.0;
    }

    /**
     * @brief 计算段位匹配分
     * @details 段位差越小，分数越高，范围 0-1
     */
    double calculateTierScore(int tier_diff, const MatchPoolConfig& config) const {
        // 段位差为0时得1分，段位差>=5时得0分
        double max_tier_diff = 5.0;
        double score = 1.0 - (static_cast<double>(tier_diff) / max_tier_diff);
        return std::max(0.0, std::min(1.0, score));
    }
};

/**
 * @brief 休闲模式匹配策略
 * @details 休闲模式匹配策略，优先考虑等待时间而非评分
 */
class CasualMatchStrategy : public IMatchStrategy {
public:
    std::optional<MatchRequest> findBestMatch(
        const MatchRequest& request,
        const std::vector<MatchRequest>& candidates,
        const MatchPoolConfig& config) override {

        // 休闲模式：简单按等待时间排序，选择等待最久的
        std::optional<MatchRequest> best_match;
        int max_wait = -1;

        for (const auto& candidate : candidates) {
            if (candidate.user_id == request.user_id) {
                continue;
            }

            if (candidate.status != MatchStatus::WAITING) {
                continue;
            }

            if (candidate.game_mode != request.game_mode) {
                continue;
            }

            if (static_cast<int>(candidate.wait_seconds) > max_wait) {
                max_wait = candidate.wait_seconds;
                best_match = candidate;
            }
        }

        return best_match;
    }

    MatchScore calculateMatchScore(
        const MatchRequest& request1,
        const MatchRequest& request2,
        const MatchPoolConfig& config) const override {

        MatchScore score;
        // 休闲模式主要看等待时间
        int total_wait = request1.wait_seconds + request2.wait_seconds;
        double threshold = static_cast<double>(config.priority_wait_threshold_seconds * 2);
        score.wait_time_score = std::min(1.0, static_cast<double>(total_wait) / threshold);
        score.total_score = score.wait_time_score;
        return score;
    }

    bool canMatch(
        const MatchRequest& request1,
        const MatchRequest& request2,
        const MatchPoolConfig& config) const override {

        if (request1.user_id == request2.user_id) {
            return false;
        }

        if (request1.game_mode != request2.game_mode) {
            return false;
        }

        if (request1.status != MatchStatus::WAITING ||
            request2.status != MatchStatus::WAITING) {
            return false;
        }

        return true;
    }

    std::string getName() const override {
        return "CasualMatchStrategy";
    }

    std::string getDescription() const override {
        return "休闲模式匹配策略，优先考虑等待时间而非评分";
    }
};

/**
 * @brief 匹配策略工厂
 * @details 根据匹配模式创建合适的匹配策略
 */
class MatchStrategyFactory {
public:
    /**
     * @brief 根据匹配模式创建策略
     */
    static std::unique_ptr<IMatchStrategy> create(MatchMode mode) {
        switch (mode) {
            case MatchMode::RANKED:
                return std::make_unique<RatingMatchStrategy>();
            case MatchMode::CASUAL:
                return std::make_unique<CasualMatchStrategy>();
            default:
                return std::make_unique<RatingMatchStrategy>();
        }
    }

    /**
     * @brief 获取默认策略
     */
    static std::unique_ptr<IMatchStrategy> createDefault() {
        return std::make_unique<RatingMatchStrategy>();
    }
};

} // namespace gomoku
} // namespace game_services
