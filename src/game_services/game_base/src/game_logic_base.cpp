#include "game_logic_base.h"
#include "common/logger/logger.h"
#include <algorithm>

namespace game_services {
    namespace game_base {

        GameLogicBase::GameLogicBase(GameType game_type)
            : game_type_(game_type)
            , game_state_(RoomState::WAITING)
            , start_time_(std::chrono::system_clock::now())
            , last_tick_time_(std::chrono::system_clock::now())
            , running_(false) {

            LOG_INFO("GameLogic created for game type: " + std::to_string(static_cast<int>(game_type_)));
        }

        GameLogicBase::~GameLogicBase() {
            LOG_INFO("GameLogic destroyed");
        }

        bool GameLogicBase::initializeGame(const std::vector<std::string>& player_ids,
                                          const nlohmann::json& config) {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            if (game_state_ != RoomState::WAITING) {
                LOG_WARNING("Cannot initialize game, current state: " + std::to_string(static_cast<int>(game_state_)));
                return false;
            }

            player_ids_ = player_ids;
            game_config_ = config;
            tick_count_.store(0);
            
            // 初始化游戏特定逻辑
            if (!initializeGameSpecific(player_ids, config)) {
                LOG_ERROR("Failed to initialize game specific logic");
                return false;
            }

            LOG_INFO("Game initialized with " + std::to_string(player_ids.size()) + " players");
            return true;
        }

        bool GameLogicBase::startGame() {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            if (game_state_ != RoomState::WAITING) {
                LOG_WARNING("Cannot start game, current state: " + std::to_string(static_cast<int>(game_state_)));
                return false;
            }

            if (player_ids_.empty()) {
                LOG_WARNING("Cannot start game with no players");
                return false;
            }

            changeGameState(RoomState::PLAYING);
            start_time_ = std::chrono::system_clock::now();
            last_tick_time_ = start_time_;
            tick_count_.store(0);
            
            // 🔧 新增：启动游戏主循环定时器
            if (!startGameLoop()) {
                LOG_ERROR("Failed to start game loop, reverting to waiting state");
                changeGameState(RoomState::WAITING);
                return false;
            }
            
            LOG_INFO("Game started with main loop");
            return true;
        }

        bool GameLogicBase::pauseGame() {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            if (game_state_ != RoomState::PLAYING) {
                return false;
            }

            pauseGameSpecific();
            changeGameState(RoomState::PAUSED);
            
            LOG_INFO("Game paused");
            return true;
        }

        bool GameLogicBase::resumeGame() {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            if (game_state_ != RoomState::PAUSED) {
                return false;
            }

            resumeGameSpecific();
            changeGameState(RoomState::PLAYING);
            last_tick_time_ = std::chrono::system_clock::now();
            
            LOG_INFO("Game resumed");
            return true;
        }

        void GameLogicBase::stopGame() {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            if (game_state_ == RoomState::FINISHED) {
                return;
            }

            // 🔧 新增：停止游戏主循环定时器
            stopGameLoop();
            
            changeGameState(RoomState::FINISHED);
            LOG_INFO("Game stopped");
        }

        void GameLogicBase::resetGame() {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            resetGameSpecific();
            changeGameState(RoomState::WAITING);
            tick_count_.store(0);
            player_ids_.clear();
            game_config_ = nlohmann::json{};
            
            LOG_INFO("Game reset");
        }

        void GameLogicBase::tick() {
            std::shared_lock<std::shared_mutex> lock(logic_mutex_);

            if (game_state_ != RoomState::PLAYING) {
                return;
            }

            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_time_);

            // 检查是否到了下一个tick时间
            if (elapsed.count() < tick_interval_ms_) {
                return;
            }

            last_tick_time_ = now;
            tick_count_++;

            // 执行游戏特定的tick逻辑
            try {
                tickGameSpecific();

                // 检查游戏是否结束
                if (checkGameEndSpecific()) {
                    // 游戏结束，需要释放锁后调用stopGame
                    lock.unlock();
                    stopGame();
                    return;
                }

                // [FIX] 移除自动广播状态，改为由具体游戏逻辑在状态变化时手动调用
                // 对于回合制游戏（如五子棋），不需要每 100ms 都广播状态
                // notifyStateUpdate();

            } catch (const std::exception& e) {
                LOG_ERROR("Game tick error: " + std::string(e.what()));
                notifyError("Game tick error: " + std::string(e.what()));
            }
        }

