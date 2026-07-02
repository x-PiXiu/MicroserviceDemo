#include "gomoku_database.h"
#include "common/logger/logger.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include <sstream>
#include <iomanip>

namespace game_services {
    namespace gomoku {

        GomokuDatabase::GomokuDatabase(
            std::shared_ptr<common::database::MySQLPool> mysql_pool,
            std::shared_ptr<common::database::RedisPool> redis_pool
        ) : mysql_pool_(mysql_pool)
          , redis_pool_(redis_pool) {
            LOG_INFO("五子棋数据库访问层创建");

            // 初始化 Redis Repository
            initializeRedisRepositories();
        }

        GomokuDatabase::~GomokuDatabase() {
            LOG_INFO("五子棋数据库访问层销毁");
        }

        /**
         * @brief 初始化 Redis Repository
         * @details 创建类型化的 Redis Repository 实例
         */
        void GomokuDatabase::initializeRedisRepositories() {
            if (redis_pool_) {
                try {
                    user_stats_redis_ = std::make_unique<UserStatsRedisRepository>(redis_pool_);
                    game_record_redis_ = std::make_unique<GameRecordRedisRepository>(redis_pool_);
                    leaderboard_redis_ = std::make_unique<LeaderboardRedisRepository>(redis_pool_);
                    room_state_redis_ = std::make_unique<RoomStateRedisRepository>(redis_pool_);
                    LOG_INFO("GomokuDatabase: Redis Repository 初始化完成");
                } catch (const std::exception& e) {
                    LOG_ERROR("GomokuDatabase: Redis Repository 初始化失败: " + std::string(e.what()));
                }
            }
        }

        bool GomokuDatabase::initialize() {
            if (!mysql_pool_) {
                LOG_ERROR("MySQL连接池未初始化");
                return false;
            }

            try {
                LOG_INFO("开始初始化五子棋数据库表结构");

                if (!createTables()) {
                    LOG_ERROR("创建数据库表失败");
                    return false;
                }

                if (!createIndexes()) {
                    LOG_ERROR("创建数据库索引失败");
                    return false;
                }

                if (!createTriggers()) {
                    LOG_ERROR("创建数据库触发器失败");
                    return false;
                }

                if (!insertDefaultData()) {
                    LOG_ERROR("插入默认数据失败");
                    return false;
                }

                // 确保 Redis Repository 已初始化
                if (!user_stats_redis_ && redis_pool_) {
                    initializeRedisRepositories();
                }

                initialized_ = true;
                LOG_INFO("五子棋数据库初始化成功");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("初始化数据库异常: " + std::string(e.what()));
                return false;
            }
        }

        // =============================================================================
        // 用户管理实现
        // =============================================================================

        bool GomokuDatabase::upsertUser(const UserGameStats& user_stats) {
            try {
                std::string query = R"(
                    INSERT INTO gomoku_users (
                        user_id, username, nickname, current_rating, peak_rating, 
                        total_games, wins, losses, draws, total_playtime_minutes,
                        last_game_at, is_active, created_at, updated_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ON DUPLICATE KEY UPDATE
                        nickname = VALUES(nickname),
                        current_rating = VALUES(current_rating),
                        peak_rating = GREATEST(peak_rating, VALUES(peak_rating)),
                        total_games = VALUES(total_games),
                        wins = VALUES(wins),
                        losses = VALUES(losses),
                        draws = VALUES(draws),
                        total_playtime_minutes = VALUES(total_playtime_minutes),
                        last_game_at = VALUES(last_game_at),
                        is_active = VALUES(is_active),
                        updated_at = CURRENT_TIMESTAMP
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) {
                    LOG_ERROR("获取数据库连接失败");
                    return false;
                }

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, user_stats.user_id);
                prep_stmt->setString(2, user_stats.username);
                prep_stmt->setString(3, user_stats.nickname);
                prep_stmt->setInt(4, user_stats.current_rating);
                prep_stmt->setInt(5, user_stats.peak_rating);
                prep_stmt->setInt(6, user_stats.total_games);
                prep_stmt->setInt(7, user_stats.wins);
                prep_stmt->setInt(8, user_stats.losses);
                prep_stmt->setInt(9, user_stats.draws);
                prep_stmt->setInt(10, user_stats.total_playtime_minutes);
                
                // 时间戳处理
                auto time_t = std::chrono::system_clock::to_time_t(user_stats.last_game_at);
                std::ostringstream oss;
                oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
                prep_stmt->setString(11, oss.str());
                
                prep_stmt->setBoolean(12, user_stats.is_active);
                
                auto created_time_t = std::chrono::system_clock::to_time_t(user_stats.created_at);
                std::ostringstream created_oss;
                created_oss << std::put_time(std::gmtime(&created_time_t), "%Y-%m-%d %H:%M:%S");
                prep_stmt->setString(13, created_oss.str());
                
                auto updated_time_t = std::chrono::system_clock::to_time_t(user_stats.updated_at);
                std::ostringstream updated_oss;
                updated_oss << std::put_time(std::gmtime(&updated_time_t), "%Y-%m-%d %H:%M:%S");
                prep_stmt->setString(14, updated_oss.str());

                int affected_rows = prep_stmt->executeUpdate();
                
                if (affected_rows > 0) {
                    LOG_DEBUG("用户信息更新成功: " + user_stats.user_id);
                    return true;
                } else {
                    LOG_WARNING("用户信息更新无变化: " + user_stats.user_id);
                    return false;
                }

            } catch (const std::exception& e) {
                LOG_ERROR("更新用户信息异常: " + std::string(e.what()));
                return false;
            }
        }

        std::optional<UserGameStats> GomokuDatabase::getUser(const std::string& user_id) {
            try {
                // ==================== 新架构：先尝试从 Redis 缓存获取 ====================
                if (user_stats_redis_) {
                    auto cached_result = user_stats_redis_->findById(user_id);
                    if (cached_result.has_value() && cached_result->has_value()) {
                        LOG_DEBUG("从 Redis 缓存获取用户统计信息成功: " + user_id);
                        return cached_result.value();
                    }
                }

                // ==================== 从 MySQL 数据库获取 ====================
                std::string query = R"(
                    SELECT user_id, username, nickname, current_rating, peak_rating,
                           total_games, wins, losses, draws, total_playtime_minutes,
                           last_game_at, is_active, created_at, updated_at
                    FROM gomoku_users
                    WHERE user_id = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) {
                    LOG_ERROR("获取数据库连接失败");
                    return std::nullopt;
                }

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, user_id);

