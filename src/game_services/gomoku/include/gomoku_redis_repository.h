#pragma once

#include "common/repository/base_redis_repository.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include "common/logger/logger.h"
#include <memory>

// 注意：此头文件需要在使用前包含 gomoku_database.h
// gomoku_database.h 会在定义类型后包含此文件

namespace game_services {
namespace gomoku {

// 引入 RedisResult 和 RepositoryError 类型别名，方便使用
using common::repository::RedisResult;
using common::repository::RepositoryError;

/**
 * 用户游戏统计 Redis Repository
 * @note 类型 UserGameStats, GameRecord, LeaderboardEntry 定义在 gomoku_database.h 中
 */
class UserStatsRedisRepository : public common::repository::BaseRedisRepository<UserGameStats> {
public:
    using Base = common::repository::BaseRedisRepository<UserGameStats>;

    explicit UserStatsRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::GOMOKU_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_STATS,
               common::repository::RedisTTLConfig::GomokuService::USER_STATS) {}

    ~UserStatsRedisRepository() override = default;

protected:
    nlohmann::json toJson(const UserGameStats& stats) const override {
        nlohmann::json j;
        j["user_id"] = stats.user_id;
        j["username"] = stats.username;
        j["nickname"] = stats.nickname;
        j["current_rating"] = stats.current_rating;
        j["peak_rating"] = stats.peak_rating;
        j["total_games"] = stats.total_games;
        j["wins"] = stats.wins;
        j["losses"] = stats.losses;
        j["draws"] = stats.draws;
        j["win_rate"] = stats.win_rate;
        j["total_playtime_minutes"] = stats.total_playtime_minutes;
        j["is_active"] = stats.is_active;

        if (stats.last_game_at.time_since_epoch().count() > 0) {
            j["last_game_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                stats.last_game_at.time_since_epoch()).count();
        }
        if (stats.created_at.time_since_epoch().count() > 0) {
            j["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                stats.created_at.time_since_epoch()).count();
        }
        if (stats.updated_at.time_since_epoch().count() > 0) {
            j["updated_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                stats.updated_at.time_since_epoch()).count();
        }

        return j;
    }

    std::optional<UserGameStats> fromJson(const nlohmann::json& j) const override {
        try {
            UserGameStats stats;
            if (j.contains("user_id")) stats.user_id = j["user_id"];
            if (j.contains("username")) stats.username = j["username"];
            if (j.contains("nickname")) stats.nickname = j["nickname"];
            if (j.contains("current_rating")) stats.current_rating = j["current_rating"];
            if (j.contains("peak_rating")) stats.peak_rating = j["peak_rating"];
            if (j.contains("total_games")) stats.total_games = j["total_games"];
            if (j.contains("wins")) stats.wins = j["wins"];
            if (j.contains("losses")) stats.losses = j["losses"];
            if (j.contains("draws")) stats.draws = j["draws"];
            if (j.contains("win_rate")) stats.win_rate = j["win_rate"];
            if (j.contains("total_playtime_minutes")) stats.total_playtime_minutes = j["total_playtime_minutes"];
            if (j.contains("is_active")) stats.is_active = j["is_active"];

            if (j.contains("last_game_at")) {
                auto ms = j["last_game_at"].get<int64_t>();
                stats.last_game_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{ms}
                };
            }
            if (j.contains("created_at")) {
                auto ms = j["created_at"].get<int64_t>();
                stats.created_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{ms}
                };
            }
            if (j.contains("updated_at")) {
                auto ms = j["updated_at"].get<int64_t>();
                stats.updated_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{ms}
                };
            }

            return stats;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse UserGameStats from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const std::string& user_id) const override {
        return common::repository::RedisKeyBuilder::GomokuService::userStats(user_id);
    }
};

/**
 * 游戏记录 Redis Repository
 */
class GameRecordRedisRepository : public common::repository::BaseRedisRepository<GameRecord, int64_t> {
public:
    using Base = common::repository::BaseRedisRepository<GameRecord, int64_t>;

    explicit GameRecordRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : Base(redis_pool,
               common::repository::RedisKeyBuilder::GOMOKU_SERVICE,
               common::repository::RedisKeyBuilder::ENTITY_GAME_RECORD,
               common::repository::RedisTTLConfig::GomokuService::GAME_RECORD) {}

    ~GameRecordRedisRepository() override = default;

