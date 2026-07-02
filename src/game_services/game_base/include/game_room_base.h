#pragma once

#include "game_types.h"
#include "game_logic_base.h"
#include "player_session_base.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <nlohmann/json.hpp>

/**
 * @file game_room_base.h
 * @brief 游戏房间基类定义
 * @details 提供游戏房间的通用功能，包括玩家管理、游戏状态管理等
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 游戏房间基类
         * @details 所有具体游戏房间的基类，提供通用的房间管理功能
         */
        class GameRoomBase {
        public:
            // 回调函数类型定义
            using StateChangeCallback = std::function<void(RoomState, RoomState)>;
            using PlayerJoinCallback = std::function<void(const std::string&)>;
            using PlayerLeaveCallback = std::function<void(const std::string&)>;
            using GameEndCallback = std::function<void(const std::vector<std::string>&)>;
            using MessageCallback = std::function<void(const std::string&, const nlohmann::json&)>;

            /**
             * @brief 构造函数
             * @param room_id 房间ID
             * @param creator_id 创建者ID
             * @param game_type 游戏类型
             * @param max_players 最大玩家数
             */
            explicit GameRoomBase(const std::string& room_id, 
                                 const std::string& creator_id,
                                 GameType game_type,
                                 int max_players = 4);

            /**
             * @brief 虚析构函数
             */
            virtual ~GameRoomBase();

            // 禁用拷贝构造和赋值
            GameRoomBase(const GameRoomBase&) = delete;
            GameRoomBase& operator=(const GameRoomBase&) = delete;

            // 纯虚函数 - 子类必须实现
            /**
             * @brief 启动游戏特定逻辑
             * @return 是否启动成功
             */
            virtual bool startGameSpecific() = 0;

            /**
             * @brief 处理游戏特定操作
             * @param player_id 玩家ID
             * @param action 操作数据
             * @return 是否处理成功
             */
            virtual bool processGameActionSpecific(const std::string& player_id, 
                                                  const nlohmann::json& action) = 0;

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
             * @brief 结束游戏特定逻辑
             */
            virtual void endGameSpecific() {}

            // 通用功能 - 基类实现
            /**
             * @brief 添加玩家到房间
             * @param player_id 玩家ID
             * @param session 玩家会话
             * @return 是否添加成功
             */
            bool addPlayer(const std::string& player_id, std::shared_ptr<PlayerSessionBase> session);

            /**
             * @brief 从房间移除玩家
             * @param player_id 玩家ID
             * @return 是否移除成功
             */
            bool removePlayer(const std::string& player_id);

            /**
             * @brief 检查玩家是否在房间中
             * @param player_id 玩家ID
             * @return 是否在房间中
             */
            bool isPlayerInRoom(const std::string& player_id) const;

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
             * @brief 结束游戏
             * @param winners 获胜者列表
             */
            void endGame(const std::vector<std::string>& winners = {});

            /**
             * @brief 处理游戏操作
             * @param player_id 玩家ID
             * @param action 操作数据
             * @return 是否处理成功
             */
            bool processGameAction(const std::string& player_id, const nlohmann::json& action);

            // 状态查询
            RoomState getState() const { return state_; }
            std::string getRoomId() const { return room_id_; }
            std::string getCreatorId() const { return creator_id_; }
            GameType getGameType() const { return game_type_; }
            int getPlayerCount() const;
            int getMaxPlayers() const { return max_players_; }
            bool isFull() const { return getPlayerCount() >= max_players_; }
            bool isEmpty() const { return getPlayerCount() == 0; }
            std::vector<std::string> getPlayerIds() const;

            /**
             * @brief 获取房间信息
             * @return 房间信息JSON
             */
            virtual nlohmann::json getRoomInfo() const;

            /**
             * @brief 检查玩家是否可以开始游戏
             * @param player_id 玩家ID
             * @return 是否有权限开始游戏
             */
            virtual bool canPlayerStartGame(const std::string& player_id) const;

            /**
             * @brief 获取完整游戏状态
             * @return 游戏状态JSON
             */
            nlohmann::json getFullGameState() const;

            // 消息广播
            /**
             * @brief 向所有玩家广播消息
             * @param message 消息内容
             */
            void broadcastToPlayers(const nlohmann::json& message);

            /**
             * @brief 向指定玩家发送消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void sendToPlayer(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 向除指定玩家外的其他玩家广播消息
             * @param exclude_player_id 排除的玩家ID
             * @param message 消息内容
             */
            void broadcastToOthers(const std::string& exclude_player_id, const nlohmann::json& message);

            // 回调函数设置
            void setStateChangeCallback(StateChangeCallback callback) { state_change_callback_ = callback; }
            void setPlayerJoinCallback(PlayerJoinCallback callback) { player_join_callback_ = callback; }
            void setPlayerLeaveCallback(PlayerLeaveCallback callback) { player_leave_callback_ = callback; }
            void setGameEndCallback(GameEndCallback callback) { game_end_callback_ = callback; }
            void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }

            /**
             * @brief 更新最后活动时间
             */
            void updateLastActivity();

            /**
             * @brief 获取最后活动时间
             * @return 最后活动时间
             */
            std::chrono::system_clock::time_point getLastActivity() const { return last_activity_; }

            /**
             * @brief 检查房间是否过期
             * @param timeout_ms 超时时间(毫秒)
             * @return 是否过期
             */
            bool isExpired(int timeout_ms = 300000) const; // 默认5分钟

            /**
             * @brief 强制停止房间
             * @param reason 停止原因
             */
            void forceStop(const std::string& reason = "Force stop");

            /**
             * @brief 清理房间资源
             */
            void cleanup();

        protected:
            std::string room_id_;                           // 房间ID
            std::string creator_id_;                        // 创建者ID
            GameType game_type_;                            // 游戏类型
            RoomState state_;                               // 房间状态
            int max_players_;                               // 最大玩家数

            // 玩家管理
            std::unordered_map<std::string, std::shared_ptr<PlayerSessionBase>> players_;
            mutable std::shared_mutex players_mutex_;

            // 游戏逻辑
            std::unique_ptr<GameLogicBase> game_logic_;

            // 时间戳
            std::chrono::system_clock::time_point created_at_;
            std::chrono::system_clock::time_point last_activity_;

            // 回调函数
            StateChangeCallback state_change_callback_;
            PlayerJoinCallback player_join_callback_;
            PlayerLeaveCallback player_leave_callback_;
            GameEndCallback game_end_callback_;
            MessageCallback message_callback_;

            /**
             * @brief 改变房间状态
             * @param new_state 新状态
             */
            void changeState(RoomState new_state);

            /**
             * @brief 玩家加入时的处理
             * @param player_id 玩家ID
             * @param current_player_count 当前玩家数量（避免重复加锁）
             */
            virtual void onPlayerJoined(const std::string& player_id, int current_player_count = -1);

            /**
             * @brief 玩家离开时的处理
             * @param player_id 玩家ID
             */
            virtual void onPlayerLeft(const std::string& player_id);

            /**
             * @brief 游戏状态改变时的处理
             */
            virtual void onGameStateChanged();

        private:
            mutable std::shared_mutex room_mutex_;
        };

    } // namespace game_base
} // namespace game_services
