#pragma once

#include "player_session_base.h"
#include "gomoku_types.h"
#include <atomic>
#include <memory>

/**
 * @file gomoku_player_session.h
 * @brief 五子棋玩家会话类定义
 * @details 实现五子棋玩家的连接管理和游戏状态跟踪
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 五子棋玩家会话类
         * @details 继承自PlayerSessionBase，添加五子棋特定的功能
         */
        class GomokuPlayerSession : public game_base::PlayerSessionBase {
        public:
            /**
             * @brief 构造函数
             * @param player_id 玩家ID
             * @param connection_fd 连接文件描述符
             * @param client_ip 客户端IP地址
             */
            explicit GomokuPlayerSession(const std::string& player_id,
                                       int connection_fd = -1,
                                       const std::string& client_ip = "");

            /**
             * @brief 虚析构函数
             */
            virtual ~GomokuPlayerSession();

            // 继承自PlayerSessionBase的纯虚函数实现
            bool sendMessage(const nlohmann::json& message) override;
            void closeConnection() override;
            bool isConnectionValid() const override;
            void markConnectionInvalid() override;

            // 五子棋特定功能
            /**
             * @brief 设置玩家棋子类型
             * @param piece 棋子类型
             */
            void setPieceType(PieceType piece) { piece_type_ = piece; }

            /**
             * @brief 获取玩家棋子类型
             * @return 棋子类型
             */
            PieceType getPieceType() const { return piece_type_; }

            /**
             * @brief 设置是否为观战者
             * @param is_spectator 是否为观战者
             */
            void setSpectator(bool is_spectator) { is_spectator_ = is_spectator; }

            /**
             * @brief 检查是否为观战者
             * @return 是否为观战者
             */
            bool isSpectator() const { return is_spectator_; }

            /**
             * @brief 设置对手会话
             * @param opponent 对手会话
             */
            void setOpponent(std::shared_ptr<GomokuPlayerSession> opponent) { 
                opponent_session_ = opponent; 
            }

            /**
             * @brief 获取对手会话
             * @return 对手会话，如果没有则返回nullptr
             */
            std::shared_ptr<GomokuPlayerSession> getOpponent() const { 
                return opponent_session_.lock(); 
            }

            /**
             * @brief 清除对手关联
             */
            void clearOpponent() { opponent_session_.reset(); }

            /**
             * @brief 设置准备状态
             * @param ready 是否准备就绪
             */
            void setReady(bool ready) { ready_ = ready; }

            /**
             * @brief 获取准备状态
             * @return 是否准备就绪
             */
            bool isReady() const { return ready_; }

            /**
             * @brief 设置是否轮到该玩家
             * @param is_turn 是否轮到该玩家
             */
            void setTurn(bool is_turn) { is_turn_ = is_turn; }

            /**
             * @brief 检查是否轮到该玩家
             * @return 是否轮到该玩家
             */
            bool isTurn() const { return is_turn_; }

            /**
             * @brief 设置剩余思考时间
             * @param time_left 剩余时间（秒）
             */
            void setTimeLeft(int time_left) { time_left_ = time_left; }

            /**
             * @brief 获取剩余思考时间
             * @return 剩余时间（秒）
             */
            int getTimeLeft() const { return time_left_.load(); }

            /**
             * @brief 减少思考时间
             * @param seconds 减少的秒数
             */
            void decreaseTime(int seconds) { time_left_ -= seconds; }

            /**
             * @brief 增加思考时间
             * @param seconds 增加的秒数
             */
            void increaseTime(int seconds) { time_left_ += seconds; }

            /**
             * @brief 检查时间是否不足
             * @param warning_threshold 警告阈值（秒）
             * @return 是否时间不足
             */
            bool isLowOnTime(int warning_threshold = 30) const {
                return time_left_.load() <= warning_threshold;
            }

            /**
             * @brief 检查是否超时
             * @return 是否超时
             */
            bool isTimeOut() const { return time_left_.load() <= 0; }

            /**
             * @brief 获取游戏统计信息
             * @return 统计信息
             */
            nlohmann::json getGomokuStats() const;

            /**
             * @brief 重置游戏相关状态
             */
            void resetGameState();

            // 五子棋专用消息发送方法
            /**
             * @brief 发送游戏状态消息
             * @param game_state 游戏状态
             * @return 是否发送成功
             */
            bool sendGameState(const nlohmann::json& game_state);

            /**
             * @brief 发送移动结果消息
             * @param position 落子位置
             * @param success 是否成功
             * @param error_message 错误消息（如果失败）
             * @return 是否发送成功
             */
            bool sendMoveResult(const Position& position, bool success, 
                              const std::string& error_message = "");

            /**
             * @brief 发送游戏结束消息
             * @param result 游戏结果
             * @param winner_piece 获胜棋子（如果有）
             * @return 是否发送成功
             */
            bool sendGameEnd(GameResult result, PieceType winner_piece = PieceType::EMPTY);

            /**
             * @brief 发送时间更新消息
             * @param black_time 黑方剩余时间
             * @param white_time 白方剩余时间
             * @return 是否发送成功
             */
            bool sendTimeUpdate(int black_time, int white_time);

            /**
             * @brief 发送对手信息消息
             * @param opponent_info 对手信息
             * @return 是否发送成功
             */
            bool sendOpponentInfo(const nlohmann::json& opponent_info);

            /**
             * @brief 发送观战者更新消息
             * @param spectator_count 观战者数量
             * @return 是否发送成功
             */
            bool sendSpectatorUpdate(int spectator_count);

            /**
             * @brief 发送房间配置消息
             * @param config 房间配置
             * @return 是否发送成功
             */
            bool sendRoomConfig(const GomokuConfig& config);

            /**
             * @brief 发送和棋提议消息
             * @param from_player 提议玩家
             * @return 是否发送成功
             */
            bool sendDrawOffer(const std::string& from_player);

            /**
             * @brief 发送和棋回应消息
             * @param from_player 回应玩家
             * @param accepted 是否接受
             * @return 是否发送成功
             */
            bool sendDrawResponse(const std::string& from_player, bool accepted);

            /**
             * @brief 发送系统通知消息
             * @param notification_type 通知类型
             * @param message 通知内容
             * @return 是否发送成功
             */
            bool sendNotification(const std::string& notification_type, const std::string& message);

            /**
             * @brief 处理接收到的五子棋消息
             * @param message 消息内容
             */
            void handleGomokuMessage(const nlohmann::json& message);

        private:
            // 五子棋特定状态
            PieceType piece_type_ = PieceType::EMPTY;       // 玩家棋子类型
            bool is_spectator_ = false;                     // 是否为观战者
            bool ready_ = false;                            // 是否准备就绪
            bool is_turn_ = false;                          // 是否轮到该玩家
            std::atomic<int> time_left_{900};               // 剩余思考时间（秒）

            // 游戏统计
            std::atomic<int> games_played_{0};              // 已玩游戏数
            std::atomic<int> games_won_{0};                 // 获胜游戏数
            std::atomic<int> total_moves_{0};               // 总移动数
            std::atomic<int64_t> total_thinking_time_{0};   // 总思考时间（毫秒）

            // 对手会话（弱引用，避免循环引用）
            std::weak_ptr<GomokuPlayerSession> opponent_session_;

            // 连接状态
            std::atomic<bool> connection_valid_{true};

            /**
             * @brief 创建五子棋专用消息格式
             * @param type 消息类型
             * @param data 消息数据
             * @return 格式化的消息
             */
            nlohmann::json createGomokuMessage(const std::string& type, const nlohmann::json& data);

            /**
             * @brief 处理落子操作消息
             * @param message 消息内容
             */
            void handlePlacePieceMessage(const nlohmann::json& message);

            /**
             * @brief 处理悔棋请求消息
             * @param message 消息内容
             */
            void handleUndoMoveMessage(const nlohmann::json& message);

            /**
             * @brief 处理投降消息
             * @param message 消息内容
             */
            void handleSurrenderMessage(const nlohmann::json& message);

            /**
             * @brief 处理和棋相关消息
             * @param message 消息内容
             */
            void handleDrawMessage(const nlohmann::json& message);

            /**
             * @brief 处理准备状态消息
             * @param message 消息内容
             */
            void handleReadyMessage(const nlohmann::json& message);

            /**
             * @brief 处理聊天消息
             * @param message 消息内容
             */
            void handleChatMessage(const nlohmann::json& message);

            /**
             * @brief 增加游戏统计
             */
            void incrementGamesPlayed() { games_played_++; }
            void incrementGamesWon() { games_won_++; }
            void incrementTotalMoves() { total_moves_++; }
            void addThinkingTime(int64_t milliseconds) { total_thinking_time_ += milliseconds; }

        public:
            // 统计信息访问方法
            int getGamesPlayed() const { return games_played_.load(); }
            int getGamesWon() const { return games_won_.load(); }
            int getTotalMoves() const { return total_moves_.load(); }
            int64_t getTotalThinkingTime() const { return total_thinking_time_.load(); }
            double getWinRate() const {
                int played = games_played_.load();
                return played > 0 ? static_cast<double>(games_won_.load()) / played : 0.0;
            }

            /**
             * @brief 游戏结束时更新统计
             * @param won 是否获胜
             * @param move_count 本局移动数
             * @param thinking_time 本局思考时间
             */
            void updateGameStats(bool won, int move_count, int64_t thinking_time) {
                incrementGamesPlayed();
                if (won) incrementGamesWon();
                total_moves_ += move_count;
                addThinkingTime(thinking_time);
            }

            /**
             * @brief 创建五子棋玩家会话
             */
            static std::shared_ptr<GomokuPlayerSession> create(const std::string& player_id,
                                                             int connection_fd,
                                                             const std::string& client_ip);
        };

    } // namespace gomoku
} // namespace game_services




