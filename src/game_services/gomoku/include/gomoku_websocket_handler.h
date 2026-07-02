#pragma once

#include "websocket_handler_base.h"
#include "gomoku_player_session.h"
#include "service_client.h"
#include "match_pool.h"
// [FIX] 已移除JWT验证器，认证由API网关统一处理
#include <memory>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

// 前向声明
namespace game_services {
namespace gomoku {
    class AuthServiceClient;
}
}

/**
 * @file gomoku_websocket_handler.h
 * @brief 五子棋WebSocket处理器类定义
 * @details 实现五子棋游戏的实时通信功能
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 五子棋WebSocket处理器类
         * @details 继承自WebSocketHandlerBase，实现五子棋特定的WebSocket通信
         */
        class GomokuWebSocketHandler : public game_base::WebSocketHandlerBase {
        public:
            // 游戏服务器回调类型定义
            using GameActionCallback = std::function<bool(const std::string&, const nlohmann::json&)>;
            using RoomOperationCallback = std::function<bool(const std::string&, const nlohmann::json&)>;
            using SubscriptionCallback = std::function<void(const std::string&, bool)>;  // player_id, subscribe
            using RoomListDataCallback = std::function<nlohmann::json()>;  // returns room list data

            // ========== 匹配系统回调类型定义 ==========
            using MatchStartCallback = std::function<bool(const std::string&, MatchMode, GameMode)>;  // player_id, mode, game_mode
            using MatchCancelCallback = std::function<bool(const std::string&)>;  // player_id
            using MatchPoolStatusCallback = std::function<MatchPoolStatus()>;  // returns pool status

            /**
             * @brief 构造函数
             * @param event_loop 事件循环
             */
            explicit GomokuWebSocketHandler(std::shared_ptr<common::network::EventLoop> event_loop);

            /**
             * @brief 虚析构函数
             */
            virtual ~GomokuWebSocketHandler();

            /**
             * @brief 设置游戏操作回调
             * @param callback 游戏操作回调函数
             */
            void setGameActionCallback(GameActionCallback callback) {
                game_action_callback_ = callback;
            }

            /**
             * @brief 设置房间操作回调
             * @param callback 房间操作回调函数
             */
            void setRoomOperationCallback(RoomOperationCallback callback) {
                room_operation_callback_ = callback;
            }

            /**
             * @brief 设置订阅操作回调
             * @param callback 订阅操作回调函数
             */
            void setSubscriptionCallback(SubscriptionCallback callback) {
                subscription_callback_ = callback;
            }

            /**
             * @brief 设置获取房间列表数据回调
             * @param callback 获取房间列表数据回调函数
             */
            void setRoomListDataCallback(RoomListDataCallback callback) {
                room_list_data_callback_ = callback;
            }

            // ========== 匹配系统回调设置 ==========

            /**
             * @brief 设置开始匹配回调
             * @param callback 开始匹配回调函数
             */
            void setMatchStartCallback(MatchStartCallback callback) {
                match_start_callback_ = std::move(callback);
            }

            /**
             * @brief 设置取消匹配回调
             * @param callback 取消匹配回调函数
             */
            void setMatchCancelCallback(MatchCancelCallback callback) {
                match_cancel_callback_ = std::move(callback);
            }

            /**
             * @brief 设置获取匹配池状态回调
             * @param callback 获取匹配池状态回调函数
             */
            void setMatchPoolStatusCallback(MatchPoolStatusCallback callback) {
                match_pool_status_callback_ = std::move(callback);
            }

            /**
             * @brief 向房间内所有玩家发送游戏状态
             * @param room_id 房间ID
             * @param game_state 游戏状态
             * @return 发送成功的玩家数量
             */
            int sendGameStateToRoom(const std::string& room_id, const nlohmann::json& game_state);

            /**
             * @brief 向房间内所有玩家发送移动结果
             * @param room_id 房间ID
             * @param player_id 移动玩家ID
             * @param position 移动位置
             * @param success 是否成功
             * @return 发送成功的玩家数量
             */
            int sendMoveResultToRoom(const std::string& room_id, const std::string& player_id, 
                                   const Position& position, bool success);

            /**
             * @brief 向房间内所有玩家发送游戏结束消息
             * @param room_id 房间ID
             * @param result 游戏结果
             * @param winner_id 获胜者ID
             * @return 发送成功的玩家数量
             */
            int sendGameEndToRoom(const std::string& room_id, GameResult result, 
                                const std::string& winner_id = "");

            /**
             * @brief 向房间内所有玩家发送时间更新
             * @param room_id 房间ID
             * @param black_time 黑方剩余时间
             * @param white_time 白方剩余时间
             * @return 发送成功的玩家数量
             */
            int sendTimeUpdateToRoom(const std::string& room_id, int black_time, int white_time);

            // ========== 匹配系统消息发送 ==========

            /**
             * @brief 发送匹配开始确认给玩家
             * @param player_id 玩家ID
             * @param success 是否成功开始匹配
             * @param message 消息
             */
            void sendMatchStartResponse(const std::string& player_id, bool success, const std::string& message = "");

            /**
             * @brief 发送匹配取消确认给玩家
             * @param player_id 玩家ID
             * @param success 是否成功取消
             * @param message 消息
             */
            void sendMatchCancelResponse(const std::string& player_id, bool success, const std::string& message = "");

            /**
             * @brief 发送匹配成功通知给玩家
             * @param player_id 玩家ID
             * @param result 匹配结果
             */
            void sendMatchFound(const std::string& player_id, const MatchResult& result);

            /**
             * @brief 发送匹配状态更新给玩家
             * @param player_id 玩家ID
             * @param wait_seconds 当前等待时间
             * @param pool_size 当前池大小
             */
            void sendMatchStatusUpdate(const std::string& player_id, int wait_seconds, int pool_size);

            /**
             * @brief 发送匹配池状态给玩家
             * @param player_id 玩家ID
             * @param status 匹配池状态
             */
            void sendMatchPoolStatus(const std::string& player_id, const MatchPoolStatus& status);

            /**
             * @brief 广播匹配超时通知
             * @param player_id 玩家ID
             */
            void sendMatchTimeout(const std::string& player_id);

            /**
             * @brief 获取五子棋玩家会话
             * @param player_id 玩家ID
             * @return 五子棋玩家会话，如果不存在则返回nullptr
             */
            std::shared_ptr<GomokuPlayerSession> getGomokuPlayerSession(const std::string& player_id);

            /**
             * @brief 根据房间ID获取房间内的玩家ID列表
             * @param room_id 房间ID
             * @return 玩家ID列表
             */
            std::vector<std::string> getPlayersInRoom(const std::string& room_id) const;

            /**
             * @brief 设置玩家房间关联
             * @param player_id 玩家ID
             * @param room_id 房间ID
             */
            void setPlayerRoom(const std::string& player_id, const std::string& room_id);

            /**
             * @brief 移除玩家房间关联
             * @param player_id 玩家ID
             */
            void removePlayerRoom(const std::string& player_id);

            /**
             * @brief 获取玩家所在房间ID
             * @param player_id 玩家ID
             * @return 房间ID，如果玩家不在房间则返回空字符串
             */
            std::string getPlayerRoom(const std::string& player_id) const;

            /**
             * @brief 验证玩家会话 (信任API网关认证结果)
             * @param player_id 玩家ID (由API网关验证后传入)
             * @return 是否验证成功
             */
            bool authenticatePlayer(const std::string& player_id);

            /**
             * @brief 异步加载用户信息 (P1 优化)
             * @param player_id 玩家ID
             */
            void asyncLoadUserInfo(const std::string& player_id);

            /**
             * @brief 异步更新在线状态 (P1 优化)
             * @param player_id 玩家ID
             */
            void asyncUpdateOnlineStatus(const std::string& player_id);

            /**
             * @brief 发送五子棋特定的欢迎消息
             * @param player_id 玩家ID
             */
            void sendGomokuWelcome(const std::string& player_id);

            /**
             * @brief 获取服务客户端管理器
             * @return 服务客户端管理器指针
             */
            std::shared_ptr<ServiceClientManager> getServiceClientManager() const;

            /**
             * @brief 获取玩家信息
             * @param player_id 玩家ID
             * @return 玩家信息，不存在返回空
             */
            std::optional<game_services::game_base::PlayerInfo> getPlayerInfo(const std::string& player_id) const;

        protected:
            // 重写基类虚函数
            void handleWebSocketConnection(int client_fd, const std::string& client_ip) override;
            void handleWebSocketMessage(const std::string& player_id, const std::string& message) override;
            void handleWebSocketDisconnection(const std::string& player_id) override;

            /**
             * @brief 处理准备状态消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleReadyMessage(const std::string& player_id, const nlohmann::json& message);
            void handleQt6SpecificMessage(const std::string& player_id, const nlohmann::json& message) override;
            bool validateGameSessionToken(const std::string& token, int client_fd) override;
            std::shared_ptr<game_base::PlayerSessionBase> createPlayerSession(
                const std::string& player_id, int client_fd, const std::string& client_ip) override;
            std::vector<std::string> getSupportedProtocols() override;

            // ✅ 重写房间操作虚方法
            /**
             * @brief 处理创建房间请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleCreateRoomWebSocket(const std::string& player_id, const nlohmann::json& message) override;

            /**
             * @brief 处理加入房间请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleJoinRoomWebSocket(const std::string& player_id, const nlohmann::json& message) override;

            /**
             * @brief 处理离开房间请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleLeaveRoomWebSocket(const std::string& player_id, const nlohmann::json& message) override;

            /**
             * @brief 处理开始游戏请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleStartGameWebSocket(const std::string& player_id, const nlohmann::json& message) override;

            /**
             * @brief 重写认证状态设置方法
             * @param player_id 玩家ID
             * @param authenticated 认证状态
             */
            void setPlayerAuthenticated(const std::string& player_id, bool authenticated) override;

            // 五子棋特定的消息处理
            /**
             * @brief 处理五子棋游戏消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleGomokuGameMessage(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理房间操作消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleRoomMessage(const std::string& player_id, const nlohmann::json& message);

            // [FIX] 已移除认证消息处理，信任API网关的认证结果

            /**
             * @brief 处理心跳消息
             * @param player_id 玩家ID
             * @param message 心跳消息
             */
            void handleHeartbeatMessage(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理房间列表订阅消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleSubscribeRoomList(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理取消房间列表订阅消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleUnsubscribeRoomList(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理断线重连消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleReconnect(const std::string& player_id, const nlohmann::json& message);

            // ========== 匹配系统消息处理 ==========

            /**
             * @brief 处理匹配消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleMatchMessage(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理开始匹配请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleMatchStart(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理取消匹配请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleMatchCancel(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理获取匹配池状态请求
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            void handleGetMatchPoolStatus(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 验证消息格式
             * @param message 消息内容
             * @return 是否有效
             */
            bool validateGomokuMessage(const nlohmann::json& message) const;

            /**
             * @brief 检查玩家权限
             * @param player_id 玩家ID
             * @param action 操作类型
             * @return 是否有权限
             */
            bool checkPlayerPermission(const std::string& player_id, const std::string& action);

            // ✅ 增强版重写方法

            /**
             * @brief 认证玩家 (增强版本)
             * @param player_id 玩家ID
             * @param auth_token JWT认证令牌
             * @return 认证是否成功
             */

            /**
             * @brief 记录游戏结果 (增强版本)
             * @param game_id 游戏ID
             * @param black_player_id 黑方玩家ID
             * @param white_player_id 白方玩家ID
             * @param result 游戏结果
             * @param game_data 游戏数据
             */
            virtual void recordGameResult(const std::string& game_id,
                                         const std::string& black_player_id,
                                         const std::string& white_player_id,
                                         const std::string& result,
                                         const nlohmann::json& game_data);
            
            /**
             * @brief 玩家断开连接处理 (增强版本)
             * @param player_id 玩家ID
             */
            virtual void onPlayerDisconnected(const std::string& player_id);

        private:
            // 回调函数
            GameActionCallback game_action_callback_;
            RoomOperationCallback room_operation_callback_;
            SubscriptionCallback subscription_callback_;
            RoomListDataCallback room_list_data_callback_;

            // 匹配系统回调函数
            MatchStartCallback match_start_callback_;
            MatchCancelCallback match_cancel_callback_;
            MatchPoolStatusCallback match_pool_status_callback_;
            
            // ✅ 增强版：服务客户端管理器
            std::shared_ptr<ServiceClientManager> service_client_manager_;
            
            // ✅ 增强版：JWT验证器
            // [FIX] 已移除JWT验证器成员变量
            
            // ✅ 增强版：玩家信息缓存
            mutable std::shared_mutex players_mutex_;
            std::unordered_map<std::string, game_services::game_base::PlayerInfo> authenticated_players_;
            
            // 认证服务客户端 (保留向后兼容)
            std::shared_ptr<AuthServiceClient> auth_service_client_;

            // 玩家-房间映射
            std::unordered_map<std::string, std::string> player_rooms_;  // player_id -> room_id
            std::unordered_map<std::string, std::vector<std::string>> room_players_;  // room_id -> player_ids
            mutable std::shared_mutex room_mapping_mutex_;

            // 消息统计
            std::atomic<int64_t> total_messages_processed_{0};
            std::atomic<int64_t> game_messages_processed_{0};
            std::atomic<int64_t> room_messages_processed_{0};
            std::atomic<int64_t> match_messages_processed_{0};  // 匹配消息统计

            /**
             * @brief 创建五子棋消息
             * @param type 消息类型
             * @param data 消息数据
             * @return 格式化的消息
             */
            nlohmann::json createGomokuMessage(const std::string& type, 
                                             const nlohmann::json& data) const;

            /**
             * @brief 发送错误消息
             * @param player_id 玩家ID
             * @param error_code 错误代码
             * @param error_message 错误消息
             */
            void sendGomokuError(const std::string& player_id, 
                               const std::string& error_code, 
                               const std::string& error_message);

            /**
             * @brief 发送成功消息
             * @param player_id 玩家ID
             * @param action 操作类型
             * @param data 相关数据
             */
            void sendGomokuSuccess(const std::string& player_id, 
                                 const std::string& action, 
                                 const nlohmann::json& data = {});

            /**
             * @brief 更新房间玩家映射
             * @param player_id 玩家ID
             * @param old_room_id 旧房间ID
             * @param new_room_id 新房间ID
             */
            void updateRoomMapping(const std::string& player_id, 
                                 const std::string& old_room_id, 
                                 const std::string& new_room_id);

            /**
             * @brief 清理玩家房间映射
             * @param player_id 玩家ID
             */
            void cleanupPlayerRoomMapping(const std::string& player_id);

            /**
             * @brief 记录消息统计
             * @param message_type 消息类型
             */
            void recordMessageStats(const std::string& message_type);

            /**
             * @brief 验证落子位置
             * @param position_json 位置JSON
             * @return 是否有效
             */
            bool validatePosition(const nlohmann::json& position_json) const;

            /**
             * @brief 处理玩家连接后的初始化
             * @param player_id 玩家ID
             */
            void handlePlayerInitialization(const std::string& player_id);

            /**
             * @brief 处理玩家断开前的清理
             * @param player_id 玩家ID
             */
            void handlePlayerCleanup(const std::string& player_id);

            // ✅ 增强版新增私有方法
            
            /**
             * @brief 初始化服务客户端
             */
            void initializeServiceClients();
            
            /**
             * @brief 初始化JWT验证器
             */
            // [FIX] 已移除JWT验证器初始化方法
            
            /**
             * @brief 计算评分变化
             * @param black_rating 黑方评分
             * @param white_rating 白方评分
             * @param result 游戏结果
             * @return 黑方评分变化
             */
            int calculateRatingChange(int black_rating, int white_rating, const std::string& result);
            
            /**
             * @brief 更新成就进度
             * @param black_player_id 黑方玩家ID
             * @param white_player_id 白方玩家ID
             * @param result 游戏结果
             * @param game_data 游戏数据
             */
            void updateAchievements(const std::string& black_player_id,
                                   const std::string& white_player_id,
                                   const std::string& result,
                                   const nlohmann::json& game_data);
            
            /**
             * @brief 发放奖励
             * @param black_player_id 黑方玩家ID
             * @param white_player_id 白方玩家ID
             * @param result 游戏结果
             */
            void rewardPlayers(const std::string& black_player_id,
                              const std::string& white_player_id,
                              const std::string& result);
            
            /**
             * @brief 清理资源
             */
            void cleanup();

        public:
            /**
             * @brief 获取WebSocket处理器统计信息
             * @return 统计信息JSON
             */
            nlohmann::json getHandlerStats() const;

            /**
             * @brief 获取所有房间的玩家分布
             * @return 房间-玩家映射
             */
            std::unordered_map<std::string, std::vector<std::string>> getRoomPlayerDistribution() const;
        };

    } // namespace gomoku
} // namespace game_services
