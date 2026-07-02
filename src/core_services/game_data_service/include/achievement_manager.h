/**
 * @file achievement_manager.h
 * @brief 成就管理器 - 处理成就定义、检查、解锁和奖励发放
 * @author AI Assistant
 * @date 2026-02-21
 * @version 1.0.0
 *
 * 职责：
 * - 管理成就定义配置
 * - 检查成就解锁条件
 * - 处理成就解锁逻辑
 * - 发放成就奖励
 * - 发送解锁通知
 */

#pragma once

#include "game_models.h"
#include "reward_config.h"  // 使用已有的 GameResult 枚举
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

// 前向声明
class GameRepository;

/**
 * 成就奖励结构（简化的奖励，专门用于成就解锁）
 */
struct AchievementReward {
    int gold = 0;
    int gems = 0;
    int honor_points = 0;
    int experience = 0;

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"gold", gold},
            {"gems", gems},
            {"honor_points", honor_points},
            {"experience", experience}
        };
    }
};

/**
 * 成就稀有度
 */
enum class AchievementRarity {
    COMMON = 0,      // 普通 - 灰色
    RARE = 1,        // 稀有 - 蓝色
    EPIC = 2,        // 史诗 - 紫色
    LEGENDARY = 3    // 传说 - 橙色
};

/**
 * 成就类型
 */
enum class AchievementType {
    MILESTONE,       // 里程碑成就（一次性触发）
    PROGRESS,        // 进度型成就（累积进度触发）
    HIDDEN,          // 隐藏成就（特殊条件触发）
    SOCIAL           // 社交成就
};

/**
 * 成就定义
 */
struct AchievementDefinition {
    std::string id;                          // 成就ID
    std::string name;                        // 成就名称
    std::string description;                 // 成就描述
    AchievementType type;                    // 成就类型
    AchievementRarity rarity;                // 稀有度
    int points;                              // 成就点数

    // 奖励
    int gold_reward = 0;                     // 金币奖励
    int gem_reward = 0;                      // 宝石奖励
    int honor_reward = 0;                    // 荣誉奖励
    int experience_reward = 0;               // 经验奖励

    // 条件表达式（简单的条件描述）
    std::string condition_expr;              // 如 "total_wins >= 1", "rating >= 1500"

    // 进度型成就的目标值
    int target_progress = 0;

    // 是否隐藏（未解锁前不显示详情）
    bool is_hidden = false;

    // 游戏类型限制（空表示通用）
    std::string game_type_restriction;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AchievementDefinition,
        id, name, description, points,
        gold_reward, gem_reward, honor_reward, experience_reward,
        condition_expr, target_progress, is_hidden, game_type_restriction)
};

/**
 * 成就检查上下文
 */
struct AchievementContext {
    std::string user_id;

    // 游戏统计数据
    int total_games = 0;
    int total_wins = 0;
    int total_losses = 0;
    int total_draws = 0;
    int current_win_streak = 0;
    int best_win_streak = 0;
    int current_rating = 0;
    int peak_rating = 0;
    int level = 1;

    // 本局游戏信息
    GameResult last_game_result = GameResult::DRAW;
    int last_game_moves = 0;
    int last_game_duration = 0;
    int opponent_rating = 0;
    bool was_underdog = false;  // 是否处于评分劣势

    // 游戏类型
    std::string game_type;

    // 自定义字段
    nlohmann::json custom_data;
};

/**
 * 成就解锁结果
 */
struct AchievementUnlockResult {
    std::string achievement_id;
    std::string achievement_name;
    AchievementRarity rarity;
    int points;
    AchievementReward reward;
    bool newly_unlocked;  // 是新解锁还是已经解锁

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"achievement_id", achievement_id},
            {"achievement_name", achievement_name},
            {"rarity", static_cast<int>(rarity)},
            {"points", points},
            {"reward", reward.toJson()},
            {"newly_unlocked", newly_unlocked}
        };
    }
};

/**
 * 成就管理器配置
 */
struct AchievementManagerConfig {
    bool enabled = true;
    bool send_notifications = true;
    bool auto_claim_rewards = true;

    // 缓存配置
    int definition_cache_ttl_seconds = 3600;  // 成就定义缓存时间
    int user_progress_cache_ttl_seconds = 300; // 用户进度缓存时间

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AchievementManagerConfig,
        enabled, send_notifications, auto_claim_rewards)
};

/**
 * 成就解锁通知回调
 */
using AchievementNotificationCallback = std::function<void(
    const std::string& user_id,
    const AchievementUnlockResult& result)>;

/**
 * 成就管理器
 *
 * 负责管理游戏成就系统，包括：
 * 1. 成就定义加载和管理
 * 2. 成就条件检查
 * 3. 成就解锁处理
 * 4. 奖励发放
 * 5. 通知发送
 */
