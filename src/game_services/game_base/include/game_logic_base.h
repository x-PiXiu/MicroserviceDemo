#pragma once

#include "game_types.h"
#include "common/network/event_loop.h"
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

/**
 * @file game_logic_base.h
 * @brief 游戏逻辑基类定义
 * @details 提供游戏逻辑的通用框架，包括游戏循环、状态管理等
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 游戏逻辑基类
         * @details 所有具体游戏逻辑的基类，提供通用的游戏循环和状态管理
         */
        class GameLogicBase {
        public:
            // 回调函数类型定义
            using StateUpdateCallback = std::function<void(const nlohmann::json&)>;
            using GameEndCallback = std::function<void(const std::vector<std::string>&)>;
            using PlayerActionCallback = std::function<void(const std::string&, const nlohmann::json&)>;
            using ErrorCallback = std::function<void(const std::string&)>;

            /**
             * @brief 构造函数
             * @param game_type 游戏类型
             */
            explicit GameLogicBase(GameType game_type);

            /**
             * @brief 虚析构函数
             */
            virtual ~GameLogicBase();

            // 禁用拷贝构造和赋值
            GameLogicBase(const GameLogicBase&) = delete;
            GameLogicBase& operator=(const GameLogicBase&) = delete;

            // 纯虚函数 - 子类必须实现
            /**
             * @brief 初始化游戏特定逻辑
             * @param player_ids 玩家ID列表
             * @param config 游戏配置
             * @return 是否初始化成功
             */
            virtual bool initializeGameSpecific(const std::vector<std::string>& player_ids,
                                               const nlohmann::json& config = {}) = 0;

            /**
             * @brief 游戏特定的循环逻辑
             * @details 每个游戏循环周期调用一次
             */
            virtual void tickGameSpecific() = 0;

            /**
             * @brief 处理玩家特定操作
             * @param player_id 玩家ID
             * @param action 操作数据
             * @return 是否处理成功
             */
            virtual bool processPlayerActionSpecific(const std::string& player_id,
                                                    const nlohmann::json& action) = 0;

            /**
             * @brief 检查游戏是否结束
             * @return 是否结束
             */
            virtual bool checkGameEndSpecific() = 0;

            /**
             * @brief 获取游戏特定状态
             * @return 游戏状态JSON
             */
            virtual nlohmann::json getGameSpecificState() const = 0;

            /**
             * @brief 暂停游戏特定逻辑
             */
            virtual void pauseGameSpecific() {}

            /**
             * @brief 恢复游戏特定逻辑
             */
            virtual void resumeGameSpecific() {}

            /**
             * @brief 重置游戏特定状态
             */
            virtual void resetGameSpecific() {}

            // 通用功能 - 基类实现
            /**
             * @brief 初始化游戏
             * @param player_ids 玩家ID列表
             * @param config 游戏配置
             * @return 是否初始化成功
             */
            bool initializeGame(const std::vector<std::string>& player_ids,
                               const nlohmann::json& config = {});

            /**
             * @brief 启动游戏
             * @return 是否启动成功
             */
            bool startGame();

            /**
             * @brief 暂停游戏
             * @return 是否暂停成功
             */
            bool pauseGame();

            /**
             * @brief 恢复游戏
             * @return 是否恢复成功
             */
            bool resumeGame();

            /**
             * @brief 停止游戏
             */
            void stopGame();

            /**
             * @brief 强制停止游戏
             */
            void forceStop();

            /**
             * @brief 清理游戏资源
             */
            void cleanup();

            /**
             * @brief 重置游戏
             */
            void resetGame();

            /**
             * @brief 游戏主循环
             * @details 由外部定时器调用
             */
            void tick();

            /**
             * @brief 处理玩家操作
             * @param player_id 玩家ID
             * @param action 操作数据
             * @return 是否处理成功
             */
            bool processPlayerAction(const std::string& player_id, const nlohmann::json& action);

            /**
             * @brief 添加玩家
             * @param player_id 玩家ID
             * @return 是否添加成功
             */
            bool addPlayer(const std::string& player_id);

            /**
             * @brief 移除玩家
             * @param player_id 玩家ID
             * @return 是否移除成功
             */
            bool removePlayer(const std::string& player_id);

            // 状态查询
            /**
             * @brief 获取游戏类型
             * @return 游戏类型
             */
            GameType getGameType() const { return game_type_; }

            /**
             * @brief 获取游戏状态
             * @return 游戏状态
             */
            RoomState getGameState() const { return game_state_; }

            /**
             * @brief 检查游戏是否正在运行
             * @return 是否正在运行
             */
            bool isGameRunning() const { return game_state_ == RoomState::PLAYING; }

            /**
             * @brief 检查游戏是否已暂停
             * @return 是否已暂停
             */
            bool isGamePaused() const { return game_state_ == RoomState::PAUSED; }

            /**
             * @brief 检查游戏是否已结束
             * @return 是否已结束
             */
            bool isGameFinished() const { return game_state_ == RoomState::FINISHED; }

            /**
             * @brief 获取玩家列表
             * @return 玩家ID列表
             */
            std::vector<std::string> getPlayerIds() const { return player_ids_; }

            /**
             * @brief 获取玩家数量
             * @return 玩家数量
             */
            int getPlayerCount() const { return static_cast<int>(player_ids_.size()); }

            /**
             * @brief 获取游戏开始时间
             * @return 开始时间
             */
            std::chrono::system_clock::time_point getStartTime() const { return start_time_; }

            /**
             * @brief 获取游戏持续时间
             * @return 持续时间(毫秒)
             */
            int64_t getGameDurationMs() const;

            /**
             * @brief 获取游戏循环次数
             * @return 循环次数
             */
            int64_t getTickCount() const { return tick_count_; }

            /**
             * @brief 获取完整游戏状态
             * @return 游戏状态JSON
             */
            nlohmann::json getFullGameState() const;

            // 回调函数设置
            void setStateUpdateCallback(StateUpdateCallback callback) { state_update_callback_ = callback; }
            void setGameEndCallback(GameEndCallback callback) { game_end_callback_ = callback; }
            void setPlayerActionCallback(PlayerActionCallback callback) { player_action_callback_ = callback; }
            void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }

            /**
             * @brief 设置游戏循环间隔
             * @param interval_ms 间隔时间(毫秒)
             */
            void setTickInterval(int interval_ms) { tick_interval_ms_ = interval_ms; }

            /**
             * @brief 获取游戏循环间隔
             * @return 间隔时间(毫秒)
             */
            int getTickInterval() const { return tick_interval_ms_; }

            /**
             * @brief 设置EventLoop（用于游戏主循环）
             * @param event_loop EventLoop实例
             */
            void setEventLoop(std::shared_ptr<common::network::EventLoop> event_loop) {
                event_loop_ = event_loop;
            }

            /**
             * @brief 检查游戏循环是否运行
             * @return 是否运行中
             */
            bool isGameLoopRunning() const { return game_loop_running_.load(); }

        protected:
            GameType game_type_;                            // 游戏类型
            RoomState game_state_;                          // 游戏状态
            std::vector<std::string> player_ids_;           // 玩家ID列表
            nlohmann::json game_config_;                    // 游戏配置

            // 时间管理
            std::chrono::system_clock::time_point start_time_;
            std::chrono::system_clock::time_point last_tick_time_;
            std::atomic<int64_t> tick_count_{0};
            int tick_interval_ms_ = 100;                    // 默认100ms

            // 游戏主循环（时间轮实现）
            std::weak_ptr<common::network::EventLoop> event_loop_;  // EventLoop弱引用
            uint64_t game_loop_timer_id_ = 0;               // 游戏主循环定时器ID
            std::atomic<bool> game_loop_running_{false};    // 游戏循环运行状态

            // 回调函数
            StateUpdateCallback state_update_callback_;
            GameEndCallback game_end_callback_;
            PlayerActionCallback player_action_callback_;
            ErrorCallback error_callback_;

            /**
             * @brief 通知状态更新
             */
            void notifyStateUpdate();

            /**
             * @brief 通知游戏结束
             * @param winners 获胜者列表
             */
            void notifyGameEnd(const std::vector<std::string>& winners);

            /**
             * @brief 通知玩家操作
             * @param player_id 玩家ID
             * @param action 操作数据
             */
            void notifyPlayerAction(const std::string& player_id, const nlohmann::json& action);

            /**
             * @brief 通知错误
             * @param error_message 错误消息
             */
            void notifyError(const std::string& error_message);

            /**
             * @brief 改变游戏状态
             * @param new_state 新状态
             */
            void changeGameState(RoomState new_state);

            /**
             * @brief 强制停止时的清理操作（由子类实现）
             */
            virtual void onForceStop() {}

            /**
             * @brief 清理时的操作（由子类实现）
             */
            virtual void onCleanup() {}

            /**
             * @brief 启动游戏主循环定时器
             */
            bool startGameLoop();

            /**
             * @brief 停止游戏主循环定时器
             */
            void stopGameLoop();

        private:
            mutable std::shared_mutex logic_mutex_;
            std::atomic<bool> running_{false};
        };

    } // namespace game_base
} // namespace game_services