    /**
     * 通过房间 ID 查找游戏记录
     */
    RedisResult<std::optional<GameRecord>> findByRoomId(const std::string& room_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::gameRecordByRoom(room_id);
            std::string game_id_str = conn->get(key);

            if (game_id_str.empty()) {
                return std::nullopt;
            }

            int64_t game_id = std::stoll(game_id_str);
            return findById(game_id);

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to find game by room id: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 保存游戏记录并创建房间索引
     */
    RedisResult<bool> saveWithIndex(const GameRecord& record) {
        auto save_result = save(record.game_id, record);
        if (!save_result.has_value() || !save_result.value()) {
            return save_result;
        }

        // 创建房间索引
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (conn) {
                std::string key = common::repository::RedisKeyBuilder::GomokuService::gameRecordByRoom(record.room_id);
                conn->set(key, std::to_string(record.game_id),
                         static_cast<int>(common::repository::RedisTTLConfig::GomokuService::GAME_RECORD.count()));
            }
        } catch (const std::exception& e) {
            LOG_WARNING("Failed to create room index for game: " + std::string(e.what()));
        }

        return true;
    }

protected:
    nlohmann::json toJson(const GameRecord& record) const override {
        nlohmann::json j;
        j["game_id"] = record.game_id;
        j["room_id"] = record.room_id;
        j["black_player_id"] = record.black_player_id;
        j["white_player_id"] = record.white_player_id;
        j["game_mode"] = static_cast<int>(record.game_mode);
        j["game_type"] = record.game_type;
        j["total_moves"] = record.total_moves;
        j["game_duration_seconds"] = record.game_duration_seconds;
        j["black_time_used_seconds"] = record.black_time_used_seconds;
        j["white_time_used_seconds"] = record.white_time_used_seconds;
        j["average_time_per_move"] = record.average_time_per_move;
        j["black_rating_before"] = record.black_rating_before;
        j["black_rating_after"] = record.black_rating_after;
        j["white_rating_before"] = record.white_rating_before;
        j["white_rating_after"] = record.white_rating_after;
        j["rating_change"] = record.rating_change;
        j["game_config"] = record.game_config;
        j["server_id"] = record.server_id;
        j["server_version"] = record.server_version;

        if (record.game_result.has_value()) {
            j["game_result"] = static_cast<int>(record.game_result.value());
        }
        if (record.winner_id.has_value()) {
            j["winner_id"] = record.winner_id.value();
        }
        if (record.win_type.has_value()) {
            j["win_type"] = record.win_type.value();
        }
        if (record.winning_line.has_value()) {
            j["winning_line"] = record.winning_line.value();
        }
        if (record.created_at.time_since_epoch().count() > 0) {
            j["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                record.created_at.time_since_epoch()).count();
        }
        if (record.started_at.has_value()) {
            j["started_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                record.started_at->time_since_epoch()).count();
        }
        if (record.finished_at.has_value()) {
            j["finished_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                record.finished_at->time_since_epoch()).count();
        }

        return j;
    }

    std::optional<GameRecord> fromJson(const nlohmann::json& j) const override {
        try {
            GameRecord record;
            if (j.contains("game_id")) record.game_id = j["game_id"];
            if (j.contains("room_id")) record.room_id = j["room_id"];
            if (j.contains("black_player_id")) record.black_player_id = j["black_player_id"];
            if (j.contains("white_player_id")) record.white_player_id = j["white_player_id"];
            if (j.contains("game_mode")) record.game_mode = static_cast<GameMode>(j["game_mode"].get<int>());
            if (j.contains("game_type")) record.game_type = j["game_type"];
            if (j.contains("total_moves")) record.total_moves = j["total_moves"];
            if (j.contains("game_duration_seconds")) record.game_duration_seconds = j["game_duration_seconds"];
            if (j.contains("black_time_used_seconds")) record.black_time_used_seconds = j["black_time_used_seconds"];
            if (j.contains("white_time_used_seconds")) record.white_time_used_seconds = j["white_time_used_seconds"];
            if (j.contains("average_time_per_move")) record.average_time_per_move = j["average_time_per_move"];
            if (j.contains("black_rating_before")) record.black_rating_before = j["black_rating_before"];
            if (j.contains("black_rating_after")) record.black_rating_after = j["black_rating_after"];
            if (j.contains("white_rating_before")) record.white_rating_before = j["white_rating_before"];
            if (j.contains("white_rating_after")) record.white_rating_after = j["white_rating_after"];
            if (j.contains("rating_change")) record.rating_change = j["rating_change"];
            if (j.contains("game_config")) record.game_config = j["game_config"];
            if (j.contains("server_id")) record.server_id = j["server_id"];
            if (j.contains("server_version")) record.server_version = j["server_version"];

            if (j.contains("game_result")) {
                record.game_result = static_cast<GameResult>(j["game_result"].get<int>());
            }
            if (j.contains("winner_id")) {
                record.winner_id = j["winner_id"];
            }
            if (j.contains("win_type")) {
                record.win_type = j["win_type"];
            }
            if (j.contains("winning_line")) {
                record.winning_line = j["winning_line"];
            }
            if (j.contains("created_at")) {
                auto ms = j["created_at"].get<int64_t>();
                record.created_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{ms}
                };
            }
            if (j.contains("started_at")) {
                auto ms = j["started_at"].get<int64_t>();
                record.started_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{ms}
                };
            }
            if (j.contains("finished_at")) {
                auto ms = j["finished_at"].get<int64_t>();
                record.finished_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{ms}
                };
            }

            return record;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse GameRecord from JSON: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string buildKey(const int64_t& game_id) const override {
        return common::repository::RedisKeyBuilder::GomokuService::gameRecord(game_id);
    }
};

/**
 * 排行榜 Redis Repository
 *
 * 使用 Redis Sorted Set (ZSET) 管理排行榜
 */
class LeaderboardRedisRepository {
public:
    explicit LeaderboardRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : redis_pool_(redis_pool) {}

    /**
     * 更新用户积分
     */
    RedisResult<bool> updateScore(const std::string& game_mode,
                                   const std::string& user_id,
                                   double score) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::leaderboard(game_mode);
            bool success = conn->zadd(key, score, user_id);

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to update leaderboard score: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 获取排行榜范围（从高到低）
     */
    RedisResult<std::vector<LeaderboardEntry>> getRange(
        const std::string& game_mode,
        int start,
        int stop
    ) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::leaderboard(game_mode);
            auto members = conn->zrevrange(key, start, stop, true);

            std::vector<LeaderboardEntry> entries;
            int rank = start + 1;
            for (size_t i = 0; i < members.size(); i += 2) {
                LeaderboardEntry entry;
                entry.user_id = members[i];
                entry.game_mode = stringToGameMode(game_mode);
                if (i + 1 < members.size()) {
                    entry.current_rating = static_cast<int>(std::stod(members[i + 1]));
                }
                entry.rank_position = rank++;
                entries.push_back(entry);
            }

            return entries;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get leaderboard range: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 获取用户排名
     */
    RedisResult<int> getUserRank(const std::string& game_mode,
                                  const std::string& user_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::leaderboard(game_mode);

            // ZREVRANK 返回排名（从0开始，分数高的排名靠前）
            // 这里需要使用 zrevrank，但当前 RedisConnection 可能没有实现
            // 简化处理，返回 -1 表示需要从数据库查询
            return -1;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get user rank: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 使排行榜缓存失效
     */
    RedisResult<bool> invalidate(const std::string& game_mode) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::leaderboard(game_mode);
            conn->del(key);

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to invalidate leaderboard: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

private:
    std::shared_ptr<common::database::RedisPool> redis_pool_;

    // 🔧 已移除 getConnection() 方法，所有调用改为使用 RedisConnectionGuard
    // std::shared_ptr<common::database::RedisConnection> getConnection() {
    //     if (!redis_pool_) return nullptr;
    //     return redis_pool_->getConnection();
    // }

    GameMode stringToGameMode(const std::string& mode_str) {
        if (mode_str == "standard") return GameMode::FREESTYLE;
        if (mode_str == "renju") return GameMode::RENJU;
        return GameMode::FREESTYLE;
    }
};

/**
 * 房间状态 Redis Repository
 */
class RoomStateRedisRepository {
public:
    explicit RoomStateRedisRepository(std::shared_ptr<common::database::RedisPool> redis_pool)
        : redis_pool_(redis_pool) {}

    /**
     * 保存房间状态
     */
    RedisResult<bool> saveRoomState(const std::string& room_id,
                                     const nlohmann::json& state) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::roomState(room_id);
            std::string value = state.dump();

            bool success = conn->set(key, value,
                static_cast<int>(common::repository::RedisTTLConfig::GomokuService::ROOM_STATE.count()));

            return success;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save room state: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 获取房间状态
     */
    RedisResult<std::optional<nlohmann::json>> getRoomState(const std::string& room_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::roomState(room_id);
            std::string value = conn->get(key);

            if (value.empty()) {
                return std::nullopt;
            }

            return nlohmann::json::parse(value);

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get room state: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 删除房间状态
     */
    RedisResult<bool> deleteRoomState(const std::string& room_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::roomState(room_id);
            conn->del(key);

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to delete room state: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

    /**
     * 刷新房间状态 TTL
     */
    RedisResult<bool> refreshTTL(const std::string& room_id) {
        try {
            // 🔧 RAII修复：使用连接守护确保连接自动归还
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn) {
                return common::utils::unexpected(common::repository::RepositoryError::connectionFailed(
                    "Failed to get Redis connection"));
            }

            std::string key = common::repository::RedisKeyBuilder::GomokuService::roomState(room_id);
            conn->expire(key, static_cast<int>(
                common::repository::RedisTTLConfig::GomokuService::ROOM_STATE.count()));

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to refresh room TTL: " + std::string(e.what()));
            return common::utils::unexpected(common::repository::RepositoryError::cacheError(e.what()));
        }
    }

private:
    std::shared_ptr<common::database::RedisPool> redis_pool_;

    // 🔧 已移除 getConnection() 方法，所有调用改为使用 RedisConnectionGuard
    // std::shared_ptr<common::database::RedisConnection> getConnection() {
    //     if (!redis_pool_) return nullptr;
    //     return redis_pool_->getConnection();
    // }
};

} // namespace gomoku
} // namespace game_services
