#include "game_room_base.h"
#include "common/logger/logger.h"
#include <algorithm>

namespace game_services {
    namespace game_base {

        GameRoomBase::GameRoomBase(const std::string& room_id, 
                                  const std::string& creator_id,
                                  GameType game_type,
                                  int max_players)
            : room_id_(room_id)
            , creator_id_(creator_id)
            , game_type_(game_type)
            , state_(RoomState::WAITING)
            , max_players_(max_players)
            , created_at_(std::chrono::system_clock::now())
            , last_activity_(std::chrono::system_clock::now()) {
            
            LOG_INFO("GameRoom created: " + room_id_ + " by " + creator_id_);
        }

        GameRoomBase::~GameRoomBase() {
            try {
                LOG_INFO("GameRoom destroying: " + room_id_);

                // 1. 首先停止所有活动
                if (state_ == RoomState::PLAYING || state_ == RoomState::PAUSED) {
                    LOG_INFO("Force stopping active game in room " + room_id_ + " during destruction");
                    forceStop("Room destruction");
                }

                // 2. 清理所有资源
                cleanup();

                LOG_INFO("GameRoom destroyed: " + room_id_);

            } catch (const std::exception& e) {
                // 析构函数中不应该抛出异常
                LOG_ERROR("Error in GameRoomBase destructor for room " + room_id_ + ": " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown error in GameRoomBase destructor for room " + room_id_);
            }
        }

        bool GameRoomBase::addPlayer(const std::string& player_id, std::shared_ptr<PlayerSessionBase> session) {
            std::unique_lock<std::shared_mutex> lock(players_mutex_);
            
            // 检查玩家是否已在房间中
            if (players_.find(player_id) != players_.end()) {
                LOG_WARNING("Player " + player_id + " already in room " + room_id_);
                return false;
            }

            // 检查房间是否已满
            if (players_.size() >= static_cast<size_t>(max_players_)) {
                LOG_WARNING("Room " + room_id_ + " is full");
                return false;
            }

            // 添加玩家
            players_[player_id] = session;
            session->setRoomId(room_id_);
            session->setPlayerState(PlayerState::IN_ROOM);
            
            updateLastActivity();
            
            LOG_INFO("Player " + player_id + " joined room " + room_id_);

            // 获取当前玩家数量（在锁内获取，避免重复加锁）
            int current_player_count = static_cast<int>(players_.size());

            // 释放锁后再调用回调，避免死锁
            lock.unlock();

            // 通知玩家加入
            onPlayerJoined(player_id, current_player_count);

            if (player_join_callback_) {
                player_join_callback_(player_id);
            }

            return true;
        }

        bool GameRoomBase::removePlayer(const std::string& player_id) {
            std::unique_lock<std::shared_mutex> lock(players_mutex_);

            auto it = players_.find(player_id);
            if (it == players_.end()) {
                return false;
            }

            // 更新玩家状态（在锁内）
            if (it->second) {
                it->second->setRoomId("");
                it->second->setPlayerState(PlayerState::CONNECTED);
            }

            // 移除玩家（在锁内）
            players_.erase(it);
            updateLastActivity();

            LOG_INFO("Player " + player_id + " left room " + room_id_);

            // 在锁内计算结束条件，避免回调期间访问 players_
            bool should_end_game = players_.empty() && state_ != RoomState::WAITING;

            // 释放锁后再进行回调/状态改变，避免潜在死锁
            lock.unlock();

            // 通知玩家离开（锁外）
            onPlayerLeft(player_id);

            if (player_leave_callback_) {
                player_leave_callback_(player_id);
            }

            // 锁外结束游戏，避免锁顺序反转
            if (should_end_game) {
                endGame();
            }

            return true;
        }

        bool GameRoomBase::isPlayerInRoom(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            return players_.find(player_id) != players_.end();
        }

        bool GameRoomBase::startGame() {
            std::unique_lock<std::shared_mutex> lock(room_mutex_);
            
            if (state_ != RoomState::WAITING) {
                LOG_WARNING("Cannot start game in room " + room_id_ + ", current state: " + std::to_string(static_cast<int>(state_)));
                return false;
            }

            if (players_.empty()) {
                LOG_WARNING("Cannot start game in empty room " + room_id_);
                return false;
            }

            // 启动游戏特定逻辑
            if (!startGameSpecific()) {
                LOG_ERROR("Failed to start game specific logic in room " + room_id_);
                return false;
            }

            // 更新房间状态
            changeState(RoomState::PLAYING);
            
            // 更新所有玩家状态
            {
                std::shared_lock<std::shared_mutex> players_lock(players_mutex_);
                for (auto& [player_id, session] : players_) {
                    session->setPlayerState(PlayerState::PLAYING);
                }
            }

            updateLastActivity();
            LOG_INFO("Game started in room " + room_id_);
            
            return true;
        }

        bool GameRoomBase::pauseGame() {
            std::unique_lock<std::shared_mutex> lock(room_mutex_);
            
            if (state_ != RoomState::PLAYING) {
                return false;
            }

            pauseGameSpecific();
            changeState(RoomState::PAUSED);
            updateLastActivity();
            
            LOG_INFO("Game paused in room " + room_id_);
            return true;
        }

        bool GameRoomBase::resumeGame() {
            std::unique_lock<std::shared_mutex> lock(room_mutex_);
            
            if (state_ != RoomState::PAUSED) {
                return false;
            }

            resumeGameSpecific();
            changeState(RoomState::PLAYING);
            updateLastActivity();
            
            LOG_INFO("Game resumed in room " + room_id_);
            return true;
        }

        void GameRoomBase::endGame(const std::vector<std::string>& winners) {
            std::unique_lock<std::shared_mutex> lock(room_mutex_);
            
            if (state_ == RoomState::FINISHED) {
                return;
            }

            endGameSpecific();
            changeState(RoomState::FINISHED);
            
            // 更新所有玩家状态
            {
                std::shared_lock<std::shared_mutex> players_lock(players_mutex_);
                for (auto& [player_id, session] : players_) {
                    session->setPlayerState(PlayerState::IN_ROOM);
                }
            }

            updateLastActivity();
            
            if (game_end_callback_) {
                game_end_callback_(winners);
            }
            
            LOG_INFO("Game ended in room " + room_id_);
        }

        bool GameRoomBase::processGameAction(const std::string& player_id, const nlohmann::json& action) {
            // 检查玩家是否在房间中
            if (!isPlayerInRoom(player_id)) {
                LOG_WARNING("Player " + player_id + " not in room " + room_id_);
                return false;
            }

            // 检查游戏状态
            if (state_ != RoomState::PLAYING) {
                LOG_WARNING("Cannot process action in room " + room_id_ + ", game not playing");
                return false;
            }

            updateLastActivity();
            
            // 处理游戏特定操作
            bool result = processGameActionSpecific(player_id, action);
            
            if (result) {
                // 通知游戏状态变化
                onGameStateChanged();
            }

            return result;
        }

        int GameRoomBase::getPlayerCount() const {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            return static_cast<int>(players_.size());
        }

        std::vector<std::string> GameRoomBase::getPlayerIds() const {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            std::vector<std::string> player_ids;
            player_ids.reserve(players_.size());
            
            for (const auto& [player_id, session] : players_) {
                player_ids.push_back(player_id);
            }
            
            return player_ids;
        }

        nlohmann::json GameRoomBase::getRoomInfo() const {
            std::shared_lock<std::shared_mutex> room_lock(room_mutex_);
            std::shared_lock<std::shared_mutex> players_lock(players_mutex_);

            // [DEBUG] 记录 players_ 映射的大小
            LOG_INFO("[DEBUG] GameRoomBase::getRoomInfo - players_ map size: " +
                    std::to_string(players_.size()) + ", room_id: " + room_id_);

            nlohmann::json player_list = nlohmann::json::array();
            for (const auto& [player_id, session] : players_) {
                player_list.push_back({
                    {"playerId", player_id},  // camelCase
                    {"state", static_cast<int>(session->getPlayerState())},
                    {"isQt6", session->isQt6Client()}  // camelCase
                });
                LOG_INFO("[DEBUG] Added player to list: " + player_id);
            }

            return nlohmann::json{
                {"roomId", room_id_},  // camelCase
                {"creatorId", creator_id_},  // camelCase
                {"gameType", static_cast<int>(game_type_)},  // camelCase
                {"roomState", static_cast<int>(state_)},  // camelCase
                {"currentPlayers", static_cast<int>(players_.size())},  // camelCase
                {"maxPlayers", max_players_},  // camelCase
                {"players", player_list},
                {"createdAt", std::chrono::duration_cast<std::chrono::milliseconds>(  // camelCase
                    created_at_.time_since_epoch()).count()},
                {"lastActivity", std::chrono::duration_cast<std::chrono::milliseconds>(  // camelCase
                    last_activity_.time_since_epoch()).count()}
            };
        }

        bool GameRoomBase::canPlayerStartGame(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);

            // 只有房间创建者可以开始游戏
            if (player_id != creator_id_) {
                return false;
            }

            // 房间必须处于等待状态
            if (state_ != RoomState::WAITING) {
                return false;
            }

            // 至少需要一个玩家
            if (players_.empty()) {
                return false;
            }

            return true;
        }

