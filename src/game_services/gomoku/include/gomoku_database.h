#pragma once

#include "gomoku_types.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include <memory>
#include <vector>
#include <optional>
#include <chrono>

/**
 * @file gomoku_database.h
 * @brief 五子棋游戏数据库访问层
 * @details 提供五子棋游戏数据的持久化功能，包括用户管理、游戏记录、排行榜等
 *          集成统一 Redis Repository 模式进行缓存管理
 * @version 2.0 - 集成统一 Redis Repository 模式
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 用户游戏统计信息
         */
        struct UserGameStats {
            std::string user_id;
            std::string username;
            std::string nickname;
            int current_rating = 1500;
            int peak_rating = 1500;
            int total_games = 0;
            int wins = 0;
            int losses = 0;
            int draws = 0;
            double win_rate = 0.0;
            int total_playtime_minutes = 0;
            std::chrono::system_clock::time_point last_game_at;
            bool is_active = true;
            std::chrono::system_clock::time_point created_at;
            std::chrono::system_clock::time_point updated_at;
        };

        /**
         * @brief 游戏记录信息
         */
        struct GameRecord {
            int64_t game_id = 0;
            std::string room_id;
            std::string black_player_id;
            std::string white_player_id;
            GameMode game_mode = GameMode::FREESTYLE;
            std::string game_type = "casual";

            std::optional<GameResult> game_result;
            std::optional<std::string> winner_id;
            std::optional<std::string> win_type;
            std::optional<std::string> winning_line; // JSON格式

            int total_moves = 0;
            int game_duration_seconds = 0;
            int black_time_used_seconds = 0;
            int white_time_used_seconds = 0;
            double average_time_per_move = 0.0;

            int black_rating_before = 1500;
            int black_rating_after = 1500;
            int white_rating_before = 1500;
            int white_rating_after = 1500;
            int rating_change = 0;

            nlohmann::json game_config;
            std::chrono::system_clock::time_point created_at;
            std::optional<std::chrono::system_clock::time_point> started_at;
            std::optional<std::chrono::system_clock::time_point> finished_at;

            std::string server_id;
            std::string server_version;
        };

        /**
         * @brief 移动记录信息
         */
        struct MoveRecord {
            int64_t move_id = 0;
            int64_t game_id = 0;
            int move_number = 0;
            std::string player_id;
            PieceType piece_type;
            Position position;

            std::chrono::system_clock::time_point move_timestamp;
            double time_used_seconds = 0.0;
            int remaining_time_seconds = 0;

            std::optional<nlohmann::json> board_state;
            bool is_forbidden = false;
            std::optional<std::string> forbidden_reason;

            std::optional<double> position_evaluation;
            bool is_winning_move = false;
            bool is_mistake = false;
        };

        /**
         * @brief 排行榜条目
         */
        struct LeaderboardEntry {
            std::string user_id;
            std::string username;
            std::string nickname;
            GameMode game_mode = GameMode::FREESTYLE;
            int current_rating = 1500;
            int peak_rating = 1500;
            int rank_position = 0;
            int total_games = 0;
            int wins = 0;
            int losses = 0;
            int draws = 0;
            double win_rate = 0.0;
            int consecutive_wins = 0;
            int max_consecutive_wins = 0;
            std::optional<std::chrono::system_clock::time_point> last_game_at;
            std::chrono::system_clock::time_point updated_at;
        };

    } // namespace gomoku
} // namespace game_services

// 在命名空间外部包含 Redis Repository 头文件
#include "gomoku_redis_repository.h"

namespace game_services {
    namespace gomoku {

        /**
         * @brief 五子棋数据库访问类
         * @details 集成 MySQL 和 Redis 进行数据访问和缓存
         */
        class GomokuDatabase {
        public:
            explicit GomokuDatabase(
                std::shared_ptr<common::database::MySQLPool> mysql_pool,
                std::shared_ptr<common::database::RedisPool> redis_pool = nullptr
            );
            ~GomokuDatabase();

            bool initialize();

            // =============================================================================
            // 用户管理
            // =============================================================================

            bool upsertUser(const UserGameStats& user);
            std::optional<UserGameStats> getUser(const std::string& user_id);
            bool updateUserStats(const std::string& user_id, bool is_win, bool is_draw, int rating_change);
            bool updateUserRating(const std::string& user_id, int new_rating);

            // =============================================================================
            // 游戏记录
            // =============================================================================

            int64_t saveGameRecord(const GameRecord& record);
            int64_t createGameRecord(const GameRecord& record);
            std::optional<GameRecord> getGameRecord(int64_t game_id);
            std::optional<GameRecord> getGameRecordByRoomId(const std::string& room_id);
            std::vector<GameRecord> getUserGameHistory(const std::string& user_id, int limit = 50, int offset = 0);
            bool updateGameResult(int64_t game_id, GameResult result,
                                 const std::optional<std::string>& winner_id,
                                 const std::string& win_type,
                                 const std::optional<std::string>& winning_line);
            bool finishGameRecord(int64_t game_id, const GameRecord& record);

            // =============================================================================
            // 排行榜
            // =============================================================================

            std::vector<LeaderboardEntry> getLeaderboard(GameMode game_mode = GameMode::FREESTYLE,
                                                          int limit = 100, int offset = 0);
            bool updateLeaderboard(const std::string& user_id, GameMode game_mode, int rating);
            bool updateLeaderboard(GameMode game_mode);
            int getUserRank(const std::string& user_id, GameMode game_mode);

            // =============================================================================
            // 移动记录
            // =============================================================================

            bool saveMoveRecord(const MoveRecord& move);
            bool addMoveRecord(const MoveRecord& move);
            std::vector<MoveRecord> getGameMoves(int64_t game_id);
            std::vector<MoveRecord> getLastMoves(int64_t game_id, int count = 10);

            // =============================================================================
            // 统计与聊天
            // =============================================================================

            nlohmann::json getServerStatistics();
            nlohmann::json getDailyStatistics(const std::optional<std::string>& date = std::nullopt);
            bool updateDailyStatistics();
            bool saveChatMessage(const std::string& room_id, const std::string& user_id,
                               const std::string& username, const std::string& message,
                               const std::optional<std::string>& message_type = std::nullopt);
            nlohmann::json getRoomChatHistory(const std::string& room_id, int limit = 50);

        private:
            std::shared_ptr<common::database::MySQLPool> mysql_pool_;
            std::shared_ptr<common::database::RedisPool> redis_pool_;
            bool initialized_ = false;

            // Redis Repositories
            std::unique_ptr<UserStatsRedisRepository> user_stats_redis_;
            std::unique_ptr<GameRecordRedisRepository> game_record_redis_;
            std::unique_ptr<LeaderboardRedisRepository> leaderboard_redis_;
            std::unique_ptr<RoomStateRedisRepository> room_state_redis_;

            // 数据库初始化方法
            bool createTables();
            bool createIndexes();
            bool createTriggers();
            bool insertDefaultData();

            // 辅助方法
            std::string generateGameId();
            void initializeRedisRepositories();

            // 类型转换方法
            static GameMode stringToGameMode(const std::string& mode_str);
            static std::string gameModeToString(GameMode mode);
            static GameResult stringToGameResult(const std::string& result_str);
            static std::string gameResultToString(GameResult result);
            static std::string pieceTypeToString(PieceType piece);
            static PieceType stringToPieceType(const std::string& piece_str);
        };

    } // namespace gomoku
} // namespace game_services
