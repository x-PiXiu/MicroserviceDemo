/**
 * @file game_end_processor.cpp
 * @brief 游戏结束处理器实现
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 */

#include "game_end_processor.h"
#include "game_repository.h"
#include <common/logger/logger.h>
#include <algorithm>
#include <future>

namespace core_services {
namespace game_service {

// ==================== GameEndProcessor ====================
// 注意: gameModeToString 函数已在 reward_config.cpp 中实现

GameEndProcessor::GameEndProcessor(
    std::shared_ptr<GameRepository> repository,
    const GameEndProcessorConfig& config)
    : repository_(repository)
    , config_(config) {

    if (!repository_) {
        throw std::invalid_argument("GameRepository cannot be null");
    }

    // 初始化组件
    elo_calculator_ = std::make_unique<EloRatingCalculator>(config.elo_config);
    reward_config_ = std::make_unique<RewardConfig>(RewardConfig::createGomokuDefault());
    currency_manager_ = std::make_unique<CurrencyManager>(repository_, config.currency_config);
}

GameEndProcessor::~GameEndProcessor() = default;

// ==================== 主要接口 ====================

GameEndResponse GameEndProcessor::processGameEnd(const GameEndRequest& request) {
    LOG_INFO("[GameEnd] 开始处理游戏结算: game_id=" + request.game_id +
             ", players=" + std::to_string(request.players.size()));

    GameEndResponse response;
    response.game_id = request.game_id;
    response.game_type = request.game_type;
    response.mode = request.mode;
    response.duration_seconds = request.duration_seconds;
    response.ended_at = request.ended_at;

    // 1. 验证请求
    std::string validation_error;
    if (!validateRequest(request, validation_error)) {
        LOG_ERROR("[GameEnd] 验证失败: " + validation_error);
        return GameEndResponse::failure("Validation failed: " + validation_error);
    }

    // 2. 处理每个玩家的结算
    int player_index = 0;
    for (const auto& player : request.players) {
        player_index++;
        LOG_INFO("[GameEnd] 处理玩家 " + std::to_string(player_index) + "/" +
                  std::to_string(request.players.size()) + ": " + player.user_id);

        PlayerSettlement settlement;
        settlement.user_id = player.user_id;
        settlement.result = player.result;

        // 2.1 计算评分变化
        LOG_INFO("[GameEnd] 计算评分变化: " + player.user_id);
        RatingResult rating_result = calculateRatingChange(player, request.players);
        settlement.rating_before = rating_result.rating_before;
        settlement.rating_after = rating_result.rating_after;
        settlement.rating_change = rating_result.rating_change;
        settlement.tier_before = rating_result.tier_before;
        settlement.tier_after = rating_result.tier_after;
        settlement.tier_changed = rating_result.tier_changed;
        settlement.tier_promoted = rating_result.tier_promoted;
        settlement.tier_demoted = rating_result.tier_demoted;

        // 2.2 计算完整奖励
        LOG_INFO("[GameEnd] 计算奖励: " + player.user_id);
        settlement.reward = calculateTotalReward(player, rating_result, request);

        // 2.3 发放奖励
        LOG_INFO("[GameEnd] 发放奖励: " + player.user_id);
        if (!grantRewards(player.user_id, settlement.reward, request.game_id)) {
            // 奖励发放失败，但游戏结果仍然有效
            settlement.reward.bonuses["grant_error"] = 1;
        }

        // 2.4 更新玩家统计
        LOG_INFO("[GameEnd] 更新玩家统计: " + player.user_id);
        updatePlayerStats(settlement, player, request);

        LOG_INFO("[GameEnd] 玩家统计更新完成: " + player.user_id);

        // 2.5 检查成就 - 支持新的 achievement_manager_ 或旧的 achievement_checker_
        if (config_.enable_achievements && (achievement_manager_ || achievement_checker_)) {
            LOG_INFO("[GameEnd] 开始检查成就: " + player.user_id);
            settlement.achievements_unlocked = checkAchievements(settlement, request);
            if (!settlement.achievements_unlocked.empty()) {
                LOG_INFO("[GameEnd] 玩家 " + player.user_id + " 解锁成就: " +
                        std::to_string(settlement.achievements_unlocked.size()) + " 个");
            }
            LOG_INFO("[GameEnd] 成就检查完成: " + player.user_id);
        } else {
            LOG_INFO("[GameEnd] 跳过成就检查: enable_achievements=" + std::string(config_.enable_achievements ? "true" : "false") +
                      ", has_manager=" + std::string(achievement_manager_ ? "true" : "false") +
                      ", has_checker=" + std::string(achievement_checker_ ? "true" : "false"));
        }

        // 2.6 更新排行榜 - 支持新的 leaderboard_manager_ 或旧的 leaderboard_updater_
        if (config_.enable_leaderboard && (leaderboard_manager_ || leaderboard_updater_)) {
            LOG_INFO("[GameEnd] 开始更新排行榜: " + player.user_id);
            updateLeaderboard(player.user_id, settlement.rating_after, settlement.tier_after);
            LOG_INFO("[GameEnd] 排行榜更新完成: " + player.user_id);
        } else {
            LOG_INFO("[GameEnd] 跳过排行榜更新: enable_leaderboard=" + std::string(config_.enable_leaderboard ? "true" : "false") +
                      ", has_manager=" + std::string(leaderboard_manager_ ? "true" : "false") +
                      ", has_updater=" + std::string(leaderboard_updater_ ? "true" : "false"));
        }

        response.settlements.push_back(std::move(settlement));
        LOG_INFO("[GameEnd] 玩家处理完成: " + player.user_id);
    }

    // 3. 保存游戏记录
    LOG_INFO("[GameEnd] 开始保存游戏记录");
    saveGameRecord(request, response);

    response.success = true;
    LOG_INFO("[GameEnd] 游戏结算处理完成: game_id=" + request.game_id);
    return response;
}

void GameEndProcessor::processGameEndAsync(
    const GameEndRequest& request,
    std::function<void(const GameEndResponse&)> callback) {

    if (config_.async_processing) {
        // 异步处理
        std::thread([this, request, callback]() {
            try {
                GameEndResponse response = processGameEnd(request);
                if (callback) {
                    callback(response);
                }
            } catch (const std::exception& e) {
                if (callback) {
                    callback(GameEndResponse::failure(e.what()));
                }
            }
        }).detach();
    } else {
        // 同步处理
        GameEndResponse response = processGameEnd(request);
        if (callback) {
            callback(response);
        }
    }
}

// ==================== 配置 ====================

void GameEndProcessor::updateConfig(const GameEndProcessorConfig& config) {
    config_ = config;

    // 更新子组件配置
    elo_calculator_ = std::make_unique<EloRatingCalculator>(config.elo_config);
    currency_manager_->updateConfig(config.currency_config);
}

// ==================== 内部处理方法 ====================

bool GameEndProcessor::validateRequest(const GameEndRequest& request, std::string& error) {
    // 检查游戏ID
    if (request.game_id.empty()) {
        error = "Game ID is required";
        return false;
    }

    // 检查游戏类型
    if (request.game_type.empty()) {
        error = "Game type is required";
        return false;
    }

    // 检查玩家数量
    if (request.players.empty()) {
        error = "At least one player is required";
        return false;
    }

    // 检查玩家数据
    for (const auto& player : request.players) {
        if (player.user_id.empty()) {
            error = "Player user_id is required";
            return false;
        }

        // 检查评分范围
        if (player.game_data.rating_before < config_.elo_config.min_rating ||
            player.game_data.rating_before > config_.elo_config.max_rating) {
            error = "Player rating out of range: " + player.user_id;
            return false;
        }
    }

    return true;
}

RatingResult GameEndProcessor::calculateRatingChange(
    const GameEndRequest::PlayerResult& player,
    const std::vector<GameEndRequest::PlayerResult>& all_players) {

    // 确定对手评分
    int opponent_rating = determineOpponentRating(player, all_players);

    // 确定得分
    double score = 0.5;  // 默认平局
    switch (player.result) {
        case GameResult::WIN:
            score = 1.0;
            break;
        case GameResult::LOSS:
        case GameResult::SURRENDER:
        case GameResult::TIMEOUT:
        case GameResult::DISCONNECT:
            score = 0.0;
            break;
        case GameResult::DRAW:
            score = 0.5;
            break;
        case GameResult::ABORTED:
            // 游戏中止，不计算评分
            RatingResult no_change;
            no_change.rating_before = player.game_data.rating_before;
            no_change.rating_after = player.game_data.rating_before;
            no_change.rating_change = 0;
            no_change.tier_before = elo_calculator_->getTier(player.game_data.rating_before);
            no_change.tier_after = no_change.tier_before;
            return no_change;
    }

    // 计算 ELO 变化
    return elo_calculator_->calculateNewRating(
        player.game_data.rating_before,
        opponent_rating,
        score,
        player.game_data.games_played
    );
}

Reward GameEndProcessor::calculateTotalReward(
    const GameEndRequest::PlayerResult& player,
    const RatingResult& rating_result,
    const GameEndRequest& request) {

    // 1. 获取基础奖励
    Reward reward = reward_config_->getBaseReward(request.mode, player.result);

    // 2. 设置评分变化
    reward.rating_change = rating_result.rating_change;

    // 3. 应用段位修正
    reward = reward_config_->applyTierModifier(reward, player.game_data.tier_level);

    // 4. 如果是胜利，应用连胜奖励
    if (player.result == GameResult::WIN && player.game_data.win_streak > 0) {
        reward = reward_config_->applyWinStreakBonus(reward, player.game_data.win_streak + 1);
    }

    // 5. 如果是今日首胜，应用首胜奖励
    if (player.result == GameResult::WIN && player.game_data.is_first_win_today) {
        reward = reward_config_->applyFirstWinBonus(reward);
    }

    // 6. 应用每日游戏奖励
    reward = reward_config_->applyDailyGameBonus(reward, player.game_data.games_today);

    // 7. 根据游戏时长调整（短于最小时长减少奖励）
    int min_duration = 60;  // 最小60秒
    if (request.duration_seconds < min_duration && player.result == GameResult::WIN) {
        double duration_factor = static_cast<double>(request.duration_seconds) / min_duration;
        for (auto& currency : reward.currencies) {
            currency.amount = static_cast<int>(currency.amount * duration_factor);
        }
        reward.experience = static_cast<int>(reward.experience * duration_factor);
    }

    return reward;
}

bool GameEndProcessor::grantRewards(
    const std::string& user_id,
    const Reward& reward,
    const std::string& game_id) {

    // 发放货币奖励
    auto results = currency_manager_->grantReward(
        user_id,
        reward,
        "game_end",
        game_id
    );

    // 检查是否所有奖励都成功发放
    for (const auto& result : results) {
        if (!result.success) {
            return false;
        }
    }

    // 发放经验值 - 更新用户档案中的经验
    if (reward.experience > 0 && repository_) {
        try {
            auto profile_opt = repository_->getUserProfile(user_id);
            if (profile_opt.has_value()) {
                auto profile = profile_opt.value();
                profile.experience_points += reward.experience;

                // 简单的等级计算：每1000经验升1级
                int new_level = static_cast<int>(profile.experience_points / 1000) + 1;
                if (new_level > profile.level) {
                    profile.level = new_level;
                }

                repository_->updateUserProfile(profile);
            }
        } catch (const std::exception& e) {
            // 经验值更新失败，但不影响货币奖励
        }
    }

    // 记录奖励日志
    if (repository_ && !reward.currencies.empty()) {
        for (const auto& currency : reward.currencies) {
            RewardLog log;
            log.log_id = "REWARD-" + game_id + "-" + user_id + "-" +
                        std::to_string(static_cast<int>(currency.type));
            log.user_id = user_id;
            log.game_type_id = 1;  // 默认五子棋
            log.game_id = game_id;
            log.reward_type = "game_end";
            log.experience = reward.experience;
            log.rating_change = reward.rating_change;

            switch (currency.type) {
                case CurrencyType::GOLD: log.currency_type = "GOLD"; break;
                case CurrencyType::GEM: log.currency_type = "GEM"; break;
                case CurrencyType::HONOR: log.currency_type = "HONOR"; break;
            }
            log.currency_amount = currency.amount;
            log.reason = "Game end reward";

            repository_->recordRewardLog(log);
        }
    }

    return true;
}

void GameEndProcessor::updatePlayerStats(
    PlayerSettlement& settlement,
    const GameEndRequest::PlayerResult& player,
    const GameEndRequest& request) {

    LOG_INFO("[updatePlayerStats] 开始: " + player.user_id);

    // 更新总场次
    settlement.new_games_played = player.game_data.games_played + 1;

    // 更新今日场次
    settlement.new_games_today = player.game_data.games_today + 1;

    // 计算新连胜
    settlement.new_win_streak = calculateNewStreak(
        player.game_data.win_streak,
        player.result
    );

    // ✅ 计算更新后的胜/负/平场次（用于成就检查）
    int is_win = (player.result == GameResult::WIN) ? 1 : 0;
    int is_loss = (player.result == GameResult::LOSS ||
                   player.result == GameResult::SURRENDER ||
                   player.result == GameResult::TIMEOUT ||
                   player.result == GameResult::DISCONNECT) ? 1 : 0;
    int is_draw = (player.result == GameResult::DRAW) ? 1 : 0;

    // 从 repository 获取当前统计（用于计算更新后的值）
    int current_wins = 0, current_losses = 0, current_draws = 0;
    int current_level = 1, current_best_streak = 0;

    if (repository_) {
        LOG_INFO("[updatePlayerStats] 获取用户档案: " + player.user_id);
        auto profile_opt = repository_->getUserProfile(player.user_id);
        if (profile_opt) {
            current_wins = profile_opt->wins;
            current_losses = profile_opt->losses;
            current_draws = profile_opt->draws;
            current_level = profile_opt->level;
            current_best_streak = profile_opt->best_win_streak;
        }
        LOG_INFO("[updatePlayerStats] 用户档案获取完成: " + player.user_id);
    }

    // 设置更新后的统计数据
    settlement.new_wins = current_wins + is_win;
    settlement.new_losses = current_losses + is_loss;
    settlement.new_draws = current_draws + is_draw;
    settlement.new_best_win_streak = std::max(current_best_streak, settlement.new_win_streak);
    settlement.new_level = current_level;  // 等级在 grantRewards 中可能更新

    // 更新 repository 中的统计数据
    if (repository_) {
        try {
            // 转换游戏结果为字符串
            std::string result_str;
            switch (player.result) {
                case GameResult::WIN: result_str = "win"; break;
                case GameResult::LOSS: result_str = "loss"; break;
                case GameResult::DRAW: result_str = "draw"; break;
                case GameResult::SURRENDER: result_str = "surrender"; break;
                case GameResult::TIMEOUT: result_str = "timeout"; break;
                case GameResult::DISCONNECT: result_str = "disconnect"; break;
                case GameResult::ABORTED: result_str = "aborted"; break;
            }

            // 更新用户评分
            LOG_INFO("[updatePlayerStats] 更新用户评分: " + player.user_id);
            repository_->updateUserRating(
                player.user_id,
                1,  // game_type_id 默认为1（五子棋）
                settlement.rating_after,
                settlement.rating_change
            );

            // 更新用户游戏统计（场次、胜败、时长等）
            LOG_INFO("[updatePlayerStats] 更新游戏统计: " + player.user_id);
            repository_->updateUserGameStats(
                player.user_id,
                1,  // game_type_id
                result_str,
                request.duration_seconds,
                settlement.new_win_streak
            );

            // 更新每日统计
            LOG_INFO("[updatePlayerStats] 更新每日统计: " + player.user_id);
            repository_->incrementUserDailyStats(
                player.user_id,
                1,  // game_type_id
                1,  // games_played
                is_win,
                is_loss,
                is_draw,
                settlement.reward.getTotalGold(),
                settlement.reward.getTotalHonor(),
                settlement.reward.experience,
                settlement.rating_change,
                player.result == GameResult::WIN
            );
            LOG_INFO("[updatePlayerStats] 每日统计更新完成: " + player.user_id);

            // 记录段位变化历史
            if (settlement.tier_changed) {
                LOG_INFO("[updatePlayerStats] 记录段位变化: " + player.user_id);
                TierHistory history;
                history.user_id = player.user_id;
                history.game_type_id = 1;
                history.tier_before = static_cast<int>(settlement.tier_before);
                history.tier_after = static_cast<int>(settlement.tier_after);
                history.rating_before = settlement.rating_before;
                history.rating_after = settlement.rating_after;
                history.is_promotion = settlement.tier_promoted;
                history.game_id = request.game_id;

                repository_->recordTierHistory(history);
            }

        } catch (const std::exception& e) {
            // 统计更新失败，记录错误但不中断流程
            LOG_ERROR("[updatePlayerStats] 异常: " + std::string(e.what()));
        }
    }

    LOG_INFO("[updatePlayerStats] 完成: " + player.user_id);
}

std::vector<std::string> GameEndProcessor::checkAchievements(
    const PlayerSettlement& settlement,
    const GameEndRequest& request) {

    std::vector<std::string> unlocked;

    // 优先使用成就管理器（新接口）
    if (achievement_manager_) {
        // 构建成就检查上下文
        AchievementContext context;
        context.user_id = settlement.user_id;
        context.game_type = request.game_type;

        // ✅ 直接使用 settlement 中的更新后数据，避免从数据库读取旧值
        context.total_games = settlement.new_games_played;
        context.total_wins = settlement.new_wins;
        context.total_losses = settlement.new_losses;
        context.total_draws = settlement.new_draws;
        context.current_win_streak = settlement.new_win_streak;
        context.best_win_streak = settlement.new_best_win_streak;
        context.current_rating = settlement.rating_after;
        context.peak_rating = settlement.rating_after;  // 如果创新高，rating_after 就是新的 peak
        context.level = settlement.new_level;

        // 设置本局游戏信息
        context.last_game_result = settlement.result;
        // 安全获取 metadata 中的值，避免 null 对象调用 value()
        context.last_game_moves = 0;
        if (!request.metadata.is_null() && request.metadata.contains("moves_count")) {
            context.last_game_moves = request.metadata["moves_count"].get<int>();
        }
        context.last_game_duration = request.duration_seconds;

        // 检查是否处于评分劣势（用于隐藏成就）
        for (const auto& player : request.players) {
            if (player.user_id != settlement.user_id) {
                context.opponent_rating = player.game_data.rating_before;
                context.was_underdog = (player.game_data.rating_before - settlement.rating_before) >= 200;
                break;
            }
        }

        // 执行成就检查
        auto results = achievement_manager_->checkAndUnlock(context);
        for (const auto& result : results) {
            unlocked.push_back(result.achievement_id);
        }
    }
    // 兼容旧接口
    else if (achievement_checker_) {
        unlocked = achievement_checker_(settlement.user_id, settlement, request);
    }

    return unlocked;
}

void GameEndProcessor::updateLeaderboard(
    const std::string& user_id,
    int new_rating,
    Tier new_tier) {

    // 优先使用排行榜管理器（新接口）
    if (leaderboard_manager_) {
        LeaderboardUpdateData data;
        data.user_id = user_id;
        data.rating = new_rating;
        data.tier_name = elo_calculator_->getTierName(new_tier);

        // 从 repository 获取更多信息
        if (repository_) {
            auto profile_opt = repository_->getUserProfile(user_id);
            if (profile_opt) {
                data.display_name = profile_opt->display_name;
                data.avatar_url = profile_opt->avatar_url;
                data.win_streak = profile_opt->current_win_streak;
                data.playtime_seconds = 0;  // UserGameProfile doesn't track playtime
                data.experience = profile_opt->experience_points;
                data.total_games = profile_opt->total_games;
                data.wins = profile_opt->wins;

                if (profile_opt->total_games > 0) {
                    data.win_rate = static_cast<float>(profile_opt->wins) / profile_opt->total_games;
                }
            }
        }

        leaderboard_manager_->updateLeaderboard(data);
    }
    // 兼容旧接口
    else if (leaderboard_updater_) {
        leaderboard_updater_(user_id, new_rating, new_tier);
    }
}

bool GameEndProcessor::saveGameRecord(const GameEndRequest& request, const GameEndResponse& response) {
    if (!repository_) {
        return true;  // 没有仓储，直接返回成功
    }

    LOG_INFO("开始保存游戏记录: " + request.game_id);

    try {
        // 1. 生成结算ID
        std::string settlement_id = "SETTLE-" + request.game_id;

        // 2. 构建游戏结算记录
        GameSettlement settlement;
        settlement.settlement_id = settlement_id;
        settlement.game_id = request.game_id;
        settlement.game_type = request.game_type;
        settlement.game_type_id = 1;  // 默认五子棋
        settlement.game_mode = gameModeToString(request.mode);
        settlement.duration_seconds = request.duration_seconds;
        settlement.started_at = request.started_at;
        settlement.ended_at = request.ended_at;
        settlement.player_count = static_cast<int>(request.players.size());

        // 构建结算数据 JSON
        nlohmann::json settlement_data;
        settlement_data["game_id"] = request.game_id;
        settlement_data["game_type"] = request.game_type;
        settlement_data["mode"] = gameModeToString(request.mode);
        settlement_data["duration_seconds"] = request.duration_seconds;
        settlement_data["player_count"] = request.players.size();
        settlement.settlement_data = settlement_data.dump();

        // 3. 保存游戏结算记录
        LOG_INFO("保存游戏结算记录: " + settlement_id);
        if (!repository_->saveGameSettlement(settlement)) {
            LOG_ERROR("保存游戏结算记录失败: " + settlement_id);
            return false;
        }

        // 4. 保存玩家结算记录
        LOG_INFO("构建玩家结算记录，玩家数: " + std::to_string(response.settlements.size()));
        std::vector<GameSettlementPlayer> players;
        for (const auto& s : response.settlements) {
            GameSettlementPlayer player;
            player.settlement_id = settlement_id;
            player.user_id = s.user_id;

            // 转换游戏结果
            switch (s.result) {
                case GameResult::WIN: player.result = "win"; break;
                case GameResult::LOSS: player.result = "loss"; break;
                case GameResult::DRAW: player.result = "draw"; break;
                case GameResult::SURRENDER: player.result = "surrender"; break;
                case GameResult::TIMEOUT: player.result = "timeout"; break;
                case GameResult::DISCONNECT: player.result = "disconnect"; break;
                case GameResult::ABORTED: player.result = "aborted"; break;
            }

            player.rating_before = s.rating_before;
            player.rating_after = s.rating_after;
            player.rating_change = s.rating_change;
            player.tier_before = static_cast<int>(s.tier_before);
            player.tier_after = static_cast<int>(s.tier_after);
            player.tier_changed = s.tier_changed;
            player.gold_earned = s.reward.getTotalGold();
            player.gem_earned = s.reward.getTotalGem();
            player.honor_earned = s.reward.getTotalHonor();
            player.experience_earned = s.reward.experience;

            // 从原始请求数据中获取 win_streak_before
            player.win_streak_before = 0;
            bool was_first_win_today = false;  // 游戏前是否今日首胜状态
            for (const auto& req_player : request.players) {
                if (req_player.user_id == s.user_id) {
                    player.win_streak_before = req_player.game_data.win_streak;
                    was_first_win_today = req_player.game_data.is_first_win_today;
                    break;
                }
            }
            player.win_streak_after = s.new_win_streak;

            // 判断本局是否是今日首胜：
            // 1. 本局胜利
            // 2. 游戏前还没有首胜（is_first_win_today = false，表示今日还没赢过）
            player.is_first_win_today = (s.result == GameResult::WIN && !was_first_win_today);

            // 成就列表转 JSON
            if (!s.achievements_unlocked.empty()) {
                nlohmann::json ach_json = s.achievements_unlocked;
                player.achievements_unlocked = ach_json.dump();
            }

            players.push_back(player);
        }

        // 5. 批量保存玩家记录
        LOG_INFO("保存玩家结算记录，玩家数: " + std::to_string(players.size()));
        bool result = repository_->saveGameSettlementPlayers(players);
        if (result) {
            LOG_INFO("游戏记录保存完成: " + request.game_id);
        } else {
            LOG_ERROR("保存玩家结算记录失败: " + request.game_id);
        }
        return result;

    } catch (const std::exception& e) {
        LOG_ERROR("保存游戏记录异常: " + std::string(e.what()));
        return false;
    }
}

int GameEndProcessor::determineOpponentRating(
    const GameEndRequest::PlayerResult& player,
    const std::vector<GameEndRequest::PlayerResult>& all_players) {

    // 找出所有对手，计算平均评分
    int total_rating = 0;
    int opponent_count = 0;

    for (const auto& p : all_players) {
        if (p.user_id != player.user_id) {
            total_rating += p.game_data.rating_before;
            opponent_count++;
        }
    }

    if (opponent_count == 0) {
        // 没有对手，返回自己的评分（评分不变）
        return player.game_data.rating_before;
    }

    return total_rating / opponent_count;
}

int GameEndProcessor::calculateNewStreak(int current_streak, GameResult result) {
    switch (result) {
        case GameResult::WIN:
            // 胜利增加连胜
            if (current_streak >= 0) {
                return current_streak + 1;  // 继续连胜
            } else {
                return 1;  // 结束连败，开始连胜
            }

        case GameResult::LOSS:
        case GameResult::SURRENDER:
        case GameResult::TIMEOUT:
        case GameResult::DISCONNECT:
            // 失败增加连败
            if (current_streak <= 0) {
                return current_streak - 1;  // 继续连败
            } else {
                return -1;  // 结束连胜，开始连败
            }

        default:
            // 平局或中止，重置连胜/败
            return 0;
    }
}

} // namespace game_service
} // namespace core_services
