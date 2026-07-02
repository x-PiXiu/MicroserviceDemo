#include "game_persistence_manager.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <random>
#include <zlib.h>
#include <openssl/sha.h>

namespace game_services {
    namespace game_base {

        // ==================== GameSession 实现 ====================

        nlohmann::json GameSession::toJson() const {
            nlohmann::json json;
            json["session_id"] = session_id;
            json["room_id"] = room_id;
            json["game_type"] = static_cast<int>(game_type);
            json["game_mode"] = game_mode;
            json["session_state"] = static_cast<int>(session_state);
            json["player_count"] = player_count;
            json["player_ids"] = player_ids;
            json["game_config"] = game_config;
            json["session_metadata"] = session_metadata;
            
            // 时间信息（转换为时间戳）
            json["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                created_at.time_since_epoch()).count();
            if (started_at != std::chrono::system_clock::time_point{}) {
                json["started_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    started_at.time_since_epoch()).count();
            }
            if (finished_at != std::chrono::system_clock::time_point{}) {
                json["finished_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    finished_at.time_since_epoch()).count();
            }
            json["last_activity_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                last_activity_at.time_since_epoch()).count();
            
            json["game_duration_ms"] = game_duration_ms;
            json["final_scores"] = final_scores;
            json["winners"] = winners;
            
            return json;
        }

        GameSession GameSession::fromJson(const nlohmann::json& json) {
            GameSession session;
            session.session_id = json.value("session_id", "");
            session.room_id = json.value("room_id", "");
            session.game_type = static_cast<GameType>(json.value("game_type", 0));
            session.game_mode = json.value("game_mode", "classic");
            session.session_state = static_cast<RoomState>(json.value("session_state", 0));
            session.player_count = json.value("player_count", 0);
            
            if (json.contains("player_ids") && json["player_ids"].is_array()) {
                session.player_ids = json["player_ids"].get<std::vector<std::string>>();
            }
            
            session.game_config = json.value("game_config", nlohmann::json{});
            session.session_metadata = json.value("session_metadata", nlohmann::json{});
            
            // 时间信息
            auto ms_to_time_point = [](int64_t ms) {
                return std::chrono::system_clock::time_point{std::chrono::milliseconds{ms}};
            };
            
            if (json.contains("created_at")) {
                session.created_at = ms_to_time_point(json["created_at"]);
            }
            if (json.contains("started_at")) {
                session.started_at = ms_to_time_point(json["started_at"]);
            }
            if (json.contains("finished_at")) {
                session.finished_at = ms_to_time_point(json["finished_at"]);
            }
            if (json.contains("last_activity_at")) {
                session.last_activity_at = ms_to_time_point(json["last_activity_at"]);
            }
            
            session.game_duration_ms = json.value("game_duration_ms", 0);
            session.final_scores = json.value("final_scores", nlohmann::json{});
            
            if (json.contains("winners") && json["winners"].is_array()) {
                session.winners = json["winners"].get<std::vector<std::string>>();
            }
            
            return session;
        }

        // ==================== GameSnapshot 实现 ====================

        nlohmann::json GameSnapshot::toJson() const {
            nlohmann::json json;
            json["snapshot_id"] = snapshot_id;
            json["session_id"] = session_id;
            json["snapshot_type"] = snapshot_type;
            json["snapshot_reason"] = snapshot_reason;
            json["game_state"] = game_state;
            json["tick_count"] = tick_count;
            json["game_duration_ms"] = game_duration_ms;
            json["players_state"] = players_state;
            json["world_state"] = world_state;
            json["is_compressed"] = is_compressed;
            json["compression_type"] = compression_type;
            json["state_version"] = state_version;
            json["state_checksum"] = state_checksum;
            json["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                created_at.time_since_epoch()).count();
            
            return json;
        }

        GameSnapshot GameSnapshot::fromJson(const nlohmann::json& json) {
            GameSnapshot snapshot;
            snapshot.snapshot_id = json.value("snapshot_id", "");
            snapshot.session_id = json.value("session_id", "");
            snapshot.snapshot_type = json.value("snapshot_type", "auto");
            snapshot.snapshot_reason = json.value("snapshot_reason", "");
            snapshot.game_state = json.value("game_state", nlohmann::json{});
            snapshot.tick_count = json.value("tick_count", 0);
            snapshot.game_duration_ms = json.value("game_duration_ms", 0);
            snapshot.players_state = json.value("players_state", nlohmann::json{});
            snapshot.world_state = json.value("world_state", nlohmann::json{});
            snapshot.is_compressed = json.value("is_compressed", false);
            snapshot.compression_type = json.value("compression_type", "");
            snapshot.state_version = json.value("state_version", 1);
            snapshot.state_checksum = json.value("state_checksum", "");
            
            if (json.contains("created_at")) {
                snapshot.created_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{json["created_at"]}};
            }
            
            return snapshot;
        }

        // ==================== GameActionLog 实现 ====================

        nlohmann::json GameActionLog::toJson() const {
            nlohmann::json json;
            json["log_id"] = log_id;
            json["session_id"] = session_id;
            json["player_id"] = player_id;
            json["action_type"] = action_type;
            json["action_data"] = action_data;
            json["action_timestamp"] = action_timestamp;
            json["tick_number"] = tick_number;
            json["is_valid"] = is_valid;
            json["validation_result"] = validation_result;
            json["server_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                server_timestamp.time_since_epoch()).count();
            json["server_id"] = server_id;
            
            return json;
        }

        GameActionLog GameActionLog::fromJson(const nlohmann::json& json) {
            GameActionLog log;
            log.log_id = json.value("log_id", "");
            log.session_id = json.value("session_id", "");
            log.player_id = json.value("player_id", "");
            log.action_type = json.value("action_type", "");
            log.action_data = json.value("action_data", nlohmann::json{});
            log.action_timestamp = json.value("action_timestamp", 0);
            log.tick_number = json.value("tick_number", 0);
            log.is_valid = json.value("is_valid", true);
            log.validation_result = json.value("validation_result", nlohmann::json{});
            log.server_id = json.value("server_id", "");
            
            if (json.contains("server_timestamp")) {
                log.server_timestamp = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{json["server_timestamp"]}};
            }
            
            return log;
        }

        // ==================== GameRecoveryPoint 实现 ====================

        nlohmann::json GameRecoveryPoint::toJson() const {
            nlohmann::json json;
            json["recovery_id"] = recovery_id;
            json["session_id"] = session_id;
            json["snapshot_id"] = snapshot_id;
            json["recovery_type"] = recovery_type;
            json["recovery_reason"] = recovery_reason;
            json["is_stable"] = is_stable;
            json["state_validation"] = state_validation;
            json["recovery_metadata"] = recovery_metadata;
            json["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                created_at.time_since_epoch()).count();
            
            if (last_used_at != std::chrono::system_clock::time_point{}) {
                json["last_used_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    last_used_at.time_since_epoch()).count();
            }
            
            return json;
        }

        GameRecoveryPoint GameRecoveryPoint::fromJson(const nlohmann::json& json) {
            GameRecoveryPoint point;
            point.recovery_id = json.value("recovery_id", "");
            point.session_id = json.value("session_id", "");
            point.snapshot_id = json.value("snapshot_id", "");
            point.recovery_type = json.value("recovery_type", "auto");
            point.recovery_reason = json.value("recovery_reason", "");
            point.is_stable = json.value("is_stable", true);
            point.state_validation = json.value("state_validation", nlohmann::json{});
            point.recovery_metadata = json.value("recovery_metadata", nlohmann::json{});
            
            if (json.contains("created_at")) {
                point.created_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{json["created_at"]}};
            }
            if (json.contains("last_used_at")) {
                point.last_used_at = std::chrono::system_clock::time_point{
                    std::chrono::milliseconds{json["last_used_at"]}};
            }
            
            return point;
        }

        // ==================== GamePersistenceConfig 实现 ====================

        GamePersistenceConfig GamePersistenceConfig::fromConfigManager() {
            auto& config_manager = common::config::ConfigManager::getInstance();
            
            GamePersistenceConfig config;
            config.auto_snapshot_interval_ms = config_manager.get<int>("game.persistence.auto_snapshot_interval_ms", 30000);
            config.max_snapshots_per_session = config_manager.get<int>("game.persistence.max_snapshots_per_session", 50);
            config.snapshot_compression_enabled = config_manager.get<bool>("game.persistence.snapshot_compression_enabled", true);
            config.snapshot_compression_threshold = config_manager.get<int>("game.persistence.snapshot_compression_threshold", 10240);
            config.action_log_retention_days = config_manager.get<int>("game.persistence.action_log_retention_days", 30);
            config.recovery_point_retention_hours = config_manager.get<int>("game.persistence.recovery_point_retention_hours", 72);
            config.enable_state_validation = config_manager.get<bool>("game.persistence.enable_state_validation", true);
            config.max_recovery_attempts = config_manager.get<int>("game.persistence.max_recovery_attempts", 3);
            config.batch_size = config_manager.get<int>("game.persistence.batch_size", 100);
            config.cleanup_interval_hours = config_manager.get<int>("game.persistence.cleanup_interval_hours", 24);
            
            return config;
        }

        bool GamePersistenceConfig::validate() const {
            if (auto_snapshot_interval_ms <= 0) {
                LOG_ERROR("GamePersistenceConfig: auto_snapshot_interval_ms must be positive");
                return false;
            }
            if (max_snapshots_per_session <= 0) {
                LOG_ERROR("GamePersistenceConfig: max_snapshots_per_session must be positive");
                return false;
            }
            if (snapshot_compression_threshold < 0) {
                LOG_ERROR("GamePersistenceConfig: snapshot_compression_threshold cannot be negative");
                return false;
            }
            if (action_log_retention_days <= 0) {
                LOG_ERROR("GamePersistenceConfig: action_log_retention_days must be positive");
                return false;
            }
            if (recovery_point_retention_hours <= 0) {
                LOG_ERROR("GamePersistenceConfig: recovery_point_retention_hours must be positive");
                return false;
            }
            if (max_recovery_attempts <= 0) {
                LOG_ERROR("GamePersistenceConfig: max_recovery_attempts must be positive");
                return false;
            }
            if (batch_size <= 0) {
                LOG_ERROR("GamePersistenceConfig: batch_size must be positive");
                return false;
            }
            if (cleanup_interval_hours <= 0) {
                LOG_ERROR("GamePersistenceConfig: cleanup_interval_hours must be positive");
                return false;
            }
            
            return true;
        }

        // ==================== GamePersistenceManager 实现 ====================

        GamePersistenceManager::GamePersistenceManager(
            const GamePersistenceConfig& config,
            std::shared_ptr<common::database::MySQLPool> mysql_pool,
            std::shared_ptr<common::database::RedisPool> redis_pool,
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool)
            : config_(config)
            , mysql_pool_(mysql_pool)
            , redis_pool_(redis_pool)
            , thread_pool_(thread_pool) {
            
            if (!config_.validate()) {
                throw std::invalid_argument("Invalid GamePersistenceConfig");
            }
            
            if (!mysql_pool_) {
                throw std::invalid_argument("MySQL pool cannot be null");
            }
            
            LOG_INFO("GamePersistenceManager created");
        }

        GamePersistenceManager::~GamePersistenceManager() {
            stop();
            LOG_INFO("GamePersistenceManager destroyed");
        }

        bool GamePersistenceManager::start() {
            if (running_.load()) {
                LOG_WARNING("GamePersistenceManager is already running");
                return true;
            }
            
            try {
                // 启动清理任务
                scheduleCleanupTask();
                
                running_.store(true);
                LOG_INFO("GamePersistenceManager started successfully");
                return true;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to start GamePersistenceManager: " + std::string(e.what()));
                return false;
            }
        }

        void GamePersistenceManager::stop() {
            if (!running_.load()) {
                return;
            }
            
            running_.store(false);
            
            // 清理自动快照会话
            {
                std::lock_guard<std::mutex> lock(auto_snapshot_mutex_);
                auto_snapshot_sessions_.clear();
            }
            
            LOG_INFO("GamePersistenceManager stopped");
        }

        // ==================== 会话管理 ====================

        PersistenceResult GamePersistenceManager::createGameSession(const GameSession& session) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            try {
                // 🔧 RAII修复：使用MySQLConnectionGuard自动管理连接
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) {
                    return PersistenceResult(false, "Failed to get database connection");
                }
                
                std::string sql = R"(
                    INSERT INTO game_sessions (
                        session_id, room_id, game_type, game_mode, session_state, 
                        player_count, player_ids, game_config, session_metadata,
                        created_at, last_activity_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, FROM_UNIXTIME(?/1000), FROM_UNIXTIME(?/1000))
                )";
                
                auto stmt = conn_guard->prepareStatement(sql);
                
                // 转换时间为毫秒时间戳
                auto created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    session.created_at.time_since_epoch()).count();
                auto activity_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    session.last_activity_at.time_since_epoch()).count();
                
                // 序列化JSON数组
                nlohmann::json player_ids_json = session.player_ids;
                
                stmt->setString(1, session.session_id);
                stmt->setString(2, session.room_id);
                stmt->setInt(3, static_cast<int>(session.game_type));
                stmt->setString(4, session.game_mode);
                stmt->setInt(5, static_cast<int>(session.session_state));
                stmt->setInt(6, session.player_count);
                stmt->setString(7, player_ids_json.dump());
                stmt->setString(8, session.game_config.dump());
                stmt->setString(9, session.session_metadata.dump());
                stmt->setInt64(10, created_ms);
                stmt->setInt64(11, activity_ms);
                
                stmt->executeUpdate();
                
                // 更新统计
                total_sessions_created_++;
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                
                PersistenceResult result(true);
                result.operation_id = session.session_id;
                result.execution_time_ms = duration.count();
                
                LOG_INFO("Created game session: " + session.session_id);
                return result;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to create game session " + session.session_id + ": " + e.what());
                return PersistenceResult(false, e.what());
            }
        }