        bool GameLogicBase::processPlayerAction(const std::string& player_id, const nlohmann::json& action) {
            std::shared_lock<std::shared_mutex> lock(logic_mutex_);
            
            if (game_state_ != RoomState::PLAYING) {
                LOG_WARNING("Cannot process action, game not playing");
                return false;
            }

            // 检查玩家是否在游戏中
            auto it = std::find(player_ids_.begin(), player_ids_.end(), player_id);
            if (it == player_ids_.end()) {
                LOG_WARNING("Player " + player_id + " not in game");
                return false;
            }

            try {
                bool result = processPlayerActionSpecific(player_id, action);
                
                if (result) {
                    // 通知玩家操作
                    notifyPlayerAction(player_id, action);
                    
                    // 检查游戏是否结束
                    if (checkGameEndSpecific()) {
                        // 游戏结束，需要释放锁后调用stopGame
                        lock.unlock();
                        stopGame();
                    }
                }
                
                return result;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Process player action error: " + std::string(e.what()));
                notifyError("Process action error: " + std::string(e.what()));
                return false;
            }
        }

        bool GameLogicBase::addPlayer(const std::string& player_id) {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            // 检查玩家是否已存在
            auto it = std::find(player_ids_.begin(), player_ids_.end(), player_id);
            if (it != player_ids_.end()) {
                LOG_WARNING("Player " + player_id + " already in game");
                return false;
            }

            player_ids_.push_back(player_id);
            LOG_INFO("Player " + player_id + " added to game");
            return true;
        }

        bool GameLogicBase::removePlayer(const std::string& player_id) {
            std::unique_lock<std::shared_mutex> lock(logic_mutex_);
            
            auto it = std::find(player_ids_.begin(), player_ids_.end(), player_id);
            if (it == player_ids_.end()) {
                LOG_WARNING("Player " + player_id + " not in game");
                return false;
            }

            player_ids_.erase(it);
            LOG_INFO("Player " + player_id + " removed from game");
            
            // 如果没有玩家了，停止游戏（记录详细原因，便于排查“未撞墙却结束”的问题）
            if (player_ids_.empty() && game_state_ == RoomState::PLAYING) {
                LOG_WARNING("Finishing game because no players remain while PLAYING (trigger=removePlayer, last_removed=" + player_id + ")");
                changeGameState(RoomState::FINISHED);
            }

            return true;
        }

        int64_t GameLogicBase::getGameDurationMs() const {
            auto now = std::chrono::system_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
        }

        nlohmann::json GameLogicBase::getFullGameState() const {
            std::shared_lock<std::shared_mutex> lock(logic_mutex_);
            
            auto game_specific_state = getGameSpecificState();
            
            nlohmann::json full_state = {
                {"game_type", static_cast<int>(game_type_)},
                {"game_state", static_cast<int>(game_state_)},
                {"player_ids", player_ids_},
                {"tick_count", tick_count_.load()},
                {"duration_ms", getGameDurationMs()},
                {"tick_interval_ms", tick_interval_ms_},
                {"config", game_config_},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };
            
            // 合并游戏特定状态
            if (game_specific_state.is_object()) {
                for (auto& [key, value] : game_specific_state.items()) {
                    full_state[key] = value;
                }
            } else {
                full_state["game_data"] = game_specific_state;
            }
            
            return full_state;
        }

        void GameLogicBase::notifyStateUpdate() {
            if (state_update_callback_) {
                try {
                    auto state = getFullGameState();
                    state_update_callback_(state);
                } catch (const std::exception& e) {
                    LOG_ERROR("State update callback error: " + std::string(e.what()));
                }
            }
        }

        void GameLogicBase::notifyGameEnd(const std::vector<std::string>& winners) {
            if (game_end_callback_) {
                try {
                    game_end_callback_(winners);
                } catch (const std::exception& e) {
                    LOG_ERROR("Game end callback error: " + std::string(e.what()));
                }
            }
        }

        void GameLogicBase::notifyPlayerAction(const std::string& player_id, const nlohmann::json& action) {
            if (player_action_callback_) {
                try {
                    player_action_callback_(player_id, action);
                } catch (const std::exception& e) {
                    LOG_ERROR("Player action callback error: " + std::string(e.what()));
                }
            }
        }