        nlohmann::json GameRoomBase::getFullGameState() const {
            auto room_info = getRoomInfo();
            auto game_state = getGameSpecificState();
            
            room_info["game_state"] = game_state;
            return room_info;
        }

        void GameRoomBase::broadcastToPlayers(const nlohmann::json& message) {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            
            for (const auto& [player_id, session] : players_) {
                if (session && session->isConnectionValid()) {
                    session->sendMessage(message);
                }
            }
        }

        void GameRoomBase::sendToPlayer(const std::string& player_id, const nlohmann::json& message) {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            
            auto it = players_.find(player_id);
            if (it != players_.end() && it->second && it->second->isConnectionValid()) {
                it->second->sendMessage(message);
            }
        }

        void GameRoomBase::broadcastToOthers(const std::string& exclude_player_id, const nlohmann::json& message) {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            
            for (const auto& [player_id, session] : players_) {
                if (player_id != exclude_player_id && session && session->isConnectionValid()) {
                    session->sendMessage(message);
                }
            }
        }

        void GameRoomBase::updateLastActivity() {
            last_activity_ = std::chrono::system_clock::now();
        }

        bool GameRoomBase::isExpired(int timeout_ms) const {
            auto now = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity_);
            return duration.count() > timeout_ms;
        }

        void GameRoomBase::changeState(RoomState new_state) {
            RoomState old_state = state_;
            state_ = new_state;
            
            if (state_change_callback_) {
                state_change_callback_(old_state, new_state);
            }
            
            // 广播状态变化
            nlohmann::json state_message = {
                {"type", "room_state_change"},
                {"old_state", static_cast<int>(old_state)},
                {"new_state", static_cast<int>(new_state)},
                {"room_id", room_id_}
            };
            
            broadcastToPlayers(state_message);
        }

