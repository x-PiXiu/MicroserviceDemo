/**
 * @file replay_manager.h
 * @brief 游戏回放管理器
 * @details 管理游戏回放的存储、检索、播放和分享
 * @author Game Server Team
 * @date 2026-02-23
 * @version 1.0.0
 */

#pragma once

#include "game_plugin.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <chrono>
#include <optional>
#include <atomic>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

// 前向声明
class GameRepository;

/**
 * @brief 回放存储配置
 */
struct ReplayStorageConfig {
    bool enabled = true;
    int max_replays_per_user = 100;     // 每个用户最多保存的回放数
    int max_replay_age_days = 30;       // 回放最长保留天数
    int64_t max_replay_size_bytes = 10485760;  // 单个回放最大大小（10MB）
    bool compress_replays = true;       // 是否压缩回放
    std::string storage_path = "data/replays";  // 存储路径

    nlohmann::json toJson() const {
        return {
            {"enabled", enabled},
            {"max_replays_per_user", max_replays_per_user},
            {"max_replay_age_days", max_replay_age_days},
            {"max_replay_size_bytes", max_replay_size_bytes},
            {"compress_replays", compress_replays},
            {"storage_path", storage_path}
        };
    }
};

/**
 * @brief 回放摘要（用于列表展示）
 */
struct ReplaySummary {
    std::string replay_id;
    std::string game_id;
    std::string game_type;
    std::chrono::system_clock::time_point played_at;
    int duration_seconds = 0;
    std::vector<std::string> player_ids;
    std::vector<std::string> player_names;
    std::string winner_id;
    int result = 0;  // GameResult

    // 观看统计
    int view_count = 0;
    int like_count = 0;
    bool is_public = false;

    nlohmann::json toJson() const {
        return {
            {"replay_id", replay_id},
            {"game_id", game_id},
            {"game_type", game_type},
            {"played_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                played_at.time_since_epoch()).count()},
            {"duration_seconds", duration_seconds},
            {"player_ids", player_ids},
            {"player_names", player_names},
            {"winner_id", winner_id},
            {"result", result},
            {"view_count", view_count},
            {"like_count", like_count},
            {"is_public", is_public}
        };
    }
};

/**
 * @brief 回放播放状态
 */
enum class PlaybackState {
    STOPPED,    // 已停止
    PLAYING,    // 播放中
    PAUSED,     // 已暂停
    SEEKING     // 跳转中
};

/**
 * @brief 回放播放器
 */
class ReplayPlayer {
public:
    explicit ReplayPlayer(const game_services::GameReplay& replay);

    /**
     * @brief 开始播放
     * @param speed 播放速度（1.0为正常）
     */
    void play(double speed = 1.0);

    /**
     * @brief 暂停播放
     */
    void pause();

    /**
     * @brief 停止播放
     */
    void stop();

    /**
     * @brief 跳转到指定动作
     * @param action_index 动作索引
     */
    void seekTo(size_t action_index);

    /**
     * @brief 跳转到指定时间
     * @param seconds 秒数
     */
    void seekToTime(int seconds);

    /**
     * @brief 下一个动作
     */
    void nextAction();

    /**
     * @brief 上一个动作
     */
    void previousAction();

    /**
     * @brief 获取当前播放状态
     */
    PlaybackState getState() const { return state_; }

    /**
     * @brief 获取当前动作索引
     */
    size_t getCurrentActionIndex() const { return current_action_; }

    /**
     * @brief 获取总动作数
     */
    size_t getTotalActions() const { return replay_.actions.size(); }

    /**
     * @brief 获取当前时间（秒）
     */
    int getCurrentTime() const;

    /**
     * @brief 获取总时长（秒）
     */
    int getTotalDuration() const { return replay_.duration_seconds; }

    /**
     * @brief 获取当前游戏状态
     */
    nlohmann::json getCurrentState() const { return current_state_; }

    /**
     * @brief 获取回放数据
     */
    const game_services::GameReplay& getReplay() const { return replay_; }

    /**
     * @brief 设置状态变更回调
     */
    void setStateChangeCallback(std::function<void(const nlohmann::json&)> callback) {
        state_callback_ = std::move(callback);
    }

    /**
     * @brief 设置动作回调
     */
    void setActionCallback(std::function<void(const game_services::GameAction&)> callback) {
        action_callback_ = std::move(callback);
    }

private:
    game_services::GameReplay replay_;
    PlaybackState state_ = PlaybackState::STOPPED;
    size_t current_action_ = 0;
    double speed_ = 1.0;
    nlohmann::json current_state_;

    std::function<void(const nlohmann::json&)> state_callback_;
    std::function<void(const game_services::GameAction&)> action_callback_;

    void applyAction(const game_services::GameAction& action);
};

/**
 * @brief 游戏回放管理器
 */
class ReplayManager {
public:
    /**
     * @brief 构造函数
     */
    ReplayManager(
        std::shared_ptr<GameRepository> repository,
        const ReplayStorageConfig& config = ReplayStorageConfig());