        void GameLogicBase::notifyError(const std::string& error_message) {
            if (error_callback_) {
                try {
                    error_callback_(error_message);
                } catch (const std::exception& e) {
                    LOG_ERROR("Error callback error: " + std::string(e.what()));
                }
            }
        }

        void GameLogicBase::changeGameState(RoomState new_state) {
            RoomState old_state = game_state_;
            game_state_ = new_state;

            LOG_INFO("Game state changed from " + std::to_string(static_cast<int>(old_state)) +
                    " to " + std::to_string(static_cast<int>(new_state)));

            // 🔧 修复：移除自动的游戏结束通知，避免与子类的endGame冲突
            // 子类应该在适当的时候调用notifyGameEnd，而不是在这里自动调用
            // if (new_state == RoomState::FINISHED && old_state != RoomState::FINISHED) {
            //     // 这里可以确定获胜者，但由于是基类，留给子类实现
            //     notifyGameEnd({});
            // }
        }

        void GameLogicBase::forceStop() {
            LOG_INFO("Force stopping game logic");

            try {
                std::unique_lock<std::shared_mutex> lock(logic_mutex_);

                // 立即改变状态为结束
                changeGameState(RoomState::FINISHED);

                // 停止游戏循环
                running_.store(false);

                // 清理游戏特定资源（由子类实现）
                onForceStop();

                LOG_INFO("Game logic force stopped successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Error force stopping game logic: " + std::string(e.what()));
            }
        }

        void GameLogicBase::cleanup() {
            LOG_INFO("Cleaning up game logic");

            try {
                std::unique_lock<std::shared_mutex> lock(logic_mutex_);

                // 停止运行
                running_.store(false);

                // 清理玩家列表
                player_ids_.clear();

                // 重置状态
                game_state_ = RoomState::WAITING;
                tick_count_.store(0);

                // 清理配置
                game_config_.clear();

                // 重置回调函数
                state_update_callback_ = nullptr;
                game_end_callback_ = nullptr;
                player_action_callback_ = nullptr;
                error_callback_ = nullptr;

                // 清理游戏特定资源（由子类实现）
                onCleanup();

                LOG_INFO("Game logic cleaned up successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Error cleaning up game logic: " + std::string(e.what()));
            }
        }

        // 🔧 新增：游戏主循环定时器实现
        bool GameLogicBase::startGameLoop() {
            auto event_loop_shared = event_loop_.lock();
            if (!event_loop_shared) {
                LOG_ERROR("EventLoop已失效，无法启动游戏主循环");
                return false;
            }

            if (game_loop_timer_id_ != 0) {
                LOG_WARNING("游戏主循环已在运行，跳过启动");
                return true;
            }

            // 启动游戏主循环定时器
            game_loop_timer_id_ = event_loop_shared->runEvery(
                tick_interval_ms_,
                [this]() {
                    if (game_loop_running_.load()) {
                        try {
                            this->tick();  // 调用游戏逻辑的tick方法
                        } catch (const std::exception& e) {
                            LOG_ERROR("游戏主循环异常: " + std::string(e.what()));
                        } catch (...) {
                            LOG_ERROR("游戏主循环发生未知异常");
                        }
                    }
                }
            );

            if (game_loop_timer_id_ == 0) {
                LOG_ERROR("启动游戏主循环定时器失败");
                return false;
            }

            game_loop_running_.store(true);
            LOG_INFO("游戏主循环定时器启动成功，间隔: " + std::to_string(tick_interval_ms_) + "ms, ID: " + std::to_string(game_loop_timer_id_));
            return true;
        }

        void GameLogicBase::stopGameLoop() {
            if (game_loop_timer_id_ == 0) {
                return; // 已经停止
            }

            game_loop_running_.store(false);

            auto event_loop_shared = event_loop_.lock();
            if (event_loop_shared) {
                event_loop_shared->cancelTimer(game_loop_timer_id_);
                LOG_INFO("游戏主循环定时器已停止，ID: " + std::to_string(game_loop_timer_id_));
            } else {
                LOG_WARNING("EventLoop已失效，无法正常取消游戏主循环定时器");
            }

            game_loop_timer_id_ = 0;
        }

    } // namespace game_base
} // namespace game_services