        void GameRoomBase::onPlayerJoined(const std::string& player_id, int current_player_count) {
            // 向其他玩家广播新玩家加入（符合文档格式 §7.4.4）
            int count = current_player_count > 0 ? current_player_count : getPlayerCount();

            nlohmann::json join_message = {
                {"type", "player_joined"},
                {"data", {
                    {"playerId", player_id},
                    {"roomId", room_id_},
                    {"playerCount", count}
                    // 注意：pieceType 和 playerInfo 由子类添加
                }}
            };

            broadcastToOthers(player_id, join_message);

            // 向新玩家发送房间信息
            nlohmann::json room_info_message = {
                {"type", "room_info"},
                {"data", getRoomInfo()}
            };

            sendToPlayer(player_id, room_info_message);
        }

        void GameRoomBase::onPlayerLeft(const std::string& player_id) {
            // 向其他玩家广播玩家离开（符合文档格式）
            nlohmann::json leave_message = {
                {"type", "player_left"},
                {"data", {
                    {"playerId", player_id},
                    {"roomId", room_id_},
                    {"playerCount", getPlayerCount()}
                }}
            };

            broadcastToPlayers(leave_message);
        }

        void GameRoomBase::onGameStateChanged() {
            // 广播游戏状态更新
            nlohmann::json state_message = {
                {"type", "game_state_update"},
                {"data", getGameSpecificState()},
                {"room_id", room_id_}
            };

            broadcastToPlayers(state_message);
        }

        void GameRoomBase::forceStop(const std::string& reason) {
            LOG_INFO("Force stopping room " + room_id_ + ": " + reason);

            try {
                // 改变房间状态为结束
                changeState(RoomState::FINISHED);

                // 停止游戏逻辑
                if (game_logic_) {
                    game_logic_->forceStop();
                }

                // 通知所有玩家游戏强制结束
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                for (const auto& [player_id, session] : players_) {
                    if (session) {
                        session->sendSystemMessage("game_force_stopped", {
                            {"reason", reason},
                            {"room_id", room_id_}
                        });
                    }
                }

                LOG_INFO("Room " + room_id_ + " force stopped successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Error force stopping room " + room_id_ + ": " + std::string(e.what()));
            }
        }

        void GameRoomBase::cleanup() {
            LOG_INFO("Cleaning up room " + room_id_);

            try {
                // 1. 首先设置房间状态为FINISHED，防止新的操作
                {
                    std::unique_lock<std::shared_mutex> lock(room_mutex_);
                    if (state_ != RoomState::FINISHED) {
                        state_ = RoomState::FINISHED;
                    }
                }

                // 2. 清理游戏逻辑
                if (game_logic_) {
                    try {
                        game_logic_->cleanup();
                    } catch (const std::exception& e) {
                        LOG_ERROR("Error cleaning up game logic in room " + room_id_ + ": " + std::string(e.what()));
                    }
                    game_logic_.reset();
                }

                // 3. 清理所有玩家会话
                {
                    std::unique_lock<std::shared_mutex> lock(players_mutex_);
                    for (auto& [player_id, session] : players_) {
                        if (session) {
                            try {
                                // 通知玩家房间即将销毁
                                session->sendSystemMessage("room_destroyed", {
                                    {"room_id", room_id_},
                                    {"reason", "Room cleanup"}
                                });

                                // 重置玩家状态
                                session->setRoomId("");
                                session->setPlayerState(PlayerState::CONNECTED);

                                LOG_DEBUG("Cleaned up player session: " + player_id);
                            } catch (const std::exception& e) {
                                LOG_ERROR("Error cleaning up player " + player_id + " in room " + room_id_ + ": " + std::string(e.what()));
                            }
                        }
                    }
                    players_.clear();
                }

                // 4. 重置回调函数（防止悬空指针）
                state_change_callback_ = nullptr;
                player_join_callback_ = nullptr;
                player_leave_callback_ = nullptr;
                game_end_callback_ = nullptr;
                message_callback_ = nullptr;

                LOG_INFO("Room " + room_id_ + " cleaned up successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Error cleaning up room " + room_id_ + ": " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown error cleaning up room " + room_id_);
            }
        }

    } // namespace game_base
} // namespace game_services
