/**
 * @file elo_rating_calculator.cpp
 * @brief ELO 评分计算器实现
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 */

#include "elo_rating_calculator.h"
#include <stdexcept>
#include <sstream>

namespace core_services {
namespace game_service {

// 静态成员初始化
constexpr std::array<int, 16> EloRatingCalculator::TIER_THRESHOLDS;

EloRatingCalculator::EloRatingCalculator(const EloConfig& config)
    : config_(config) {
    // 验证配置
    if (config_.min_rating >= config_.max_rating) {
        throw std::invalid_argument("min_rating must be less than max_rating");
    }
    if (config_.initial_rating < config_.min_rating ||
        config_.initial_rating > config_.max_rating) {
        throw std::invalid_argument("initial_rating must be within rating range");
    }
}

double EloRatingCalculator::expectedScore(int rating_a, int rating_b) const {
    // ELO 期望胜率公式: E(A) = 1 / (1 + 10^((Rb - Ra) / 400))
    double exponent = static_cast<double>(rating_b - rating_a) / 400.0;
    return 1.0 / (1.0 + std::pow(10.0, exponent));
}

RatingResult EloRatingCalculator::calculateNewRating(
    int current_rating,
    int opponent_rating,
    double score,
    int games_played) {

    RatingResult result;
    result.rating_before = current_rating;
    result.tier_before = getTier(current_rating);

    // 计算 K 因子
    int k = getKFactor(games_played);

    // 计算期望胜率
    double expected = expectedScore(current_rating, opponent_rating);

    // 计算新评分: R' = R + K * (S - E)
    // R = 当前评分, K = K因子, S = 实际得分, E = 期望得分
    double rating_change_double = k * (score - expected);
    int rating_change = static_cast<int>(std::round(rating_change_double));

    // 应用评分变化
    result.rating_after = current_rating + rating_change;

    // 限制评分范围
    result.rating_after = std::clamp(result.rating_after, config_.min_rating, config_.max_rating);

    // 重新计算评分变化（可能被范围限制影响）
    result.rating_change = result.rating_after - result.rating_before;

    // 计算新段位
    result.tier_after = getTier(result.rating_after);

    // 检查段位变化
    result.tier_changed = (result.tier_before != result.tier_after);

    if (result.tier_changed) {
        int level_before = static_cast<int>(result.tier_before);
        int level_after = static_cast<int>(result.tier_after);
        result.tier_promoted = (level_after > level_before);
        result.tier_demoted = (level_after < level_before);
    }

    return result;
}

std::pair<RatingResult, RatingResult> EloRatingCalculator::calculateBothRatings(
    int rating_a,
    int rating_b,
    double score_a,
    int games_a,
    int games_b) {

    // 玩家 B 的得分是玩家 A 得分的补数
    double score_b = 1.0 - score_a;

    // 分别计算双方评分
    RatingResult result_a = calculateNewRating(rating_a, rating_b, score_a, games_a);
    RatingResult result_b = calculateNewRating(rating_b, rating_a, score_b, games_b);

    return {result_a, result_b};
}

Tier EloRatingCalculator::getTier(int rating) const {
    // 使用 TIER_THRESHOLDS 数组进行段位判定
    // 注意：数组索引对应段位等级，值表示该段位的最低评分
    for (int i = static_cast<int>(TIER_THRESHOLDS.size()) - 1; i >= 0; --i) {
        if (rating >= TIER_THRESHOLDS[i]) {
            return static_cast<Tier>(i);
        }
    }
    // 如果评分低于最低阈值，返回最低段位
    return Tier::BRONZE_III;
}

std::string EloRatingCalculator::getTierName(Tier tier) const {
    switch (tier) {
        case Tier::BRONZE_III:    return "青铜 III";
        case Tier::BRONZE_II:     return "青铜 II";
        case Tier::BRONZE_I:      return "青铜 I";
        case Tier::SILVER_III:    return "白银 III";
        case Tier::SILVER_II:     return "白银 II";
        case Tier::SILVER_I:      return "白银 I";
        case Tier::GOLD_III:      return "黄金 III";
        case Tier::GOLD_II:       return "黄金 II";
        case Tier::GOLD_I:        return "黄金 I";
        case Tier::PLATINUM_III:  return "铂金 III";
        case Tier::PLATINUM_II:   return "铂金 II";
        case Tier::PLATINUM_I:    return "铂金 I";
        case Tier::DIAMOND_III:   return "钻石 III";
        case Tier::DIAMOND_II:    return "钻石 II";
        case Tier::DIAMOND_I:     return "钻石 I";
        case Tier::MASTER:        return "大师";
        default:                  return "未知";
    }
}

std::string EloRatingCalculator::getTierIcon(Tier tier) const {
    switch (tier) {
        case Tier::BRONZE_III:
        case Tier::BRONZE_II:
        case Tier::BRONZE_I:
            return "bronze";
        case Tier::SILVER_III:
        case Tier::SILVER_II:
        case Tier::SILVER_I:
            return "silver";
        case Tier::GOLD_III:
        case Tier::GOLD_II:
        case Tier::GOLD_I:
            return "gold";
        case Tier::PLATINUM_III:
        case Tier::PLATINUM_II:
        case Tier::PLATINUM_I:
            return "platinum";
        case Tier::DIAMOND_III:
        case Tier::DIAMOND_II:
        case Tier::DIAMOND_I:
            return "diamond";
        case Tier::MASTER:
            return "master";
        default:
            return "unknown";
    }
}

int EloRatingCalculator::getTierLevel(Tier tier) {
    return static_cast<int>(tier);
}

Tier EloRatingCalculator::getTierFromLevel(int level) {
    if (level < 0 || level > 15) {
        return Tier::SILVER_I; // 默认返回白银 I
    }
    return static_cast<Tier>(level);
}

int EloRatingCalculator::getKFactor(int games_played) const {
    // 新玩家（provisional 期间）使用较高的 K 因子
    // 这样评分可以更快地反映玩家真实水平
    if (games_played < config_.provisional_games) {
        return config_.k_factor_new;
    }
    return config_.k_factor_stable;
}

} // namespace game_service
} // namespace core_services