class AchievementManager {
public:
    /**
     * 构造函数
     */
    AchievementManager(
        std::shared_ptr<GameRepository> repository,
        const AchievementManagerConfig& config = AchievementManagerConfig());

    ~AchievementManager();

    // ========== 初始化和配置 ==========

    /**
     * 初始化成就系统
     * @return 是否成功
     */
    bool initialize();

    /**
     * 加载成就定义
     * @param config_path 配置文件路径（可选）
     * @return 是否成功
     */
    bool loadDefinitions(const std::string& config_path = "");

    /**
     * 重新加载成就定义
     */
    void reloadDefinitions();

    /**
     * 更新配置
     */
    void updateConfig(const AchievementManagerConfig& config);

    /**
     * 获取配置
     */
    const AchievementManagerConfig& getConfig() const { return config_; }

    // ========== 成就定义查询 ==========

    /**
     * 获取所有成就定义
     */
    std::vector<AchievementDefinition> getAllDefinitions() const;

    /**
     * 获取成就定义
     * @param achievement_id 成就ID
     * @return 成就定义（可能为空）
     */
    std::optional<AchievementDefinition> getDefinition(const std::string& achievement_id) const;

    /**
     * 获取指定类型的成就定义
     */
    std::vector<AchievementDefinition> getDefinitionsByType(AchievementType type) const;

    /**
     * 获取指定稀有度的成就定义
     */
    std::vector<AchievementDefinition> getDefinitionsByRarity(AchievementRarity rarity) const;

    // ========== 成就检查和解锁 ==========

    /**
     * 检查并解锁成就
     * @param context 成就检查上下文
     * @return 解锁的成就列表
     */
    std::vector<AchievementUnlockResult> checkAndUnlock(const AchievementContext& context);

    /**
     * 检查单个成就
     * @param achievement_id 成就ID
     * @param context 成就检查上下文
     * @return 是否满足解锁条件
     */
    bool checkAchievement(const std::string& achievement_id, const AchievementContext& context);

    /**
     * 手动解锁成就（管理员接口）
     * @param user_id 用户ID
     * @param achievement_id 成就ID
     * @return 解锁结果（可能为空表示失败）
     */
    std::optional<AchievementUnlockResult> manualUnlock(
        const std::string& user_id,
        const std::string& achievement_id);

    // ========== 用户成就查询 ==========

    /**
     * 获取用户已解锁的成就
     * @param user_id 用户ID
     * @return 成就列表
     */
    std::vector<GameAchievement> getUserAchievements(const std::string& user_id);

    /**
     * 获取用户成就点数
     * @param user_id 用户ID
     * @return 成就点数
     */
    int getUserAchievementPoints(const std::string& user_id);

    /**
     * 获取用户成就进度
     * @param user_id 用户ID
     * @param achievement_id 成就ID
     * @return 当前进度（0-target_progress）
     */
    int getUserAchievementProgress(const std::string& user_id, const std::string& achievement_id);

    /**
     * 检查用户是否已解锁成就
     */
    bool hasUserUnlocked(const std::string& user_id, const std::string& achievement_id);

    // ========== 回调设置 ==========

    /**
     * 设置成就解锁通知回调
     */
    void setNotificationCallback(AchievementNotificationCallback callback) {
        notification_callback_ = std::move(callback);
    }

    // ========== 统计信息 ==========

    /**
     * 获取成就系统统计信息
     */
    nlohmann::json getStatistics() const;

private:
    std::shared_ptr<GameRepository> repository_;
    AchievementManagerConfig config_;

    // 成就定义缓存
    mutable std::mutex definitions_mutex_;
    std::unordered_map<std::string, AchievementDefinition> definitions_;
    bool definitions_loaded_ = false;

    // 通知回调
    AchievementNotificationCallback notification_callback_;

    // ========== 内部方法 ==========

    /**
     * 从配置文件加载成就定义
     */
    bool loadDefinitionsFromFile(const std::string& config_path);

    /**
     * 加载内置成就定义
     */
    void loadBuiltinDefinitions();

    /**
     * 评估条件表达式
     */
    bool evaluateCondition(const std::string& condition, const AchievementContext& context);

    /**
     * 解析简单的条件表达式
     * 支持格式: "field >= value", "field == value", "field > value"
     */
    bool parseAndEvaluate(const std::string& condition, const AchievementContext& context);

    /**
     * 从上下文获取字段值
     */
    int getContextValue(const std::string& field, const AchievementContext& context);

    /**
     * 发放成就奖励
     */
    bool grantAchievementReward(
        const std::string& user_id,
        const AchievementDefinition& definition);

    /**
     * 发送解锁通知
     */
    void sendUnlockNotification(
        const std::string& user_id,
        const AchievementUnlockResult& result);

    /**
     * 稀有度转字符串
     */
    static std::string rarityToString(AchievementRarity rarity);

    /**
     * 成就类型转字符串
     */
    static std::string typeToString(AchievementType type);
};

} // namespace game_service
} // namespace core_services
