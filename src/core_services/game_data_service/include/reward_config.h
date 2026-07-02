/**
 * @file reward_config.h
 * @brief 游戏奖励配置
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 *
 * 定义游戏中各种奖励的配置参数，包括：
 * - 货币奖励（金币、宝石、荣誉值）
 * - 经验值奖励
 * - 胜负平不同结果的奖励系数
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

/**
 * 货币类型
 */
enum class CurrencyType {
    GOLD = 0,    // 金币 - 基础货币
    GEM = 1,     // 宝石 - 高级货币
    HONOR = 2    // 荣誉值 - 竞技货币
};

// 使用 nlohmann 宏定义枚举序列化（避免 adl_serializer 重定义问题）
NLOHMANN_JSON_SERIALIZE_ENUM(CurrencyType, {
    {CurrencyType::GOLD, "gold"},
    {CurrencyType::GEM, "gem"},
    {CurrencyType::HONOR, "honor"}
})

/**
 * 游戏结果类型
 */
enum class GameResult {
    WIN = 0,
    LOSS = 1,
    DRAW = 2,
    SURRENDER = 3,      // 投降
    TIMEOUT = 4,        // 超时
    DISCONNECT = 5,     // 断线
    ABORTED = 6         // 游戏中止
};

/**
 * 游戏模式
 */
enum class GameMode {
    CASUAL = 0,         // 休闲模式
    RANKED = 1,         // 排位模式
    TOURNAMENT = 2,     // 锦标赛模式
    FRIENDLY = 3,       // 友谊赛
    PRACTICE = 4        // 练习模式
};

/**
 * 单项货币奖励
 */
struct CurrencyReward {
    CurrencyType type;
    int amount;
    std::string reason;  // 奖励原因描述

    CurrencyReward() : type(CurrencyType::GOLD), amount(0) {}
    CurrencyReward(CurrencyType t, int a, const std::string& r = "")
        : type(t), amount(a), reason(r) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CurrencyReward, type, amount, reason)
};

/**
 * 完整奖励信息
 */
struct Reward {
    int experience = 0;                           // 经验值
    std::vector<CurrencyReward> currencies;       // 货币奖励列表
    std::vector<std::string> achievement_ids;     // 达成的成就ID
    int rating_change = 0;                        // 评分变化
    std::unordered_map<std::string, int> bonuses; // 额外奖励（首次胜利、连胜等）

    // 添加货币奖励的便捷方法
    void addCurrency(CurrencyType type, int amount, const std::string& reason = "") {
        currencies.emplace_back(type, amount, reason);
    }

    // 计算总金币
    int getTotalGold() const {
        int total = 0;
        for (const auto& c : currencies) {
            if (c.type == CurrencyType::GOLD) {
                total += c.amount;
            }
        }
        return total;
    }

    // 计算总宝石
    int getTotalGem() const {
        int total = 0;
        for (const auto& c : currencies) {
            if (c.type == CurrencyType::GEM) {
                total += c.amount;
            }
        }
        return total;
    }

    // 计算总荣誉值
    int getTotalHonor() const {
        int total = 0;
        for (const auto& c : currencies) {
            if (c.type == CurrencyType::HONOR) {
                total += c.amount;
            }
        }
        return total;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Reward, experience, currencies, achievement_ids, rating_change, bonuses)
};

/**
 * 基础奖励配置（针对特定结果和模式）
 */
struct BaseRewardConfig {
    int gold = 0;           // 金币
    int gem = 0;            // 宝石
    int honor = 0;          // 荣誉值
    int experience = 0;     // 经验值

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(BaseRewardConfig, gold, gem, honor, experience)

    // 转换为 Reward 对象
    Reward toReward(const std::string& reason = "") const {
        Reward reward;
        reward.experience = experience;
        if (gold > 0) reward.addCurrency(CurrencyType::GOLD, gold, reason);
        if (gem > 0) reward.addCurrency(CurrencyType::GEM, gem, reason);
        if (honor > 0) reward.addCurrency(CurrencyType::HONOR, honor, reason);
        return reward;
    }
};

/**
 * 游戏模式奖励配置
 */
struct GameModeRewardConfig {
    BaseRewardConfig win_reward;        // 胜利奖励
    BaseRewardConfig loss_reward;       // 失败奖励
    BaseRewardConfig draw_reward;       // 平局奖励
    BaseRewardConfig surrender_reward;  // 投降奖励
    BaseRewardConfig timeout_reward;    // 超时奖励
    double rating_multiplier = 1.0;     // 评分变化倍率
    bool gives_experience = true;       // 是否给予经验
    bool gives_currency = true;         // 是否给予货币
    bool affects_rating = true;         // 是否影响评分

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameModeRewardConfig,
        win_reward, loss_reward, draw_reward, surrender_reward, timeout_reward,
        rating_multiplier, gives_experience, gives_currency, affects_rating)

    // 根据游戏结果获取基础奖励
    BaseRewardConfig getBaseReward(GameResult result) const {
        switch (result) {
            case GameResult::WIN:
                return win_reward;
            case GameResult::LOSS:
                return loss_reward;
            case GameResult::DRAW:
                return draw_reward;
            case GameResult::SURRENDER:
                return surrender_reward;
            case GameResult::TIMEOUT:
                return timeout_reward;
            default:
                return loss_reward;  // 默认按失败处理
        }
    }
};