    ~ReplayManager();

    /**
     * @brief 初始化
     */
    bool initialize();

    // ========== 回放录制 ==========

    /**
     * @brief 开始录制回放
     * @param game_id 游戏ID
     * @param game_type 游戏类型
     * @param players 玩家列表
     * @param initial_state 初始状态
     * @return 回放ID
     */
    std::string startRecording(
        const std::string& game_id,
        const std::string& game_type,
        const std::vector<game_services::PlayerInfo>& players,
        const nlohmann::json& initial_state);

    /**
     * @brief 记录游戏动作
     * @param game_id 游戏ID
     * @param action 游戏动作
     */
    void recordAction(const std::string& game_id, const game_services::GameAction& action);

    /**
     * @brief 结束录制
     * @param game_id 游戏ID
     * @param winner_id 胜者ID
     * @param result 游戏结果
     * @param final_state 最终状态
     * @return 回放数据
     */
    game_services::GameReplay stopRecording(
        const std::string& game_id,
        const std::string& winner_id,
        game_services::GameResult result,
        const nlohmann::json& final_state);

    /**
     * @brief 取消录制
     * @param game_id 游戏ID
     */
    void cancelRecording(const std::string& game_id);

    // ========== 回放存储 ==========

    /**
     * @brief 保存回放
     * @param replay 回放数据
     * @return 是否成功
     */
    bool saveReplay(const game_services::GameReplay& replay);

    /**
     * @brief 删除回放
     * @param replay_id 回放ID
     * @return 是否成功
     */
    bool deleteReplay(const std::string& replay_id);

    /**
     * @brief 获取回放
     * @param replay_id 回放ID
     * @return 回放数据
     */
    std::optional<game_services::GameReplay> getReplay(const std::string& replay_id);

    // ========== 回放查询 ==========

    /**
     * @brief 获取用户回放列表
     * @param user_id 用户ID
     * @param limit 数量限制
     * @return 回放摘要列表
     */
    std::vector<ReplaySummary> getUserReplays(
        const std::string& user_id,
        int limit = 50);

    /**
     * @brief 获取游戏回放列表
     * @param game_type 游戏类型
     * @param limit 数量限制
     * @return 回放摘要列表
     */
    std::vector<ReplaySummary> getGameReplays(
        const std::string& game_type,
        int limit = 50);

    /**
     * @brief 获取热门回放
     * @param limit 数量限制
     * @return 回放摘要列表
     */
    std::vector<ReplaySummary> getPopularReplays(int limit = 50);

    /**
     * @brief 搜索回放
     * @param query 查询条件
     * @param limit 数量限制
     * @return 回放摘要列表
     */
    std::vector<ReplaySummary> searchReplays(
        const nlohmann::json& query,
        int limit = 50);

    // ========== 回放播放 ==========

    /**
     * @brief 创建回放播放器
     * @param replay_id 回放ID
     * @return 播放器实例
     */
    std::unique_ptr<ReplayPlayer> createPlayer(const std::string& replay_id);

    // ========== 回放分享 ==========

    /**
     * @brief 设置回放公开状态
     * @param replay_id 回放ID
     * @param is_public 是否公开
     * @return 是否成功
     */
    bool setReplayPublic(const std::string& replay_id, bool is_public);

    /**
     * @brief 生成分享链接
     * @param replay_id 回放ID
     * @return 分享链接
     */
    std::string generateShareLink(const std::string& replay_id);

    /**
     * @brief 记录观看
     * @param replay_id 回放ID
     * @param viewer_id 观看者ID
     */
    void recordView(const std::string& replay_id, const std::string& viewer_id);

    /**
     * @brief 点赞/取消点赞
     * @param replay_id 回放ID
     * @param user_id 用户ID
     * @param like 是否点赞
     * @return 当前点赞数
     */
    int toggleLike(const std::string& replay_id, const std::string& user_id, bool like);

    // ========== 维护操作 ==========

    /**
     * @brief 清理过期回放
     */
    void cleanupExpiredReplays();

    /**
     * @brief 获取存储统计
     */
    nlohmann::json getStorageStats() const;

private:
    std::shared_ptr<GameRepository> repository_;
    ReplayStorageConfig config_;

    // 正在录制的回放
    mutable std::mutex recording_mutex_;
    std::unordered_map<std::string, game_services::GameReplay> active_recordings_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> recording_start_times_;

    // 统计
    std::atomic<int64_t> total_recordings_{0};
    std::atomic<int64_t> total_saved_{0};
    std::atomic<int64_t> total_deleted_{0};
    std::atomic<int64_t> total_views_{0};

    /**
     * @brief 生成回放ID
     */
    std::string generateReplayId();

    /**
     * @brief 压缩回放数据
     */
    std::string compressReplay(const game_services::GameReplay& replay);

    /**
     * @brief 解压回放数据
     */
    game_services::GameReplay decompressReplay(const std::string& data);
};

} // namespace game_service
} // namespace core_services
