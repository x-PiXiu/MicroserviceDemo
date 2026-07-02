#pragma once

#include "game_room_base.h"
#include "gomoku_logic.h"
#include "gomoku_types.h"
#include <unordered_set>
#include <memory>

/**
 * @file gomoku_room.h
 * @brief 五子棋游戏房间类定义
 * @details 实现五子棋的房间管理功能，包括玩家匹配、游戏控制、观战等
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 五子棋游戏房间类
         * @details 继承自GameRoomBase，实现五子棋特定的房间管理功能
         */
        class GomokuRoom : public game_base::GameRoomBase {
        public:
            /**
             * @brief 构造函数
             * @param room_id 房间ID
             * @param creator_id 创建者ID
             * @param config 五子棋配置
             * @param event_loop EventLoop实例（用于时间轮定时器）
             */
            explicit GomokuRoom(const std::string& room_id, 
                              const std::string& creator_id,
                              const GomokuConfig& config = GomokuConfig(),
                              std::shared_ptr<common::network::EventLoop> event_loop = nullptr);

            /**
             * @brief 虚析构函数
             */
            virtual ~GomokuRoom();

            // 继承自GameRoomBase的纯虚函数实现
            bool startGameSpecific() override;
            bool processGameActionSpecific(const std::string& player_id,
                                         const nlohmann::json& action) override;
            nlohmann::json getGameSpecificState() const override;
            nlohmann::json getRoomInfo() const override;  // 重写以添加玩家详细信息
            void pauseGameSpecific() override;
            void resumeGameSpecific() override;
            void endGameSpecific() override;

            // 五子棋特定功能
            /**
             * @brief 设置玩家棋子颜色
             * @param player_id 玩家ID
             * @param piece 棋子类型
             * @return 是否设置成功
             */
            bool setPlayerPiece(const std::string& player_id, PieceType piece);

            /**
             * @brief 获取玩家棋子颜色
             * @param player_id 玩家ID
             * @return 棋子类型
             */
            PieceType getPlayerPiece(const std::string& player_id) const;

            /**
             * @brief 设置玩家准备状态
             * @param player_id 玩家ID
             * @param ready 是否准备
             * @return 是否设置成功
             */
            bool setPlayerReady(const std::string& player_id, bool ready);

            /**
             * @brief 获取玩家准备状态
             * @param player_id 玩家ID
             * @return 是否已准备
             */
            bool isPlayerReady(const std::string& player_id) const;

            /**
             * @brief 获取所有玩家的准备状态
             * @return 玩家准备状态列表
             */
            std::vector<std::pair<std::string, bool>> getAllPlayersReadyStatus() const;

            /**
             * @brief 添加观战者
             * @param spectator_id 观战者ID
             * @param session 观战者会话
             * @return 是否添加成功
             */
            bool addSpectator(const std::string& spectator_id, 
                            std::shared_ptr<game_base::PlayerSessionBase> session);

            /**
             * @brief 移除观战者
             * @param spectator_id 观战者ID
             * @return 是否移除成功
             */
            bool removeSpectator(const std::string& spectator_id);

            /**
             * @brief 检查是否为观战者
             * @param spectator_id 观战者ID
             * @return 是否为观战者
             */
            bool isSpectator(const std::string& spectator_id) const;

            /**
             * @brief 获取观战者数量
             * @return 观战者数量
             */
            int getSpectatorCount() const;

            /**
             * @brief 获取观战者列表
             * @return 观战者ID列表
             */
            std::vector<std::string> getSpectatorIds() const;

            /**
             * @brief 向观战者广播消息
             * @param message 消息内容
             */
            void broadcastToSpectators(const nlohmann::json& message);

            /**
             * @brief 处理落子操作
             * @param player_id 玩家ID
             * @param position 落子位置
             * @return 是否成功
             */
            bool handlePlacePiece(const std::string& player_id, const Position& position);

            /**
             * @brief 处理悔棋请求
             * @param player_id 玩家ID
             * @return 是否成功
             */
            bool handleUndoMove(const std::string& player_id);

            /**
             * @brief 处理投降
             * @param player_id 玩家ID
             * @return 是否成功
             */
            bool handleSurrender(const std::string& player_id);

            /**
             * @brief 处理和棋提议
             * @param player_id 玩家ID
             * @return 是否成功
             */
            bool handleDrawOffer(const std::string& player_id);

            /**
             * @brief 处理和棋回应
             * @param player_id 玩家ID
             * @param accept 是否接受
             * @return 是否成功
             */
            bool handleDrawResponse(const std::string& player_id, bool accept);

            /**
             * @brief 处理悔棋请求（发送给对手确认）
             * @param player_id 请求悔棋的玩家ID
             * @return 是否成功发送请求
             */
            bool handleUndoRequest(const std::string& player_id);

            /**
             * @brief 处理悔棋响应（对手同意/拒绝）
             * @param player_id 响应的玩家ID
             * @param accept 是否同意
             * @return 是否成功处理响应
             */
            bool handleUndoResponse(const std::string& player_id, bool accept);

            /**
             * @brief 获取游戏配置
             */
            const GomokuConfig& getGomokuConfig() const { return gomoku_config_; }

            /**
             * @brief 获取游戏模式
             * @return 当前房间的游戏模式
             */
            GameMode getGameMode() const { return gomoku_config_.gameMode; }

            /**
             * @brief 获取游戏开始时间
             * @return 游戏实际开始时间
             */
            std::chrono::system_clock::time_point getGameStartTime() const { return game_start_time_; }

            /**
             * @brief 更新游戏配置（仅在游戏开始前）
             * @param config 新配置
             * @return 是否更新成功
             */
            bool updateGomokuConfig(const GomokuConfig& config);

            /**
             * @brief 获取游戏逻辑对象
             */
            std::shared_ptr<GomokuLogic> getGomokuLogic() const { return gomoku_logic_; }
            
            /**
             * @brief 获取游戏阶段描述字符串
             * @return 游戏阶段描述
             */
            std::string getGamePhaseString() const;

            /**
             * @brief 自动匹配棋子颜色
             * @details 第一个玩家为黑子，第二个玩家为白子
             */
            void autoAssignPieces();

            /**
             * @brief 检查是否可以开始游戏
             * @return 是否可以开始
             */
            bool canStartGame() const;

            /**
             * @brief 获取对手玩家ID
             * @param player_id 玩家ID
             * @return 对手玩家ID，如果不存在则返回空字符串
             */
            std::string getOpponentId(const std::string& player_id) const;

            /**
             * @brief 获取当前回合玩家ID
             * @return 当前回合玩家ID
             */
            std::string getCurrentTurnPlayerId() const;

            /**
             * @brief 检查玩家是否有权限开始游戏
             * @param player_id 玩家ID
             * @return 是否有权限
             */
            bool canPlayerStartGame(const std::string& player_id) const override;

            /**
             * @brief 获取房间统计信息
             * @return 统计信息JSON
             */
            nlohmann::json getRoomStats() const;

            /**
             * @brief 获取房间创建时间
             * @return 创建时间
             */
            std::chrono::system_clock::time_point getCreatedAt() const { return created_at_; }

            /**
             * @brief 重置房间到初始状态
             */
            void resetRoom();

            // 生命周期回调方法
            /**
             * @brief 设置房间创建回调
             * @param callback 房间创建时的回调函数
             */
            void setRoomCreatedCallback(std::function<void(const std::string&)> callback);

            /**
             * @brief 设置房间销毁回调
             * @param callback 房间销毁时的回调函数
             */
            void setRoomDestroyedCallback(std::function<void(const std::string&)> callback);

            /**
             * @brief 设置玩家加入回调
             * @param callback 玩家加入时的回调函数(room_id, player_id)
             */
            void setPlayerJoinCallback(std::function<void(const std::string&, const std::string&)> callback);

            /**
             * @brief 设置玩家离开回调
             * @param callback 玩家离开时的回调函数(room_id, player_id)
             */
            void setPlayerLeaveCallback(std::function<void(const std::string&, const std::string&)> callback);

            /**
             * @brief 设置游戏结束回调
             * @param callback 游戏结束时的回调函数(room_id, winner_ids)
             */
            void setGameEndCallback(std::function<void(const std::string&, const std::vector<std::string>&)> callback);

            /**
             * @brief 设置游戏结束回调（带上下文）
             * @details 新增回调，传递完整的游戏结束上下文，用于调用 Game Data Service 结算 API
             * @param callback 游戏结束时的回调函数(room_id, winner_ids, context)
             */
            void setGameEndCallbackWithContext(
                std::function<void(const std::string&, const std::vector<std::string>&, const GameEndContext&)> callback);

            /**
             * @brief 构建游戏结束上下文
             * @details 收集游戏结束时的所有数据，用于提交给 Game Data Service 结算
             * @param result 游戏结果
             * @param winner_id 获胜者ID
             * @return 游戏结束上下文
             */
            GameEndContext buildGameEndContext(GameResult result, const std::string& winner_id);

            /**
             * @brief 设置玩家准备状态变化回调
             * @param callback 准备状态变化时的回调函数(room_id, player_id, ready)
             */
            void setPlayerReadyCallback(std::function<void(const std::string&, const std::string&, bool)> callback);

            /**
             * @brief 设置游戏开始回调
             * @param callback 游戏开始时的回调函数(room_id)
             */
            void setGameStartCallback(std::function<void(const std::string&)> callback);

            /**
             * @brief 设置获取玩家信息回调
             * @param callback 获取玩家信息的回调函数(player_id) -> PlayerInfo JSON
             */
            void setPlayerInfoCallback(std::function<nlohmann::json(const std::string&)> callback);

        protected:
            /**
             * @brief 玩家加入时的处理
             */
            void onPlayerJoined(const std::string& player_id, int current_player_count = -1) override;

            /**
             * @brief 玩家离开时的处理
             */
            void onPlayerLeft(const std::string& player_id) override;

            /**
             * @brief 游戏状态改变时的处理
             */
            void onGameStateChanged() override;

        private:
            GomokuConfig gomoku_config_;                    // 五子棋配置
            std::shared_ptr<GomokuLogic> gomoku_logic_;     // 五子棋游戏逻辑
            std::weak_ptr<common::network::EventLoop> event_loop_;  // EventLoop实例（弱引用）

            // 玩家棋子映射
            std::unordered_map<std::string, PieceType> player_pieces_;
            std::unordered_map<PieceType, std::string> piece_players_;

            // 观战者管理
            std::unordered_map<std::string, std::shared_ptr<game_base::PlayerSessionBase>> spectators_;

            // 生命周期回调函数
            std::function<void(const std::string&)> room_created_callback_;
            std::function<void(const std::string&)> room_destroyed_callback_;
            std::function<void(const std::string&, const std::string&)> player_join_callback_;
            std::function<void(const std::string&, const std::string&)> player_leave_callback_;
            std::function<void(const std::string&, const std::vector<std::string>&)> game_end_callback_;
            std::function<void(const std::string&, const std::vector<std::string>&, const GameEndContext&)> game_end_callback_with_context_;
            std::function<void(const std::string&, const std::string&, bool)> player_ready_callback_;
            std::function<void(const std::string&)> game_start_callback_;
            std::function<nlohmann::json(const std::string&)> player_info_callback_;  // 获取玩家信息回调
            mutable std::shared_mutex spectators_mutex_;

            // 游戏统计
            std::chrono::system_clock::time_point game_start_time_;   // 游戏实际开始时间
            std::chrono::system_clock::time_point game_end_time_;     // 游戏结束时间
            int total_moves_ = 0;

            // 悔棋请求状态（请求-响应流程）
            std::string pending_undo_requester_;  // 发起悔棋请求的玩家ID
            bool has_pending_undo_request_ = false;  // 是否有待处理的悔棋请求

            /**
             * @brief 初始化游戏逻辑
             */
            void initializeGameLogic();

            /**
             * @brief 设置游戏逻辑回调
             */
            void setupGameLogicCallbacks();

            /**
             * @brief 广播游戏状态
             */
            void broadcastGameState();

            /**
             * @brief 广播移动结果
             */
            void broadcastMoveResult(const std::string& player_id, const Position& position, bool success);

            /**
             * @brief 广播游戏结束
             */
            void broadcastGameEnd(GameResult result, const std::string& winner_id = "");

            /**
             * @brief 广播时间更新
             */
            void broadcastTimeUpdate();

            /**
             * @brief 处理游戏结束
             */
            void handleGameEnd(GameResult result);

            /**
             * @brief 验证玩家操作权限
             */
            bool validatePlayerAction(const std::string& player_id) const;

            /**
             * @brief 通知观战者玩家变动
             */
            void notifySpectatorsPlayerChange(const std::string& player_id, bool joined);

            /**
             * @brief 创建玩家状态消息
             */
            nlohmann::json createPlayerStateMessage(const std::string& player_id) const;

            /**
             * @brief 创建观战者状态消息
             */
            nlohmann::json createSpectatorStateMessage() const;

            /**
             * @brief 创建游戏配置消息
             */
            nlohmann::json createGameConfigMessage() const;

            /**
             * @brief 处理时间更新回调
             */
            void onTimeUpdate(PieceType piece, int timeLeft);

            /**
             * @brief 处理游戏状态更新回调
             */
            void onGameStateUpdate(const nlohmann::json& state);

            /**
             * @brief 处理游戏结束回调
             */
            void onGameEnd(const std::vector<std::string>& winners);

            /**
             * @brief 发送错误消息给玩家
             */
            void sendErrorToPlayer(const std::string& player_id, const std::string& error_code, 
                                 const std::string& error_message);

            /**
             * @brief 发送成功消息给玩家
             */
            void sendSuccessToPlayer(const std::string& player_id, const std::string& action, 
                                   const nlohmann::json& data = {});

            /**
             * @brief 记录游戏日志
             */
            void logGameEvent(const std::string& event, const nlohmann::json& data = {});

            /**
             * @brief 创建五子棋消息
             */
            nlohmann::json createGomokuMessage(const std::string& type, const nlohmann::json& data) const;

        public:
            // 静态工具方法
            /**
             * @brief 创建五子棋房间
             * @param room_id 房间ID
             * @param creator_id 创建者ID  
             * @param config 五子棋配置
             * @param event_loop EventLoop实例（用于时间轮定时器）
             */
            static std::shared_ptr<GomokuRoom> create(const std::string& room_id,
                                                    const std::string& creator_id,
                                                    const GomokuConfig& config = GomokuConfig(),
                                                    std::shared_ptr<common::network::EventLoop> event_loop = nullptr);

            /**
             * @brief 获取默认房间配置
             */
            static GomokuConfig getDefaultRoomConfig();

            /**
             * @brief 验证房间配置
             */
            static bool validateRoomConfig(const GomokuConfig& config);
        };

    } // namespace gomoku
} // namespace game_services


