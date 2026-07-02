#include "gomoku_player_session.h"
#include "common/logger/logger.h"
#include "common/network/channel.h"  // 🔧 添加Channel完整定义
#include <sstream>
#include <iomanip>
#include <unistd.h>

namespace game_services {
    namespace gomoku {

        GomokuPlayerSession::GomokuPlayerSession(const std::string& player_id,
                                               int connection_fd,
                                               const std::string& client_ip)
            : PlayerSessionBase(player_id, connection_fd, client_ip) {
            // 初始化五子棋特定状态
            resetGameState();
            LOG_INFO("五子棋玩家会话创建: " + player_id + ", IP: " + client_ip);
        }

        GomokuPlayerSession::~GomokuPlayerSession() {
            LOG_INFO("五子棋玩家会话销毁: " + getPlayerId());
        }

        bool GomokuPlayerSession::sendMessage(const nlohmann::json& message) {
            if (!connection_valid_.load()) {
                return false;
            }

            try {
                // 如果是Qt6客户端，使用优化格式
                nlohmann::json formatted_message = message;
                if (isQt6Client()) {
                    formatted_message = createQt6Message(message.value("type", "unknown"), message);
                }

                std::string message_str = formatted_message.dump();
                
                // 🔧 修复：使用发送回调实际发送消息
                bool success = false;
                if (send_callback_) {
                    success = send_callback_(getPlayerId(), message_str);
                } else {
                    LOG_ERROR("❌ 发送回调未设置: " + getPlayerId());
                    return false;
                }
                
                // 只有发送成功才更新统计
                if (success) {
                    incrementSentMessages();
                    addSentMessageSize(message_str.size());
                    updateLastActivity();
                    
                    LOG_DEBUG("✅ 消息发送成功: " + getPlayerId() + " (" + std::to_string(message_str.length()) + " bytes)");
                } else {
                    LOG_WARNING("❌ 消息发送失败: " + getPlayerId());
                }
                
                return success;
            } catch (const std::exception& e) {
                LOG_ERROR("❌ 发送消息异常: " + std::string(e.what()) + ", 玩家: " + getPlayerId());
                return false;
            }
        }

        void GomokuPlayerSession::closeConnection() {
            connection_valid_.store(false);

            // 🔧 关键修复：立即禁用Channel事件，防止继续触发读取
            // 但不在这里移除 Channel，让 removePlayerSessionSafely 统一处理
            auto channel = getChannel();
            if (channel) {
                try {
                    // 只禁用事件，不调用 remove()
                    // remove() 将在 removePlayerSessionSafely 中通过 EventLoop 线程调用
                    channel->disableAll();
                    LOG_DEBUG("🔧 已禁用五子棋玩家Channel事件: " + getPlayerId() +
                             " fd=" + std::to_string(getConnectionFd()));
                } catch (const std::exception& e) {
                    LOG_WARNING("禁用Channel失败: " + std::string(e.what()));
                }
            }

            // 通知对手连接断开
            auto opponent = getOpponent();
            if (opponent) {
                opponent->sendNotification("opponent_disconnected",
                    "对手 " + getPlayerId() + " 已断开连接");
                opponent->clearOpponent();
            }

            LOG_INFO("五子棋玩家连接关闭: " + getPlayerId());
        }

        bool GomokuPlayerSession::isConnectionValid() const {
            return connection_valid_.load();
        }

        void GomokuPlayerSession::markConnectionInvalid() {
            connection_valid_.store(false);
        }

        nlohmann::json GomokuPlayerSession::getGomokuStats() const {
            auto base_stats = getSessionStats();
            
            nlohmann::json gomoku_stats = base_stats;
            gomoku_stats["gomoku"] = {
                {"pieceType", static_cast<int>(piece_type_)},
                {"isSpectator", is_spectator_},
                {"ready", ready_},
                {"isTurn", is_turn_},
                {"timeLeft", time_left_.load()},
                {"gamesPlayed", games_played_.load()},
                {"gamesWon", games_won_.load()},
                {"winRate", getWinRate()},
                {"totalMoves", total_moves_.load()},
                {"totalThinkingTime", total_thinking_time_.load()}
            };
            
            return gomoku_stats;
        }

        void GomokuPlayerSession::resetGameState() {
            piece_type_ = PieceType::EMPTY;
            is_spectator_ = false;
            ready_ = false;
            is_turn_ = false;
            time_left_ = 900; // 默认15分钟
            clearOpponent();
        }

        bool GomokuPlayerSession::sendGameState(const nlohmann::json& game_state) {
            return sendMessage(createGomokuMessage(constants::MSG_GAME_STATE, game_state));
        }

