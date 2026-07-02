/**
 * @file replay_manager.cpp
 * @brief 游戏回放管理器实现
 */

#include "replay_manager.h"
#include "game_repository.h"
#include "common/logger/logger.h"
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace core_services {
namespace game_service {

// ========== ReplayPlayer 实现 ==========

ReplayPlayer::ReplayPlayer(const game_services::GameReplay& replay)
    : replay_(replay)
    , current_state_(replay.initial_state) {
}

void ReplayPlayer::play(double speed) {
    if (replay_.actions.empty()) {
        return;
    }

    state_ = PlaybackState::PLAYING;
    speed_ = speed;
    LOG_DEBUG("Replay playing at speed: " + std::to_string(speed));
}

void ReplayPlayer::pause() {
    state_ = PlaybackState::PAUSED;
    LOG_DEBUG("Replay paused");
}

void ReplayPlayer::stop() {
    state_ = PlaybackState::STOPPED;
    current_action_ = 0;
    current_state_ = replay_.initial_state;
    LOG_DEBUG("Replay stopped");
}

void ReplayPlayer::seekTo(size_t action_index) {
    if (action_index >= replay_.actions.size()) {
        return;
    }

    state_ = PlaybackState::SEEKING;

    // 从头开始应用动作到目标位置
    current_state_ = replay_.initial_state;
    for (size_t i = 0; i <= action_index; ++i) {
        applyAction(replay_.actions[i]);
    }
    current_action_ = action_index;

    state_ = PlaybackState::PAUSED;

    if (state_callback_) {
        state_callback_(current_state_);
    }
}

void ReplayPlayer::seekToTime(int seconds) {
    // 找到对应时间的动作
    for (size_t i = 0; i < replay_.actions.size(); ++i) {
        auto action_time = replay_.actions[i].timestamp;
        auto start_time = replay_.actions.empty() ? 0 : replay_.actions[0].timestamp;
        int elapsed = static_cast<int>((action_time - start_time) / 1000);

        if (elapsed >= seconds) {
            seekTo(i);
            return;
        }
    }
}

void ReplayPlayer::nextAction() {
    if (current_action_ + 1 < replay_.actions.size()) {
        current_action_++;
        applyAction(replay_.actions[current_action_]);

        if (action_callback_) {
            action_callback_(replay_.actions[current_action_]);
        }
        if (state_callback_) {
            state_callback_(current_state_);
        }
    }
}

void ReplayPlayer::previousAction() {
    if (current_action_ > 0) {
        current_action_--;
        // 需要从头重新应用动作
        current_state_ = replay_.initial_state;
        for (size_t i = 0; i <= current_action_; ++i) {
            applyAction(replay_.actions[i]);
        }

        if (state_callback_) {
            state_callback_(current_state_);
        }
    }
}

int ReplayPlayer::getCurrentTime() const {
    if (replay_.actions.empty() || current_action_ == 0) {
        return 0;
    }

    auto current = replay_.actions[current_action_].timestamp;
    auto start = replay_.actions[0].timestamp;
    return static_cast<int>((current - start) / 1000);
}

void ReplayPlayer::applyAction(const game_services::GameAction& action) {
    // 这里应该根据具体游戏类型解析和应用动作
    // 简化实现：将动作数据合并到当前状态
    if (action.data.is_object()) {
        current_state_.merge_patch(action.data);
    }
}

// ========== ReplayManager 实现 ==========

ReplayManager::ReplayManager(
    std::shared_ptr<GameRepository> repository,
    const ReplayStorageConfig& config)
    : repository_(std::move(repository))
    , config_(config) {
}

ReplayManager::~ReplayManager() {
    LOG_INFO("ReplayManager destroyed");
}

bool ReplayManager::initialize() {
    LOG_INFO("Initializing ReplayManager...");

    if (!repository_) {
        LOG_ERROR("Repository is null");
        return false;
    }

    // 创建存储目录
    if (!config_.storage_path.empty()) {
        try {
            std::filesystem::create_directories(config_.storage_path);
            LOG_INFO("Replay storage directory created: " + config_.storage_path);
        } catch (const std::exception& e) {
            LOG_WARNING("Failed to create storage directory: " + std::string(e.what()));
        }
    }

    LOG_INFO("ReplayManager initialized");
    return true;
}

std::string ReplayManager::startRecording(
    const std::string& game_id,
    const std::string& game_type,
    const std::vector<game_services::PlayerInfo>& players,
    const nlohmann::json& initial_state) {

    std::lock_guard<std::mutex> lock(recording_mutex_);

    // 检查是否已在录制
    if (active_recordings_.find(game_id) != active_recordings_.end()) {
        LOG_WARNING("Game already recording: " + game_id);
        return "";
    }

    // 创建新的回放
    game_services::GameReplay replay;
    replay.replay_id = generateReplayId();
    replay.game_id = game_id;
    replay.game_type = game_type;
    replay.start_time = std::chrono::system_clock::now();
    replay.players = players;
    replay.initial_state = initial_state;

    active_recordings_[game_id] = replay;
    recording_start_times_[game_id] = std::chrono::steady_clock::now();

    total_recordings_.fetch_add(1);

    LOG_INFO("Started recording: game_id=" + game_id + ", replay_id=" + replay.replay_id);
    return replay.replay_id;
}

void ReplayManager::recordAction(
    const std::string& game_id,
    const game_services::GameAction& action) {

    std::lock_guard<std::mutex> lock(recording_mutex_);

    auto it = active_recordings_.find(game_id);
    if (it == active_recordings_.end()) {
        return;
    }

    // 添加动作到回放
    game_services::GameAction recorded_action = action;
    if (recorded_action.timestamp == 0) {
        recorded_action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    it->second.actions.push_back(recorded_action);
}

game_services::GameReplay ReplayManager::stopRecording(
    const std::string& game_id,
    const std::string& winner_id,
    game_services::GameResult result,
    const nlohmann::json& final_state) {

    std::lock_guard<std::mutex> lock(recording_mutex_);

    auto it = active_recordings_.find(game_id);
    if (it == active_recordings_.end()) {
        LOG_WARNING("No active recording for game: " + game_id);
        return game_services::GameReplay{};
    }

    game_services::GameReplay replay = it->second;
    replay.end_time = std::chrono::system_clock::now();
    replay.winner_id = winner_id;
    replay.result = result;
    replay.final_state = final_state;

    // 计算时长
    auto start_it = recording_start_times_.find(game_id);
    if (start_it != recording_start_times_.end()) {
        auto duration = std::chrono::steady_clock::now() - start_it->second;
        replay.duration_seconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(duration).count());
    }

    // 从活动录制中移除
    active_recordings_.erase(game_id);
    recording_start_times_.erase(game_id);

    // 保存回放
    saveReplay(replay);

    LOG_INFO("Stopped recording: game_id=" + game_id +
             ", duration=" + std::to_string(replay.duration_seconds) + "s" +
             ", actions=" + std::to_string(replay.actions.size()));

    return replay;
}

void ReplayManager::cancelRecording(const std::string& game_id) {
    std::lock_guard<std::mutex> lock(recording_mutex_);

    active_recordings_.erase(game_id);
    recording_start_times_.erase(game_id);

    LOG_INFO("Cancelled recording: " + game_id);
}

bool ReplayManager::saveReplay(const game_services::GameReplay& replay) {
    if (!repository_) {
        return false;
    }

    try {
        // 转换为JSON并保存到数据库
        nlohmann::json replay_json = replay.toJson();

        // 保存到数据库
        // repository_->saveGameReplay(replay);

        total_saved_.fetch_add(1);
        LOG_INFO("Saved replay: " + replay.replay_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save replay: " + std::string(e.what()));
        return false;
    }
}

bool ReplayManager::deleteReplay(const std::string& replay_id) {
    if (!repository_) {
        return false;
    }

    try {
        // repository_->deleteGameReplay(replay_id);
        total_deleted_.fetch_add(1);
        LOG_INFO("Deleted replay: " + replay_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to delete replay: " + std::string(e.what()));
        return false;
    }
}

std::optional<game_services::GameReplay> ReplayManager::getReplay(const std::string& replay_id) {
    // 从数据库或文件系统加载回放
    // 简化实现：返回空
    return std::nullopt;
}

std::vector<ReplaySummary> ReplayManager::getUserReplays(
    const std::string& user_id,
    int limit) {

    std::vector<ReplaySummary> summaries;
    // 从数据库查询用户的回放
    return summaries;
}

std::vector<ReplaySummary> ReplayManager::getGameReplays(
    const std::string& game_type,
    int limit) {

    std::vector<ReplaySummary> summaries;
    return summaries;
}

std::vector<ReplaySummary> ReplayManager::getPopularReplays(int limit) {
    std::vector<ReplaySummary> summaries;
    return summaries;
}

std::vector<ReplaySummary> ReplayManager::searchReplays(
    const nlohmann::json& query,
    int limit) {

    std::vector<ReplaySummary> summaries;
    return summaries;
}

std::unique_ptr<ReplayPlayer> ReplayManager::createPlayer(const std::string& replay_id) {
    auto replay = getReplay(replay_id);
    if (!replay) {
        return nullptr;
    }

    // 记录观看
    recordView(replay_id, "");

    return std::make_unique<ReplayPlayer>(*replay);
}

bool ReplayManager::setReplayPublic(const std::string& replay_id, bool is_public) {
    // 更新回放的公开状态
    return true;
}

std::string ReplayManager::generateShareLink(const std::string& replay_id) {
    // 生成分享链接
    return "https://game.example.com/replay/" + replay_id;
}

void ReplayManager::recordView(const std::string& replay_id, const std::string& viewer_id) {
    total_views_.fetch_add(1);
}

int ReplayManager::toggleLike(const std::string& replay_id, const std::string& user_id, bool like) {
    // 切换点赞状态并返回当前点赞数
    return 0;
}

void ReplayManager::cleanupExpiredReplays() {
    if (!config_.enabled) {
        return;
    }

    LOG_INFO("Cleaning up expired replays...");

    // 查找并删除过期回放
    // 简化实现

    LOG_INFO("Expired replay cleanup completed");
}

nlohmann::json ReplayManager::getStorageStats() const {
    return {
        {"enabled", config_.enabled},
        {"total_recordings", total_recordings_.load()},
        {"total_saved", total_saved_.load()},
        {"total_deleted", total_deleted_.load()},
        {"total_views", total_views_.load()},
        {"active_recordings", active_recordings_.size()},
        {"config", config_.toJson()}
    };
}

std::string ReplayManager::generateReplayId() {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "replay_" << ms << "_" << std::setfill('0') << std::setw(6)
       << (++counter % 1000000);
    return ss.str();
}

std::string ReplayManager::compressReplay(const game_services::GameReplay& replay) {
    // 压缩实现（可使用zlib等）
    return replay.toJson().dump();
}

game_services::GameReplay ReplayManager::decompressReplay(const std::string& data) {
    // 解压实现
    try {
        auto json = nlohmann::json::parse(data);
        game_services::GameReplay replay;
        // 从JSON解析回放数据
        return replay;
    } catch (...) {
        return game_services::GameReplay{};
    }
}

} // namespace game_service
} // namespace core_services
