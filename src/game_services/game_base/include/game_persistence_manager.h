#pragma once

#include "game_types.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include "common/thread_pool/thread_pool.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

/**
 * @file game_persistence_manager.h
 * @brief 游戏状态持久化管理器
 * @details 负责游戏会话、状态快照、操作日志的持久化管理
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 游戏会话信息
         */
        struct GameSession {
            std::string session_id;                    ///< 会话ID
            std::string room_id;                       ///< 房间ID
            GameType game_type;                        ///< 游戏类型
            std::string game_mode;                     ///< 游戏模式
            RoomState session_state;                   ///< 会话状态
            int player_count;                          ///< 玩家数量
            std::vector<std::string> player_ids;       ///< 玩家ID列表
            nlohmann::json game_config;                ///< 游戏配置
            nlohmann::json session_metadata;           ///< 会话元数据
            
            // 时间信息
            std::chrono::system_clock::time_point created_at;
            std::chrono::system_clock::time_point started_at;
            std::chrono::system_clock::time_point finished_at;
            std::chrono::system_clock::time_point last_activity_at;
            
            // 游戏结果
            int64_t game_duration_ms = 0;
            nlohmann::json final_scores;
            std::vector<std::string> winners;
            
            /**
             * @brief 转换为JSON
             */
            nlohmann::json toJson() const;
            
            /**
             * @brief 从JSON创建
             */
            static GameSession fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 游戏状态快照
         */
        struct GameSnapshot {
            std::string snapshot_id;                   ///< 快照ID
            std::string session_id;                    ///< 会话ID
            std::string snapshot_type;                 ///< 快照类型：auto/manual/checkpoint/error_recovery
            std::string snapshot_reason;               ///< 快照原因
            
            // 游戏状态数据
            nlohmann::json game_state;                 ///< 完整游戏状态
            int64_t tick_count = 0;                    ///< 游戏循环计数
            int64_t game_duration_ms = 0;              ///< 游戏持续时间
            nlohmann::json players_state;              ///< 玩家状态
            nlohmann::json world_state;                ///< 游戏世界状态
            
            // 压缩和版本信息
            bool is_compressed = false;                ///< 是否压缩存储
            std::string compression_type;              ///< 压缩类型
            int state_version = 1;                     ///< 状态版本
            std::string state_checksum;                ///< 状态校验和
            
            std::chrono::system_clock::time_point created_at;
            
            /**
             * @brief 转换为JSON
             */
            nlohmann::json toJson() const;
            
            /**
             * @brief 从JSON创建
             */
            static GameSnapshot fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 游戏操作日志
         */
        struct GameActionLog {
            std::string log_id;                        ///< 日志ID
            std::string session_id;                    ///< 会话ID
            std::string player_id;                     ///< 玩家ID
            
            // 操作信息
            std::string action_type;                   ///< 操作类型
            nlohmann::json action_data;                ///< 操作数据
            int64_t action_timestamp;                  ///< 操作时间戳（游戏内时间）
            int64_t tick_number = 0;                   ///< 操作发生的tick数
            
            // 验证信息
            bool is_valid = true;                      ///< 操作是否有效
            nlohmann::json validation_result;          ///< 验证结果
            
            // 系统信息
            std::chrono::system_clock::time_point server_timestamp;
            std::string server_id;                     ///< 处理服务器ID
            
            /**
             * @brief 转换为JSON
             */
            nlohmann::json toJson() const;
            
            /**
             * @brief 从JSON创建
             */
            static GameActionLog fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 恢复点信息
         */
        struct GameRecoveryPoint {
            std::string recovery_id;                   ///< 恢复点ID
            std::string session_id;                    ///< 会话ID
            std::string snapshot_id;                   ///< 关联的快照ID
            
            std::string recovery_type;                 ///< 恢复点类型
            std::string recovery_reason;               ///< 恢复原因
            bool is_stable = true;                     ///< 是否为稳定状态
            
            nlohmann::json state_validation;           ///< 状态验证信息
            nlohmann::json recovery_metadata;          ///< 恢复元数据
            
            std::chrono::system_clock::time_point created_at;
            std::chrono::system_clock::time_point last_used_at;
            
            /**
             * @brief 转换为JSON
             */
            nlohmann::json toJson() const;
            
            /**
             * @brief 从JSON创建
             */
            static GameRecoveryPoint fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 持久化结果
         */
        struct PersistenceResult {
            bool success = false;                      ///< 是否成功
            std::string error_message;                 ///< 错误信息
            std::string operation_id;                  ///< 操作ID
            int64_t execution_time_ms = 0;             ///< 执行时间
            nlohmann::json metadata;                   ///< 附加元数据
            
            PersistenceResult() = default;
            PersistenceResult(bool success, const std::string& error = "")
                : success(success), error_message(error) {}
        };

        /**
         * @brief 游戏状态持久化管理器配置
         */
        struct GamePersistenceConfig {
            // 快照配置
            int auto_snapshot_interval_ms = 30000;    ///< 自动快照间隔（毫秒）
            int max_snapshots_per_session = 50;       ///< 每个会话最大快照数量
            bool snapshot_compression_enabled = true;  ///< 是否启用快照压缩
            int snapshot_compression_threshold = 10240; ///< 快照压缩阈值（字节）
            
            // 日志配置
            int action_log_retention_days = 30;       ///< 操作日志保留天数
            int recovery_point_retention_hours = 72;  ///< 恢复点保留小时数
            
            // 验证配置
            bool enable_state_validation = true;      ///< 是否启用状态验证
            int max_recovery_attempts = 3;            ///< 最大恢复尝试次数
            
            // 性能配置
            int batch_size = 100;                     ///< 批处理大小
            int cleanup_interval_hours = 24;          ///< 清理间隔（小时）
            
            /**
             * @brief 从配置管理器加载
             */
            static GamePersistenceConfig fromConfigManager();
            
            /**
             * @brief 验证配置
             */
            bool validate() const;
        };

        /**
         * @brief 游戏状态持久化管理器
         */
        class GamePersistenceManager {
        public:
            /**
             * @brief 构造函数
             */
            explicit GamePersistenceManager(
                const GamePersistenceConfig& config,
                std::shared_ptr<common::database::MySQLPool> mysql_pool,
                std::shared_ptr<common::database::RedisPool> redis_pool = nullptr,
                std::shared_ptr<common::thread_pool::ThreadPool> thread_pool = nullptr
            );

            /**
             * @brief 析构函数
             */
            ~GamePersistenceManager();

            // 禁用拷贝构造和赋值
            GamePersistenceManager(const GamePersistenceManager&) = delete;
            GamePersistenceManager& operator=(const GamePersistenceManager&) = delete;

            /**
             * @brief 启动持久化管理器
             */
            bool start();

            /**
             * @brief 停止持久化管理器
             */
            void stop();

            /**
             * @brief 检查是否运行中
             */
            bool isRunning() const { return running_; }

            // ==================== 会话管理 ====================

            /**
             * @brief 创建游戏会话
             */
            PersistenceResult createGameSession(const GameSession& session);

            /**
             * @brief 更新游戏会话
             */
            PersistenceResult updateGameSession(const GameSession& session);

            /**
             * @brief 获取游戏会话
             */
            std::optional<GameSession> getGameSession(const std::string& session_id);

            /**
             * @brief 结束游戏会话
             */
            PersistenceResult finishGameSession(const std::string& session_id, 
                                              const nlohmann::json& final_scores,
                                              const std::vector<std::string>& winners);

            /**
             * @brief 删除游戏会话
             */
            PersistenceResult deleteGameSession(const std::string& session_id);

            // ==================== 快照管理 ====================

            /**
             * @brief 保存游戏状态快照
             */
            PersistenceResult saveGameSnapshot(const GameSnapshot& snapshot);

            /**
             * @brief 获取游戏状态快照
             */
            std::optional<GameSnapshot> getGameSnapshot(const std::string& snapshot_id);

            /**
             * @brief 获取会话的所有快照
             */
            std::vector<GameSnapshot> getSessionSnapshots(const std::string& session_id, 
                                                         int limit = 50);

            /**
             * @brief 获取最新快照
             */
            std::optional<GameSnapshot> getLatestSnapshot(const std::string& session_id);

            /**
             * @brief 删除快照
             */
            PersistenceResult deleteGameSnapshot(const std::string& snapshot_id);

            // ==================== 操作日志 ====================

            /**
             * @brief 记录游戏操作
             */
            PersistenceResult logGameAction(const GameActionLog& action_log);

            /**
             * @brief 批量记录游戏操作
             */
            PersistenceResult logGameActions(const std::vector<GameActionLog>& action_logs);

            /**
             * @brief 获取操作日志
             */
            std::vector<GameActionLog> getActionLogs(const std::string& session_id, 
                                                   int64_t from_tick = 0, 
                                                   int limit = 1000);

            // ==================== 恢复管理 ====================

            /**
             * @brief 创建恢复点
             */
            PersistenceResult createRecoveryPoint(const GameRecoveryPoint& recovery_point);

            /**
             * @brief 获取恢复点
             */
            std::optional<GameRecoveryPoint> getRecoveryPoint(const std::string& recovery_id);

            /**
             * @brief 获取会话的恢复点列表
             */
            std::vector<GameRecoveryPoint> getSessionRecoveryPoints(const std::string& session_id);

            /**
             * @brief 恢复游戏状态
             */
            std::optional<GameSnapshot> recoverGameState(const std::string& session_id, 
                                                       const std::string& recovery_id = "");

            // ==================== 自动化功能 ====================

            /**
             * @brief 启用自动快照
             */
            void enableAutoSnapshot(const std::string& session_id);

            /**
             * @brief 禁用自动快照
             */
            void disableAutoSnapshot(const std::string& session_id);

            /**
             * @brief 触发自动快照
             */
            void triggerAutoSnapshot(const std::string& session_id, 
                                   const nlohmann::json& game_state,
                                   int64_t tick_count,
                                   int64_t game_duration_ms);

            // ==================== 统计和监控 ====================

            /**
             * @brief 获取持久化统计信息
             */
            nlohmann::json getPersistenceStats() const;

            /**
             * @brief 获取会话统计信息
             */
            nlohmann::json getSessionStats(const std::string& session_id) const;

            /**
             * @brief 清理过期数据
             */
            PersistenceResult cleanupExpiredData();

        private:
            GamePersistenceConfig config_;
            std::shared_ptr<common::database::MySQLPool> mysql_pool_;
            std::shared_ptr<common::database::RedisPool> redis_pool_;
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;

            std::atomic<bool> running_{false};
            std::mutex auto_snapshot_mutex_;
            std::unordered_map<std::string, bool> auto_snapshot_sessions_;

            // 统计信息
            mutable std::mutex stats_mutex_;
            std::atomic<int64_t> total_sessions_created_{0};
            std::atomic<int64_t> total_snapshots_saved_{0};
            std::atomic<int64_t> total_actions_logged_{0};
            std::atomic<int64_t> total_recoveries_performed_{0};

            // 内部方法
            std::string generateId() const;
            std::string calculateChecksum(const nlohmann::json& data) const;
            std::string compressData(const std::string& data) const;
            std::string decompressData(const std::string& compressed_data) const;
            bool validateGameState(const nlohmann::json& game_state) const;
            
            // 数据库操作
            PersistenceResult executeTransaction(std::function<void()> operations);
            void scheduleCleanupTask();
        };

    } // namespace game_base
} // namespace game_services


