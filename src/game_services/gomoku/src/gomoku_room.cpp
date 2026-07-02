#include "gomoku_room.h"
#include "gomoku_player_session.h"
#include "common/logger/logger.h"
#include <algorithm>

namespace game_services {
    namespace gomoku {

        GomokuRoom::GomokuRoom(const std::string& room_id, 
                              const std::string& creator_id,
                              const GomokuConfig& config,
                              std::shared_ptr<common::network::EventLoop> event_loop)
            : GameRoomBase(room_id, creator_id, game_base::GameType::CHESS, 2),
              gomoku_config_(config), event_loop_(event_loop),
              game_start_time_(std::chrono::system_clock::now()) {
            
            initializeGameLogic();
            LOG_INFO("五子棋房间创建: " + room_id + ", 创建者: " + creator_id);
            
            // 触发房间创建回调
            if (room_created_callback_) {
                room_created_callback_(room_id);
            }
        }

        GomokuRoom::~GomokuRoom() {
            LOG_INFO("五子棋房间销毁: " + getRoomId());
            
            // 触发房间销毁回调
            if (room_destroyed_callback_) {
                room_destroyed_callback_(getRoomId());
            }
        }

        bool GomokuRoom::startGameSpecific() {
            if (getPlayerCount() != 2) {
                LOG_WARNING("五子棋游戏需要恰好2个玩家，当前: " + std::to_string(getPlayerCount()));
                return false;
            }

            if (!canStartGame()) {
                LOG_WARNING("房间 " + getRoomId() + " 不满足开始游戏条件");
                return false;
            }

            // 记录游戏实际开始时间
            game_start_time_ = std::chrono::system_clock::now();

            // 自动分配棋子颜色
            autoAssignPieces();

            // 初始化游戏逻辑
            auto player_ids = getPlayerIds();
            
            // [FIX] 将Room层的棋子分配传递给Logic层
            nlohmann::json config_with_pieces = gomoku_config_.toJson();
            nlohmann::json piece_assignments;
            
            for (const auto& player_id : player_ids) {
                PieceType piece = getPlayerPiece(player_id);
                piece_assignments[player_id] = static_cast<int>(piece);
            }
            config_with_pieces["piece_assignments"] = piece_assignments;
            
            LOG_INFO("[PIECE] 传递棋子分配给Logic层: " + piece_assignments.dump());
            
            if (!gomoku_logic_->initializeGame(player_ids, config_with_pieces)) {
                LOG_ERROR("初始化五子棋游戏逻辑失败");
                return false;
            }

            // 启动游戏逻辑
            if (!gomoku_logic_->startGame()) {
                LOG_ERROR("启动五子棋游戏失败");
                return false;
            }

            // [FIX] 启动时间控制定时器，每秒广播 time_update 消息
            gomoku_logic_->startTimeControl();
            LOG_INFO("[TIME] 时间控制定时器已启动，将每秒广播 time_update 消息");

            // 设置玩家回合状态
            std::string black_player_id = getCurrentTurnPlayerId();
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                for (const auto& pair : players_) {
                    auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
                    if (gomoku_session) {
                        bool is_turn = (pair.first == black_player_id);
                        gomoku_session->setTurn(is_turn);
                    }
                }
            }

            game_start_time_ = std::chrono::system_clock::now();

            // [FIX] 广播游戏开始给所有玩家和观众
            // 1. 先发送 game_started 消息（客户端监听此消息更新游戏状态为 playing）
            nlohmann::json game_started_msg = createGomokuMessage("game_started", getGameSpecificState());
            broadcastToPlayers(game_started_msg);
            broadcastToSpectators(game_started_msg);
            LOG_INFO("📤 广播 game_started 消息给所有玩家和观众");

            // 2. 发送 game_state 消息（客户端监听此消息同步棋盘状态）
            broadcastGameState();

            // 触发游戏开始回调
            if (game_start_callback_) {
                game_start_callback_(getRoomId());
            }

            LOG_INFO("五子棋游戏开始: " + getRoomId());
            return true;
        }

