#pragma once

#include "game_logic_base.h"
#include "game_persistence_manager.h"
#include <memory>
#include <chrono>

/**
 * @file persistent_game_logic.h
 * @brief 支持持久化的游戏逻辑基类
 * @details 扩展GameLogicBase，增加游戏状态持久化功能
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 支持持久化的游戏逻辑基类
         */
        class PersistentGameLogic : public GameLogicBase {
        public:
            /**
             * @brief 构造函数
             * @param game_type 游戏类型
             * @param persistence_manager 持久化管理器
             */
            explicit PersistentGameLogic(
                GameType game_type,
                std::shared_ptr<GamePersistenceManager> persistence_manager = nullptr
            );

            /**
             * @brief 析构函数
             */
            ~PersistentGameLogic() override;

            // 重写基类方法以增加持久化功能

            /**
             * @brief 初始化游戏（支持恢复）
             * @param player_ids 玩家ID列表
             * @param config 游戏配置
             * @param session_id 会话ID（用于恢复）
             * @return 是否初始化成功
             */
            bool initializeGame(const std::vector<std::string>& player_ids,
                               const nlohmann::json& config = {},
                               const std::string& session_id = "");

            /**
             * @brief 启动游戏
             * @return 是否启动成功
             */
            bool startGame() override;

            /**
             * @brief 暂停游戏
             * @return 是否暂停成功
             */
            bool pauseGame() override;

            /**
             * @brief 恢复游戏
             * @return 是否恢复成功
             */
            bool resumeGame() override;

            /**
             * @brief 停止游戏
             */
            void stopGame() override;

            /**
             * @brief 游戏主循环（增加自动保存）
             */
            void tick() override;

            /**
             * @brief 处理玩家操作（增加日志记录）
             * @param player_id 玩家ID
             * @param action 操作数据
             * @return 是否处理成功
             */
            bool processPlayerAction(const std::string& player_id, const nlohmann::json& action) override;

            // 持久化相关方法

            /**
             * @brief 设置持久化管理器
             */
            void setPersistenceManager(std::shared_ptr<GamePersistenceManager> manager);

            /**
             * @brief 获取持久化管理器
             */
            std::shared_ptr<GamePersistenceManager> getPersistenceManager() const;

            /**
             * @brief 设置会话ID
             */
            void setSessionId(const std::string& session_id);

            /**
             * @brief 获取会话ID
             */
            std::string getSessionId() const;

            /**
             * @brief 设置房间ID
             */
            void setRoomId(const std::string& room_id);

            /**
             * @brief 获取房间ID
             */
            std::string getRoomId() const;

            /**
             * @brief 启用自动快照
             * @param interval_ms 快照间隔（毫秒），0表示使用默认值
             */
            void enableAutoSnapshot(int interval_ms = 0);

            /**
             * @brief 禁用自动快照
             */
            void disableAutoSnapshot();

            /**
             * @brief 手动保存游戏快照
             * @param reason 保存原因
             * @return 是否保存成功
             */
            bool saveSnapshot(const std::string& reason = "manual");

            /**
             * @brief 从快照恢复游戏状态
             * @param snapshot_id 快照ID，空字符串表示最新快照
             * @return 是否恢复成功
             */
            bool restoreFromSnapshot(const std::string& snapshot_id = "");

            /**
             * @brief 创建恢复点
             * @param reason 恢复原因
             * @return 恢复点ID，失败返回空字符串
             */
            std::string createRecoveryPoint(const std::string& reason = "manual");

            /**
             * @brief 从恢复点恢复
             * @param recovery_id 恢复点ID，空字符串表示最新恢复点
             * @return 是否恢复成功
             */
            bool restoreFromRecoveryPoint(const std::string& recovery_id = "");

            /**
             * @brief 获取持久化统计信息
             */
            nlohmann::json getPersistenceStats() const;

            /**
             * @brief 设置游戏模式
             */
            void setGameMode(const std::string& game_mode);

            /**
             * @brief 获取游戏模式
             */
            std::string getGameMode() const;

        protected:
            /**
             * @brief 游戏状态改变时的回调（增加持久化）
             * @param new_state 新状态
             */
            void changeGameState(RoomState new_state) override;

            /**
             * @brief 创建游戏会话
             * @return 是否创建成功
             */
            bool createGameSession();

            /**
             * @brief 更新游戏会话
             * @return 是否更新成功
             */
            bool updateGameSession();

            /**
             * @brief 完成游戏会话
             * @param final_scores 最终分数
             * @param winners 获胜者列表
             * @return 是否完成成功
             */
            bool finishGameSession(const nlohmann::json& final_scores, 
                                 const std::vector<std::string>& winners);

            /**
             * @brief 记录玩家操作日志
             * @param player_id 玩家ID
             * @param action_type 操作类型
             * @param action_data 操作数据
             * @param is_valid 操作是否有效
             */
            void logPlayerAction(const std::string& player_id,
                               const std::string& action_type,
                               const nlohmann::json& action_data,
                               bool is_valid = true);

            /**
             * @brief 自动快照检查
             */
            void checkAutoSnapshot();

            /**
             * @brief 获取扩展的游戏状态（用于持久化）
             * @details 子类可重写此方法以添加额外的状态信息
             */
            virtual nlohmann::json getExtendedGameState() const;

            /**
             * @brief 应用扩展的游戏状态（用于恢复）
             * @details 子类可重写此方法以恢复额外的状态信息
             */
            virtual bool applyExtendedGameState(const nlohmann::json& extended_state);

            /**
             * @brief 验证游戏状态
             * @details 子类可重写此方法以提供自定义验证逻辑
             */
            virtual bool validateGameState(const nlohmann::json& game_state) const;

        private:
            std::shared_ptr<GamePersistenceManager> persistence_manager_;
            std::string session_id_;
            std::string room_id_;
            std::string game_mode_;
            
            // 自动快照控制
            bool auto_snapshot_enabled_ = false;
            int auto_snapshot_interval_ms_ = 30000;  // 默认30秒
            std::chrono::system_clock::time_point last_snapshot_time_;
            
            // 统计信息
            mutable std::mutex persistence_mutex_;
            std::atomic<int64_t> snapshots_saved_{0};
            std::atomic<int64_t> actions_logged_{0};
            std::atomic<int64_t> recoveries_performed_{0};

            // 内部方法
            std::string generateSessionId() const;
            GameSession createSessionObject() const;
            GameSnapshot createSnapshotObject(const std::string& snapshot_type, 
                                            const std::string& reason) const;
            GameActionLog createActionLogObject(const std::string& player_id,
                                               const std::string& action_type,
                                               const nlohmann::json& action_data,
                                               bool is_valid) const;
        };

        /**
         * @brief 持久化游戏逻辑工厂
         */
        class PersistentGameLogicFactory {
        public:
            /**
             * @brief 创建持久化游戏逻辑实例
             * @param game_type 游戏类型
             * @param persistence_manager 持久化管理器
             * @return 游戏逻辑实例
             */
            static std::unique_ptr<PersistentGameLogic> create(
                GameType game_type,
                std::shared_ptr<GamePersistenceManager> persistence_manager = nullptr
            );

            /**
             * @brief 从会话恢复游戏逻辑实例
             * @param session_id 会话ID
             * @param persistence_manager 持久化管理器
             * @return 游戏逻辑实例，失败返回nullptr
             */
            static std::unique_ptr<PersistentGameLogic> restoreFromSession(
                const std::string& session_id,
                std::shared_ptr<GamePersistenceManager> persistence_manager
            );
        };

    } // namespace game_base
} // namespace game_services


