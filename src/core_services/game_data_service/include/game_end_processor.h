/**
 * @file game_end_processor.h
 * @brief 游戏结束处理器 - 处理游戏结束时的完整结算流程
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 *
 * 职责：
 * - 整合 ELO 评分计算
 * - 计算和发放奖励
 * - 更新用户统计数据
 * - 记录游戏结果
 * - 通知相关系统（排行榜、成就等）
 */

#pragma once

#include "elo_rating_calculator.h"
#include "reward_config.h"
#include "currency_manager.h"
#include "achievement_manager.h"
#include "leaderboard_manager.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

// 前向声明
class GameRepository;

/**
 * 玩家游戏数据
 */
struct PlayerGameData {
    std::string user_id;
    int rating_before;              // 游戏前评分
    int games_played;               // 总游戏场次
    int win_streak;                 // 当前连胜场次（负数为连败）
    bool is_first_win_today;        // 今日是否首胜
    int games_today;                // 今日游戏场次
    int tier_level;                 // 当前段位等级

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerGameData,
        user_id, rating_before, games_played, win_streak,
        is_first_win_today, games_today, tier_level)
};

/**
 * 游戏结束请求
 */
struct GameEndRequest {
    std::string game_id;            // 游戏ID
    std::string game_type;          // 游戏类型（gomoku, chess 等）
    GameMode mode;                  // 游戏模式
    int duration_seconds;           // 游戏时长（秒）
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point ended_at;

    // 玩家信息
    struct PlayerResult {
        std::string user_id;
        GameResult result;          // 游戏结果
        PlayerGameData game_data;   // 游戏前数据

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerResult, user_id, result, game_data)
    };

    std::vector<PlayerResult> players;

    // 额外信息
    nlohmann::json metadata;        // 游戏特定元数据

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameEndRequest,
        game_id, game_type, mode, duration_seconds, players, metadata)
};

/**
 * 玩家结算结果
 */
struct PlayerSettlement {
    std::string user_id;

    // 游戏结果
    GameResult result;

    // 评分变化
    int rating_before;
    int rating_after;
    int rating_change;
    Tier tier_before;
    Tier tier_after;
    bool tier_changed;
    bool tier_promoted;
    bool tier_demoted;

    // 获得奖励
    Reward reward;

    // 统计更新（更新后的值，用于成就检查）
    int new_win_streak;             // 新连胜/败
    int new_best_win_streak;        // 新最佳连胜
    int new_games_played;           // 新总场次
    int new_games_today;            // 新今日场次
    int new_wins;                   // 新总胜场（更新后）
    int new_losses;                 // 新总败场（更新后）
    int new_draws;                  // 新总平局（更新后）
    int new_level;                  // 新等级（更新后）

    // 成就进度
    std::vector<std::string> achievements_unlocked;
    std::vector<std::pair<std::string, int>> achievement_progress;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerSettlement,
        user_id, result,
        rating_before, rating_after, rating_change,
        tier_before, tier_after, tier_changed, tier_promoted, tier_demoted,
        reward, new_win_streak, new_best_win_streak, new_games_played, new_games_today,
        new_wins, new_losses, new_draws, new_level,
        achievements_unlocked, achievement_progress)
};

/**
 * 游戏结束响应
 */
struct GameEndResponse {
    bool success = false;
    std::string error_message;
    std::string game_id;

    std::vector<PlayerSettlement> settlements;

    // 游戏摘要
    std::string game_type;
    GameMode mode;
    int duration_seconds;
    std::chrono::system_clock::time_point ended_at;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameEndResponse,
        success, error_message, game_id, settlements,
        game_type, mode, duration_seconds)

    static GameEndResponse failure(const std::string& error) {
        GameEndResponse response;
        response.success = false;
        response.error_message = error;
        return response;
    }
};

/**
 * 成就检查回调类型
 */
using AchievementChecker = std::function<std::vector<std::string>(
    const std::string& user_id,
    const PlayerSettlement& settlement,
    const GameEndRequest& request)>;

/**
 * 排行榜更新回调类型
 */
using LeaderboardUpdater = std::function<void(
    const std::string& user_id,
    int new_rating,
    Tier new_tier)>;

/**
 * 游戏结束处理器配置
 */
struct GameEndProcessorConfig {
    EloConfig elo_config;
    RewardConfig reward_config;
    CurrencyConfig currency_config;
    AchievementManagerConfig achievement_config;
    LeaderboardManagerConfig leaderboard_config;

    // 是否启用成就检查
    bool enable_achievements = true;

    // 是否启用排行榜更新
    bool enable_leaderboard = true;

    // 是否异步处理
    bool async_processing = false;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameEndProcessorConfig,
        enable_achievements, enable_leaderboard, async_processing)
};

/**
 * 游戏结束处理器
 *
 * 处理游戏结束的完整流程：
 * 1. 验证请求
 * 2. 计算 ELO 评分变化
 * 3. 计算奖励（基础 + 修正）
 * 4. 发放货币和经验
 * 5. 更新统计数据
 * 6. 检查成就
 * 7. 更新排行榜
 * 8. 返回结算结果
 */