        PersistenceResult GamePersistenceManager::updateGameSession(const GameSession& session) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            try {
                // 🔧 RAII修复：使用MySQLConnectionGuard自动管理连接
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) {
                    return PersistenceResult(false, "Failed to get database connection");
                }
                
                std::string sql = R"(
                    UPDATE game_sessions SET
                        session_state = ?, player_count = ?, player_ids = ?,
                        game_config = ?, session_metadata = ?, 
                        started_at = FROM_UNIXTIME(?/1000),
                        finished_at = FROM_UNIXTIME(?/1000),
                        game_duration_ms = ?, final_scores = ?, winners = ?
                    WHERE session_id = ?
                )";
                
                auto stmt = conn_guard->prepareStatement(sql);
                
                // 处理可选时间字段
                int64_t started_ms = 0;
                int64_t finished_ms = 0;
                
                if (session.started_at != std::chrono::system_clock::time_point{}) {
                    started_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        session.started_at.time_since_epoch()).count();
                }
                if (session.finished_at != std::chrono::system_clock::time_point{}) {
                    finished_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        session.finished_at.time_since_epoch()).count();
                }
                
                nlohmann::json player_ids_json = session.player_ids;
                nlohmann::json winners_json = session.winners;
                
                stmt->setInt(1, static_cast<int>(session.session_state));
                stmt->setInt(2, session.player_count);
                stmt->setString(3, player_ids_json.dump());
                stmt->setString(4, session.game_config.dump());
                stmt->setString(5, session.session_metadata.dump());
                
                if (started_ms > 0) {
                    stmt->setInt64(6, started_ms);
                } else {
                    stmt->setNull(6, sql::DataType::BIGINT);
                }
                
                if (finished_ms > 0) {
                    stmt->setInt64(7, finished_ms);
                } else {
                    stmt->setNull(7, sql::DataType::BIGINT);
                }
                
                stmt->setInt64(8, session.game_duration_ms);
                stmt->setString(9, session.final_scores.dump());
                stmt->setString(10, winners_json.dump());
                stmt->setString(11, session.session_id);
                
                int affected_rows = stmt->executeUpdate();
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                
                PersistenceResult result(affected_rows > 0);
                result.operation_id = session.session_id;
                result.execution_time_ms = duration.count();
                
                if (affected_rows > 0) {
                    LOG_DEBUG("Updated game session: " + session.session_id);
                } else {
                    LOG_WARNING("No session found to update: " + session.session_id);
                    result.error_message = "Session not found";
                }
                
                return result;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to update game session " + session.session_id + ": " + e.what());
                return PersistenceResult(false, e.what());
            }
        }

        std::optional<GameSession> GamePersistenceManager::getGameSession(const std::string& session_id) {
            try {
                // 🔧 RAII修复：使用MySQLConnectionGuard自动管理连接
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                if (!conn_guard.isValid()) {
                    LOG_ERROR("Failed to get database connection");
                    return std::nullopt;
                }
                
                std::string sql = R"(
                    SELECT session_id, room_id, game_type, game_mode, session_state,
                           player_count, player_ids, game_config, session_metadata,
                           UNIX_TIMESTAMP(created_at)*1000 as created_at_ms,
                           UNIX_TIMESTAMP(started_at)*1000 as started_at_ms,
                           UNIX_TIMESTAMP(finished_at)*1000 as finished_at_ms,
                           UNIX_TIMESTAMP(last_activity_at)*1000 as last_activity_at_ms,
                           game_duration_ms, final_scores, winners
                    FROM game_sessions WHERE session_id = ?
                )";
                
                auto stmt = conn_guard->prepareStatement(sql);
                stmt->setString(1, session_id);
                
                auto rs = stmt->executeQuery();
                if (rs->next()) {
                    GameSession session;
                    session.session_id = rs->getString("session_id");
                    session.room_id = rs->getString("room_id");
                    session.game_type = static_cast<GameType>(rs->getInt("game_type"));
                    session.game_mode = rs->getString("game_mode");
                    session.session_state = static_cast<RoomState>(rs->getInt("session_state"));
                    session.player_count = rs->getInt("player_count");
                    
                    // 解析JSON字段
                    try {
                        auto player_ids_json = nlohmann::json::parse(std::string(rs->getString("player_ids")));
                        if (player_ids_json.is_array()) {
                            session.player_ids = player_ids_json.get<std::vector<std::string>>();
                        }
                    } catch (...) {
                        LOG_WARNING("Failed to parse player_ids JSON for session: " + session_id);
                    }
                    
                    try {
                        session.game_config = nlohmann::json::parse(std::string(rs->getString("game_config")));
                    } catch (...) {
                        session.game_config = nlohmann::json{};
                    }
                    
                    try {
                        session.session_metadata = nlohmann::json::parse(std::string(rs->getString("session_metadata")));
                    } catch (...) {
                        session.session_metadata = nlohmann::json{};
                    }
                    
                    // 时间字段
                    session.created_at = std::chrono::system_clock::time_point{
                        std::chrono::milliseconds{rs->getInt64("created_at_ms")}};
                    
                    if (!rs->wasNull()) {
                        auto started_ms = rs->getInt64("started_at_ms");
                        if (!rs->wasNull() && started_ms > 0) {
                            session.started_at = std::chrono::system_clock::time_point{
                                std::chrono::milliseconds{started_ms}};
                        }
                    }
                    
                    if (!rs->wasNull()) {
                        auto finished_ms = rs->getInt64("finished_at_ms");
                        if (!rs->wasNull() && finished_ms > 0) {
                            session.finished_at = std::chrono::system_clock::time_point{
                                std::chrono::milliseconds{finished_ms}};
                        }
                    }
                    
                    session.last_activity_at = std::chrono::system_clock::time_point{
                        std::chrono::milliseconds{rs->getInt64("last_activity_at_ms")}};
                    
                    session.game_duration_ms = rs->getInt64("game_duration_ms");
                    
                    try {
                        session.final_scores = nlohmann::json::parse(std::string(rs->getString("final_scores")));
                    } catch (...) {
                        session.final_scores = nlohmann::json{};
                    }
                    
                    try {
                        auto winners_json = nlohmann::json::parse(std::string(rs->getString("winners")));
                        if (winners_json.is_array()) {
                            session.winners = winners_json.get<std::vector<std::string>>();
                        }
                    } catch (...) {
                        // 忽略解析错误
                    }
                    
                    return session;
                }
                
                return std::nullopt;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to get game session " + session_id + ": " + e.what());
                return std::nullopt;
            }
        }

        // ==================== 内部辅助方法 ====================

        std::string GamePersistenceManager::generateId() const {
            // 生成UUID风格的ID
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            
            // 生成128位随机数（16字节）
            for (int i = 0; i < 4; ++i) {
                uint32_t val = static_cast<uint32_t>(gen());
                ss << std::setw(8) << val;
                if (i == 0 || i == 1) ss << "-";
            }
            
            return ss.str();
        }

        std::string GamePersistenceManager::calculateChecksum(const nlohmann::json& data) const {
            std::string json_str = data.dump();
            
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char*>(json_str.c_str()), json_str.length(), hash);
            
            std::stringstream ss;
            for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
            }
            
            return ss.str();
        }

        void GamePersistenceManager::scheduleCleanupTask() {
            if (!thread_pool_) {
                LOG_WARNING("Thread pool not available, cleanup task not scheduled");
                return;
            }
            
            // 定期清理过期数据
            auto cleanup_task = [this]() {
                if (!running_.load()) return;
                
                try {
                    cleanupExpiredData();
                } catch (const std::exception& e) {
                    LOG_ERROR("Cleanup task failed: " + std::string(e.what()));
                }
                
                // 重新调度下次清理
                if (running_.load() && thread_pool_) {
                    std::this_thread::sleep_for(std::chrono::hours(config_.cleanup_interval_hours));
                    thread_pool_->submit([this]() { scheduleCleanupTask(); });
                }
            };
            
            thread_pool_->submit(cleanup_task);
        }

        nlohmann::json GamePersistenceManager::getPersistenceStats() const {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            
            nlohmann::json stats;
            stats["total_sessions_created"] = total_sessions_created_.load();
            stats["total_snapshots_saved"] = total_snapshots_saved_.load();
            stats["total_actions_logged"] = total_actions_logged_.load();
            stats["total_recoveries_performed"] = total_recoveries_performed_.load();
            stats["is_running"] = running_.load();
            stats["config"] = {
                {"auto_snapshot_interval_ms", config_.auto_snapshot_interval_ms},
                {"max_snapshots_per_session", config_.max_snapshots_per_session},
                {"snapshot_compression_enabled", config_.snapshot_compression_enabled},
                {"action_log_retention_days", config_.action_log_retention_days}
            };
            
            return stats;
        }

        // 注意：由于文件大小限制，这里只实现了部分核心方法
        // 其他方法（如saveGameSnapshot, getGameSnapshot等）需要在后续添加

    } // namespace game_base
} // namespace game_services