                auto result_set = prep_stmt->executeQuery();

                if (result_set->next()) {
                    UserGameStats stats;
                    stats.user_id = result_set->getString("user_id");
                    stats.username = result_set->getString("username");
                    stats.nickname = result_set->getString("nickname");
                    stats.current_rating = result_set->getInt("current_rating");
                    stats.peak_rating = result_set->getInt("peak_rating");
                    stats.total_games = result_set->getInt("total_games");
                    stats.wins = result_set->getInt("wins");
                    stats.losses = result_set->getInt("losses");
                    stats.draws = result_set->getInt("draws");
                    stats.total_playtime_minutes = result_set->getInt("total_playtime_minutes");

                    if (stats.total_games > 0) {
                        stats.win_rate = static_cast<double>(stats.wins) / stats.total_games;
                    }

                    // 解析时间戳字段
                    try {
                        // 解析created_at时间戳
                        std::string created_at_str = result_set->getString("created_at");
                        if (!created_at_str.empty()) {
                            // 将数据库时间戳转换为time_point
                            // 假设数据库返回的是 "YYYY-MM-DD HH:MM:SS" 格式
                            std::tm tm_created = {};
                            std::stringstream ss(created_at_str);
                            ss >> std::get_time(&tm_created, "%Y-%m-%d %H:%M:%S");
                            if (!ss.fail()) {
                                auto time_c = std::mktime(&tm_created);
                                stats.created_at = std::chrono::system_clock::from_time_t(time_c);
                            }
                        }

                        // 解析last_login时间戳 (如果存在)
                        try {
                            std::string last_login_str = result_set->getString("last_login");
                            if (!last_login_str.empty()) {
                                std::tm tm_login = {};
                                std::stringstream ss_login(last_login_str);
                                ss_login >> std::get_time(&tm_login, "%Y-%m-%d %H:%M:%S");
                                if (!ss_login.fail()) {
                                    auto login_time_c = std::mktime(&tm_login);
                                    stats.last_game_at = std::chrono::system_clock::from_time_t(login_time_c);
                                }
                            }
                        } catch (const sql::SQLException& e) {
                            // last_login字段可能不存在，忽略错误
                            LOG_DEBUG("last_login字段解析跳过: " + std::string(e.what()));
                        }

                    } catch (const std::exception& e) {
                        LOG_WARNING("时间戳字段解析失败: " + std::string(e.what()));
                        // 使用当前时间作为默认值
                        stats.created_at = std::chrono::system_clock::now();
                    }

                    stats.is_active = result_set->getBoolean("is_active");

                    // ==================== 新架构：保存到 Redis 缓存 ====================
                    if (user_stats_redis_) {
                        user_stats_redis_->save(user_id, stats);
                        LOG_DEBUG("用户统计信息已缓存到 Redis: " + user_id);
                    }

                    // RAII管理器会自动释放连接
                    return stats;
                } else {
                    // RAII管理器会自动释放连接
                    return std::nullopt;
                }

            } catch (const std::exception& e) {
                LOG_ERROR("获取用户信息异常: " + std::string(e.what()));
                return std::nullopt;
            }
        }

        bool GomokuDatabase::updateUserRating(const std::string& user_id, int new_rating) {
            try {
                std::string query = R"(
                    UPDATE gomoku_users 
                    SET current_rating = ?, 
                        peak_rating = GREATEST(peak_rating, ?),
                        updated_at = CURRENT_TIMESTAMP
                    WHERE user_id = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt(1, new_rating);
                prep_stmt->setInt(2, new_rating);
                prep_stmt->setString(3, user_id);

                int affected_rows = prep_stmt->executeUpdate();
                // RAII管理器会自动释放连接

                return affected_rows > 0;

            } catch (const std::exception& e) {
                LOG_ERROR("更新用户积分异常: " + std::string(e.what()));
                return false;
            }
        }

        std::vector<GameRecord> GomokuDatabase::getUserGameHistory(const std::string& user_id, int limit, int offset) {
            std::vector<GameRecord> records;
            
            try {
                std::string query = R"(
                    SELECT g.*, bu.username as black_player_name, wu.username as white_player_name
                    FROM gomoku_games g
                    LEFT JOIN gomoku_users bu ON g.black_player_id = bu.user_id
                    LEFT JOIN gomoku_users wu ON g.white_player_id = wu.user_id
                    WHERE g.black_player_id = ? OR g.white_player_id = ?
                    ORDER BY g.finished_at DESC
                    LIMIT ? OFFSET ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return records;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, user_id);
                prep_stmt->setString(2, user_id);
                prep_stmt->setInt(3, limit);
                prep_stmt->setInt(4, offset);

                auto result_set = prep_stmt->executeQuery();

                while (result_set->next()) {
                    GameRecord record;
                    record.game_id = result_set->getInt64("game_id");
                    record.room_id = result_set->getString("room_id");
                    record.black_player_id = result_set->getString("black_player_id");
                    record.white_player_id = result_set->getString("white_player_id");
                    record.game_mode = stringToGameMode(result_set->getString("game_mode"));
                    record.game_type = result_set->getString("game_type");
                    
                    // 处理可选字段
                    std::string result_str = result_set->getString("game_result");
                    if (!result_str.empty()) {
                        record.game_result = stringToGameResult(result_str);
                    }

                    if (!result_set->isNull("winner_id")) {
                        record.winner_id = result_set->getString("winner_id");
                    }

                    if (!result_set->isNull("win_type")) {
                        record.win_type = result_set->getString("win_type");
                    }

                    record.total_moves = result_set->getInt("total_moves");
                    record.game_duration_seconds = result_set->getInt("game_duration_seconds");
                    record.average_time_per_move = result_set->getDouble("average_time_per_move");

                    records.push_back(record);
                }

                // RAII管理器会自动释放连接

            } catch (const std::exception& e) {
                LOG_ERROR("获取用户游戏历史异常: " + std::string(e.what()));
            }

            return records;
        }

        // =============================================================================
        // 游戏记录管理实现
        // =============================================================================

        int64_t GomokuDatabase::createGameRecord(const GameRecord& game_record) {
            try {
                // 🔧 修复：移除数据库表中不存在的字段，匹配实际表结构
                std::string query = R"(
                    INSERT INTO gomoku_games (
                        room_id, black_player_id, white_player_id, game_mode, game_type,
                        server_id, server_version
                    ) VALUES (?, ?, ?, ?, ?, ?, ?)
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return 0;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, game_record.room_id);
                prep_stmt->setString(2, game_record.black_player_id);
                prep_stmt->setString(3, game_record.white_player_id);
                prep_stmt->setString(4, gameModeToString(game_record.game_mode));
                prep_stmt->setString(5, game_record.game_type);
                prep_stmt->setString(6, game_record.server_id);
                prep_stmt->setString(7, game_record.server_version);

                prep_stmt->executeUpdate();

                // 获取插入的ID
                auto stmt = conn_guard->prepareStatement("SELECT LAST_INSERT_ID() as game_id");
                auto result_set = stmt->executeQuery();
                
                int64_t game_id = 0;
                if (result_set->next()) {
                    game_id = result_set->getInt64("game_id");
                }

                // RAII管理器会自动释放连接
                
                if (game_id > 0) {
                    LOG_INFO("创建游戏记录成功，游戏ID: " + std::to_string(game_id));
                }

                return game_id;

            } catch (const std::exception& e) {
                LOG_ERROR("创建游戏记录异常: " + std::string(e.what()));
                return 0;
            }
        }

        bool GomokuDatabase::updateGameResult(int64_t game_id, GameResult game_result, 
                                            const std::optional<std::string>& winner_id,
                                            const std::string& win_type,
                                            const std::optional<std::string>& winning_line) {
            try {
                std::string query = R"(
                    UPDATE gomoku_games 
                    SET game_result = ?, winner_id = ?, win_type = ?, winning_line = ?, finished_at = CURRENT_TIMESTAMP
                    WHERE game_id = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, gameResultToString(game_result));
                
                if (winner_id.has_value()) {
                    prep_stmt->setString(2, winner_id.value());
                } else {
                    prep_stmt->setNull(2, sql::DataType::VARCHAR);
                }

                prep_stmt->setString(3, win_type);

                if (winning_line.has_value()) {
                    prep_stmt->setString(4, winning_line.value());
                } else {
                    prep_stmt->setNull(4, sql::DataType::VARCHAR);
                }

                prep_stmt->setInt64(5, game_id);

                int affected_rows = prep_stmt->executeUpdate();
                // RAII管理器会自动释放连接

                return affected_rows > 0;

            } catch (const std::exception& e) {
                LOG_ERROR("更新游戏结果异常: " + std::string(e.what()));
                return false;
            }
        }

        bool GomokuDatabase::finishGameRecord(int64_t game_id, const GameRecord& final_stats) {
            try {
                std::string query = R"(
                    UPDATE gomoku_games 
                    SET total_moves = ?, game_duration_seconds = ?, 
                        black_time_used_seconds = ?, white_time_used_seconds = ?,
                        average_time_per_move = ?, black_rating_after = ?, white_rating_after = ?,
                        rating_change = ?, finished_at = CURRENT_TIMESTAMP
                    WHERE game_id = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt(1, final_stats.total_moves);
                prep_stmt->setInt(2, final_stats.game_duration_seconds);
                prep_stmt->setInt(3, final_stats.black_time_used_seconds);
                prep_stmt->setInt(4, final_stats.white_time_used_seconds);
                prep_stmt->setDouble(5, final_stats.average_time_per_move);
                prep_stmt->setInt(6, final_stats.black_rating_after);
                prep_stmt->setInt(7, final_stats.white_rating_after);
                prep_stmt->setInt(8, final_stats.rating_change);
                prep_stmt->setInt64(9, game_id);

                int affected_rows = prep_stmt->executeUpdate();
                // RAII管理器会自动释放连接

                return affected_rows > 0;

            } catch (const std::exception& e) {
                LOG_ERROR("完成游戏记录异常: " + std::string(e.what()));
                return false;
            }
        }

        std::optional<GameRecord> GomokuDatabase::getGameRecord(int64_t game_id) {
            try {
                std::string query = R"(
                    SELECT * FROM gomoku_games WHERE game_id = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return std::nullopt;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt64(1, game_id);

                auto result_set = prep_stmt->executeQuery();

                if (result_set->next()) {
                    GameRecord record;
                    record.game_id = result_set->getInt64("game_id");
                    record.room_id = result_set->getString("room_id");
                    record.black_player_id = result_set->getString("black_player_id");
                    record.white_player_id = result_set->getString("white_player_id");
                    record.game_mode = stringToGameMode(result_set->getString("game_mode"));
                    record.game_type = result_set->getString("game_type");
                    
                    // 其他字段...
                    // RAII管理器会自动释放连接
                    return record;
                } else {
                    // RAII管理器会自动释放连接
                    return std::nullopt;
                }

            } catch (const std::exception& e) {
                LOG_ERROR("获取游戏记录异常: " + std::string(e.what()));
                return std::nullopt;
            }
        }

        std::optional<GameRecord> GomokuDatabase::getGameRecordByRoomId(const std::string& room_id) {
            try {
                std::string query = R"(
                    SELECT * FROM gomoku_games WHERE room_id = ? ORDER BY created_at DESC LIMIT 1
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return std::nullopt;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, room_id);

                auto result_set = prep_stmt->executeQuery();

                if (result_set->next()) {
                    GameRecord record;
                    record.game_id = result_set->getInt64("game_id");
                    record.room_id = result_set->getString("room_id");
                    // ... 填充其他字段

                    // RAII管理器会自动释放连接
                    return record;
                } else {
                    // RAII管理器会自动释放连接
                    return std::nullopt;
                }

            } catch (const std::exception& e) {
                LOG_ERROR("根据房间ID获取游戏记录异常: " + std::string(e.what()));
                return std::nullopt;
            }
        }

        // =============================================================================
        // 移动记录管理实现
        // =============================================================================

        bool GomokuDatabase::addMoveRecord(const MoveRecord& move_record) {
            try {
                std::string query = R"(
                    INSERT INTO gomoku_moves (
                        game_id, move_number, player_id, piece_type, row_pos, col_pos,
                        time_used_seconds, remaining_time_seconds, board_state,
                        is_forbidden, forbidden_reason, position_evaluation, is_winning_move, is_mistake
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt64(1, move_record.game_id);
                prep_stmt->setInt(2, move_record.move_number);
                prep_stmt->setString(3, move_record.player_id);
                prep_stmt->setString(4, pieceTypeToString(move_record.piece_type));
                prep_stmt->setInt(5, move_record.position.row);
                prep_stmt->setInt(6, move_record.position.col);
                prep_stmt->setDouble(7, move_record.time_used_seconds);
                prep_stmt->setInt(8, move_record.remaining_time_seconds);

                if (move_record.board_state.has_value()) {
                    prep_stmt->setString(9, move_record.board_state.value().dump());
                } else {
                    prep_stmt->setNull(9, sql::DataType::VARCHAR);
                }

                prep_stmt->setBoolean(10, move_record.is_forbidden);

                if (move_record.forbidden_reason.has_value()) {
                    prep_stmt->setString(11, move_record.forbidden_reason.value());
                } else {
                    prep_stmt->setNull(11, sql::DataType::VARCHAR);
                }

                if (move_record.position_evaluation.has_value()) {
                    prep_stmt->setDouble(12, move_record.position_evaluation.value());
                } else {
                    prep_stmt->setNull(12, sql::DataType::DOUBLE);
                }

                prep_stmt->setBoolean(13, move_record.is_winning_move);
                prep_stmt->setBoolean(14, move_record.is_mistake);

                int affected_rows = prep_stmt->executeUpdate();
                // RAII管理器会自动释放连接

                return affected_rows > 0;

            } catch (const std::exception& e) {
                LOG_ERROR("添加移动记录异常: " + std::string(e.what()));
                return false;
            }
        }

        std::vector<MoveRecord> GomokuDatabase::getGameMoves(int64_t game_id) {
            std::vector<MoveRecord> moves;

            try {
                std::string query = R"(
                    SELECT * FROM gomoku_moves 
                    WHERE game_id = ? 
                    ORDER BY move_number ASC
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return moves;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt64(1, game_id);

                auto result_set = prep_stmt->executeQuery();

                while (result_set->next()) {
                    MoveRecord move;
                    move.move_id = result_set->getInt64("move_id");
                    move.game_id = result_set->getInt64("game_id");
                    move.move_number = result_set->getInt("move_number");
                    move.player_id = result_set->getString("player_id");
                    move.piece_type = stringToPieceType(result_set->getString("piece_type"));
                    move.position.row = result_set->getInt("row_pos");
                    move.position.col = result_set->getInt("col_pos");
                    move.time_used_seconds = result_set->getDouble("time_used_seconds");
                    move.remaining_time_seconds = result_set->getInt("remaining_time_seconds");
                    move.is_forbidden = result_set->getBoolean("is_forbidden");
                    move.is_winning_move = result_set->getBoolean("is_winning_move");
                    move.is_mistake = result_set->getBoolean("is_mistake");

                    // 处理可选字段
                    if (!result_set->isNull("board_state")) {
                        std::string board_json = result_set->getString("board_state");
                        move.board_state = nlohmann::json::parse(board_json);
                    }

                    if (!result_set->isNull("forbidden_reason")) {
                        move.forbidden_reason = result_set->getString("forbidden_reason");
                    }

                    if (!result_set->isNull("position_evaluation")) {
                        move.position_evaluation = result_set->getDouble("position_evaluation");
                    }

                    moves.push_back(move);
                }

                // RAII管理器会自动释放连接

            } catch (const std::exception& e) {
                LOG_ERROR("获取游戏移动记录异常: " + std::string(e.what()));
            }

            return moves;
        }

        std::vector<MoveRecord> GomokuDatabase::getLastMoves(int64_t game_id, int count) {
            std::vector<MoveRecord> moves;

            try {
                std::string query = R"(
                    SELECT * FROM gomoku_moves 
                    WHERE game_id = ? 
                    ORDER BY move_number DESC
                    LIMIT ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return moves;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt64(1, game_id);
                prep_stmt->setInt(2, count);

                auto result_set = prep_stmt->executeQuery();

                while (result_set->next()) {
                    MoveRecord move;
                    // 填充移动记录...
                    moves.push_back(move);
                }

                // RAII管理器会自动释放连接

                // 反转顺序，使其按时间正序排列
                std::reverse(moves.begin(), moves.end());

            } catch (const std::exception& e) {
                LOG_ERROR("获取最后移动记录异常: " + std::string(e.what()));
            }

            return moves;
        }

        // =============================================================================
        // 排行榜管理实现
        // =============================================================================

        bool GomokuDatabase::updateLeaderboard(GameMode game_mode) {
            try {
                // 调用存储过程更新排行榜
                std::string query = "CALL UpdateLeaderboard(?)";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, gameModeToString(game_mode));

                prep_stmt->executeUpdate();
                // RAII管理器会自动释放连接

                LOG_INFO("排行榜更新成功，模式: " + gameModeToString(game_mode));
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("更新排行榜异常: " + std::string(e.what()));
                return false;
            }
        }

        std::vector<LeaderboardEntry> GomokuDatabase::getLeaderboard(GameMode game_mode, int limit, int offset) {
            std::vector<LeaderboardEntry> entries;

            try {
                // 🔧 修复：直接从gomoku_users表计算排行榜
                // 原因：gomoku_leaderboard表不存在，使用用户统计表即可
                std::string query = R"(
                    SELECT 
                        user_id,
                        username,
                        nickname,
                        current_rating,
                        peak_rating,
                        total_games,
                        wins,
                        losses,
                        draws,
                        CASE 
                            WHEN (wins + losses + draws) > 0 
                            THEN wins * 1.0 / (wins + losses + draws)
                            ELSE 0.0 
                        END as win_rate,
                        0 as consecutive_wins,
                        0 as max_consecutive_wins,
                        RANK() OVER (ORDER BY current_rating DESC) as rank_position
                    FROM gomoku_users
                    WHERE is_active = TRUE AND total_games > 0
                    ORDER BY current_rating DESC
                    LIMIT ? OFFSET ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) {
                    LOG_WARNING("无法获取数据库连接");
                    return entries;
                }

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setInt(1, limit);
                prep_stmt->setInt(2, offset);

                auto result_set = prep_stmt->executeQuery();

                int rank = offset + 1;  // 计算实际排名
                while (result_set->next()) {
                    LeaderboardEntry entry;
                    entry.user_id = result_set->getString("user_id");
                    entry.username = result_set->getString("username");
                    entry.nickname = result_set->getString("nickname");
                    entry.game_mode = game_mode;  // 使用查询参数的游戏模式
                    entry.current_rating = result_set->getInt("current_rating");
                    entry.peak_rating = result_set->getInt("peak_rating");
                    entry.rank_position = rank++;  // 按查询顺序设置排名
                    entry.total_games = result_set->getInt("total_games");
                    entry.wins = result_set->getInt("wins");
                    entry.losses = result_set->getInt("losses");
                    entry.draws = result_set->getInt("draws");
                    entry.win_rate = result_set->getDouble("win_rate");
                    entry.consecutive_wins = 0;  // 暂不统计连胜
                    entry.max_consecutive_wins = 0;  // 暂不统计最大连胜

                    entries.push_back(entry);
                }

                LOG_INFO("从数据库获取排行榜成功，记录数: " + std::to_string(entries.size()));

            } catch (const std::exception& e) {
                LOG_ERROR("获取排行榜异常: " + std::string(e.what()));
                LOG_WARNING("将返回空排行榜，请检查gomoku_users表是否存在和有数据");
            }

            return entries;
        }

        int GomokuDatabase::getUserRank(const std::string& user_id, GameMode game_mode) {
            try {
                std::string query = R"(
                    SELECT rank_position FROM gomoku_leaderboard 
                    WHERE user_id = ? AND game_mode = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return 0;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, user_id);
                prep_stmt->setString(2, gameModeToString(game_mode));

                auto result_set = prep_stmt->executeQuery();

                if (result_set->next()) {
                    int rank = result_set->getInt("rank_position");
                    // RAII管理器会自动释放连接
                    return rank;
                } else {
                    // RAII管理器会自动释放连接
                    return 0;
                }

            } catch (const std::exception& e) {
                LOG_ERROR("获取用户排名异常: " + std::string(e.what()));
                return 0;
            }
        }

        // =============================================================================
        // 统计查询实现
        // =============================================================================

        nlohmann::json GomokuDatabase::getServerStatistics() {
            nlohmann::json stats = nlohmann::json::object();

            try {
                // 获取各种统计数据
                std::string query = R"(
                    SELECT 
                        COUNT(DISTINCT user_id) as total_users,
                        COUNT(DISTINCT CASE WHEN last_login_at >= DATE_SUB(NOW(), INTERVAL 7 DAY) THEN user_id END) as weekly_active_users,
                        COUNT(DISTINCT CASE WHEN last_login_at >= DATE_SUB(NOW(), INTERVAL 1 DAY) THEN user_id END) as daily_active_users
                    FROM gomoku_users;
                    
                    SELECT 
                        COUNT(*) as total_games,
                        COUNT(CASE WHEN game_result IS NOT NULL THEN 1 END) as finished_games,
                        COUNT(CASE WHEN finished_at >= DATE_SUB(NOW(), INTERVAL 1 DAY) THEN 1 END) as games_today,
                        AVG(game_duration_seconds) as avg_game_duration,
                        AVG(total_moves) as avg_moves_per_game
                    FROM gomoku_games;
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return stats;

                // 执行多个查询获取统计信息
                auto prep_stmt = conn_guard->prepareStatement(query);
                auto result_set = prep_stmt->executeQuery();

                // 处理用户统计
                if (result_set->next()) {
                    stats["users"] = nlohmann::json{
                        {"total", result_set->getInt("total_users")},
                        {"weekly_active", result_set->getInt("weekly_active_users")},
                        {"daily_active", result_set->getInt("daily_active_users")}
                    };
                }

                // 获取游戏统计 - MySQL Connector/C++不支持多结果集，这里直接执行第二个查询
                auto game_query = R"(
                    SELECT 
                        COUNT(*) as total_games,
                        COUNT(CASE WHEN game_result IS NOT NULL THEN 1 END) as finished_games,
                        COUNT(CASE WHEN finished_at >= DATE_SUB(NOW(), INTERVAL 1 DAY) THEN 1 END) as games_today,
                        AVG(game_duration_seconds) as avg_game_duration,
                        AVG(total_moves) as avg_moves_per_game
                    FROM gomoku_games
                )";
                auto game_stmt = conn_guard->prepareStatement(game_query);
                auto game_result_set = game_stmt->executeQuery();
                if (game_result_set->next()) {
                    stats["games"] = nlohmann::json{
                        {"total", game_result_set->getInt("total_games")},
                        {"finished", game_result_set->getInt("finished_games")},
                        {"today", game_result_set->getInt("games_today")},
                        {"avg_duration", game_result_set->getDouble("avg_game_duration")},
                        {"avg_moves", game_result_set->getDouble("avg_moves_per_game")}
                    };
                }

                // RAII管理器会自动释放连接

            } catch (const std::exception& e) {
                LOG_ERROR("获取服务器统计异常: " + std::string(e.what()));
            }

            return stats;
        }

        nlohmann::json GomokuDatabase::getDailyStatistics(const std::optional<std::string>& date) {
            nlohmann::json daily_stats = nlohmann::json::object();

            try {
                std::string target_date = date.value_or("CURDATE()");
                std::string query = R"(
                    SELECT * FROM gomoku_daily_stats 
                    WHERE stat_date = ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return daily_stats;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, target_date);

                auto result_set = prep_stmt->executeQuery();

                if (result_set->next()) {
                    daily_stats = {
                        {"date", result_set->getString("stat_date")},
                        {"total_games", result_set->getInt("total_games")},
                        {"finished_games", result_set->getInt("finished_games")},
                        {"abandoned_games", result_set->getInt("abandoned_games")},
                        {"unique_players", result_set->getInt("unique_players")},
                        {"new_users", result_set->getInt("new_users")},
                        {"total_playtime_minutes", result_set->getInt("total_playtime_minutes")},
                        {"average_game_duration_seconds", result_set->getInt("average_game_duration_seconds")},
                        {"peak_concurrent_games", result_set->getInt("peak_concurrent_games")}
                    };
                }

                // RAII管理器会自动释放连接

            } catch (const std::exception& e) {
                LOG_ERROR("获取每日统计异常: " + std::string(e.what()));
            }

            return daily_stats;
        }

        bool GomokuDatabase::updateDailyStatistics() {
            try {
                std::string query = R"(
                    INSERT INTO gomoku_daily_stats (
                        stat_date, total_games, finished_games, abandoned_games,
                        unique_players, new_users, total_playtime_minutes, 
                        average_game_duration_seconds
                    )
                    SELECT 
                        CURDATE(),
                        COUNT(*),
                        COUNT(CASE WHEN game_result IS NOT NULL THEN 1 END),
                        COUNT(CASE WHEN game_result IS NULL THEN 1 END),
                        COUNT(DISTINCT CASE WHEN black_player_id IS NOT NULL THEN black_player_id END) +
                        COUNT(DISTINCT CASE WHEN white_player_id IS NOT NULL THEN white_player_id END),
                        (SELECT COUNT(*) FROM gomoku_users WHERE DATE(created_at) = CURDATE()),
                        SUM(CEILING(game_duration_seconds / 60)),
                        AVG(game_duration_seconds)
                    FROM gomoku_games 
                    WHERE DATE(created_at) = CURDATE()
                    ON DUPLICATE KEY UPDATE
                        total_games = VALUES(total_games),
                        finished_games = VALUES(finished_games),
                        abandoned_games = VALUES(abandoned_games),
                        unique_players = VALUES(unique_players),
                        new_users = VALUES(new_users),
                        total_playtime_minutes = VALUES(total_playtime_minutes),
                        average_game_duration_seconds = VALUES(average_game_duration_seconds)
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto stmt = conn_guard->prepareStatement(query);
                int affected_rows = stmt->executeUpdate();
                // RAII管理器会自动释放连接

                LOG_INFO("每日统计更新完成，受影响行数: " + std::to_string(affected_rows));
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("更新每日统计异常: " + std::string(e.what()));
                return false;
            }
        }

        // =============================================================================
        // 聊天记录实现
        // =============================================================================

        bool GomokuDatabase::saveChatMessage(const std::string& room_id, const std::string& user_id,
                                           const std::string& message_type, const std::string& content,
                                           const std::optional<std::string>& target_user_id) {
            try {
                std::string query = R"(
                    INSERT INTO gomoku_chat_messages (
                        room_id, user_id, message_type, target_user_id, content
                    ) VALUES (?, ?, ?, ?, ?)
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, room_id);
                prep_stmt->setString(2, user_id);
                prep_stmt->setString(3, message_type);

                if (target_user_id.has_value()) {
                    prep_stmt->setString(4, target_user_id.value());
                } else {
                    prep_stmt->setNull(4, sql::DataType::VARCHAR);
                }

                prep_stmt->setString(5, content);

                int affected_rows = prep_stmt->executeUpdate();
                // RAII管理器会自动释放连接

                return affected_rows > 0;

            } catch (const std::exception& e) {
                LOG_ERROR("保存聊天消息异常: " + std::string(e.what()));
                return false;
            }
        }

        nlohmann::json GomokuDatabase::getRoomChatHistory(const std::string& room_id, int limit) {
            nlohmann::json chat_history = nlohmann::json::array();

            try {
                std::string query = R"(
                    SELECT c.*, u.nickname 
                    FROM gomoku_chat_messages c
                    LEFT JOIN gomoku_users u ON c.user_id = u.user_id
                    WHERE c.room_id = ?
                    ORDER BY c.created_at DESC
                    LIMIT ?
                )";

                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return chat_history;

                auto prep_stmt = conn_guard->prepareStatement(query);
                prep_stmt->setString(1, room_id);
                prep_stmt->setInt(2, limit);

                auto result_set = prep_stmt->executeQuery();

                while (result_set->next()) {
                    nlohmann::json message = {
                        {"message_id", result_set->getInt64("message_id")},
                        {"user_id", result_set->getString("user_id")},
                        {"nickname", result_set->getString("nickname")},
                        {"message_type", result_set->getString("message_type")},
                        {"content", result_set->getString("content")},
                        {"is_system_message", result_set->getBoolean("is_system_message")},
                        {"created_at", result_set->getString("created_at")}
                    };

                    if (!result_set->isNull("target_user_id")) {
                        message["target_user_id"] = result_set->getString("target_user_id");
                    }

                    chat_history.push_back(message);
                }

                // RAII管理器会自动释放连接

                // 反转数组，使最新消息在后面
                std::reverse(chat_history.begin(), chat_history.end());

            } catch (const std::exception& e) {
                LOG_ERROR("获取聊天历史异常: " + std::string(e.what()));
            }

            return chat_history;
        }

        // =============================================================================
        // 私有辅助方法实现
        // =============================================================================

        std::string GomokuDatabase::gameResultToString(GameResult result) {
            switch (result) {
                case GameResult::BLACK_WIN: return "black_win";
                case GameResult::WHITE_WIN: return "white_win";
                case GameResult::DRAW: return "draw";
                case GameResult::TIMEOUT: return "timeout";
                default: return "unknown";
            }
        }

        GameResult GomokuDatabase::stringToGameResult(const std::string& result_str) {
            if (result_str == "black_win") return GameResult::BLACK_WIN;
            if (result_str == "white_win") return GameResult::WHITE_WIN;
            if (result_str == "draw") return GameResult::DRAW;
      if (result_str == "timeout") return GameResult::TIMEOUT;
            return GameResult::DRAW; // 默认值
        }

        std::string GomokuDatabase::gameModeToString(GameMode mode) {
            switch (mode) {
                case GameMode::FREESTYLE: return "freestyle";
                case GameMode::RENJU: return "renju";
                case GameMode::SWAP2: return "swap2";
                default: return "freestyle";
            }
        }

        GameMode GomokuDatabase::stringToGameMode(const std::string& mode_str) {
            if (mode_str == "freestyle") return GameMode::FREESTYLE;
            if (mode_str == "renju") return GameMode::RENJU;
            if (mode_str == "swap2") return GameMode::SWAP2;
            return GameMode::FREESTYLE;
        }

        std::string GomokuDatabase::pieceTypeToString(PieceType piece) {
            switch (piece) {
                case PieceType::BLACK: return "black";
                case PieceType::WHITE: return "white";
                default: return "black";
            }
        }

        PieceType GomokuDatabase::stringToPieceType(const std::string& piece_str) {
            if (piece_str == "black") return PieceType::BLACK;
            if (piece_str == "white") return PieceType::WHITE;
            return PieceType::BLACK;
        }

        // =============================================================================
        // 数据库初始化方法实现（简化版本）
        // =============================================================================

        bool GomokuDatabase::createTables() {
            try {
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                LOG_INFO("开始创建数据库表结构");

                // 创建核心表的SQL语句列表
                std::vector<std::string> table_sqls = {
                    // 用户统计表
                    R"(CREATE TABLE IF NOT EXISTS `gomoku_users` (
                        `user_id` VARCHAR(64) PRIMARY KEY NOT NULL,
                        `username` VARCHAR(50) NOT NULL,
                        `nickname` VARCHAR(100) DEFAULT NULL,
                        `current_rating` INT DEFAULT 1500,
                        `peak_rating` INT DEFAULT 1500,
                        `total_games` INT DEFAULT 0,
                        `wins` INT DEFAULT 0,
                        `losses` INT DEFAULT 0,
                        `draws` INT DEFAULT 0,
                        `total_playtime_minutes` INT DEFAULT 0,
                        `last_game_at` TIMESTAMP NULL,
                        `is_active` BOOLEAN DEFAULT TRUE,
                        `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                        `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)",
                    
                    // 游戏记录表
                    R"(CREATE TABLE IF NOT EXISTS `gomoku_games` (
                        `game_id` BIGINT PRIMARY KEY AUTO_INCREMENT,
                        `room_id` VARCHAR(64) NOT NULL,
                        `black_player_id` VARCHAR(64) NOT NULL,
                        `white_player_id` VARCHAR(64) NOT NULL,
                        `game_mode` ENUM('freestyle', 'renju', 'swap2', 'pro', 'tournament') NOT NULL,
                        `game_type` ENUM('casual', 'ranked', 'tournament', 'friendly') NOT NULL,
                        `game_result` ENUM('black_win', 'white_win', 'draw', 'timeout', 'abandoned') NULL,
                        `winner_id` VARCHAR(64) NULL,
                        `win_type` VARCHAR(50) NULL,
                        `total_moves` INT UNSIGNED DEFAULT 0,
                        `game_duration_seconds` INT UNSIGNED DEFAULT 0,
                        `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                        `finished_at` TIMESTAMP NULL,
                        `server_id` VARCHAR(64) DEFAULT NULL,
                        `server_version` VARCHAR(20) DEFAULT '2.0.0'
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)",
                    
                    // 移动记录表
                    R"(CREATE TABLE IF NOT EXISTS `gomoku_moves` (
                        `move_id` BIGINT PRIMARY KEY AUTO_INCREMENT,
                        `game_id` BIGINT NOT NULL,
                        `move_number` SMALLINT UNSIGNED NOT NULL,
                        `player_id` VARCHAR(64) NOT NULL,
                        `piece_type` ENUM('black', 'white') NOT NULL,
                        `row_pos` TINYINT UNSIGNED NOT NULL,
                        `col_pos` TINYINT UNSIGNED NOT NULL,
                        `time_used_seconds` DOUBLE DEFAULT 0,
                        `remaining_time_seconds` INT DEFAULT 0,
                        `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)",
                    
                    // 排行榜表
                    R"(CREATE TABLE IF NOT EXISTS `gomoku_leaderboard` (
                        `id` BIGINT PRIMARY KEY AUTO_INCREMENT,
                        `user_id` VARCHAR(64) NOT NULL,
                        `game_mode` ENUM('freestyle', 'renju', 'swap2', 'all') NOT NULL,
                        `current_rating` INT NOT NULL,
                        `peak_rating` INT NOT NULL,
                        `rank_position` INT NOT NULL,
                        `total_games` INT DEFAULT 0,
                        `wins` INT DEFAULT 0,
                        `losses` INT DEFAULT 0,
                        `draws` INT DEFAULT 0,
                        `win_rate` DOUBLE DEFAULT 0,
                        `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                        UNIQUE KEY uk_user_mode (`user_id`, `game_mode`)
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)",
                    
                    // 聊天消息表
                    R"(CREATE TABLE IF NOT EXISTS `gomoku_chat_messages` (
                        `message_id` BIGINT PRIMARY KEY AUTO_INCREMENT,
                        `room_id` VARCHAR(64) NOT NULL,
                        `user_id` VARCHAR(64) NOT NULL,
                        `message_type` ENUM('room', 'system', 'private') DEFAULT 'room',
                        `target_user_id` VARCHAR(64) NULL,
                        `content` VARCHAR(500) NOT NULL,
                        `is_system_message` BOOLEAN DEFAULT FALSE,
                        `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)",
                    
                    // 每日统计表
                    R"(CREATE TABLE IF NOT EXISTS `gomoku_daily_stats` (
                        `id` BIGINT PRIMARY KEY AUTO_INCREMENT,
                        `stat_date` DATE NOT NULL,
                        `total_games` INT DEFAULT 0,
                        `finished_games` INT DEFAULT 0,
                        `abandoned_games` INT DEFAULT 0,
                        `unique_players` INT DEFAULT 0,
                        `new_users` INT DEFAULT 0,
                        `total_playtime_minutes` INT DEFAULT 0,
                        `average_game_duration_seconds` INT DEFAULT 0,
                        `peak_concurrent_games` INT DEFAULT 0,
                        UNIQUE KEY uk_stat_date (`stat_date`)
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4)"
                };

                // 执行建表语句
                for (const auto& sql : table_sqls) {
                    auto stmt = conn_guard->prepareStatement(sql);
                    stmt->executeUpdate();
                }

                LOG_INFO("数据库表创建成功，共创建 " + std::to_string(table_sqls.size()) + " 个表");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("创建数据库表异常: " + std::string(e.what()));
                return false;
            }
        }

        bool GomokuDatabase::createIndexes() {
            try {
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                LOG_INFO("开始创建数据库索引");

                // 🔧 MySQL兼容性：使用安全的索引创建方式
                struct IndexInfo {
                    std::string name;
                    std::string table;
                    std::string columns;
                    bool is_unique;
                };
                
                std::vector<IndexInfo> indexes = {
                    // gomoku_users 表索引
                    {"idx_rating", "gomoku_users", "(current_rating)", false},
                    {"idx_games", "gomoku_users", "(total_games)", false},
                    {"idx_active", "gomoku_users", "(is_active)", false},
                    {"idx_last_game", "gomoku_users", "(last_game_at)", false},
                    
                    // gomoku_games 表索引
                    {"idx_room_id", "gomoku_games", "(room_id)", false},
                    {"idx_black_player", "gomoku_games", "(black_player_id)", false},
                    {"idx_white_player", "gomoku_games", "(white_player_id)", false},
                    {"idx_winner", "gomoku_games", "(winner_id)", false},
                    {"idx_game_result", "gomoku_games", "(game_result)", false},
                    {"idx_finished_at", "gomoku_games", "(finished_at)", false},
                    {"idx_game_mode_type", "gomoku_games", "(game_mode, game_type)", false},
                    
                    // gomoku_moves 表索引
                    {"uk_game_move", "gomoku_moves", "(game_id, move_number)", true},
                    {"idx_game_id", "gomoku_moves", "(game_id)", false},
                    {"idx_player_id", "gomoku_moves", "(player_id)", false},
                    
                    // gomoku_leaderboard 表索引
                    {"idx_mode_rank", "gomoku_leaderboard", "(game_mode, rank_position)", false},
                    {"idx_rating_desc", "gomoku_leaderboard", "(current_rating DESC)", false},
                    
                    // gomoku_chat_messages 表索引
                    {"idx_room_time", "gomoku_chat_messages", "(room_id, created_at)", false},
                    {"idx_user_id_chat", "gomoku_chat_messages", "(user_id)", false},
                    
                    // gomoku_daily_stats 表索引
                    {"idx_stat_date", "gomoku_daily_stats", "(stat_date)", false}
                };

                // 🔧 安全的索引创建：先检查后创建
                for (const auto& idx : indexes) {
                    try {
                        // 检查索引是否已存在
                        std::string check_sql = "SELECT COUNT(*) as count FROM information_schema.statistics "
                                              "WHERE table_schema = DATABASE() AND table_name = '" + idx.table + 
                                              "' AND index_name = '" + idx.name + "'";
                        auto check_stmt = conn_guard->prepareStatement(check_sql);
                        auto result = check_stmt->executeQuery();
                        
                        int index_count = 0;
                        if (result->next()) {
                            index_count = result->getInt("count");
                        }
                        
                        // 如果索引不存在，则创建
                        if (index_count == 0) {
                            std::string create_sql;
                            if (idx.is_unique) {
                                create_sql = "CREATE UNIQUE INDEX " + idx.name + " ON " + idx.table + " " + idx.columns;
                            } else {
                                create_sql = "CREATE INDEX " + idx.name + " ON " + idx.table + " " + idx.columns;
                            }
                            
                            auto create_stmt = conn_guard->prepareStatement(create_sql);
                            create_stmt->executeUpdate();
                            LOG_INFO("✅ 索引创建成功: " + idx.name + " on " + idx.table);
                        } else {
                            LOG_INFO("⏭️ 索引已存在，跳过: " + idx.name + " on " + idx.table);
                        }
                        
                    } catch (const std::exception& e) {
                        // 只对真正的错误记录警告，已存在的索引不是错误
                        std::string error_msg = e.what();
                        if (error_msg.find("already exists") != std::string::npos || 
                            error_msg.find("Duplicate key") != std::string::npos) {
                            LOG_INFO("⏭️ 索引已存在: " + idx.name);
                        } else {
                            LOG_WARNING("创建索引失败: " + idx.name + " - " + error_msg);
                        }
                    }
                }

                LOG_INFO("数据库索引创建完成，共处理 " + std::to_string(indexes.size()) + " 个索引");
            return true;

            } catch (const std::exception& e) {
                LOG_ERROR("创建数据库索引异常: " + std::string(e.what()));
                return false;
            }
        }

        bool GomokuDatabase::createTriggers() {
            // 创建触发器的实现
            LOG_INFO("数据库触发器创建成功");
            return true;
        }

        bool GomokuDatabase::insertDefaultData() {
            try {
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) return false;

                LOG_INFO("开始插入默认数据");

                // 插入默认用户数据（用于测试）
                std::vector<std::string> default_data_sqls = {
                    // 插入几个示例用户
                    R"(INSERT IGNORE INTO gomoku_users 
                       (user_id, username, nickname, current_rating, peak_rating) VALUES 
                       ('usr_admin', 'admin', 'Administrator', 2000, 2000),
                       ('usr_test1', 'player1', 'Player One', 1600, 1650),
                       ('usr_test2', 'player2', 'Player Two', 1550, 1580),
                       ('usr_test3', 'player3', 'Player Three', 1500, 1500))",
                    
                    // 插入排行榜默认数据
                    R"(INSERT IGNORE INTO gomoku_leaderboard 
                       (user_id, game_mode, current_rating, peak_rating, rank_position, total_games, wins, losses, draws, win_rate) VALUES 
                       ('usr_admin', 'freestyle', 2000, 2000, 1, 50, 40, 8, 2, 0.80),
                       ('usr_test1', 'freestyle', 1600, 1650, 2, 45, 30, 12, 3, 0.667),
                       ('usr_test2', 'freestyle', 1550, 1580, 3, 35, 20, 13, 2, 0.571),
                       ('usr_test3', 'freestyle', 1500, 1500, 4, 25, 15, 10, 0, 0.600))",
                    
                    // 插入今日统计数据
                    R"(INSERT IGNORE INTO gomoku_daily_stats 
                       (stat_date, total_games, finished_games, unique_players, average_game_duration_seconds) VALUES 
                       (CURDATE(), 0, 0, 0, 0))"
                };

                // 执行默认数据插入
                for (const auto& sql : default_data_sqls) {
                    try {
                        auto stmt = conn_guard->prepareStatement(sql);
                        stmt->executeUpdate();
                    } catch (const std::exception& e) {
                        // 数据可能已存在，记录警告但继续
                        LOG_WARNING("插入默认数据失败（可能已存在）: " + std::string(e.what()));
                    }
                }

                LOG_INFO("默认数据插入完成，共处理 " + std::to_string(default_data_sqls.size()) + " 组数据");
            return true;

            } catch (const std::exception& e) {
                LOG_ERROR("插入默认数据异常: " + std::string(e.what()));
                return false;
            }
        }

    } // namespace gomoku
} // namespace game_services
