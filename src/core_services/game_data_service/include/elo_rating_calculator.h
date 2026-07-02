/**
 * @file elo_rating_calculator.h
 * @brief ELO 评分计算器
 * @details 实现标准 ELO 评分算法，支持可配置的 K 因子和段位判定
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 */

#pragma once

#include <string>
#include <utility>
#include <cmath>
#include <algorithm>
#include <array>

namespace core_services {
namespace game_service {

/**
 * @brief 段位枚举
 */
enum class Tier {
    BRONZE_III = 0,   ///< 青铜 III (100-399)
    BRONZE_II = 1,    ///< 青铜 II (400-599)
    BRONZE_I = 2,     ///< 青铜 I (600-799)
    SILVER_III = 3,   ///< 白银 III (800-999)
    SILVER_II = 4,    ///< 白银 II (1000-1199)
    SILVER_I = 5,     ///< 白银 I (1200-1399)
    GOLD_III = 6,     ///< 黄金 III (1400-1599)
    GOLD_II = 7,      ///< 黄金 II (1600-1799)
    GOLD_I = 8,       ///< 黄金 I (1800-1999)
    PLATINUM_III = 9, ///< 铂金 III (2000-2199)
    PLATINUM_II = 10, ///< 铂金 II (2200-2399)
    PLATINUM_I = 11,  ///< 铂金 I (2400-2599)
    DIAMOND_III = 12, ///< 钻石 III (2600-2799)
    DIAMOND_II = 13,  ///< 钻石 II (2800-2899)
    DIAMOND_I = 14,   ///< 钻石 I (2900-2999)
    MASTER = 15       ///< 大师 (3000+)
};

/**
 * @brief ELO 配置
 */
struct EloConfig {
    int initial_rating = 1200;     ///< 初始评分
    int min_rating = 100;          ///< 最低评分
    int max_rating = 3000;         ///< 最高评分
    int k_factor_new = 32;         ///< 新玩家 K 因子
    int k_factor_stable = 16;      ///< 稳定玩家 K 因子
    int provisional_games = 10;    ///< 新手保护场次

    /**
     * @brief 默认构造
     */
    EloConfig() = default;

    /**
     * @brief 参数构造
     */
    EloConfig(int initial, int min_r, int max_r, int k_new, int k_stable, int prov_games)
        : initial_rating(initial)
        , min_rating(min_r)
        , max_rating(max_r)
        , k_factor_new(k_new)
        , k_factor_stable(k_stable)
        , provisional_games(prov_games) {}
};

/**
 * @brief 评分结果
 */
struct RatingResult {
    int rating_before = 0;      ///< 评分前
    int rating_after = 0;       ///< 评分后
    int rating_change = 0;      ///< 评分变化
    Tier tier_before = Tier::SILVER_I;  ///< 段位前
    Tier tier_after = Tier::SILVER_I;   ///< 段位后
    bool tier_changed = false;  ///< 段位是否变化
    bool tier_promoted = false; ///< 是否升段
    bool tier_demoted = false;  ///< 是否降段

    /**
     * @brief 检查是否有效
     */
    bool isValid() const {
        return rating_after >= 0 && rating_change != 0 || rating_before == rating_after;
    }
};

/**
 * @brief ELO 评分计算器
 */
class EloRatingCalculator {
public:
    /**
     * @brief 构造函数
     * @param config ELO 配置
     */
    explicit EloRatingCalculator(const EloConfig& config = EloConfig{});

    /**
     * @brief 计算新评分
     * @param current_rating 当前评分
     * @param opponent_rating 对手评分
     * @param score 比赛得分 (1.0=胜, 0.5=平, 0.0=负)
     * @param games_played 已玩游戏数（用于确定 K 因子）
     * @return 评分结果
     */
    RatingResult calculateNewRating(
        int current_rating,
        int opponent_rating,
        double score,
        int games_played);

    /**
     * @brief 批量计算双方评分（用于同时更新双方）
     * @param rating_a 玩家 A 当前评分
     * @param rating_b 玩家 B 当前评分
     * @param score_a 玩家 A 得分 (1.0=胜, 0.5=平, 0.0=负)
     * @param games_a 玩家 A 已玩游戏数
     * @param games_b 玩家 B 已玩游戏数
     * @return 双方评分结果 (A, B)
     */
    std::pair<RatingResult, RatingResult> calculateBothRatings(
        int rating_a,
        int rating_b,
        double score_a,
        int games_a,
        int games_b);

    /**
     * @brief 计算期望胜率
     * @param rating_a 玩家 A 评分
     * @param rating_b 玩家 B 评分
     * @return 玩家 A 的期望胜率 (0.0-1.0)
     */
    double expectedScore(int rating_a, int rating_b) const;

    /**
     * @brief 根据评分获取段位
     * @param rating 评分
     * @return 段位
     */
    Tier getTier(int rating) const;

    /**
     * @brief 获取段位名称
     * @param tier 段位
     * @return 段位名称（中文）
     */
    std::string getTierName(Tier tier) const;

    /**
     * @brief 获取段位图标
     * @param tier 段位
     * @return 段位图标
     */
    std::string getTierIcon(Tier tier) const;

    /**
     * @brief 获取段位等级（0-15）
     * @param tier 段位
     * @return 段位等级
     */
    static int getTierLevel(Tier tier);

    /**
     * @brief 根据 level 获取段位
     * @param level 段位等级 (0-15)
     * @return 段位
     */
    static Tier getTierFromLevel(int level);

    /**
     * @brief 获取 K 因子
     * @param games_played 已玩游戏数
     * @return K 因子
     */
    int getKFactor(int games_played) const;

    /**
     * @brief 获取配置
     * @return 当前配置
     */
    const EloConfig& getConfig() const { return config_; }

    /**
     * @brief 获取评分范围
     * @return (最低评分, 最高评分)
     */
    std::pair<int, int> getRatingRange() const {
        return {config_.min_rating, config_.max_rating};
    }

private:
    EloConfig config_;

    /// 段位评分阈值
    static constexpr std::array<int, 16> TIER_THRESHOLDS = {
        100,   // BRONZE_III
        400,   // BRONZE_II
        600,   // BRONZE_I
        800,   // SILVER_III
        1000,  // SILVER_II
        1200,  // SILVER_I
        1400,  // GOLD_III
        1600,  // GOLD_II
        1800,  // GOLD_I
        2000,  // PLATINUM_III
        2200,  // PLATINUM_II
        2400,  // PLATINUM_I
        2600,  // DIAMOND_III
        2800,  // DIAMOND_II
        2900,  // DIAMOND_I
        3000   // MASTER
    };
};

} // namespace game_service
} // namespace core_services