/**
 * 段位奖励修正配置
 */
struct TierRewardModifier {
    int tier_min;    // 段位范围最小值
    int tier_max;    // 段位范围最大值
    double gold_multiplier = 1.0;
    double exp_multiplier = 1.0;
    double honor_multiplier = 1.0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TierRewardModifier,
        tier_min, tier_max, gold_multiplier, exp_multiplier, honor_multiplier)
};

/**
 * 连胜/连败奖励配置
 */
struct StreakBonus {
    int min_streak;          // 最小连胜/败次数
    int max_streak;          // 最大连胜/败次数
    int bonus_gold = 0;      // 额外金币
    int bonus_honor = 0;     // 额外荣誉值
    double multiplier = 1.0; // 奖励倍率

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(StreakBonus, min_streak, max_streak, bonus_gold, bonus_honor, multiplier)
};

/**
 * 全局奖励配置
 */
class RewardConfig {
public:
    RewardConfig();

    // ========== 基础奖励获取 ==========

    /**
     * 根据游戏模式和结果获取基础奖励
     */
    Reward getBaseReward(GameMode mode, GameResult result) const;

    /**
     * 获取游戏模式配置
     */
    std::optional<GameModeRewardConfig> getModeConfig(GameMode mode) const;

    // ========== 奖励修正 ==========

    /**
     * 应用段位修正
     */
    Reward applyTierModifier(Reward base_reward, int tier_level) const;

    /**
     * 应用连胜奖励
     */
    Reward applyWinStreakBonus(Reward base_reward, int streak_count) const;

    /**
     * 应用首胜奖励
     */
    Reward applyFirstWinBonus(Reward base_reward) const;

    /**
     * 应用每日游戏奖励
     */
    Reward applyDailyGameBonus(Reward base_reward, int games_today) const;

    // ========== 配置加载 ==========

    /**
     * 从 JSON 加载配置
     */
    bool loadFromJson(const nlohmann::json& config);

    /**
     * 从文件加载配置
     */
    bool loadFromFile(const std::string& filepath);

    /**
     * 导出为 JSON
     */
    nlohmann::json toJson() const;

    // ========== 配置设置 ==========

    /**
     * 设置游戏模式配置
     */
    void setModeConfig(GameMode mode, const GameModeRewardConfig& config);

    /**
     * 设置段位修正
     */
    void setTierModifiers(const std::vector<TierRewardModifier>& modifiers);

    /**
     * 设置连胜奖励
     */
    void setWinStreakBonuses(const std::vector<StreakBonus>& bonuses);

    /**
     * 设置首胜奖励
     */
    void setFirstWinBonus(const BaseRewardConfig& bonus);

    // ========== 获取配置值 ==========

    /**
     * 获取首胜奖励
     */
    BaseRewardConfig getFirstWinBonus() const { return first_win_bonus_; }

    /**
     * 获取每日首N局奖励倍率
     */
    double getDailyEarlyGameMultiplier(int games_today) const;

    /**
     * 获取评分范围限制
     */
    std::pair<int, int> getRatingRange() const { return {min_rating_, max_rating_}; }

    // ========== 静态工厂方法 ==========

    /**
     * 创建默认配置
     */
    static RewardConfig createDefault();

    /**
     * 创建五子棋专用配置
     */
    static RewardConfig createGomokuDefault();

private:
    // 游戏模式配置映射
    std::unordered_map<GameMode, GameModeRewardConfig> mode_configs_;

    // 段位修正列表
    std::vector<TierRewardModifier> tier_modifiers_;

    // 连胜奖励列表
    std::vector<StreakBonus> win_streak_bonuses_;

    // 首胜奖励
    BaseRewardConfig first_win_bonus_;

    // 每日前N局奖励倍率
    std::vector<std::pair<int, double>> daily_early_game_multipliers_;

    // 评分范围
    int min_rating_ = 100;
    int max_rating_ = 3000;

    // 货币上限
    int max_gold_per_game_ = 1000;
    int max_gem_per_game_ = 100;
    int max_honor_per_game_ = 200;

    // 辅助方法
    GameModeRewardConfig createDefaultModeConfig(GameMode mode) const;
    void initializeDefaultTierModifiers();
    void initializeDefaultStreakBonuses();
};

// ========== 枚举转换工具 ==========

/**
 * 将字符串转换为游戏结果
 */
GameResult stringToGameResult(const std::string& str);

/**
 * 将游戏结果转换为字符串
 */
std::string gameResultToString(GameResult result);

/**
 * 将字符串转换为游戏模式
 */
GameMode stringToGameMode(const std::string& str);

/**
 * 将游戏模式转换为字符串
 */
std::string gameModeToString(GameMode mode);

/**
 * 将字符串转换为货币类型
 */
CurrencyType stringToCurrencyType(const std::string& str);

/**
 * 将货币类型转换为字符串
 */
std::string currencyTypeToString(CurrencyType type);

} // namespace game_service
} // namespace core_services