        bool GomokuPlayerSession::sendMoveResult(const Position& position, bool success, 
                                               const std::string& error_message) {
            nlohmann::json result_data = {
                {"position", position.toJson()},
                {"success", success}
            };
            
            if (!success && !error_message.empty()) {
                result_data["error"] = error_message;
            }
            
            return sendMessage(createGomokuMessage(constants::MSG_MOVE_RESULT, result_data));
        }

        bool GomokuPlayerSession::sendGameEnd(GameResult result, PieceType winner_piece) {
            nlohmann::json end_data = {
                {"result", static_cast<int>(result)},
                {"winnerPiece", static_cast<int>(winner_piece)}
            };
            
            // 更新统计信息
            bool won = (winner_piece == piece_type_);
            updateGameStats(won, total_moves_.load(), total_thinking_time_.load());
            
            return sendMessage(createGomokuMessage(constants::MSG_GAME_END, end_data));
        }

        bool GomokuPlayerSession::sendTimeUpdate(int black_time, int white_time) {
            nlohmann::json time_data = {
                {"blackTime", black_time},
                {"whiteTime", white_time}
            };
            
            // 更新本地时间
            if (piece_type_ == PieceType::BLACK) {
                time_left_ = black_time;
            } else if (piece_type_ == PieceType::WHITE) {
                time_left_ = white_time;
            }
            
            return sendMessage(createGomokuMessage(constants::MSG_TIME_UPDATE, time_data));
        }

        bool GomokuPlayerSession::sendOpponentInfo(const nlohmann::json& opponent_info) {
            return sendMessage(createGomokuMessage("opponent_info", opponent_info));
        }

        bool GomokuPlayerSession::sendSpectatorUpdate(int spectator_count) {
            nlohmann::json spectator_data = {
                {"count", spectator_count}
            };
            
            return sendMessage(createGomokuMessage("spectator_update", spectator_data));
        }

        bool GomokuPlayerSession::sendRoomConfig(const GomokuConfig& config) {
            return sendMessage(createGomokuMessage("room_config", config.toJson()));
        }

        bool GomokuPlayerSession::sendDrawOffer(const std::string& from_player) {
            nlohmann::json draw_data = {
                {"fromPlayer", from_player}
            };
            
            return sendMessage(createGomokuMessage(constants::MSG_DRAW_OFFER, draw_data));
        }

        bool GomokuPlayerSession::sendDrawResponse(const std::string& from_player, bool accepted) {
            nlohmann::json response_data = {
                {"fromPlayer", from_player},
                {"accepted", accepted}
            };
            
            return sendMessage(createGomokuMessage(constants::MSG_DRAW_RESPONSE, response_data));
        }

        bool GomokuPlayerSession::sendNotification(const std::string& notification_type, 
                                                  const std::string& message) {
            nlohmann::json notification_data = {
                {"type", notification_type},
                {"message", message},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };
            
            return sendMessage(createGomokuMessage("notification", notification_data));
        }

        void GomokuPlayerSession::handleGomokuMessage(const nlohmann::json& message) {
            try {
                if (!message.contains("type")) {
                    LOG_WARNING("收到无类型的消息: " + getPlayerId());
                    return;
                }

                std::string msg_type = message["type"];
                
                if (msg_type == constants::MSG_PLACE_PIECE) {
                    handlePlacePieceMessage(message);
                } else if (msg_type == constants::MSG_UNDO_MOVE) {
                    handleUndoMoveMessage(message);
                } else if (msg_type == constants::MSG_SURRENDER) {
                    handleSurrenderMessage(message);
                } else if (msg_type == constants::MSG_DRAW_OFFER || 
                          msg_type == constants::MSG_DRAW_RESPONSE) {
                    handleDrawMessage(message);
                } else if (msg_type == "ready") {
                    handleReadyMessage(message);
                } else if (msg_type == "chat") {
                    handleChatMessage(message);
                } else {
                    LOG_WARNING("未知的消息类型: " + msg_type + ", 玩家: " + getPlayerId());
                }
                
                // 更新活动时间和消息统计
                updateLastActivity();
                incrementReceivedMessages();
                
            } catch (const std::exception& e) {
                LOG_ERROR("处理五子棋消息失败: " + std::string(e.what()) + ", 玩家: " + getPlayerId());
                sendNotification("error", "消息处理失败");
            }
        }

        // 私有方法实现
        nlohmann::json GomokuPlayerSession::createGomokuMessage(const std::string& type, 
                                                               const nlohmann::json& data) {
            nlohmann::json message = {
                {"type", type},
                {"data", data},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()},
                {"playerId", getPlayerId()}
            };
            
            // 如果是Qt6客户端，添加优化标识
            if (isQt6Client()) {
                message["qt6Optimized"] = true;
                message["clientVersion"] = getClientVersion();
            }
            
