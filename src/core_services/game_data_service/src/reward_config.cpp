/**
 * @file reward_config.cpp
 * @brief 游戏奖励配置实现
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 */

#include "reward_config.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace core_services {
namespace game_service {

// ==================== 枚举转换工具 ====================

GameResult stringToGameResult(const std::string& str) {
    if (str == "win" || str == "WIN") return GameResult::WIN;
    if (str == "loss" || str == "LOSS") return GameResult::LOSS;
    if (str == "draw" || str == "DRAW") return GameResult::DRAW;
    if (str == "surrender" || str == "SURRENDER") return GameResult::SURRENDER;
    if (str == "timeout" || str == "TIMEOUT") return GameResult::TIMEOUT;
    if (str == "disconnect" || str == "DISCONNECT") return GameResult::DISCONNECT;
    if (str == "aborted" || str == "ABORTED") return GameResult::ABORTED;
    return GameResult::LOSS;  // 默认
}

std::string gameResultToString(GameResult result) {
    switch (result) {
        case GameResult::WIN: return "win";
        case GameResult::LOSS: return "loss";
        case GameResult::DRAW: return "draw";
        case GameResult::SURRENDER: return "surrender";
        case GameResult::TIMEOUT: return "timeout";
        case GameResult::DISCONNECT: return "disconnect";
        case GameResult::ABORTED: return "aborted";
        default: return "unknown";
    }
}

GameMode stringToGameMode(const std::string& str) {
    if (str == "casual" || str == "CASUAL") return GameMode::CASUAL;
    if (str == "ranked" || str == "RANKED") return GameMode::RANKED;
    if (str == "tournament" || str == "TOURNAMENT") return GameMode::TOURNAMENT;
    if (str == "friendly" || str == "FRIENDLY") return GameMode::FRIENDLY;
    if (str == "practice" || str == "PRACTICE") return GameMode::PRACTICE;
    return GameMode::CASUAL;  // 默认
}

std::string gameModeToString(GameMode mode) {
    switch (mode) {
        case GameMode::CASUAL: return "casual";
        case GameMode::RANKED: return "ranked";
        case GameMode::TOURNAMENT: return "tournament";
        case GameMode::FRIENDLY: return "friendly";
        case GameMode::PRACTICE: return "practice";
        default: return "unknown";
    }
}

CurrencyType stringToCurrencyType(const std::string& str) {
    if (str == "gold" || str == "GOLD") return CurrencyType::GOLD;
    if (str == "gem" || str == "GEM") return CurrencyType::GEM;
    if (str == "honor" || str == "HONOR") return CurrencyType::HONOR;
    return CurrencyType::GOLD;  // 默认
}

std::string currencyTypeToString(CurrencyType type) {
    switch (type) {
        case CurrencyType::GOLD: return "gold";
        case CurrencyType::GEM: return "gem";
        case CurrencyType::HONOR: return "honor";
        default: return "unknown";
    }
}

// ==================== RewardConfig ====================

RewardConfig::RewardConfig() {
    // 初始化默认段位修正
    initializeDefaultTierModifiers();

    // 初始化默认连胜奖励
    initializeDefaultStreakBonuses();

    // 初始化首胜奖励
    first_win_bonus_.gold = 50;
    first_win_bonus_.experience = 100;

    // 每日前5局奖励倍率
    daily_early_game_multipliers_ = {
        {3, 1.5},   // 前3局 1.5倍
        {5, 1.2},   // 4-5局 1.2倍
        {10, 1.0}   // 6局以后正常
    };
}

Reward RewardConfig::getBaseReward(GameMode mode, GameResult result) const {
    Reward reward;

    auto mode_config = getModeConfig(mode);
    if (!mode_config) {
        // 使用默认配置
        mode_config = createDefaultModeConfig(mode);
    }

    // 检查是否给予奖励
    if (!mode_config->gives_experience && !mode_config->gives_currency) {
        return reward;  // 不给奖励
    }

    // 获取基础奖励
    BaseRewardConfig base = mode_config->getBaseReward(result);

    // 转换为 Reward 对象
    std::string reason = gameResultToString(result) + " in " + gameModeToString(mode) + " game";
    reward = base.toReward(reason);

    return reward;
}

std::optional<GameModeRewardConfig> RewardConfig::getModeConfig(GameMode mode) const {
    auto it = mode_configs_.find(mode);
    if (it != mode_configs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

Reward RewardConfig::applyTierModifier(Reward base_reward, int tier_level) const {
    for (const auto& modifier : tier_modifiers_) {
        if (tier_level >= modifier.tier_min && tier_level <= modifier.tier_max) {
            // 应用倍率修正
            for (auto& currency : base_reward.currencies) {
                switch (currency.type) {
                    case CurrencyType::GOLD:
                        currency.amount = static_cast<int>(currency.amount * modifier.gold_multiplier);
                        break;
                    case CurrencyType::HONOR:
                        currency.amount = static_cast<int>(currency.amount * modifier.honor_multiplier);
                        break;
                    default:
                        break;
                }
            }
            base_reward.experience = static_cast<int>(base_reward.experience * modifier.exp_multiplier);
            break;
        }
    }
    return base_reward;
}

Reward RewardConfig::applyWinStreakBonus(Reward base_reward, int streak_count) const {
    if (streak_count <= 1) {
        return base_reward;  // 没有连胜
    }

    for (const auto& bonus : win_streak_bonuses_) {
        if (streak_count >= bonus.min_streak && streak_count <= bonus.max_streak) {
            // 应用额外奖励
            if (bonus.bonus_gold > 0) {
                base_reward.addCurrency(CurrencyType::GOLD, bonus.bonus_gold,
                    "连胜奖励 x" + std::to_string(streak_count));
            }
            if (bonus.bonus_honor > 0) {
                base_reward.addCurrency(CurrencyType::HONOR, bonus.bonus_honor,
                    "连胜荣誉 x" + std::to_string(streak_count));
            }

            // 应用倍率
            for (auto& currency : base_reward.currencies) {
                currency.amount = static_cast<int>(currency.amount * bonus.multiplier);
            }
            break;
        }
    }

    return base_reward;
}

Reward RewardConfig::applyFirstWinBonus(Reward base_reward) const {
    if (first_win_bonus_.gold > 0) {
        base_reward.addCurrency(CurrencyType::GOLD, first_win_bonus_.gold, "首胜奖励");
    }
    if (first_win_bonus_.gem > 0) {
        base_reward.addCurrency(CurrencyType::GEM, first_win_bonus_.gem, "首胜宝石");
    }
    if (first_win_bonus_.honor > 0) {
        base_reward.addCurrency(CurrencyType::HONOR, first_win_bonus_.honor, "首胜荣誉");
    }
    base_reward.experience += first_win_bonus_.experience;
    return base_reward;
}

Reward RewardConfig::applyDailyGameBonus(Reward base_reward, int games_today) const {
    double multiplier = getDailyEarlyGameMultiplier(games_today);
    if (multiplier > 1.0) {
        // 应用到金币奖励
        for (auto& currency : base_reward.currencies) {
            if (currency.type == CurrencyType::GOLD) {
                currency.amount = static_cast<int>(currency.amount * multiplier);
                currency.reason += " (每日加成 x" + std::to_string(multiplier) + ")";
            }
        }
        // 应用到经验值
        base_reward.experience = static_cast<int>(base_reward.experience * multiplier);
    }
    return base_reward;
}

double RewardConfig::getDailyEarlyGameMultiplier(int games_today) const {
    for (const auto& [threshold, multiplier] : daily_early_game_multipliers_) {
        if (games_today < threshold) {
            return multiplier;
        }
    }
    return 1.0;
}

bool RewardConfig::loadFromJson(const nlohmann::json& config) {
    try {
        // 加载游戏模式配置
        if (config.contains("modes")) {
            for (auto& [mode_str, mode_config_json] : config["modes"].items()) {
                GameMode mode = stringToGameMode(mode_str);
                GameModeRewardConfig mode_config;
                from_json(mode_config_json, mode_config);
                mode_configs_[mode] = mode_config;
            }
        }

        // 加载段位修正
        if (config.contains("tier_modifiers")) {
            tier_modifiers_ = config["tier_modifiers"].get<std::vector<TierRewardModifier>>();
        }

        // 加载连胜奖励
        if (config.contains("win_streak_bonuses")) {
            win_streak_bonuses_ = config["win_streak_bonuses"].get<std::vector<StreakBonus>>();
        }

        // 加载首胜奖励
        if (config.contains("first_win_bonus")) {
            from_json(config["first_win_bonus"], first_win_bonus_);
        }

        // 加载货币上限
        if (config.contains("max_gold_per_game")) {
            max_gold_per_game_ = config["max_gold_per_game"].get<int>();
        }
        if (config.contains("max_gem_per_game")) {
            max_gem_per_game_ = config["max_gem_per_game"].get<int>();
        }
        if (config.contains("max_honor_per_game")) {
            max_honor_per_game_ = config["max_honor_per_game"].get<int>();
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RewardConfig::loadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json config;
        file >> config;

        return loadFromJson(config);
    } catch (const std::exception& e) {
        return false;
    }
}

nlohmann::json RewardConfig::toJson() const {
    nlohmann::json config;

    // 导出游戏模式配置
    nlohmann::json modes_json;
    for (const auto& [mode, mode_config] : mode_configs_) {
        modes_json[gameModeToString(mode)] = mode_config;
    }
    config["modes"] = modes_json;

    // 导出段位修正
    config["tier_modifiers"] = tier_modifiers_;

    // 导出连胜奖励
    config["win_streak_bonuses"] = win_streak_bonuses_;

    // 导出首胜奖励
    config["first_win_bonus"] = first_win_bonus_;

    // 导出货币上限
    config["max_gold_per_game"] = max_gold_per_game_;
    config["max_gem_per_game"] = max_gem_per_game_;
    config["max_honor_per_game"] = max_honor_per_game_;

    return config;
}

void RewardConfig::setModeConfig(GameMode mode, const GameModeRewardConfig& config) {
    mode_configs_[mode] = config;
}

void RewardConfig::setTierModifiers(const std::vector<TierRewardModifier>& modifiers) {
    tier_modifiers_ = modifiers;
}

void RewardConfig::setWinStreakBonuses(const std::vector<StreakBonus>& bonuses) {
    win_streak_bonuses_ = bonuses;
}

void RewardConfig::setFirstWinBonus(const BaseRewardConfig& bonus) {
    first_win_bonus_ = bonus;
}

GameModeRewardConfig RewardConfig::createDefaultModeConfig(GameMode mode) const {
    GameModeRewardConfig config;

    switch (mode) {
        case GameMode::CASUAL:
            // 休闲模式：低奖励，不影响评分
            config.win_reward = {30, 0, 0, 50};
            config.loss_reward = {10, 0, 0, 20};
            config.draw_reward = {20, 0, 0, 35};
            config.rating_multiplier = 0;
            config.affects_rating = false;
            break;

        case GameMode::RANKED:
            // 排位模式：正常奖励，影响评分
            config.win_reward = {50, 0, 20, 100};
            config.loss_reward = {15, 0, 5, 30};
            config.draw_reward = {35, 0, 15, 65};
            config.rating_multiplier = 1.0;
            config.affects_rating = true;
            break;

        case GameMode::TOURNAMENT:
            // 锦标赛模式：高奖励
            config.win_reward = {100, 5, 50, 200};
            config.loss_reward = {30, 0, 15, 60};
            config.draw_reward = {65, 2, 30, 130};
            config.rating_multiplier = 1.5;
            config.affects_rating = true;
            break;

        case GameMode::FRIENDLY:
            // 友谊赛：基础奖励，不影响评分
            config.win_reward = {20, 0, 0, 30};
            config.loss_reward = {10, 0, 0, 15};
            config.draw_reward = {15, 0, 0, 22};
            config.rating_multiplier = 0;
            config.affects_rating = false;
            break;

        case GameMode::PRACTICE:
            // 练习模式：极低奖励
            config.win_reward = {10, 0, 0, 15};
            config.loss_reward = {5, 0, 0, 10};
            config.draw_reward = {7, 0, 0, 12};
            config.rating_multiplier = 0;
            config.affects_rating = false;
            config.gives_currency = false;  // 练习模式不给货币
            break;

        default:
            // 默认配置
            config.win_reward = {30, 0, 0, 50};
            config.loss_reward = {10, 0, 0, 20};
            config.draw_reward = {20, 0, 0, 35};
            break;
    }

    // 投降和超时按失败处理，略微减少
    config.surrender_reward = config.loss_reward;
    config.surrender_reward.gold = static_cast<int>(config.loss_reward.gold * 0.8);
    config.surrender_reward.experience = static_cast<int>(config.loss_reward.experience * 0.8);

    config.timeout_reward = config.loss_reward;
    config.timeout_reward.gold = static_cast<int>(config.loss_reward.gold * 0.5);
    config.timeout_reward.experience = static_cast<int>(config.loss_reward.experience * 0.5);

    return config;
}

void RewardConfig::initializeDefaultTierModifiers() {
    // 低段位（青铜）：正常奖励
    tier_modifiers_.push_back({0, 2, 1.0, 1.0, 1.0});

    // 中低段位（白银）：略微增加
    tier_modifiers_.push_back({3, 5, 1.1, 1.1, 1.1});

    // 中段位（黄金）：增加奖励
    tier_modifiers_.push_back({6, 8, 1.2, 1.2, 1.3});

    // 中高段位（铂金）：较高奖励
    tier_modifiers_.push_back({9, 11, 1.3, 1.3, 1.5});

    // 高段位（钻石）：高奖励
    tier_modifiers_.push_back({12, 14, 1.5, 1.5, 2.0});

    // 最高段位（大师）：最高奖励
    tier_modifiers_.push_back({15, 15, 2.0, 2.0, 3.0});
}

void RewardConfig::initializeDefaultStreakBonuses() {
    // 3连胜
    win_streak_bonuses_.push_back({3, 3, 20, 10, 1.1});

    // 4-5连胜
    win_streak_bonuses_.push_back({4, 5, 30, 15, 1.15});

    // 6-7连胜
    win_streak_bonuses_.push_back({6, 7, 50, 25, 1.2});

    // 8-10连胜
    win_streak_bonuses_.push_back({8, 10, 80, 40, 1.3});

    // 10+连胜
    win_streak_bonuses_.push_back({11, 100, 100, 50, 1.5});
}

// ==================== 静态工厂方法 ====================

RewardConfig RewardConfig::createDefault() {
    RewardConfig config;

    // 设置各模式默认配置
    config.setModeConfig(GameMode::CASUAL, config.createDefaultModeConfig(GameMode::CASUAL));
    config.setModeConfig(GameMode::RANKED, config.createDefaultModeConfig(GameMode::RANKED));
    config.setModeConfig(GameMode::TOURNAMENT, config.createDefaultModeConfig(GameMode::TOURNAMENT));
    config.setModeConfig(GameMode::FRIENDLY, config.createDefaultModeConfig(GameMode::FRIENDLY));
    config.setModeConfig(GameMode::PRACTICE, config.createDefaultModeConfig(GameMode::PRACTICE));

    return config;
}

RewardConfig RewardConfig::createGomokuDefault() {
    RewardConfig config = createDefault();

    // 五子棋特定调整
    // 排位模式增加荣誉值获取（五子棋更注重竞技性）
    GameModeRewardConfig ranked = config.getModeConfig(GameMode::RANKED).value_or(
        config.createDefaultModeConfig(GameMode::RANKED));
    ranked.win_reward.honor = 25;  // 胜利获得更多荣誉值
    ranked.loss_reward.honor = 8;
    ranked.draw_reward.honor = 18;
    config.setModeConfig(GameMode::RANKED, ranked);

    return config;
}

} // namespace game_service
} // namespace core_services
