/**
 * @file achievement_manager.cpp
 * @brief 成就管理器实现
 */

#include "achievement_manager.h"
#include "game_repository.h"
#include "common/logger/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace core_services {
namespace game_service {

// ========== 构造函数和析构函数 ==========

AchievementManager::AchievementManager(
    std::shared_ptr<GameRepository> repository,
    const AchievementManagerConfig& config)
    : repository_(std::move(repository))
    , config_(config) {
}

AchievementManager::~AchievementManager() {
    LOG_INFO("AchievementManager destroyed");
}

// ========== 初始化和配置 ==========

bool AchievementManager::initialize() {
    LOG_INFO("Initializing AchievementManager...");

    if (!repository_) {
        LOG_ERROR("Repository is null");
        return false;
    }

    // 加载成就定义
    if (!loadDefinitions()) {
        LOG_WARNING("Failed to load achievement definitions, using builtin definitions");
        loadBuiltinDefinitions();
    }

    LOG_INFO("AchievementManager initialized with " + std::to_string(definitions_.size()) + " definitions");
    return true;
}

bool AchievementManager::loadDefinitions(const std::string& config_path) {
    std::lock_guard<std::mutex> lock(definitions_mutex_);

    if (!config_path.empty()) {
        if (loadDefinitionsFromFile(config_path)) {
            definitions_loaded_ = true;
            return true;
        }
    }

    // 尝试默认路径
    std::vector<std::string> default_paths = {
        "config/achievements.json",
        "config/gomoku_achievements.json",
        "../config/achievements.json"
    };

    for (const auto& path : default_paths) {
        if (loadDefinitionsFromFile(path)) {
            definitions_loaded_ = true;
            return true;
        }
    }

    // 使用内置定义
    loadBuiltinDefinitions();
    definitions_loaded_ = true;
    return true;
}

void AchievementManager::reloadDefinitions() {
    definitions_loaded_ = false;
    loadDefinitions();
}

void AchievementManager::updateConfig(const AchievementManagerConfig& config) {
    config_ = config;
}

// ========== 成就定义查询 ==========

std::vector<AchievementDefinition> AchievementManager::getAllDefinitions() const {
    std::lock_guard<std::mutex> lock(definitions_mutex_);
    std::vector<AchievementDefinition> result;
    result.reserve(definitions_.size());
    for (const auto& pair : definitions_) {
        result.push_back(pair.second);
    }
    return result;
}

std::optional<AchievementDefinition> AchievementManager::getDefinition(
    const std::string& achievement_id) const {
    std::lock_guard<std::mutex> lock(definitions_mutex_);
    auto it = definitions_.find(achievement_id);
    if (it != definitions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<AchievementDefinition> AchievementManager::getDefinitionsByType(
    AchievementType type) const {
    std::lock_guard<std::mutex> lock(definitions_mutex_);
    std::vector<AchievementDefinition> result;
    for (const auto& pair : definitions_) {
        if (pair.second.type == type) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<AchievementDefinition> AchievementManager::getDefinitionsByRarity(
    AchievementRarity rarity) const {
    std::lock_guard<std::mutex> lock(definitions_mutex_);
    std::vector<AchievementDefinition> result;
    for (const auto& pair : definitions_) {
        if (pair.second.rarity == rarity) {
            result.push_back(pair.second);
        }
    }
    return result;
}

// ========== 成就检查和解锁 ==========

std::vector<AchievementUnlockResult> AchievementManager::checkAndUnlock(
    const AchievementContext& context) {

    if (!config_.enabled) {
        LOG_WARNING("AchievementManager is disabled, skipping achievement check");
        return {};
    }

    LOG_INFO("Checking achievements for user: " + context.user_id +
              ", total_wins=" + std::to_string(context.total_wins) +
              ", total_games=" + std::to_string(context.total_games) +
              ", current_rating=" + std::to_string(context.current_rating));

    std::vector<AchievementUnlockResult> unlocked;

    // ✅ 优化：先复制定义到本地向量，然后释放锁，避免在 DB 操作期间持有锁
    std::vector<std::pair<std::string, AchievementDefinition>> definitions_copy;
    {
        std::lock_guard<std::mutex> lock(definitions_mutex_);
        definitions_copy.reserve(definitions_.size());
        for (const auto& pair : definitions_) {
            definitions_copy.push_back(pair);
        }
    }

    LOG_INFO("Checking " + std::to_string(definitions_copy.size()) + " achievement definitions");

    for (const auto& [id, definition] : definitions_copy) {
        // 检查游戏类型限制
        if (!definition.game_type_restriction.empty() &&
            definition.game_type_restriction != context.game_type) {
            continue;
        }

        // 检查是否已解锁（DB 操作，不持有锁）
        if (hasUserUnlocked(context.user_id, id)) {
            continue;
        }

        // 检查条件（不持有锁，使用本地定义副本）
        if (evaluateCondition(definition.condition_expr, context)) {
            LOG_INFO("Achievement condition met: " + id + " for user " + context.user_id);

            // 解锁成就
            GameAchievement achievement;
            achievement.user_id = context.user_id;
            achievement.achievement_type = id;
            achievement.title = definition.name;
            achievement.description = definition.description;
            achievement.points = definition.points;
            achievement.unlocked_at = std::chrono::system_clock::now();

            // 先确保成就定义存在于数据库（用于后续查询 JOIN）
            repository_->ensureAchievementDefinition(
                id,
                definition.name,
                definition.description,
                definition.points
            );

            if (repository_->unlockAchievement(achievement)) {
                AchievementUnlockResult result;
                result.achievement_id = id;
                result.achievement_name = definition.name;
                result.rarity = definition.rarity;
                result.points = definition.points;
                result.newly_unlocked = true;

                // 设置奖励
                result.reward.gold = definition.gold_reward;
                result.reward.gems = definition.gem_reward;
                result.reward.honor_points = definition.honor_reward;
                result.reward.experience = definition.experience_reward;

                // 发放奖励
                if (config_.auto_claim_rewards) {
                    grantAchievementReward(context.user_id, definition);
                }

                // 发送通知
                if (config_.send_notifications && notification_callback_) {
                    sendUnlockNotification(context.user_id, result);
                }

                unlocked.push_back(result);
                LOG_INFO("Achievement unlocked: " + id + " for user " + context.user_id);
            } else {
                LOG_ERROR("Failed to unlock achievement: " + id + " for user " + context.user_id);
            }
        }
    }

    if (!unlocked.empty()) {
        LOG_INFO("Unlocked " + std::to_string(unlocked.size()) + " achievements for user " + context.user_id);
    }

    return unlocked;
}

bool AchievementManager::checkAchievement(
    const std::string& achievement_id,
    const AchievementContext& context) {

    auto definition = getDefinition(achievement_id);
    if (!definition) {
        return false;
    }

    // 评估条件表达式
    return evaluateCondition(definition->condition_expr, context);
}

std::optional<AchievementUnlockResult> AchievementManager::manualUnlock(
    const std::string& user_id,
    const std::string& achievement_id) {

    auto definition = getDefinition(achievement_id);
    if (!definition) {
        LOG_WARNING("Achievement not found: " + achievement_id);
        return std::nullopt;
    }

    // 检查是否已解锁
    if (hasUserUnlocked(user_id, achievement_id)) {
        LOG_INFO("Achievement already unlocked: " + achievement_id);
        return std::nullopt;
    }

    // 解锁成就
    GameAchievement achievement;
    achievement.user_id = user_id;
    achievement.achievement_type = achievement_id;
    achievement.title = definition->name;
    achievement.description = definition->description;
    achievement.points = definition->points;
    achievement.unlocked_at = std::chrono::system_clock::now();

    if (!repository_->unlockAchievement(achievement)) {
        LOG_ERROR("Failed to unlock achievement: " + achievement_id);
        return std::nullopt;
    }

    AchievementUnlockResult result;
    result.achievement_id = achievement_id;
    result.achievement_name = definition->name;
    result.rarity = definition->rarity;
    result.points = definition->points;
    result.newly_unlocked = true;
    result.reward.gold = definition->gold_reward;
    result.reward.gems = definition->gem_reward;
    result.reward.honor_points = definition->honor_reward;
    result.reward.experience = definition->experience_reward;

    // 发放奖励
    if (config_.auto_claim_rewards) {
        grantAchievementReward(user_id, *definition);
    }

    // 发送通知
    if (config_.send_notifications && notification_callback_) {
        sendUnlockNotification(user_id, result);
    }

    LOG_INFO("Achievement manually unlocked: " + achievement_id + " for user " + user_id);
    return result;
}

// ========== 用户成就查询 ==========

std::vector<GameAchievement> AchievementManager::getUserAchievements(
    const std::string& user_id) {

    PaginationParams params;
    params.page = 1;
    params.limit = 1000;  // 获取所有成就

    auto response = repository_->getUserAchievements(user_id, params);
    return response.items;
}

int AchievementManager::getUserAchievementPoints(const std::string& user_id) {
    auto achievements = getUserAchievements(user_id);
    int total = 0;
    for (const auto& a : achievements) {
        total += a.points;
    }
    return total;
}

int AchievementManager::getUserAchievementProgress(
    const std::string& user_id,
    const std::string& achievement_id) {

    // 检查是否已解锁
    if (hasUserUnlocked(user_id, achievement_id)) {
        auto definition = getDefinition(achievement_id);
        return definition ? definition->target_progress : 100;
    }

    // 对于进度型成就，返回0（暂时不支持部分进度跟踪）
    auto definition = getDefinition(achievement_id);
    if (definition && definition->type == AchievementType::PROGRESS) {
        // TODO: 实现进度跟踪，需要从用户统计数据中获取当前进度
        return 0;
    }

    return 0;
}

bool AchievementManager::hasUserUnlocked(
    const std::string& user_id,
    const std::string& achievement_id) {
    return repository_->hasUserUnlockedAchievement(user_id, achievement_id);
}

// ========== 统计信息 ==========

nlohmann::json AchievementManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(definitions_mutex_);

    nlohmann::json stats;
    stats["total_definitions"] = definitions_.size();
    stats["enabled"] = config_.enabled;
    stats["definitions_loaded"] = definitions_loaded_;

    // 按类型统计
    std::unordered_map<AchievementType, int> type_counts;
    std::unordered_map<AchievementRarity, int> rarity_counts;

    for (const auto& [id, def] : definitions_) {
        type_counts[def.type]++;
        rarity_counts[def.rarity]++;
    }

    stats["by_type"]["milestone"] = type_counts[AchievementType::MILESTONE];
    stats["by_type"]["progress"] = type_counts[AchievementType::PROGRESS];
    stats["by_type"]["hidden"] = type_counts[AchievementType::HIDDEN];
    stats["by_type"]["social"] = type_counts[AchievementType::SOCIAL];

    stats["by_rarity"]["common"] = rarity_counts[AchievementRarity::COMMON];
    stats["by_rarity"]["rare"] = rarity_counts[AchievementRarity::RARE];
    stats["by_rarity"]["epic"] = rarity_counts[AchievementRarity::EPIC];
    stats["by_rarity"]["legendary"] = rarity_counts[AchievementRarity::LEGENDARY];

    return stats;
}

// ========== 内部方法 ==========

bool AchievementManager::loadDefinitionsFromFile(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        LOG_INFO("Cannot open achievement config file: " + config_path);
        return false;
    }

    try {
        nlohmann::json config;
        file >> config;

        if (!config.contains("achievements") || !config["achievements"].is_array()) {
            LOG_WARNING("Invalid achievement config format: " + config_path);
            return false;
        }

        definitions_.clear();

        for (const auto& item : config["achievements"]) {
            AchievementDefinition def;

            def.id = item.value("id", "");
            def.name = item.value("name", "");
            def.description = item.value("description", "");
            def.condition_expr = item.value("condition", "");
            def.game_type_restriction = item.value("game_type", "");

            def.points = item.value("points", 10);
            def.gold_reward = item.value("gold_reward", 0);
            def.gem_reward = item.value("gem_reward", 0);
            def.honor_reward = item.value("honor_reward", 0);
            def.experience_reward = item.value("experience_reward", 0);
            def.target_progress = item.value("target_progress", 0);
            def.is_hidden = item.value("is_hidden", false);

            // 解析类型
            std::string type_str = item.value("type", "milestone");
            if (type_str == "progress") def.type = AchievementType::PROGRESS;
            else if (type_str == "hidden") def.type = AchievementType::HIDDEN;
            else if (type_str == "social") def.type = AchievementType::SOCIAL;
            else def.type = AchievementType::MILESTONE;

            // 解析稀有度
            std::string rarity_str = item.value("rarity", "common");
            if (rarity_str == "rare") def.rarity = AchievementRarity::RARE;
            else if (rarity_str == "epic") def.rarity = AchievementRarity::EPIC;
            else if (rarity_str == "legendary") def.rarity = AchievementRarity::LEGENDARY;
            else def.rarity = AchievementRarity::COMMON;

            if (!def.id.empty() && !def.name.empty()) {
                definitions_[def.id] = def;
            }
        }

        LOG_INFO("Loaded " + std::to_string(definitions_.size()) +
                 " achievements from " + config_path);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse achievement config: " + std::string(e.what()));
        return false;
    }
}

void AchievementManager::loadBuiltinDefinitions() {
    definitions_.clear();

    // ===== 游戏成就 =====

    // 里程碑成就
    definitions_["first_win"] = AchievementDefinition{
        "first_win", "初出茅庐", "赢得第一场游戏",
        AchievementType::MILESTONE, AchievementRarity::COMMON, 10,
        100, 0, 0, 10, "total_wins >= 1", 1, false, ""
    };

    definitions_["win_10_games"] = AchievementDefinition{
        "win_10_games", "棋坛新秀", "累计赢得10场游戏",
        AchievementType::MILESTONE, AchievementRarity::RARE, 30,
        500, 0, 0, 30, "total_wins >= 10", 10, false, ""
    };

    definitions_["win_50_games"] = AchievementDefinition{
        "win_50_games", "棋艺精进", "累计赢得50场游戏",
        AchievementType::MILESTONE, AchievementRarity::RARE, 40,
        1000, 0, 20, 50, "total_wins >= 50", 50, false, ""
    };

    definitions_["win_100_games"] = AchievementDefinition{
        "win_100_games", "棋道高手", "累计赢得100场游戏",
        AchievementType::MILESTONE, AchievementRarity::EPIC, 50,
        2000, 10, 50, 100, "total_wins >= 100", 100, false, ""
    };

    definitions_["win_500_games"] = AchievementDefinition{
        "win_500_games", "棋坛大师", "累计赢得500场游戏",
        AchievementType::MILESTONE, AchievementRarity::EPIC, 70,
        5000, 30, 100, 200, "total_wins >= 500", 500, false, ""
    };

    definitions_["win_1000_games"] = AchievementDefinition{
        "win_1000_games", "棋圣", "累计赢得1000场游戏",
        AchievementType::MILESTONE, AchievementRarity::LEGENDARY, 100,
        10000, 50, 200, 500, "total_wins >= 1000", 1000, false, ""
    };

    // 连胜成就
    definitions_["win_streak_3"] = AchievementDefinition{
        "win_streak_3", "小试牛刀", "连续赢得3场游戏",
        AchievementType::PROGRESS, AchievementRarity::COMMON, 15,
        50, 0, 0, 15, "current_win_streak >= 3", 3, false, ""
    };

    definitions_["win_streak_5"] = AchievementDefinition{
        "win_streak_5", "势如破竹", "连续赢得5场游戏",
        AchievementType::PROGRESS, AchievementRarity::RARE, 30,
        200, 0, 10, 30, "current_win_streak >= 5", 5, false, ""
    };

    definitions_["win_streak_10"] = AchievementDefinition{
        "win_streak_10", "所向披靡", "连续赢得10场游戏",
        AchievementType::PROGRESS, AchievementRarity::EPIC, 50,
        500, 5, 30, 80, "current_win_streak >= 10", 10, false, ""
    };

    definitions_["win_streak_20"] = AchievementDefinition{
        "win_streak_20", "无人能挡", "连续赢得20场游戏",
        AchievementType::PROGRESS, AchievementRarity::LEGENDARY, 100,
        2000, 20, 100, 200, "current_win_streak >= 20", 20, false, ""
    };

    // 评分成就
    definitions_["rating_1200"] = AchievementDefinition{
        "rating_1200", "初露锋芒", "评分达到1200",
        AchievementType::MILESTONE, AchievementRarity::COMMON, 10,
        50, 0, 0, 10, "current_rating >= 1200", 1200, false, ""
    };

    definitions_["rating_1500"] = AchievementDefinition{
        "rating_1500", "黄金之路", "评分达到1500",
        AchievementType::MILESTONE, AchievementRarity::RARE, 30,
        200, 0, 50, 30, "current_rating >= 1500", 1500, false, ""
    };

    definitions_["rating_1800"] = AchievementDefinition{
        "rating_1800", "白金殿堂", "评分达到1800",
        AchievementType::MILESTONE, AchievementRarity::RARE, 40,
        500, 0, 80, 50, "current_rating >= 1800", 1800, false, ""
    };

    definitions_["rating_2000"] = AchievementDefinition{
        "rating_2000", "钻石荣光", "评分达到2000",
        AchievementType::MILESTONE, AchievementRarity::EPIC, 50,
        1000, 10, 100, 100, "current_rating >= 2000", 2000, false, ""
    };

    definitions_["rating_2500"] = AchievementDefinition{
        "rating_2500", "大师境界", "评分达到2500",
        AchievementType::MILESTONE, AchievementRarity::EPIC, 70,
        3000, 30, 200, 200, "current_rating >= 2500", 2500, false, ""
    };

    definitions_["rating_3000"] = AchievementDefinition{
        "rating_3000", "王者之巅", "评分达到3000",
        AchievementType::MILESTONE, AchievementRarity::LEGENDARY, 100,
        10000, 50, 500, 500, "current_rating >= 3000", 3000, false, ""
    };

    // 对局成就
    definitions_["games_100"] = AchievementDefinition{
        "games_100", "勤学苦练", "完成100场对局",
        AchievementType::MILESTONE, AchievementRarity::COMMON, 15,
        100, 0, 0, 20, "total_games >= 100", 100, false, ""
    };

    definitions_["games_500"] = AchievementDefinition{
        "games_500", "棋艺精进", "完成500场对局",
        AchievementType::MILESTONE, AchievementRarity::RARE, 30,
        500, 0, 20, 50, "total_games >= 500", 500, false, ""
    };

    definitions_["games_1000"] = AchievementDefinition{
        "games_1000", "棋道修行", "完成1000场对局",
        AchievementType::MILESTONE, AchievementRarity::EPIC, 50,
        2000, 10, 50, 100, "total_games >= 1000", 1000, false, ""
    };

    // 隐藏成就
    // 注意：GameResult 枚举中 WIN = 0，所以条件是 last_game_result == 0
    definitions_["perfect_game"] = AchievementDefinition{
        "perfect_game", "完美对局", "在30步内获胜",
        AchievementType::HIDDEN, AchievementRarity::LEGENDARY, 100,
        1000, 20, 100, 100, "last_game_moves <= 30 AND last_game_result == 0", 1, true, ""
    };

    definitions_["comeback_king"] = AchievementDefinition{
        "comeback_king", "绝地反击", "在评分劣势200分以上时获胜",
        AchievementType::HIDDEN, AchievementRarity::LEGENDARY, 100,
        1500, 20, 150, 150, "was_underdog == 1 AND last_game_result == 0", 1, true, ""
    };

    definitions_["quick_win"] = AchievementDefinition{
        "quick_win", "速战速决", "在5分钟内获胜",
        AchievementType::HIDDEN, AchievementRarity::EPIC, 50,
        500, 5, 50, 50, "last_game_duration <= 300 AND last_game_result == 0", 1, true, ""
    };

    definitions_["endurance"] = AchievementDefinition{
        "endurance", "持久战", "完成一场超过30分钟的对局",
        AchievementType::HIDDEN, AchievementRarity::RARE, 40,
        300, 0, 30, 50, "last_game_duration >= 1800", 1, true, ""
    };

    // 等级成就
    definitions_["level_10"] = AchievementDefinition{
        "level_10", "新手上路", "达到10级",
        AchievementType::MILESTONE, AchievementRarity::COMMON, 10,
        100, 0, 0, 20, "level >= 10", 10, false, ""
    };

    definitions_["level_25"] = AchievementDefinition{
        "level_25", "棋手认证", "达到25级",
        AchievementType::MILESTONE, AchievementRarity::RARE, 30,
        300, 0, 20, 50, "level >= 25", 25, false, ""
    };

    definitions_["level_50"] = AchievementDefinition{
        "level_50", "棋艺大师", "达到50级",
        AchievementType::MILESTONE, AchievementRarity::EPIC, 50,
        1000, 10, 80, 100, "level >= 50", 50, false, ""
    };

    definitions_["level_100"] = AchievementDefinition{
        "level_100", "传奇棋手", "达到100级",
        AchievementType::MILESTONE, AchievementRarity::LEGENDARY, 100,
        5000, 30, 200, 300, "level >= 100", 100, false, ""
    };

    LOG_INFO("Loaded " + std::to_string(definitions_.size()) + " builtin achievement definitions");
}

bool AchievementManager::evaluateCondition(
    const std::string& condition,
    const AchievementContext& context) {

    if (condition.empty()) {
        return false;
    }

    return parseAndEvaluate(condition, context);
}

bool AchievementManager::parseAndEvaluate(
    const std::string& condition,
    const AchievementContext& context) {

    // 处理 AND 组合条件
    size_t and_pos = condition.find(" AND ");
    if (and_pos != std::string::npos) {
        std::string left = condition.substr(0, and_pos);
        std::string right = condition.substr(and_pos + 5);
        return parseAndEvaluate(left, context) && parseAndEvaluate(right, context);
    }

    // 处理 OR 组合条件
    size_t or_pos = condition.find(" OR ");
    if (or_pos != std::string::npos) {
        std::string left = condition.substr(0, or_pos);
        std::string right = condition.substr(or_pos + 4);
        return parseAndEvaluate(left, context) || parseAndEvaluate(right, context);
    }

    // 去除空白
    std::string trimmed = condition;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

    // 解析比较运算符
    std::regex pattern(R"((\w+)\s*(>=|<=|==|!=|>|<)\s*(\d+))");
    std::smatch match;

    if (std::regex_match(trimmed, match, pattern)) {
        std::string field = match[1].str();
        std::string op = match[2].str();
        int value = std::stoi(match[3].str());

        int context_value = getContextValue(field, context);

        if (op == ">=") return context_value >= value;
        if (op == "<=") return context_value <= value;
        if (op == "==") return context_value == value;
        if (op == "!=") return context_value != value;
        if (op == ">") return context_value > value;
        if (op == "<") return context_value < value;
    }

    return false;
}

int AchievementManager::getContextValue(
    const std::string& field,
    const AchievementContext& context) {

    if (field == "total_games") return context.total_games;
    if (field == "total_wins") return context.total_wins;
    if (field == "total_losses") return context.total_losses;
    if (field == "total_draws") return context.total_draws;
    if (field == "current_win_streak") return context.current_win_streak;
    if (field == "best_win_streak") return context.best_win_streak;
    if (field == "current_rating") return context.current_rating;
    if (field == "peak_rating") return context.peak_rating;
    if (field == "level") return context.level;
    if (field == "last_game_result") return static_cast<int>(context.last_game_result);
    if (field == "last_game_moves") return context.last_game_moves;
    if (field == "last_game_duration") return context.last_game_duration;
    if (field == "opponent_rating") return context.opponent_rating;
    if (field == "was_underdog") return context.was_underdog ? 1 : 0;

    // 尝试从自定义数据中获取
    if (context.custom_data.contains(field) && context.custom_data[field].is_number()) {
        return context.custom_data[field].get<int>();
    }

    return 0;
}

bool AchievementManager::grantAchievementReward(
    const std::string& user_id,
    const AchievementDefinition& definition) {

    if (definition.gold_reward <= 0 && definition.gem_reward <= 0 &&
        definition.honor_reward <= 0 && definition.experience_reward <= 0) {
        return true;  // 无奖励，直接返回成功
    }

    // 发放金币
    if (definition.gold_reward > 0) {
        if (!repository_->addUserCurrency(user_id, "GOLD", definition.gold_reward)) {
            LOG_ERROR("Failed to add gold reward for achievement: " + definition.id);
            return false;
        }
    }

    // 发放宝石
    if (definition.gem_reward > 0) {
        if (!repository_->addUserCurrency(user_id, "GEM", definition.gem_reward)) {
            LOG_ERROR("Failed to add gem reward for achievement: " + definition.id);
            return false;
        }
    }

    // 发放荣誉
    if (definition.honor_reward > 0) {
        if (!repository_->addUserCurrency(user_id, "HONOR", definition.honor_reward)) {
            LOG_ERROR("Failed to add honor reward for achievement: " + definition.id);
            return false;
        }
    }

    // 记录奖励日志
    RewardLog log;
    log.log_id = "achv_" + definition.id + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    log.user_id = user_id;
    log.reward_type = "achievement";
    log.game_id = definition.id;
    log.reason = "Achievement reward: " + definition.name;

    // 使用 metadata 存储详细的奖励信息
    nlohmann::json metadata_json;
    metadata_json["gold"] = definition.gold_reward;
    metadata_json["gems"] = definition.gem_reward;
    metadata_json["honor"] = definition.honor_reward;
    metadata_json["experience"] = definition.experience_reward;
    log.metadata = metadata_json.dump();

    repository_->recordRewardLog(log);

    LOG_INFO("Granted achievement reward: " + definition.id + " to user " + user_id);
    return true;
}

void AchievementManager::sendUnlockNotification(
    const std::string& user_id,
    const AchievementUnlockResult& result) {

    if (notification_callback_) {
        try {
            notification_callback_(user_id, result);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in achievement notification callback: " + std::string(e.what()));
        }
    }
}

std::string AchievementManager::rarityToString(AchievementRarity rarity) {
    switch (rarity) {
        case AchievementRarity::COMMON: return "common";
        case AchievementRarity::RARE: return "rare";
        case AchievementRarity::EPIC: return "epic";
        case AchievementRarity::LEGENDARY: return "legendary";
        default: return "unknown";
    }
}

std::string AchievementManager::typeToString(AchievementType type) {
    switch (type) {
        case AchievementType::MILESTONE: return "milestone";
        case AchievementType::PROGRESS: return "progress";
        case AchievementType::HIDDEN: return "hidden";
        case AchievementType::SOCIAL: return "social";
        default: return "unknown";
    }
}

} // namespace game_service
} // namespace core_services