class GameEndProcessor {
public:
    /**
     * 构造函数
     */
    GameEndProcessor(
        std::shared_ptr<GameRepository> repository,
        const GameEndProcessorConfig& config = GameEndProcessorConfig());

    /**
     * 析构函数
     */
    ~GameEndProcessor();

    // ========== 主要接口 ==========

    /**
     * 处理游戏结束
     * @param request 游戏结束请求
     * @return 游戏结束响应
     */
    GameEndResponse processGameEnd(const GameEndRequest& request);

    /**
     * 异步处理游戏结束
     * @param request 游戏结束请求
     * @param callback 完成回调
     */
    void processGameEndAsync(
        const GameEndRequest& request,
        std::function<void(const GameEndResponse&)> callback);

    // ========== 组件访问 ==========

    /**
     * 获取 ELO 计算器
     */
    EloRatingCalculator& getEloCalculator() { return *elo_calculator_; }

    /**
     * 获取奖励配置
     */
    RewardConfig& getRewardConfig() { return *reward_config_; }

    /**
     * 获取货币管理器
     */
    CurrencyManager& getCurrencyManager() { return *currency_manager_; }

    // ========== 回调设置 ==========

    /**
     * 设置成就检查器（兼容旧接口）
     */
    void setAchievementChecker(AchievementChecker checker) {
        achievement_checker_ = std::move(checker);
    }

    /**
     * 设置排行榜更新器（兼容旧接口）
     */
    void setLeaderboardUpdater(LeaderboardUpdater updater) {
        leaderboard_updater_ = std::move(updater);
    }

    // ========== 管理器设置（新接口） ==========

    /**
     * 设置成就管理器
     */
    void setAchievementManager(std::shared_ptr<AchievementManager> manager) {
        achievement_manager_ = std::move(manager);
    }

    /**
     * 获取成就管理器
     */
    std::shared_ptr<AchievementManager> getAchievementManager() const {
        return achievement_manager_;
    }

    /**
     * 设置排行榜管理器
     */
    void setLeaderboardManager(std::shared_ptr<LeaderboardManager> manager) {
        leaderboard_manager_ = std::move(manager);
    }

    /**
     * 获取排行榜管理器
     */
    std::shared_ptr<LeaderboardManager> getLeaderboardManager() const {
        return leaderboard_manager_;
    }

    // ========== 配置 ==========

    /**
     * 更新配置
     */
    void updateConfig(const GameEndProcessorConfig& config);

    /**
     * 获取配置
     */
    const GameEndProcessorConfig& getConfig() const { return config_; }

private:
    std::shared_ptr<GameRepository> repository_;
    GameEndProcessorConfig config_;

    // 组件
    std::unique_ptr<EloRatingCalculator> elo_calculator_;
    std::unique_ptr<RewardConfig> reward_config_;
    std::unique_ptr<CurrencyManager> currency_manager_;

    // 管理器（新接口）
    std::shared_ptr<AchievementManager> achievement_manager_;
    std::shared_ptr<LeaderboardManager> leaderboard_manager_;

    // 回调（兼容旧接口）
    AchievementChecker achievement_checker_;
    LeaderboardUpdater leaderboard_updater_;

    // ========== 内部处理方法 ==========

    /**
     * 验证请求
     */
    bool validateRequest(const GameEndRequest& request, std::string& error);

    /**
     * 计算评分变化
     */
    RatingResult calculateRatingChange(
        const GameEndRequest::PlayerResult& player,
        const std::vector<GameEndRequest::PlayerResult>& all_players);

    /**
     * 计算完整奖励
     */
    Reward calculateTotalReward(
        const GameEndRequest::PlayerResult& player,
        const RatingResult& rating_result,
        const GameEndRequest& request);

    /**
     * 发放奖励
     */
    bool grantRewards(
        const std::string& user_id,
        const Reward& reward,
        const std::string& game_id);

    /**
     * 更新玩家统计
     */
    void updatePlayerStats(
        PlayerSettlement& settlement,
        const GameEndRequest::PlayerResult& player,
        const GameEndRequest& request);

    /**
     * 检查成就
     */
    std::vector<std::string> checkAchievements(
        const PlayerSettlement& settlement,
        const GameEndRequest& request);

    /**
     * 更新排行榜
     */
    void updateLeaderboard(
        const std::string& user_id,
        int new_rating,
        Tier new_tier);

    /**
     * 保存游戏记录
     */
    bool saveGameRecord(const GameEndRequest& request, const GameEndResponse& response);

    /**
     * 确定对手评分（用于 ELO 计算）
     */
    int determineOpponentRating(
        const GameEndRequest::PlayerResult& player,
        const std::vector<GameEndRequest::PlayerResult>& all_players);

    /**
     * 计算新连胜
     */
    int calculateNewStreak(int current_streak, GameResult result);
};

} // namespace game_service
} // namespace core_services