            return message;
        }

        void GomokuPlayerSession::handlePlacePieceMessage(const nlohmann::json& message) {
            if (!message.contains("data") || !message["data"].contains("position")) {
                sendMoveResult(Position(), false, "无效的落子消息格式");
                return;
            }

            try {
                Position position = Position::fromJson(message["data"]["position"]);
                
                // 验证轮次
                if (!is_turn_) {
                    sendMoveResult(position, false, "现在不是您的回合");
                    return;
                }
                
                // 🔧 修改：生成data包装格式的action消息
                if (message_callback_) {
                    nlohmann::json action = {
                        {"type", constants::MSG_PLACE_PIECE},
                        {"data", {
                            {"position", position.toJson()},
                            {"playerId", getPlayerId()}
                        }}
                    };
                    message_callback_(action);
                }
                
            } catch (const std::exception& e) {
                sendMoveResult(Position(), false, "落子位置格式错误");
                LOG_ERROR("解析落子位置失败: " + std::string(e.what()));
            }
        }

        void GomokuPlayerSession::handleUndoMoveMessage(const nlohmann::json& message) {
            // 🔧 修改：生成data包装格式的action消息
            if (message_callback_) {
                nlohmann::json action = {
                    {"type", constants::MSG_UNDO_MOVE},
                    {"data", {
                        {"playerId", getPlayerId()}
                    }}
                };
                message_callback_(action);
            }
        }

        void GomokuPlayerSession::handleSurrenderMessage(const nlohmann::json& message) {
            // 通过消息回调传递投降请求
            if (message_callback_) {
                nlohmann::json action = {
                    {"type", constants::MSG_SURRENDER},
                    {"playerId", getPlayerId()}
                };
                message_callback_(action);
            }
        }

        void GomokuPlayerSession::handleDrawMessage(const nlohmann::json& message) {
            std::string msg_type = message["type"];
            
            if (msg_type == constants::MSG_DRAW_OFFER) {
                // 和棋提议
                if (message_callback_) {
                    nlohmann::json action = {
                        {"type", constants::MSG_DRAW_OFFER},
                        {"playerId", getPlayerId()}
                    };
                    message_callback_(action);
                }
            } else if (msg_type == constants::MSG_DRAW_RESPONSE) {
                // 和棋回应
                if (!message.contains("data") || !message["data"].contains("accept")) {
                    sendNotification("error", "无效的和棋回应格式");
                    return;
                }
                
                bool accept = message["data"]["accept"];
                if (message_callback_) {
                    nlohmann::json action = {
                        {"type", constants::MSG_DRAW_RESPONSE},
                        {"accept", accept},
                        {"playerId", getPlayerId()}
                    };
                    message_callback_(action);
                }
            }
        }

        void GomokuPlayerSession::handleReadyMessage(const nlohmann::json& message) {
            if (!message.contains("data") || !message["data"].contains("ready")) {
                sendNotification("error", "无效的准备状态消息格式");
                return;
            }

            bool new_ready_state = message["data"]["ready"];
            setReady(new_ready_state);
            
            // 通知房间准备状态变化
            if (message_callback_) {
                nlohmann::json action = {
                    {"type", "ready"},
                    {"ready", new_ready_state},
                    {"playerId", getPlayerId()}
                };
                message_callback_(action);
            }
            
            LOG_INFO("玩家 " + getPlayerId() + " 设置准备状态: " + (new_ready_state ? "已准备" : "未准备"));
        }

        void GomokuPlayerSession::handleChatMessage(const nlohmann::json& message) {
            if (!message.contains("data") || !message["data"].contains("message")) {
                sendNotification("error", "无效的聊天消息格式");
                return;
            }

            std::string chat_content = message["data"]["message"];
            
            // 基本的消息过滤（长度检查）
            if (chat_content.length() > 200) {
                sendNotification("error", "聊天消息过长");
                return;
            }
            
            // 通过消息回调传递聊天消息
            if (message_callback_) {
                nlohmann::json action = {
                    {"type", "chat"},
                    {"message", chat_content},
                    {"playerId", getPlayerId()},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };
                message_callback_(action);
            }
            
            LOG_DEBUG("玩家 " + getPlayerId() + " 发送聊天消息: " + chat_content.substr(0, 50));
        }

        // 静态方法实现
        std::shared_ptr<GomokuPlayerSession> GomokuPlayerSession::create(const std::string& player_id,
                                                                        int connection_fd,
                                                                        const std::string& client_ip) {
            return std::make_shared<GomokuPlayerSession>(player_id, connection_fd, client_ip);
        }

    } // namespace gomoku
} // namespace game_services
