#pragma once

#include "game_logic_base.h"
#include "gomoku_types.h"
#include "game_mode_strategy.h"
#include <unordered_set>
#include <chrono>
#include <functional>
#include <thread>

/**
 * @file gomoku_logic.h
 * @brief 五子棋游戏逻辑类定义
 * @details 实现五子棋的游戏规则、胜负判断、禁手检测等核心逻辑
 *          使用策略模式支持不同游戏规则变体
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 五子棋游戏逻辑类
         * @details 继承自GameLogicBase，实现五子棋特定的游戏逻辑
         */
        class GomokuLogic : public game_base::GameLogicBase {
        public:
            // 回调函数类型
            using TimeUpdateCallback = std::function<void(PieceType, int)>;  // 时间更新回调
            using MoveValidationCallback = std::function<bool(const Move&)>; // 移动验证回调

            /**
             * @brief 构造函数
             * @param config 五子棋游戏配置
             * @param event_loop EventLoop实例（用于时间轮定时器）
             */
            explicit GomokuLogic(const GomokuConfig& config = GomokuConfig(),
                                std::shared_ptr<common::network::EventLoop> event_loop = nullptr);

            /**
             * @brief 虚析构函数
             */
            virtual ~GomokuLogic();

            // 继承自GameLogicBase的纯虚函数实现
            bool initializeGameSpecific(const std::vector<std::string>& player_ids,
                                       const nlohmann::json& config = {}) override;
            void tickGameSpecific() override;
            bool processPlayerActionSpecific(const std::string& player_id,
                                           const nlohmann::json& action) override;
            bool checkGameEndSpecific() override;
            nlohmann::json getGameSpecificState() const override;
            void pauseGameSpecific() override;
            void resumeGameSpecific() override;
            void resetGameSpecific() override;

            // 五子棋特定功能
            /**
             * @brief 尝试在指定位置落子
             * @param player_id 玩家ID
             * @param position 落子位置
             * @return 是否成功落子
             */
            bool placePiece(const std::string& player_id, const Position& position);

            /**
             * @brief 悔棋（如果允许）
             * @param player_id 请求悔棋的玩家ID
             * @return 是否成功悔棋
             */
            bool undoMove(const std::string& player_id);

            /**
             * @brief 投降
             * @param player_id 投降的玩家ID
             * @return 是否成功投降
             */
            bool surrender(const std::string& player_id);

            /**
             * @brief 提议和棋
             * @param player_id 提议和棋的玩家ID
             * @return 是否成功提议
             */
            bool offerDraw(const std::string& player_id);

            /**
             * @brief 回应和棋提议
             * @param player_id 回应的玩家ID
             * @param accept 是否接受和棋
             * @return 是否成功回应
             */
            bool respondToDraw(const std::string& player_id, bool accept);

            /**
             * @brief 检查指定位置是否可以落子
             * @param position 位置
             * @param piece 棋子类型
             * @return 是否可以落子
             */
            bool isValidMove(const Position& position, PieceType piece) const;

            /**
             * @brief 检查指定位置是否为禁手（连珠规则）
             * @param position 位置
             * @param piece 棋子类型（只有黑子会有禁手）
             * @return 禁手类型
             */
            ForbiddenType checkForbidden(const Position& position, PieceType piece) const;

            /**
             * @brief 检查获胜条件
             * @param position 最后落子位置
             * @param piece 棋子类型
             * @return 是否获胜
             */
            bool checkWin(const Position& position, PieceType piece) const;

            /**
             * @brief 获取当前游戏状态
             */
            const GomokuGameState& getGameState() const { return game_state_; }

            /**
             * @brief 获取游戏配置
             */
            const GomokuConfig& getConfig() const { return config_; }

            /**
             * @brief 更新游戏配置
             * @param config 新配置
             */
            void updateConfig(const GomokuConfig& config);

            /**
             * @brief 获取移动历史
             */
            const std::vector<Move>& getMoveHistory() const { return game_state_.moveHistory; }

            /**
             * @brief 获取当前玩家
             */
            PieceType getCurrentPlayer() const { return game_state_.currentPlayer; }

            /**
             * @brief 获取棋盘状态
             */
            const Board& getBoard() const { return game_state_.board; }

            /**
             * @brief 获取剩余时间
             * @param piece 棋子类型
             * @return 剩余时间（秒）
             */
            int getTimeLeft(PieceType piece) const;

            /**
             * @brief 启动时间控制（游戏开始时调用）
             * @details 启动定时器，每秒更新时间并广播 time_update 消息
             */
            void startTimeControl();

            /**
             * @brief 停止时间控制（游戏结束时调用）
             */
            void stopTimeControl();

            /**
             * @brief 设置时间更新回调
             */
            void setTimeUpdateCallback(TimeUpdateCallback callback) { 
                time_update_callback_ = callback; 
            }

            /**
             * @brief 设置移动验证回调
             */
            void setMoveValidationCallback(MoveValidationCallback callback) { 
                move_validation_callback_ = callback; 
            }

            /**
             * @brief 设置EventLoop（用于时间轮定时器）
             * @param event_loop EventLoop实例
             */
            void setEventLoop(std::shared_ptr<common::network::EventLoop> event_loop) {
                event_loop_ = event_loop;
                // 设置到基类用于游戏主循环
                GameLogicBase::setEventLoop(event_loop);
            }

            /**
             * @brief 获取推荐落子位置（AI辅助）
             * @param piece 棋子类型
             * @param count 推荐数量
             * @return 推荐位置列表
             */
            std::vector<Position> getRecommendedMoves(PieceType piece, int count = 5) const;

            /**
             * @brief 分析当前局面
             * @return 局面分析结果
             */
            nlohmann::json analyzePosition() const;

        private:
            GomokuConfig config_;               // 游戏配置
            GomokuGameState game_state_;        // 游戏状态
            std::unordered_map<std::string, PieceType> player_pieces_; // 玩家对应的棋子

            // 游戏模式策略（P2 重构）
            std::unique_ptr<IGameModeStrategy> game_mode_strategy_;

            // 时间控制（优化为时间轮）
            std::chrono::system_clock::time_point last_move_time_;
            std::atomic<bool> time_counting_{false};
            std::weak_ptr<common::network::EventLoop> event_loop_;  // 使用弱引用避免循环依赖
            uint64_t time_control_timer_id_ = 0;  // 时间轮定时器ID
            int time_update_interval_ms_ = 1000;  // 时间更新间隔（毫秒）

            // 和棋相关
            std::string draw_offer_player_;     // 提议和棋的玩家
            bool draw_offered_ = false;         // 是否有和棋提议

            // 回调函数
            TimeUpdateCallback time_update_callback_;
            MoveValidationCallback move_validation_callback_;

            /**
             * @brief 初始化棋子分配
             */
            void initializePieces();

            /**
             * @brief 切换当前玩家
             */
            void switchCurrentPlayer();

            /**
             * @brief 添加移动到历史记录
             */
            void addMoveToHistory(const Move& move);

            /**
             * @brief 更新时间
             */
            void updateTime();

            /**
             * @brief 开始计时（使用时间轮）
             */
            void startTimer();

            /**
             * @brief 停止计时（取消时间轮定时器）
             */
            void stopTimer();

            /**
             * @brief 时间轮回调函数
             */
            void timerCallback();

            /**
             * @brief 处理超时
             */
            void handleTimeout(PieceType player);

            // 胜负判断相关方法
            /**
             * @brief 检查指定方向的连子数
             * @param position 起始位置
             * @param piece 棋子类型
             * @param dx 行方向增量
             * @param dy 列方向增量
             * @return 连子数
             */
            int countDirection(const Position& position, PieceType piece, int dx, int dy) const;

            /**
             * @brief 检查指定位置的连子情况
             * @param position 位置
             * @param piece 棋子类型
             * @return 四个方向的连子数 [水平, 垂直, 主对角线, 副对角线]
             */
            std::array<int, 4> countAllDirections(const Position& position, PieceType piece) const;

            // 禁手检测相关方法（连珠规则）- 已迁移到策略模式
            // checkThreeThreeForbidden, checkFourFourForbidden, checkOverlineForbidden
            // isLiveThree, isFour 现在由 IGameModeStrategy 处理

            /**
             * @brief 更新禁手位置列表
             */
            void updateForbiddenPositions();

            // AI相关方法
            /**
             * @brief 评估位置价值
             * @param position 位置
             * @param piece 棋子类型
             * @return 位置评分
             */
            int evaluatePosition(const Position& position, PieceType piece) const;

            /**
             * @brief 查找威胁位置
             * @param piece 棋子类型
             * @return 威胁位置列表
             */
            std::vector<Position> findThreats(PieceType piece) const;

            /**
             * @brief 查找攻击位置
             * @param piece 棋子类型
             * @return 攻击位置列表
             */
            std::vector<Position> findAttacks(PieceType piece) const;

            /**
             * @brief 获取邻近位置
             * @param position 中心位置
             * @param radius 半径
             * @return 邻近位置列表
             */
            std::vector<Position> getNearbyPositions(const Position& position, int radius = 2) const;

            // 状态管理
            /**
             * @brief 重置游戏状态
             */
            void resetState();

            /**
             * @brief 结束游戏
             */
            void endGame(GameResult result);

            /**
             * @brief 通知游戏状态更新
             */
            void notifyGameStateUpdate();

            /**
             * @brief 通知时间更新
             */
            void notifyTimeUpdate(PieceType player, int timeLeft);

            /**
             * @brief 验证玩家ID
             */
            bool validatePlayerId(const std::string& player_id) const;

            /**
             * @brief 获取玩家对应的棋子类型
             */
            PieceType getPlayerPiece(const std::string& player_id) const;

            /**
             * @brief 是否为当前玩家回合
             */
            bool isPlayerTurn(const std::string& player_id) const;

        public:
            // 静态工具方法
            /**
             * @brief 从JSON创建五子棋逻辑实例
             * @param json JSON配置
             * @param event_loop EventLoop实例（用于时间轮定时器）
             */
            static std::unique_ptr<GomokuLogic> fromJson(const nlohmann::json& json,
                                                        std::shared_ptr<common::network::EventLoop> event_loop = nullptr);

            /**
             * @brief 获取默认配置
             */
            static GomokuConfig getDefaultConfig();

            /**
             * @brief 验证游戏配置
             */
            static bool validateConfig(const GomokuConfig& config);
        };

    } // namespace gomoku
} // namespace game_services