        bool GomokuRoom::processGameActionSpecific(const std::string& player_id,
                                                  const nlohmann::json& action) {
            std::string action_type = action.value("type", "");

            // [FIX] chat 消息不需要 gomoku_logic_，可以在任何时间发送
            if (action_type == "chat") {
                std::string message;
                int64_t timestamp;

                if (action.contains("data")) {
                    message = action["data"].value("message", "");
                    timestamp = action["data"].value("timestamp",
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                } else {
                    message = action.value("message", "");
                    timestamp = action.value("timestamp",
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                }

                // 广播聊天消息
                nlohmann::json chat_msg = createGomokuMessage("chat", {
                    {"playerId", player_id},
                    {"message", message},
                    {"timestamp", timestamp}
                });
                broadcastToPlayers(chat_msg);
                broadcastToSpectators(chat_msg);
                LOG_DEBUG("聊天消息广播: " + player_id + ": " + message);
                return true;
            }

            // 其他游戏动作需要 gomoku_logic_
            if (!gomoku_logic_) {
                LOG_WARNING("游戏逻辑未初始化，无法处理动作: " + action_type);
                return false;
            }

            if (action_type == constants::MSG_PLACE_PIECE) {
                // [FIX] 修改：支持data包装格式
                if (action.contains("data") && action["data"].contains("position")) {
                    return handlePlacePiece(player_id, Position::fromJson(action["data"]["position"]));
                } else if (action.contains("position")) {
                    // 兼容旧格式（直接position）
                    return handlePlacePiece(player_id, Position::fromJson(action["position"]));
                } else {
                    LOG_WARNING("落子消息缺少position字段");
                    return false;
                }
            }
            else if (action_type == constants::MSG_UNDO_MOVE) {
                // [FIX] 按照文档实现悔棋请求-响应流程
                return handleUndoRequest(player_id);
            }
            else if (action_type == constants::MSG_UNDO_RESPONSE) {
                // 处理悔棋响应
                bool accept = false;
                if (action.contains("data") && action["data"].contains("accepted")) {
                    accept = action["data"].value("accepted", false);
                } else if (action.contains("accepted")) {
                    accept = action.value("accepted", false);
                }
                return handleUndoResponse(player_id, accept);
            }
            else if (action_type == constants::MSG_SURRENDER) {
                return handleSurrender(player_id);
            }
            else if (action_type == constants::MSG_DRAW_OFFER) {
                return handleDrawOffer(player_id);
            }
            else if (action_type == constants::MSG_DRAW_RESPONSE) {
                // [FIX] 修改：支持data包装格式，字段名使用 "accepted" 与文档一致
                bool accept = false;
                if (action.contains("data") && action["data"].contains("accepted")) {
                    accept = action["data"].value("accepted", false);
                } else if (action.contains("data") && action["data"].contains("accept")) {
                    // 兼容旧格式
                    accept = action["data"].value("accept", false);
                } else if (action.contains("accepted")) {
                    accept = action.value("accepted", false);
                } else {
                    accept = action.value("accept", false);  // 兼容旧格式
                }
                return handleDrawResponse(player_id, accept);
            }
            else if (action_type == "ready") {
                // 处理准备状态变化
                return true; // 基类已处理
            }

            return false;
        }

        nlohmann::json GomokuRoom::getGameSpecificState() const {
            nlohmann::json state = nlohmann::json::object();

            // [FIX] 按照文档格式返回游戏状态
            if (gomoku_logic_) {
                state["board"] = gomoku_logic_->getGameSpecificState();

                // 当前回合玩家 (1=黑, 2=白)
                PieceType current_piece = gomoku_logic_->getCurrentPlayer();
                state["currentPlayer"] = static_cast<int>(current_piece);

                // 时间剩余
                state["blackTimeLeft"] = gomoku_logic_->getTimeLeft(PieceType::BLACK);
                state["whiteTimeLeft"] = gomoku_logic_->getTimeLeft(PieceType::WHITE);
            }

            // 黑白方玩家ID
            auto black_it = piece_players_.find(PieceType::BLACK);
            auto white_it = piece_players_.find(PieceType::WHITE);
            state["blackPlayer"] = black_it != piece_players_.end() ? black_it->second : "";
            state["whitePlayer"] = white_it != piece_players_.end() ? white_it->second : "";

            // 玩家棋子映射（保留，客户端可能需要）
            state["playerPieces"] = nlohmann::json::object();
            for (const auto& pair : player_pieces_) {
                state["playerPieces"][pair.first] = static_cast<int>(pair.second);
            }

            // 总回合数
            state["totalMoves"] = total_moves_;

            // 游戏配置（可选，保留兼容性）
            auto config_json = gomoku_config_.toJson();
            config_json["gameModeStr"] = gameModeToString(gomoku_config_.gameMode);
            state["config"] = config_json;

            // 其他信息（可选）
            state["spectatorCount"] = getSpectatorCount();
            state["currentTurn"] = getCurrentTurnPlayerId();  // 当前回合玩家ID（保留兼容性）
            state["gamePhase"] = getGamePhaseString();

            return state;
        }

        nlohmann::json GomokuRoom::getRoomInfo() const {
            // 调用基类方法获取基础信息
            auto base_info = GameRoomBase::getRoomInfo();

            // [DEBUG] 记录基类返回的玩家信息
            LOG_INFO("[DEBUG] GomokuRoom::getRoomInfo - base_info players count: " +
                    std::to_string(base_info.contains("players") && base_info["players"].is_array() ?
                                   base_info["players"].size() : -1));

            // 增强玩家信息：添加 piece, username, rating, ready
            if (base_info.contains("players") && base_info["players"].is_array()) {
                nlohmann::json enhanced_players = nlohmann::json::array();

                for (const auto& player : base_info["players"]) {
                    std::string player_id = player.value("playerId", "");  // camelCase

                    nlohmann::json enhanced_player = player;

                    // 添加棋子颜色
                    auto piece_it = player_pieces_.find(player_id);
                    if (piece_it != player_pieces_.end()) {
                        enhanced_player["piece"] = static_cast<int>(piece_it->second);
                    } else {
                        enhanced_player["piece"] = 0;  // 未分配
                    }

                    // 添加玩家详细信息（通过回调获取）
                    if (player_info_callback_) {
                        nlohmann::json player_info = player_info_callback_(player_id);
                        if (!player_info.empty()) {
                            enhanced_player["username"] = player_info.value("username", player_id);
                            enhanced_player["nickname"] = player_info.value("nickname", player_id);
                            enhanced_player["rating"] = player_info.value("rating", 1500);
                            enhanced_player["level"] = player_info.value("level", 1);
                        }
                    }

                    // 如果没有获取到玩家信息，使用默认值
                    if (!enhanced_player.contains("username")) {
                        enhanced_player["username"] = player_id;
                        enhanced_player["nickname"] = player_id;
                        enhanced_player["rating"] = 1500;
                        enhanced_player["level"] = 1;
                    }

                    // 🔧 新增：添加玩家准备状态
                    {
                        std::shared_lock<std::shared_mutex> lock(players_mutex_);
                        auto session_it = players_.find(player_id);
                        if (session_it != players_.end()) {
                            auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(session_it->second);
                            if (gomoku_session) {
                                enhanced_player["ready"] = gomoku_session->isReady();
                            } else {
                                enhanced_player["ready"] = false;  // 默认未准备
                            }
                        } else {
                            enhanced_player["ready"] = false;  // 默认未准备
                        }
                    }

                    enhanced_players.push_back(enhanced_player);
                }

                base_info["players"] = enhanced_players;
            }

            return base_info;
        }

        std::string GomokuRoom::getGamePhaseString() const {
            switch (getState()) {
                case game_services::game_base::RoomState::WAITING: 
                    return "waiting_players";
                case game_services::game_base::RoomState::PLAYING: 
                    return "game_in_progress";
                case game_services::game_base::RoomState::PAUSED: 
                    return "game_paused";
                case game_services::game_base::RoomState::FINISHED: 
                    return "game_finished";
                default: 
                    return "unknown";
            }
        }

        void GomokuRoom::pauseGameSpecific() {
            if (gomoku_logic_) {
                gomoku_logic_->pauseGame();
            }
            
            broadcastToPlayers(createGomokuMessage("game_paused", {}));
            broadcastToSpectators(createGomokuMessage("game_paused", {}));
        }

        void GomokuRoom::resumeGameSpecific() {
            if (gomoku_logic_) {
                gomoku_logic_->resumeGame();
            }
            
            broadcastToPlayers(createGomokuMessage("game_resumed", {}));
            broadcastToSpectators(createGomokuMessage("game_resumed", {}));
        }

        void GomokuRoom::endGameSpecific() {
            // [FIX] 停止时间控制定时器
            if (gomoku_logic_) {
                gomoku_logic_->stopTimeControl();
                gomoku_logic_->stopGame();
            }

            game_end_time_ = std::chrono::system_clock::now();
            
            // 重置玩家状态
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                for (const auto& pair : players_) {
                    auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
                    if (gomoku_session) {
                        gomoku_session->setTurn(false);
                        gomoku_session->setReady(false);
                    }
                }
            }
            
            LOG_INFO("五子棋游戏结束: " + getRoomId());
        }

        bool GomokuRoom::setPlayerPiece(const std::string& player_id, PieceType piece) {
            if (getState() != game_base::RoomState::WAITING) {
                return false;
            }
            
            if (!isPlayerInRoom(player_id)) {
                return false;
            }
            
            // 检查棋子是否已被占用
            for (const auto& pair : player_pieces_) {
                if (pair.second == piece && pair.first != player_id) {
                    return false;
                }
            }
            
            player_pieces_[player_id] = piece;
            piece_players_[piece] = player_id;
            
            // 更新玩家会话中的棋子类型
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                auto it = players_.find(player_id);
                if (it != players_.end()) {
                    auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(it->second);
                    if (gomoku_session) {
                        gomoku_session->setPieceType(piece);
                    }
                }
            }
            
            // 广播玩家棋子分配
            broadcastToPlayers(createGomokuMessage("player_piece_assigned", {
                {"playerId", player_id},
                {"piece", static_cast<int>(piece)}
            }));
            
            return true;
        }

        PieceType GomokuRoom::getPlayerPiece(const std::string& player_id) const {
            auto it = player_pieces_.find(player_id);
            return it != player_pieces_.end() ? it->second : PieceType::EMPTY;
        }

        bool GomokuRoom::setPlayerReady(const std::string& player_id, bool ready) {
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);

                auto it = players_.find(player_id);
                if (it == players_.end()) {
                    LOG_WARNING("设置准备状态失败：玩家不在房间中 - " + player_id);
                    return false;
                }

                auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(it->second);
                if (!gomoku_session) {
                    LOG_WARNING("设置准备状态失败：会话类型转换失败 - " + player_id);
                    return false;
                }

                gomoku_session->setReady(ready);
            }  // 释放锁

            LOG_INFO("玩家准备状态更新: " + player_id + " -> " + (ready ? "ready" : "not ready"));

            // 广播给房间内所有玩家（包括自己）
            nlohmann::json ready_message = {
                {"type", "player_ready"},
                {"data", {
                    {"playerId", player_id},
                    {"roomId", getRoomId()},
                    {"ready", ready}
                }}
            };
            broadcastToPlayers(ready_message);

            // [注意] 移除自动开始游戏逻辑，改为房主手动开始
            // 游戏开始由房主调用 startGame() 触发

            // 触发准备状态变化回调（用于通知大厅订阅者）
            // [FIX] 已修复死锁问题：回调现在使用基本房间信息，不调用 getRoomStats()
            if (player_ready_callback_) {
                player_ready_callback_(getRoomId(), player_id, ready);
            }

            return true;
        }

        bool GomokuRoom::isPlayerReady(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);

            auto it = players_.find(player_id);
            if (it == players_.end()) {
                return false;
            }

            auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(it->second);
            return gomoku_session ? gomoku_session->isReady() : false;
        }

        std::vector<std::pair<std::string, bool>> GomokuRoom::getAllPlayersReadyStatus() const {
            std::vector<std::pair<std::string, bool>> status;
            std::shared_lock<std::shared_mutex> lock(players_mutex_);

            for (const auto& pair : players_) {
                auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
                bool ready = gomoku_session ? gomoku_session->isReady() : false;
                status.push_back({pair.first, ready});
            }

            return status;
        }

        bool GomokuRoom::addSpectator(const std::string& spectator_id, 
                                     std::shared_ptr<game_base::PlayerSessionBase> session) {
            if (getSpectatorCount() >= gomoku_config_.maxSpectators) {
                return false;
            }
            
            if (!gomoku_config_.allowSpectators) {
                return false;
            }
            
            {
                std::unique_lock<std::shared_mutex> lock(spectators_mutex_);
                spectators_[spectator_id] = session;
            }
            
            // 设置为观战者
            auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(session);
            if (gomoku_session) {
                gomoku_session->setSpectator(true);
            }
            
            // 发送当前游戏状态给观战者
            session->sendMessage(createGomokuMessage("spectator_joined", getGameSpecificState()));
            
            // 通知其他人观战者加入
            notifySpectatorsPlayerChange(spectator_id, true);
            
            LOG_INFO("观战者加入房间: " + spectator_id + " -> " + getRoomId());
            return true;
        }

        bool GomokuRoom::removeSpectator(const std::string& spectator_id) {
            bool removed = false;
            
            {
                std::unique_lock<std::shared_mutex> lock(spectators_mutex_);
                auto it = spectators_.find(spectator_id);
                if (it != spectators_.end()) {
                    spectators_.erase(it);
                    removed = true;
                }
            }
            
            if (removed) {
                notifySpectatorsPlayerChange(spectator_id, false);
                LOG_INFO("观战者离开房间: " + spectator_id + " <- " + getRoomId());
            }
            
            return removed;
        }

        bool GomokuRoom::isSpectator(const std::string& spectator_id) const {
            std::shared_lock<std::shared_mutex> lock(spectators_mutex_);
            return spectators_.find(spectator_id) != spectators_.end();
        }

        int GomokuRoom::getSpectatorCount() const {
            std::shared_lock<std::shared_mutex> lock(spectators_mutex_);
            return static_cast<int>(spectators_.size());
        }

        std::vector<std::string> GomokuRoom::getSpectatorIds() const {
            std::vector<std::string> ids;
            
            std::shared_lock<std::shared_mutex> lock(spectators_mutex_);
            for (const auto& pair : spectators_) {
                ids.push_back(pair.first);
            }
            
            return ids;
        }

        void GomokuRoom::broadcastToSpectators(const nlohmann::json& message) {
            std::shared_lock<std::shared_mutex> lock(spectators_mutex_);
            
            for (const auto& pair : spectators_) {
                pair.second->sendMessage(message);
            }
        }

        bool GomokuRoom::handlePlacePiece(const std::string& player_id, const Position& position) {
            if (!gomoku_logic_) {
                sendErrorToPlayer(player_id, "GAME_NOT_READY", "游戏逻辑未就绪");
                return false;
            }
            
            if (!validatePlayerAction(player_id)) {
                sendErrorToPlayer(player_id, "INVALID_PLAYER", "无效的玩家操作");
                return false;
            }
            
            // [FIX] 详细的落子日志，用于确认正确落子
            PieceType player_piece = getPlayerPiece(player_id);
            std::string piece_color = (player_piece == PieceType::BLACK) ? "黑子" : 
                                    (player_piece == PieceType::WHITE) ? "白子" : "未知";
            
            LOG_INFO("[MOVE] 玩家落子请求: " + player_id + " (" + piece_color + ") -> 位置(" + 
                    std::to_string(position.row) + "," + std::to_string(position.col) + 
                    "), 房间: " + getRoomId() + ", 当前回合数: " + std::to_string(total_moves_ + 1));
            
            // 委托给游戏逻辑处理
            if (gomoku_logic_->placePiece(player_id, position)) {
                // [FIX] 落子成功日志
                LOG_INFO("✅ 落子成功: " + player_id + " (" + piece_color + ") 在位置(" + 
                        std::to_string(position.row) + "," + std::to_string(position.col) + 
                        ") 放置棋子，房间: " + getRoomId());
                
                broadcastMoveResult(player_id, position, true);
                
                // 更新回合状态
                std::string next_turn_player = getCurrentTurnPlayerId();
                {
                    std::shared_lock<std::shared_mutex> lock(players_mutex_);
                    for (const auto& pair : players_) {
                        auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
                        if (gomoku_session) {
                            bool is_turn = (pair.first == next_turn_player);
                            gomoku_session->setTurn(is_turn);
                        }
                    }
                }
                
                total_moves_++;
                
                // [FIX] 回合切换日志
                LOG_INFO("[SWITCH] 回合切换: 下一位玩家 " + next_turn_player + 
                        ", 总回合数: " + std::to_string(total_moves_) + ", 房间: " + getRoomId());
                
                broadcastGameState();
                
                // 检查游戏是否结束
                if (gomoku_logic_->isGameFinished()) {
                    auto game_state = gomoku_logic_->getGameState();
                    LOG_INFO("[WIN] 游戏结束检测: 房间 " + getRoomId() + " 游戏结束，结果: " + 
                            std::to_string(static_cast<int>(game_state.gameResult)));
                    handleGameEnd(game_state.gameResult);
                }
                
                return true;
            } else {
                // [FIX] 落子失败日志
                LOG_WARNING("❌ 落子失败: " + player_id + " (" + piece_color + ") 尝试在位置(" + 
                           std::to_string(position.row) + "," + std::to_string(position.col) + 
                           ") 放置棋子失败，房间: " + getRoomId());
                
                broadcastMoveResult(player_id, position, false);
                return false;
            }
        }

        bool GomokuRoom::handleUndoRequest(const std::string& player_id) {
            // [FIX] 按照文档实现悔棋请求-响应流程
            // 1. 检查是否允许悔棋
            if (!gomoku_logic_ || !gomoku_config_.allowUndo) {
                sendErrorToPlayer(player_id, "UNDO_NOT_ALLOWED", "不允许悔棋");
                return false;
            }

            // 2. 检查是否已有待处理的悔棋请求
            if (has_pending_undo_request_) {
                sendErrorToPlayer(player_id, "UNDO_PENDING", "已有待处理的悔棋请求");
                return false;
            }

            // 3. 检查是否有棋可悔
            if (gomoku_logic_->getGameState().totalMoves == 0) {
                sendErrorToPlayer(player_id, "NO_MOVES", "没有可悔的棋");
                return false;
            }

            // 4. 获取对手ID
            std::string opponent_id = getOpponentId(player_id);
            if (opponent_id.empty()) {
                sendErrorToPlayer(player_id, "NO_OPPONENT", "没有对手");
                return false;
            }

            // 5. 记录悔棋请求
            pending_undo_requester_ = player_id;
            has_pending_undo_request_ = true;

            // 6. 发送悔棋请求给对手
            nlohmann::json undo_request_data = {
                {"fromPlayer", player_id}
            };
            sendToPlayer(opponent_id, createGomokuMessage(constants::MSG_UNDO_REQUEST, undo_request_data));

            LOG_INFO("悔棋请求已发送: " + player_id + " -> " + opponent_id);
            return true;
        }

        bool GomokuRoom::handleUndoResponse(const std::string& player_id, bool accept) {
            // [FIX] 按照文档处理悔棋响应
            // 1. 检查是否有待处理的悔棋请求
            if (!has_pending_undo_request_) {
                sendErrorToPlayer(player_id, "NO_UNDO_REQUEST", "没有待处理的悔棋请求");
                return false;
            }

            // 2. 验证响应者是否是对手（不能自己响应自己的请求）
            if (player_id == pending_undo_requester_) {
                sendErrorToPlayer(player_id, "INVALID_RESPONDER", "不能响应自己的悔棋请求");
                return false;
            }

            // 3. 清除待处理状态
            std::string requester_id = pending_undo_requester_;
            has_pending_undo_request_ = false;
            pending_undo_requester_.clear();

            if (accept) {
                // 4a. 对手同意悔棋，执行悔棋
                if (gomoku_logic_->undoMove(requester_id)) {
                    // 更新回合数
                    total_moves_ = gomoku_logic_->getGameState().totalMoves;

                    // 广播悔棋成功消息
                    nlohmann::json undo_data = {
                        {"playerId", requester_id},
                        {"board", gomoku_logic_->getGameSpecificState()},
                        {"totalMoves", total_moves_}
                    };

                    broadcastToPlayers(createGomokuMessage("move_undone", undo_data));
                    broadcastToSpectators(createGomokuMessage("move_undone", undo_data));
                    broadcastGameState();

                    LOG_INFO("悔棋成功: " + requester_id + " (对手 " + player_id + " 同意)");
                    return true;
                } else {
                    sendErrorToPlayer(requester_id, "UNDO_FAILED", "悔棋失败");
                    return false;
                }
            } else {
                // 4b. 对手拒绝悔棋，通知请求者
                sendErrorToPlayer(requester_id, "UNDO_REJECTED", "对手拒绝悔棋");
                LOG_INFO("悔棋被拒绝: " + requester_id + " (对手 " + player_id + " 拒绝)");
                return true;
            }
        }

        bool GomokuRoom::handleSurrender(const std::string& player_id) {
            if (!gomoku_logic_) {
                return false;
            }

            // [FIX] 获取玩家棋子类型，按文档格式添加 piece 字段
            PieceType piece = getPlayerPiece(player_id);

            if (gomoku_logic_->surrender(player_id)) {
                nlohmann::json surrender_data = {
                    {"playerId", player_id},
                    {"piece", static_cast<int>(piece)}  // 添加棋子类型: 1=黑, 2=白
                };

                broadcastToPlayers(createGomokuMessage("player_surrendered", surrender_data));
                broadcastToSpectators(createGomokuMessage("player_surrendered", surrender_data));

                auto game_state = gomoku_logic_->getGameState();
                handleGameEnd(game_state.gameResult);
                return true;
            }

            return false;
        }

        bool GomokuRoom::handleDrawOffer(const std::string& player_id) {
            if (!gomoku_logic_) {
                return false;
            }
            
            if (gomoku_logic_->offerDraw(player_id)) {
                // 通知对手
                std::string opponent_id = getOpponentId(player_id);
                if (!opponent_id.empty()) {
                    sendToPlayer(opponent_id, createGomokuMessage(constants::MSG_DRAW_OFFER, {
                        {"fromPlayer", player_id}
                    }));
                }
                return true;
            }
            
            return false;
        }

        bool GomokuRoom::handleDrawResponse(const std::string& player_id, bool accept) {
            if (!gomoku_logic_) {
                return false;
            }
            
            if (gomoku_logic_->respondToDraw(player_id, accept)) {
                // 通知所有人和棋结果
                broadcastToPlayers(createGomokuMessage(constants::MSG_DRAW_RESPONSE, {
                    {"fromPlayer", player_id},
                    {"accepted", accept}
                }));
                
                if (accept) {
                    handleGameEnd(GameResult::DRAW);
                }
                
                return true;
            }
            
            return false;
        }

        bool GomokuRoom::updateGomokuConfig(const GomokuConfig& config) {
            if (getState() != game_base::RoomState::WAITING) {
                return false;
            }
            
            gomoku_config_ = config;
            
            if (gomoku_logic_) {
                gomoku_logic_->updateConfig(config);
            }
            
            // 广播配置更新
            broadcastToPlayers(createGameConfigMessage());
            broadcastToSpectators(createGameConfigMessage());
            
            return true;
        }

        void GomokuRoom::autoAssignPieces() {
            auto player_ids = getPlayerIds();
            if (player_ids.size() != 2) {
                return;
            }
            
            // [FIX] 确保创建者总是执黑，不依赖于getPlayerIds()的顺序
            std::string creator_id = getCreatorId();
            std::string other_player_id;
            
            // 找到非创建者的玩家
            for (const auto& player_id : player_ids) {
                if (player_id != creator_id) {
                    other_player_id = player_id;
                    break;
                }
            }
            
            if (!other_player_id.empty()) {
                // 创建者执黑，其他玩家执白
                setPlayerPiece(creator_id, PieceType::BLACK);
                setPlayerPiece(other_player_id, PieceType::WHITE);
                
                LOG_INFO("自动分配棋子: 创建者(" + creator_id + ")=黑子, 玩家(" + other_player_id + ")=白子");
            } else {
                LOG_WARNING("无法找到非创建者玩家，棋子分配失败");
            }
        }

        bool GomokuRoom::canStartGame() const {
            // [FIX] 首先检查房间状态，如果游戏已经开始则不能再次开始
            if (getState() != game_base::RoomState::WAITING) {
                LOG_DEBUG("Cannot start game: room state is not WAITING (current: " +
                         std::to_string(static_cast<int>(getState())) + ")");
                return false;
            }

            if (getPlayerCount() != 2) {
                LOG_DEBUG("Cannot start game: need 2 players, current: " + std::to_string(getPlayerCount()));
                return false;
            }

            // P0 修复: 根据配置决定是否检查准备状态
            // 如果配置不要求准备状态，直接允许开始
            if (!gomoku_config_.requireReadyToStart) {
                LOG_DEBUG("Ready check bypassed (requireReadyToStart=false)");
                return true;
            }

            // 检查所有玩家是否都准备就绪
            bool all_ready = true;
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                for (const auto& pair : players_) {
                    auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
                    if (!gomoku_session || !gomoku_session->isReady()) {
                        all_ready = false;
                        break;
                    }
                }
            }

            if (!all_ready) {
                LOG_DEBUG("Not all players are ready");
            } else {
                LOG_DEBUG("All players ready");
            }

            return all_ready;
        }

        std::string GomokuRoom::getOpponentId(const std::string& player_id) const {
            auto player_ids = getPlayerIds();
            for (const auto& id : player_ids) {
                if (id != player_id) {
                    return id;
                }
            }
            return "";
        }

        std::string GomokuRoom::getCurrentTurnPlayerId() const {
            if (!gomoku_logic_) {
                return "";
            }
            
            PieceType current_piece = gomoku_logic_->getCurrentPlayer();
            auto it = piece_players_.find(current_piece);
            return it != piece_players_.end() ? it->second : "";
        }

        bool GomokuRoom::canPlayerStartGame(const std::string& player_id) const {
            return player_id == getCreatorId() && canStartGame();
        }

        nlohmann::json GomokuRoom::getRoomStats() const {
            auto base_stats = getRoomInfo();
            
            base_stats["gomokuStats"] = {
                {"totalMoves", total_moves_},
                {"spectatorCount", getSpectatorCount()},
                {"gameMode", static_cast<int>(gomoku_config_.gameMode)}
            };
            
            if (game_start_time_ != std::chrono::system_clock::time_point{}) {
                auto now = std::chrono::system_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - game_start_time_);
                base_stats["gomokuStats"]["gameDurationSeconds"] = duration.count();
            }
            
            return base_stats;
        }

        void GomokuRoom::resetRoom() {
            player_pieces_.clear();
            piece_players_.clear();
            total_moves_ = 0;
            game_start_time_ = std::chrono::system_clock::time_point{};
            game_end_time_ = std::chrono::system_clock::time_point{};

            // 重置悔棋请求状态
            pending_undo_requester_.clear();
            has_pending_undo_request_ = false;
            
            if (gomoku_logic_) {
                gomoku_logic_->resetGame();
            }
            
            // 重置玩家状态
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                for (const auto& pair : players_) {
                    auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
                    if (gomoku_session) {
                        gomoku_session->resetGameState();
                    }
                }
            }
        }

        // 保护方法实现
        void GomokuRoom::onPlayerJoined(const std::string& player_id, int current_player_count) {
            // [FIX] 在玩家加入时立即分配棋子
            // 规则：第一个玩家 = 黑子(1)，第二个玩家 = 白子(2)
            // 使用 player_pieces_ 的大小来决定，而不是依赖 piece_players_ 的状态
            if (player_pieces_.find(player_id) == player_pieces_.end()) {
                PieceType assigned_piece;

                // 根据当前已分配的玩家数量决定棋子颜色
                // 如果只有0个玩家已分配，分配黑子；如果有1个，分配白子
                if (player_pieces_.empty()) {
                    assigned_piece = PieceType::BLACK;
                } else if (player_pieces_.size() == 1) {
                    // 检查是否已有人拿黑子
                    bool has_black = false;
                    for (const auto& pair : player_pieces_) {
                        if (pair.second == PieceType::BLACK) {
                            has_black = true;
                            break;
                        }
                    }
                    assigned_piece = has_black ? PieceType::WHITE : PieceType::BLACK;
                } else {
                    // 已有两个玩家，默认分配白子（不应该发生）
                    assigned_piece = PieceType::WHITE;
                }

                // 直接设置棋子（不广播，因为稍后会发送 room_info）
                player_pieces_[player_id] = assigned_piece;
                piece_players_[assigned_piece] = player_id;

                // 更新玩家会话中的棋子类型
                {
                    std::shared_lock<std::shared_mutex> lock(players_mutex_);
                    auto it = players_.find(player_id);
                    if (it != players_.end()) {
                        auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(it->second);
                        if (gomoku_session) {
                            gomoku_session->setPieceType(assigned_piece);
                        }
                    }
                }

                LOG_INFO("玩家加入时分配棋子: " + player_id + " -> " +
                        (assigned_piece == PieceType::BLACK ? "黑子" : "白子") +
                        " (当前player_pieces_大小: " + std::to_string(player_pieces_.size()) + ")");
            }

            // [FIX] 按照文档 §7.4.4 格式发送 player_joined 消息
            int count = current_player_count > 0 ? current_player_count : getPlayerCount();
            int piece_type = 0;
            if (player_pieces_.find(player_id) != player_pieces_.end()) {
                piece_type = static_cast<int>(player_pieces_[player_id]);
            }

            // 获取玩家详细信息（通过回调）
            nlohmann::json player_info;
            if (player_info_callback_) {
                player_info = player_info_callback_(player_id);
            }
            // 如果没有获取到玩家信息，使用基本信息
            if (player_info.empty()) {
                player_info = {
                    {"playerId", player_id},
                    {"username", player_id},
                    {"nickname", player_id}
                };
            }

            // 向其他玩家广播新玩家加入（包含 playerInfo 字段）
            nlohmann::json join_message = {
                {"type", "player_joined"},
                {"data", {
                    {"playerId", player_id},
                    {"roomId", getRoomId()},
                    {"pieceType", piece_type},
                    {"playerCount", count},
                    {"playerInfo", player_info}
                }}
            };

            broadcastToOthers(player_id, join_message);

            // [FIX] 按照文档格式发送 room_joined 消息（包含顶层 pieceType 字段）
            std::string piece_type_str = "none";
            if (piece_type == 1) piece_type_str = "black";
            else if (piece_type == 2) piece_type_str = "white";

            // [DEBUG] 获取 roomInfo 并记录详细日志
            auto room_info = getRoomInfo();
            LOG_INFO("[DEBUG] getRoomInfo() 返回: players_count=" +
                    std::to_string(room_info.contains("players") ? room_info["players"].size() : -1) +
                    ", roomId=" + room_info.value("roomId", "N/A") +
                    ", currentPlayers=" + std::to_string(room_info.value("currentPlayers", -1)));
            if (room_info.contains("players") && room_info["players"].is_array()) {
                for (const auto& p : room_info["players"]) {
                    LOG_INFO("[DEBUG] player in roomInfo: playerId=" + p.value("playerId", "N/A") +
                            ", piece=" + std::to_string(p.value("piece", -1)));
                }
            }

            nlohmann::json room_joined_message = {
                {"type", "room_joined"},
                {"data", {
                    {"roomId", getRoomId()},
                    {"playerId", player_id},
                    {"role", "player"},
                    {"pieceType", piece_type_str},
                    {"roomInfo", room_info}
                }}
            };
            sendToPlayer(player_id, room_joined_message);
            LOG_INFO("📤 发送 room_joined 消息: player=" + player_id + ", pieceType=" + piece_type_str);

            // 发送房间配置给新玩家
            sendToPlayer(player_id, createGameConfigMessage());

            // 始终发送当前状态给新玩家（包含 playerPieces、gamePhase 等）
            // 即使游戏未开始，也需要让客户端知道玩家棋子分配
            sendToPlayer(player_id, createGomokuMessage("current_state", getGameSpecificState()));

            // 触发玩家加入回调
            if (player_join_callback_) {
                player_join_callback_(getRoomId(), player_id);
            }
        }

        void GomokuRoom::onPlayerLeft(const std::string& player_id) {
            // 清理玩家棋子分配
            auto it = player_pieces_.find(player_id);
            if (it != player_pieces_.end()) {
                piece_players_.erase(it->second);
                player_pieces_.erase(it);
            }
            
            // [FIX] 修复死锁：如果游戏正在进行，异步结束游戏
            if (getState() == game_base::RoomState::PLAYING) {
                std::string opponent_id = getOpponentId(player_id);
                
                auto event_loop_shared = event_loop_.lock();
                if (event_loop_shared) {
                    // 异步结束游戏，避免死锁
                    event_loop_shared->runAfter(1, [this, opponent_id]() {
                        try {
                            if (!opponent_id.empty()) {
                                endGame({opponent_id}); // 对手获胜
                            } else {
                                endGame({}); // 平局
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("异步结束游戏失败(玩家离开): " + std::string(e.what()));
                        }
                    });
                } else {
                    // 备用方案：直接调用，但可能存在死锁风险
                    LOG_WARNING("EventLoop不可用，使用同步结束游戏(玩家离开)");
                    if (!opponent_id.empty()) {
                        endGame({opponent_id}); // 对手获胜
                    } else {
                        endGame({}); // 平局
                    }
                }
            }
            
            GameRoomBase::onPlayerLeft(player_id);
            
            // 触发玩家离开回调
            if (player_leave_callback_) {
                player_leave_callback_(getRoomId(), player_id);
            }
        }

        void GomokuRoom::onGameStateChanged() {
            broadcastGameState();
            GameRoomBase::onGameStateChanged();
        }

        // 私有方法实现
        void GomokuRoom::initializeGameLogic() {
            auto event_loop_shared = event_loop_.lock();
            gomoku_logic_ = std::make_shared<GomokuLogic>(gomoku_config_, event_loop_shared);
            
            // 如果EventLoop有效，设置到逻辑对象中
            if (event_loop_shared) {
                gomoku_logic_->setEventLoop(event_loop_shared);
                LOG_DEBUG("五子棋房间 " + getRoomId() + " 已设置EventLoop用于时间轮定时器");
            } else {
                LOG_WARNING("五子棋房间 " + getRoomId() + " 未设置EventLoop，时间控制将不可用");
            }
            
            setupGameLogicCallbacks();
        }

        void GomokuRoom::setupGameLogicCallbacks() {
            if (!gomoku_logic_) {
                return;
            }
            
            // 设置状态更新回调
            gomoku_logic_->setStateUpdateCallback(
                [this](const nlohmann::json& state) {
                    onGameStateUpdate(state);
                }
            );
            
            // 设置游戏结束回调
            gomoku_logic_->setGameEndCallback(
                [this](const std::vector<std::string>& winners) {
                    onGameEnd(winners);
                }
            );
            
            // 设置时间更新回调
            gomoku_logic_->setTimeUpdateCallback(
                [this](PieceType piece, int timeLeft) {
                    onTimeUpdate(piece, timeLeft);
                }
            );
        }

        void GomokuRoom::broadcastGameState() {
            nlohmann::json state_message = createGomokuMessage(constants::MSG_GAME_STATE, getGameSpecificState());
            broadcastToPlayers(state_message);
            broadcastToSpectators(state_message);
        }

        void GomokuRoom::broadcastMoveResult(const std::string& player_id, const Position& position, bool success) {
            // [FIX] 按照文档格式添加 piece 字段
            PieceType piece = getPlayerPiece(player_id);

            nlohmann::json result_data = {
                {"playerId", player_id},
                {"position", position.toJson()},
                {"piece", static_cast<int>(piece)},  // 添加棋子类型: 1=黑, 2=白
                {"success", success}
            };

            nlohmann::json result_message = createGomokuMessage(constants::MSG_MOVE_RESULT, result_data);
            broadcastToPlayers(result_message);
            broadcastToSpectators(result_message);
        }

        void GomokuRoom::broadcastGameEnd(GameResult result, const std::string& winner_id) {
            // [FIX] 按照文档 §7.4.5 格式构建 game_end 消息

            // 计算结果字符串
            std::string result_str;
            std::string reason;
            int winner_piece = 0;

            switch (result) {
                case GameResult::BLACK_WIN:
                    result_str = "BLACK_WIN";
                    reason = "五子连珠";
                    winner_piece = 1;
                    break;
                case GameResult::WHITE_WIN:
                    result_str = "WHITE_WIN";
                    reason = "五子连珠";
                    winner_piece = 2;
                    break;
                case GameResult::DRAW:
                    result_str = "DRAW";
                    reason = "和棋";
                    winner_piece = 0;
                    break;
                case GameResult::TIMEOUT:
                    result_str = (winner_piece == 1) ? "BLACK_WIN" : "WHITE_WIN";
                    reason = "超时判负";
                    break;
                case GameResult::SURRENDER:
                    result_str = (winner_piece == 1) ? "BLACK_WIN" : "WHITE_WIN";
                    reason = "对手认输";
                    break;
                default:
                    result_str = "UNKNOWN";
                    reason = "未知";
                    break;
            }

            // 计算游戏时长
            int duration_seconds = 0;
            if (game_start_time_ != std::chrono::system_clock::time_point{}) {
                auto now = std::chrono::system_clock::now();
                duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - game_start_time_).count();
            }

            // 获取时间剩余
            int black_time_left = 0;
            int white_time_left = 0;
            if (gomoku_logic_) {
                black_time_left = gomoku_logic_->getTimeLeft(PieceType::BLACK);
                white_time_left = gomoku_logic_->getTimeLeft(PieceType::WHITE);
            }

            // 构建完整的 game_end 消息（符合文档 §7.4.5）
            nlohmann::json end_data = {
                {"result", static_cast<int>(result)},
                {"resultStr", result_str},
                {"winnerId", winner_id},
                {"winnerPiece", winner_piece},
                {"reason", reason},
                {"stats", {
                    {"totalMoves", total_moves_},
                    {"duration", duration_seconds},
                    {"blackTimeLeft", black_time_left},
                    {"whiteTimeLeft", white_time_left}
                }}
                // 注意：winningLine 需要五子连珠位置数据，如果 gomoku_logic_ 提供则添加
            };

            nlohmann::json end_message = createGomokuMessage(constants::MSG_GAME_END, end_data);
            broadcastToPlayers(end_message);
            broadcastToSpectators(end_message);
        }

        void GomokuRoom::broadcastTimeUpdate() {
            if (!gomoku_logic_) return;
            
            nlohmann::json time_data = {
                {"blackTime", gomoku_logic_->getTimeLeft(PieceType::BLACK)},
                {"whiteTime", gomoku_logic_->getTimeLeft(PieceType::WHITE)}
            };
            
            nlohmann::json time_message = createGomokuMessage(constants::MSG_TIME_UPDATE, time_data);
            broadcastToPlayers(time_message);
        }

        void GomokuRoom::handleGameEnd(GameResult result) {
            std::string winner_id;
            
            if (result == GameResult::BLACK_WIN) {
                auto it = piece_players_.find(PieceType::BLACK);
                if (it != piece_players_.end()) {
                    winner_id = it->second;
                }
            } else if (result == GameResult::WHITE_WIN) {
                auto it = piece_players_.find(PieceType::WHITE);
                if (it != piece_players_.end()) {
                    winner_id = it->second;
                }
            }
            
            broadcastGameEnd(result, winner_id);
            
            // [FIX] 修复死锁：异步结束游戏，避免在回调链中直接调用endGame()
            auto event_loop_shared = event_loop_.lock();
            if (event_loop_shared) {
                // 捕获必要的数据
                std::vector<std::string> winners;
                if (!winner_id.empty()) {
                    winners.push_back(winner_id);
                }
                
                // 异步执行endGame，避免死锁
                event_loop_shared->runAfter(1, [this, winners]() {
                    try {
                        endGame(winners);
                    } catch (const std::exception& e) {
                        LOG_ERROR("异步结束游戏失败: " + std::string(e.what()));
                    }
                });
            } else {
                // 备用方案：直接调用，但可能存在死锁风险
                LOG_WARNING("EventLoop不可用，使用同步结束游戏");
                if (!winner_id.empty()) {
                    endGame({winner_id});
                } else {
                    endGame({});
                }
            }
        }

        bool GomokuRoom::validatePlayerAction(const std::string& player_id) const {
            bool is_in_room = isPlayerInRoom(player_id);
            auto current_state = getState();
            PieceType player_piece = getPlayerPiece(player_id);
            
            // [FIX] 详细的验证日志，用于调试落子失败原因
            LOG_INFO("[VERIFY] 玩家操作验证: " + player_id + 
                    " | 在房间: " + (is_in_room ? "是" : "否") +
                    " | 房间状态: " + std::to_string(static_cast<int>(current_state)) +
                    " (应为" + std::to_string(static_cast<int>(game_base::RoomState::PLAYING)) + ")" +
                    " | 玩家棋子: " + std::to_string(static_cast<int>(player_piece)) +
                    " (应不为" + std::to_string(static_cast<int>(PieceType::EMPTY)) + ")");
            
            bool result = is_in_room && 
                         current_state == game_base::RoomState::PLAYING &&
                         player_piece != PieceType::EMPTY;
                         
            if (!result) {
                LOG_WARNING("❌ 玩家操作验证失败: " + player_id + 
                           " | 原因: " + 
                           (!is_in_room ? "不在房间" : "") +
                           (current_state != game_base::RoomState::PLAYING ? " 房间状态错误" : "") +
                           (player_piece == PieceType::EMPTY ? " 未分配棋子" : ""));
            }
            
            return result;
        }

        void GomokuRoom::notifySpectatorsPlayerChange(const std::string& player_id, bool joined) {
            nlohmann::json notification = {
                {"spectatorId", player_id},
                {"joined", joined},
                {"spectatorCount", getSpectatorCount()}
            };
            
            broadcastToPlayers(createGomokuMessage("spectator_change", notification));
            broadcastToSpectators(createGomokuMessage("spectator_change", notification));
        }

        nlohmann::json GomokuRoom::createPlayerStateMessage(const std::string& player_id) const {
            auto piece = getPlayerPiece(player_id);
            return nlohmann::json{
                {"playerId", player_id},
                {"piece", static_cast<int>(piece)},
                {"isReady", false} // 需要从会话获取实际状态
            };
        }

        nlohmann::json GomokuRoom::createSpectatorStateMessage() const {
            return nlohmann::json{
                {"spectatorCount", getSpectatorCount()},
                {"spectatorIds", getSpectatorIds()}
            };
        }

        nlohmann::json GomokuRoom::createGameConfigMessage() const {
            return createGomokuMessage("room_config", gomoku_config_.toJson());
        }

        nlohmann::json GomokuRoom::createGomokuMessage(const std::string& type, const nlohmann::json& data) const {
            return nlohmann::json{
                {"type", type},
                {"data", data},
                {"roomId", getRoomId()},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };
        }

        void GomokuRoom::onTimeUpdate(PieceType piece, int timeLeft) {
            broadcastTimeUpdate();
        }

        void GomokuRoom::onGameStateUpdate(const nlohmann::json& state) {
            broadcastGameState();
        }

        void GomokuRoom::onGameEnd(const std::vector<std::string>& winners) {
            GameResult result = GameResult::DRAW;
            if (!winners.empty()) {
                PieceType winner_piece = getPlayerPiece(winners[0]);
                result = (winner_piece == PieceType::BLACK) ? GameResult::BLACK_WIN : GameResult::WHITE_WIN;
            }

            // 记录游戏结束时间
            game_end_time_ = std::chrono::system_clock::now();

            // [FIX] 不要重复调用handleGameEnd，避免循环调用和重复处理
            // handleGameEnd(result); // ❌ 移除这行，避免重复调用

            // [FIX] 直接广播游戏结束，不触发额外的endGame调用
            std::string winner_id;
            if (!winners.empty()) {
                winner_id = winners[0];
            }

            LOG_INFO("[WIN] 游戏最终结束: 房间 " + getRoomId() +
                    ", 结果: " + std::to_string(static_cast<int>(result)) +
                    ", 获胜者: " + (winner_id.empty() ? "平局" : winner_id));

            broadcastGameEnd(result, winner_id);

            // [NEW] 构建游戏结束上下文，用于结算 API
            GameEndContext context = buildGameEndContext(result, winner_id);

            // [FIX] 异步触发游戏结束回调，避免资源死锁
            if (game_end_callback_) {
                // 使用lambda捕获必要的数据，在当前调用栈之外执行回调
                std::string room_id = getRoomId();
                auto callback = game_end_callback_;

                // [FIX] 正确处理weak_ptr，先转换为shared_ptr
                auto event_loop_shared = event_loop_.lock();
                if (event_loop_shared) {
                    // 使用定时器异步执行，避免死锁
                    event_loop_shared->runAfter(1, [callback, room_id, winners, result]() {
                        try {
                            // [FIX] 传递正确的GameResult给回调
                            std::string winner_for_callback;
                            if (!winners.empty()) {
                                winner_for_callback = winners[0];
                            }

                            LOG_INFO("[CALLBACK] 执行游戏结束回调: 房间 " + room_id +
                                    ", 结果: " + std::to_string(static_cast<int>(result)) +
                                    ", 获胜者: " + winner_for_callback);

                            callback(room_id, winners);
                        } catch (const std::exception& e) {
                            LOG_ERROR("异步游戏结束回调执行失败: " + std::string(e.what()));
                        }
                    });
                } else {
                    // 备用方案：EventLoop不可用时直接调用，但可能存在死锁风险
                    LOG_WARNING("EventLoop不可用，使用同步游戏结束回调");
                    try {
                        callback(getRoomId(), winners);
                    } catch (const std::exception& e) {
                        LOG_ERROR("同步游戏结束回调执行失败: " + std::string(e.what()));
                    }
                }
            }

            // [NEW] 调用带上下文的回调（用于结算 API）
            if (game_end_callback_with_context_) {
                std::string room_id = getRoomId();
                auto callback = game_end_callback_with_context_;

                auto event_loop_shared = event_loop_.lock();
                if (event_loop_shared) {
                    event_loop_shared->runAfter(2, [callback, room_id, winners, context]() {
                        try {
                            LOG_INFO("[SETTLEMENT_CALLBACK] 执行结算回调: 房间 " + room_id +
                                    ", 玩家数: " + std::to_string(context.players.size()));
                            callback(room_id, winners, context);
                        } catch (const std::exception& e) {
                            LOG_ERROR("[SETTLEMENT_CALLBACK] 结算回调执行失败: " + std::string(e.what()));
                        }
                    });
                } else {
                    try {
                        callback(getRoomId(), winners, context);
                    } catch (const std::exception& e) {
                        LOG_ERROR("[SETTLEMENT_CALLBACK] 同步结算回调执行失败: " + std::string(e.what()));
                    }
                }
            }
        }

        void GomokuRoom::sendErrorToPlayer(const std::string& player_id, 
                                          const std::string& error_code, 
                                          const std::string& error_message) {
            sendToPlayer(player_id, createGomokuMessage("error", {
                {"code", error_code},
                {"message", error_message}
            }));
        }

        void GomokuRoom::sendSuccessToPlayer(const std::string& player_id, 
                                           const std::string& action, 
                                           const nlohmann::json& data) {
            sendToPlayer(player_id, createGomokuMessage("success", {
                {"action", action},
                {"data", data}
            }));
        }

        void GomokuRoom::logGameEvent(const std::string& event, const nlohmann::json& data) {
            LOG_INFO("五子棋房间事件 [" + getRoomId() + "]: " + event + 
                    (data.empty() ? "" : " - " + data.dump()));
        }

        // 静态方法实现
        std::shared_ptr<GomokuRoom> GomokuRoom::create(const std::string& room_id,
                                                      const std::string& creator_id,
                                                      const GomokuConfig& config,
                                                      std::shared_ptr<common::network::EventLoop> event_loop) {
            return std::make_shared<GomokuRoom>(room_id, creator_id, config, event_loop);
        }

        GomokuConfig GomokuRoom::getDefaultRoomConfig() {
            return GomokuConfig();
        }

        bool GomokuRoom::validateRoomConfig(const GomokuConfig& config) {
            return GomokuLogic::validateConfig(config);
        }

        // 生命周期回调方法实现
        void GomokuRoom::setRoomCreatedCallback(std::function<void(const std::string&)> callback) {
            room_created_callback_ = callback;
        }

        void GomokuRoom::setRoomDestroyedCallback(std::function<void(const std::string&)> callback) {
            room_destroyed_callback_ = callback;
        }

        void GomokuRoom::setPlayerJoinCallback(std::function<void(const std::string&, const std::string&)> callback) {
            player_join_callback_ = callback;
        }

        void GomokuRoom::setPlayerLeaveCallback(std::function<void(const std::string&, const std::string&)> callback) {
            player_leave_callback_ = callback;
        }

        void GomokuRoom::setGameEndCallback(std::function<void(const std::string&, const std::vector<std::string>&)> callback) {
            game_end_callback_ = callback;
        }

        void GomokuRoom::setPlayerReadyCallback(std::function<void(const std::string&, const std::string&, bool)> callback) {
            player_ready_callback_ = callback;
        }

        void GomokuRoom::setGameStartCallback(std::function<void(const std::string&)> callback) {
            game_start_callback_ = callback;
        }

        void GomokuRoom::setPlayerInfoCallback(std::function<nlohmann::json(const std::string&)> callback) {
            player_info_callback_ = callback;
        }

        void GomokuRoom::setGameEndCallbackWithContext(
            std::function<void(const std::string&, const std::vector<std::string>&, const GameEndContext&)> callback) {
            game_end_callback_with_context_ = callback;
        }

        GameEndContext GomokuRoom::buildGameEndContext(GameResult result, const std::string& winner_id) {
            GameEndContext context;
            context.room_id = getRoomId();
            context.game_type = "gomoku";
            context.game_mode = gameModeToString(gomoku_config_.gameMode);
            context.started_at = game_start_time_;
            context.ended_at = game_end_time_;
            context.total_moves = total_moves_;
            context.winner_id = winner_id;

            // 计算游戏时长
            if (game_start_time_ != std::chrono::system_clock::time_point{} &&
                game_end_time_ != std::chrono::system_clock::time_point{}) {
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                    game_end_time_ - game_start_time_);
                context.duration_seconds = static_cast<int>(duration.count());
            }

            // 收集玩家信息
            for (const auto& [player_id, piece] : player_pieces_) {
                GameEndContext::PlayerContext player_ctx;
                player_ctx.user_id = player_id;
                player_ctx.piece = piece;

                // 确定结果
                if (result == GameResult::DRAW) {
                    player_ctx.result = "draw";
                } else if (result == GameResult::TIMEOUT) {
                    // 超时：输的一方
                    player_ctx.result = (player_id == winner_id) ? "win" : "timeout";
                } else if (result == GameResult::SURRENDER) {
                    // 投降：输的一方
                    player_ctx.result = (player_id == winner_id) ? "win" : "surrender";
                } else if ((result == GameResult::BLACK_WIN && piece == PieceType::BLACK) ||
                           (result == GameResult::WHITE_WIN && piece == PieceType::WHITE)) {
                    player_ctx.result = "win";
                } else {
                    player_ctx.result = "loss";
                }

                // 默认值（将在 GomokuServer 中通过 Game Data Service API 获取真实数据）
                player_ctx.rating_before = 1200;
                player_ctx.games_played = 0;
                player_ctx.win_streak = 0;
                player_ctx.tier_level = 5;
                player_ctx.is_first_win_today = false;
                player_ctx.games_today = 0;

                context.players.push_back(player_ctx);
            }

            // 元数据
            context.metadata = {
                {"total_moves", total_moves_},
                {"win_type", "five_in_row"},
                {"game_mode", gameModeToString(gomoku_config_.gameMode)},
                {"time_limit", gomoku_config_.timeLimit}
            };

            return context;
        }

    } // namespace gomoku
} // namespace game_services
