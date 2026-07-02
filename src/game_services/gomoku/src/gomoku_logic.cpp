#include "gomoku_logic.h"
#include "common/logger/logger.h"
#include <algorithm>
#include <random>
#include <set>

namespace game_services {
    namespace gomoku {

        GomokuLogic::GomokuLogic(const GomokuConfig& config,
                                        std::shared_ptr<common::network::EventLoop> event_loop)
            : GameLogicBase(game_base::GameType::CHESS)
            , config_(config)
            , game_mode_strategy_(GameModeStrategyFactory::create(config.gameMode))
            , event_loop_(event_loop) {

            // 设置EventLoop到基类用于游戏主循环
            if (event_loop) {
                GameLogicBase::setEventLoop(event_loop);
                LOG_DEBUG("GomokuLogic已设置EventLoop用于游戏主循环");
            }

            resetState();
        }

        GomokuLogic::~GomokuLogic() {
            stopTimer();
        }

        bool GomokuLogic::initializeGameSpecific(const std::vector<std::string>& player_ids,
                                                const nlohmann::json& config) {
            if (player_ids.size() != 2) {
                LOG_ERROR("五子棋游戏需要恰好2个玩家，当前: " + std::to_string(player_ids.size()));
                return false;
            }

            // 解析配置
            if (!config.empty()) {
                config_ = GomokuConfig::fromJson(config);
            }

            // [FIX] 使用Room层传递的棋子分配，而不是重新分配
            if (config.contains("piece_assignments")) {
                const auto& assignments = config["piece_assignments"];
                
                // 清空现有分配
                player_pieces_.clear();
                
                // 使用Room层的分配
                for (const auto& player_id : player_ids) {
                    if (assignments.contains(player_id)) {
                        int piece_value = assignments[player_id];
                        PieceType piece = static_cast<PieceType>(piece_value);
                        player_pieces_[player_id] = piece;
                        
                        LOG_INFO("[PIECE] Logic层接收棋子分配: " + player_id + " = " + 
                                std::to_string(piece_value) + " (" + 
                                (piece == PieceType::BLACK ? "黑子" : 
                                 piece == PieceType::WHITE ? "白子" : "未知") + ")");
                    } else {
                        LOG_ERROR("玩家 " + player_id + " 没有棋子分配信息");
                        return false;
                    }
                }
            } else {
                // [FIX] 备用方案：如果没有传递分配信息，使用默认分配
                LOG_WARNING("未收到棋子分配信息，使用默认分配");
                player_pieces_[player_ids[0]] = PieceType::BLACK;  // 第一个玩家执黑
                player_pieces_[player_ids[1]] = PieceType::WHITE;  // 第二个玩家执白
            }

            // 重置游戏状态
            resetState();
            
            // 设置时间
            game_state_.blackTimeLeft = config_.timeLimit;
            game_state_.whiteTimeLeft = config_.timeLimit;
            game_state_.incrementPerMove = config_.incrementPerMove;

            // [FIX] 显示最终的棋子分配结果
            std::string black_player, white_player;
            for (const auto& [player_id, piece] : player_pieces_) {
                if (piece == PieceType::BLACK) black_player = player_id;
                else if (piece == PieceType::WHITE) white_player = player_id;
            }

            LOG_INFO("✅ 五子棋游戏初始化成功，最终棋子分配: " + 
                    black_player + "(黑子), " + white_player + "(白子)");
            return true;
        }

        void GomokuLogic::tickGameSpecific() {
            // [FIX] 移除此处的 updateTime() 调用
            // 时间控制由时间控制定时器（每秒）单独处理，避免与游戏主循环（100ms）冲突
            // 如果两者都调用 updateTime()，100ms间隔下 elapsed 秒数会被截断为0，导致时间不减少

            // P2 重构：使用策略模式检查是否需要更新禁手位置
            if (game_mode_strategy_->hasForbiddenRules()) {
                updateForbiddenPositions();
            }
        }

        bool GomokuLogic::processPlayerActionSpecific(const std::string& player_id,
                                                    const nlohmann::json& action) {
            if (!action.contains("type")) {
                return false;
            }

            std::string action_type = action["type"];

            if (action_type == constants::MSG_PLACE_PIECE) {
                if (!action.contains("position")) return false;
                Position position = Position::fromJson(action["position"]);
                return placePiece(player_id, position);
            }
            else if (action_type == constants::MSG_UNDO_MOVE) {
                return undoMove(player_id);
            }
            else if (action_type == constants::MSG_SURRENDER) {
                return surrender(player_id);
            }
            else if (action_type == constants::MSG_DRAW_OFFER) {
                return offerDraw(player_id);
            }
            else if (action_type == constants::MSG_DRAW_RESPONSE) {
                if (!action.contains("accept")) return false;
                bool accept = action["accept"];
                return respondToDraw(player_id, accept);
            }

            return false;
        }

        bool GomokuLogic::checkGameEndSpecific() {
            return game_state_.gameResult != GameResult::ONGOING;
        }

        nlohmann::json GomokuLogic::getGameSpecificState() const {
            return game_state_.toJson();
        }

        void GomokuLogic::pauseGameSpecific() {
            stopTimer();
        }

        void GomokuLogic::resumeGameSpecific() {
            if (isGameRunning()) {
                startTimer();
            }
        }

        void GomokuLogic::resetGameSpecific() {
            resetState();
        }

        bool GomokuLogic::placePiece(const std::string& player_id, const Position& position) {
            // [FIX] 详细的placePiece调试日志
            bool valid_player = validatePlayerId(player_id);
            bool is_turn = isPlayerTurn(player_id);
            PieceType player_piece = getPlayerPiece(player_id);
            
            LOG_INFO("[MOVE] GameLogic落子验证: " + player_id + 
                    " | 有效玩家: " + (valid_player ? "是" : "否") +
                    " | 当前回合: " + (is_turn ? "是" : "否") +
                    " | 玩家棋子: " + std::to_string(static_cast<int>(player_piece)) +
                    " | 当前应下: " + std::to_string(static_cast<int>(game_state_.currentPlayer)));
            
            if (!valid_player || !is_turn) {
                LOG_WARNING("[FAIL] GameLogic验证失败: " + player_id + 
                           " | 原因: " + 
                           (!valid_player ? "玩家ID无效 " : "") +
                           (!is_turn ? "不是当前回合 " : ""));
                return false;
            }

            PieceType piece = getPlayerPiece(player_id);
            if (piece == PieceType::EMPTY) {
                LOG_WARNING("[FAIL] GameLogic验证失败: 玩家 " + player_id + " 没有分配棋子");
                return false;
            }

            // 检查位置有效性
            bool valid_move = isValidMove(position, piece);
            if (!valid_move) {
                bool valid_pos = position.isValid();
                bool empty_pos = game_state_.board.isEmpty(position);
                bool game_ongoing = (game_state_.gameResult == GameResult::ONGOING);
                
                LOG_WARNING("[FAIL] GameLogic位置无效: " + player_id + 
                           " 位置(" + std::to_string(position.row) + "," + std::to_string(position.col) + ")" +
                           " | 位置有效: " + (valid_pos ? "是" : "否") +
                           " | 位置空闲: " + (empty_pos ? "是" : "否") +
                           " | 游戏进行中: " + (game_ongoing ? "是" : "否") +
                           " | 游戏状态: " + std::to_string(static_cast<int>(game_state_.gameResult)));
                return false;
            }

            // 检查禁手（使用策略模式，只有有禁手规则的模式才检查）
            if (game_mode_strategy_->hasForbiddenRules() && piece == PieceType::BLACK) {
                ForbiddenType forbidden = checkForbidden(position, piece);
                if (forbidden != ForbiddenType::NONE) {
                    LOG_WARNING("GameLogic禁手检测: 黑棋在位置(" +
                               std::to_string(position.row) + "," + std::to_string(position.col) +
                               ") 触发禁手，类型: " + std::to_string(static_cast<int>(forbidden)));
                    // 黑棋禁手失败
                    endGame(GameResult::BLACK_FORBIDDEN);
                    return false;
                }
            }

            // 落子
            if (!game_state_.board.setPiece(position, piece)) {
                LOG_ERROR("❌ GameLogic棋盘设置失败: 无法在位置(" + 
                         std::to_string(position.row) + "," + std::to_string(position.col) + ") 设置棋子");
                return false;
            }

            LOG_INFO("✅ GameLogic落子成功: " + player_id + " 在位置(" + 
                    std::to_string(position.row) + "," + std::to_string(position.col) + ") 放置了" +
                    (piece == PieceType::BLACK ? "黑子" : "白子"));

            // 创建移动记录
            Move move(position, piece, ++game_state_.totalMoves);
            addMoveToHistory(move);

            game_state_.lastMove = position;

            // 检查获胜
            if (checkWin(position, piece)) {
                GameResult result = (piece == PieceType::BLACK) ? GameResult::BLACK_WIN : GameResult::WHITE_WIN;
                LOG_INFO("[WIN] GameLogic检测到获胜: " + player_id + " 获胜，结果: " + 
                        std::to_string(static_cast<int>(result)));
                endGame(result);
                return true;
            }

            // 检查平局（棋盘满）
            if (game_state_.board.isFull()) {
                LOG_INFO("[DRAW] GameLogic检测到平局: 棋盘已满");
                endGame(GameResult::DRAW);
                return true;
            }

            // 切换玩家
            switchCurrentPlayer();
            
            LOG_INFO("[SWITCH] GameLogic回合切换: 下一位玩家应下 " + 
                    std::to_string(static_cast<int>(game_state_.currentPlayer)) + 
                    " (" + (game_state_.currentPlayer == PieceType::BLACK ? "黑子" : "白子") + ")");

            // 增加时间
            if (piece == PieceType::BLACK) {
                game_state_.blackTimeLeft += config_.incrementPerMove;
            } else {
                game_state_.whiteTimeLeft += config_.incrementPerMove;
            }

            // 通知状态更新
            notifyGameStateUpdate();

            return true;
        }

        bool GomokuLogic::undoMove(const std::string& player_id) {
            if (!config_.allowUndo || !validatePlayerId(player_id)) {
                return false;
            }

            if (game_state_.moveHistory.empty()) {
                return false;
            }

            // 移除最后一步
            Move lastMove = game_state_.moveHistory.back();
            game_state_.moveHistory.pop_back();
            game_state_.totalMoves--;

            // 清空棋盘位置
            game_state_.board.setPiece(lastMove.position, PieceType::EMPTY);

            // 切换回上一个玩家
            game_state_.currentPlayer = lastMove.piece;

            // 更新最后移动位置
            if (!game_state_.moveHistory.empty()) {
                game_state_.lastMove = game_state_.moveHistory.back().position;
            } else {
                game_state_.lastMove = Position();
            }

            notifyGameStateUpdate();
            return true;
        }

        bool GomokuLogic::surrender(const std::string& player_id) {
            if (!validatePlayerId(player_id)) {
                return false;
            }

            PieceType piece = getPlayerPiece(player_id);
            GameResult result = (piece == PieceType::BLACK) ? GameResult::WHITE_WIN : GameResult::BLACK_WIN;
            endGame(result);
            return true;
        }

        bool GomokuLogic::offerDraw(const std::string& player_id) {
            if (!validatePlayerId(player_id) || draw_offered_) {
                return false;
            }

            draw_offer_player_ = player_id;
            draw_offered_ = true;
            return true;
        }

        bool GomokuLogic::respondToDraw(const std::string& player_id, bool accept) {
            if (!draw_offered_ || player_id == draw_offer_player_) {
                return false;
            }

            draw_offered_ = false;
            draw_offer_player_.clear();

            if (accept) {
                endGame(GameResult::DRAW);
            }

            return true;
        }

        bool GomokuLogic::isValidMove(const Position& position, PieceType piece) const {
            if (!position.isValid()) {
                return false;
            }

            if (!game_state_.board.isEmpty(position)) {
                return false;
            }

            if (game_state_.gameResult != GameResult::ONGOING) {
                return false;
            }

            // P2 重构：使用策略模式检查落子有效性
            return game_mode_strategy_->isValidMove(game_state_.board, position, piece);
        }

        ForbiddenType GomokuLogic::checkForbidden(const Position& position, PieceType piece) const {
            // P2 重构：使用策略模式检查禁手
            return game_mode_strategy_->checkForbidden(game_state_.board, position, piece);
        }

        bool GomokuLogic::checkWin(const Position& position, PieceType piece) const {
            // P2 重构：使用策略模式检查获胜
            return game_mode_strategy_->checkWin(game_state_.board, position, piece);
        }

        void GomokuLogic::updateConfig(const GomokuConfig& config) {
            if (isGameRunning()) {
                LOG_WARNING("不能在游戏进行中更改配置");
                return;
            }

            config_ = config;

            // P2 重构：如果游戏模式改变，更新策略
            if (!game_mode_strategy_ || game_mode_strategy_->getGameMode() != config.gameMode) {
                game_mode_strategy_ = GameModeStrategyFactory::create(config.gameMode);
                LOG_INFO("游戏模式已切换为: " + game_mode_strategy_->getModeName());
            }

            game_state_.blackTimeLeft = config_.timeLimit;
            game_state_.whiteTimeLeft = config_.timeLimit;
            game_state_.incrementPerMove = config_.incrementPerMove;
        }

        int GomokuLogic::getTimeLeft(PieceType piece) const {
            switch (piece) {
                case PieceType::BLACK: return game_state_.blackTimeLeft;
                case PieceType::WHITE: return game_state_.whiteTimeLeft;
                default: return 0;
            }
        }

        void GomokuLogic::startTimeControl() {
            LOG_INFO("[TIME] 启动时间控制定时器");
            startTimer();
        }

        void GomokuLogic::stopTimeControl() {
            LOG_INFO("[TIME] 停止时间控制定时器");
            stopTimer();
        }

        std::vector<Position> GomokuLogic::getRecommendedMoves(PieceType piece, int count) const {
            std::vector<std::pair<Position, int>> scored_positions;
            
            // 获取所有空位
            auto empty_positions = game_state_.board.getEmptyPositions();
            
            // 如果是第一步，推荐中央位置
            if (game_state_.totalMoves == 0) {
                return {Position(7, 7)};
            }
            
            // 评估每个位置
            for (const auto& pos : empty_positions) {
                int score = evaluatePosition(pos, piece);
                scored_positions.emplace_back(pos, score);
            }
            
            // 按分数排序
            std::sort(scored_positions.begin(), scored_positions.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
            
            // 返回前count个位置
            std::vector<Position> recommendations;
            int limit = std::min(count, static_cast<int>(scored_positions.size()));
            for (int i = 0; i < limit; ++i) {
                recommendations.push_back(scored_positions[i].first);
            }
            
            return recommendations;
        }

        nlohmann::json GomokuLogic::analyzePosition() const {
            nlohmann::json analysis;
            
            // 基本信息
            analysis["totalMoves"] = game_state_.totalMoves;
            analysis["currentPlayer"] = static_cast<int>(game_state_.currentPlayer);
            analysis["gameResult"] = static_cast<int>(game_state_.gameResult);
            
            // 威胁分析
            auto blackThreats = findThreats(PieceType::BLACK);
            auto whiteThreats = findThreats(PieceType::WHITE);
            analysis["blackThreats"] = blackThreats.size();
            analysis["whiteThreats"] = whiteThreats.size();
            
            // 推荐移动
            auto blackRecommendations = getRecommendedMoves(PieceType::BLACK, 3);
            auto whiteRecommendations = getRecommendedMoves(PieceType::WHITE, 3);
            
            nlohmann::json black_recs = nlohmann::json::array();
            for (const auto& pos : blackRecommendations) {
                black_recs.push_back(pos.toJson());
            }
            analysis["blackRecommendations"] = black_recs;
            
            nlohmann::json white_recs = nlohmann::json::array();
            for (const auto& pos : whiteRecommendations) {
                white_recs.push_back(pos.toJson());
            }
            analysis["whiteRecommendations"] = white_recs;
            
            return analysis;
        }

        // 私有方法实现
        void GomokuLogic::initializePieces() {
            // 已在initializeGameSpecific中处理
        }

        void GomokuLogic::switchCurrentPlayer() {
            game_state_.currentPlayer = getOpponent(game_state_.currentPlayer);
        }

        void GomokuLogic::addMoveToHistory(const Move& move) {
            game_state_.moveHistory.push_back(move);
        }

        void GomokuLogic::updateTime() {
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_move_time_);
            
            if (game_state_.currentPlayer == PieceType::BLACK) {
                game_state_.blackTimeLeft = std::max(0, game_state_.blackTimeLeft - static_cast<int>(elapsed.count()));
                if (game_state_.blackTimeLeft <= 0) {
                    handleTimeout(PieceType::BLACK);
                }
                notifyTimeUpdate(PieceType::BLACK, game_state_.blackTimeLeft);
            } else {
                game_state_.whiteTimeLeft = std::max(0, game_state_.whiteTimeLeft - static_cast<int>(elapsed.count()));
                if (game_state_.whiteTimeLeft <= 0) {
                    handleTimeout(PieceType::WHITE);
                }
                notifyTimeUpdate(PieceType::WHITE, game_state_.whiteTimeLeft);
            }
            
            last_move_time_ = now;
        }

        void GomokuLogic::startTimer() {
            if (time_control_timer_id_ != 0) {
                return; // 已经启动
            }
            
            auto event_loop_shared = event_loop_.lock();
            if (!event_loop_shared) {
                LOG_ERROR("EventLoop已失效，无法启动时间控制定时器");
                return;
            }
            
            time_counting_.store(true);
            last_move_time_ = std::chrono::system_clock::now();
            
            // 使用时间轮启动定时器
            time_control_timer_id_ = event_loop_shared->runEvery(
                time_update_interval_ms_,
                [this]() {
                    this->timerCallback();
                }
            );
            
            if (time_control_timer_id_ == 0) {
                LOG_ERROR("启动时间控制定时器失败");
                time_counting_.store(false);
            } else {
                LOG_DEBUG("时间控制定时器启动成功，间隔: " + std::to_string(time_update_interval_ms_) + "ms");
            }
        }

        void GomokuLogic::stopTimer() {
            if (time_control_timer_id_ == 0) {
                return; // 已经停止
            }
            
            time_counting_.store(false);
            
            auto event_loop_shared = event_loop_.lock();
            if (event_loop_shared) {
                event_loop_shared->cancelTimer(time_control_timer_id_);
                LOG_DEBUG("时间控制定时器已停止，ID: " + std::to_string(time_control_timer_id_));
            } else {
                LOG_WARNING("EventLoop已失效，无法正常取消定时器");
            }
            
            time_control_timer_id_ = 0;
        }

        void GomokuLogic::timerCallback() {
            if (!time_counting_.load()) {
                return;
            }
            
            try {
                if (isGameRunning()) {
                    updateTime();
                }
            } catch (const std::exception& e) {
                LOG_ERROR("时间控制回调异常: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("时间控制回调发生未知异常");
            }
        }

        void GomokuLogic::handleTimeout(PieceType player) {
            GameResult result = (player == PieceType::BLACK) ? GameResult::WHITE_WIN : GameResult::BLACK_WIN;
            endGame(result);
        }

        int GomokuLogic::countDirection(const Position& position, PieceType piece, int dx, int dy) const {
            int count = 1; // 包含当前位置
            
            // 向一个方向计数
            for (int i = 1; i < Board::BOARD_SIZE; ++i) {
                Position pos(position.row + i * dx, position.col + i * dy);
                if (!pos.isValid() || game_state_.board.getPiece(pos) != piece) {
                    break;
                }
                count++;
            }
            
            // 向相反方向计数
            for (int i = 1; i < Board::BOARD_SIZE; ++i) {
                Position pos(position.row - i * dx, position.col - i * dy);
                if (!pos.isValid() || game_state_.board.getPiece(pos) != piece) {
                    break;
                }
                count++;
            }
            
            return count;
        }

        std::array<int, 4> GomokuLogic::countAllDirections(const Position& position, PieceType piece) const {
            return {
                countDirection(position, piece, 0, 1),   // 水平
                countDirection(position, piece, 1, 0),   // 垂直
                countDirection(position, piece, 1, 1),   // 主对角线
                countDirection(position, piece, 1, -1)   // 副对角线
            };
        }

        // 禁手检测方法已迁移到 IGameModeStrategy 策略类中

        void GomokuLogic::updateForbiddenPositions() {
            // P2 重构：使用策略模式更新禁手位置
            game_state_.forbiddenPositions = game_mode_strategy_->getForbiddenPositions(
                game_state_.board, PieceType::BLACK);
        }

        int GomokuLogic::evaluatePosition(const Position& position, PieceType piece) const {
            int score = 0;
            
            // 基于连子数的评分
            std::array<int, 4> counts = countAllDirections(position, piece);
            for (int count : counts) {
                switch (count) {
                    case 5: score += constants::SCORE_FIVE; break;      // 五连
                    case 4: score += constants::SCORE_FOUR; break;     // 四连
                    case 3: score += constants::SCORE_THREE; break;    // 三连
                    case 2: score += constants::SCORE_TWO; break;      // 二连
                    default: break;
                }
            }

            // 防守评分
            PieceType opponent = getOpponent(piece);
            std::array<int, 4> opponent_counts = countAllDirections(position, opponent);
            for (int count : opponent_counts) {
                switch (count) {
                    case 4: score += constants::SCORE_BLOCK_FOUR; break;   // 阻止对手四连
                    case 3: score += constants::SCORE_BLOCK_THREE; break;  // 阻止对手三连
                    case 2: score += constants::SCORE_BLOCK_TWO; break;    // 阻止对手二连
                    default: break;
                }
            }
            
            return score;
        }

        std::vector<Position> GomokuLogic::findThreats(PieceType piece) const {
            std::vector<Position> threats;
            auto empty_positions = game_state_.board.getEmptyPositions();

            for (const auto& pos : empty_positions) {
                std::array<int, 4> counts = countAllDirections(pos, piece);
                for (int count : counts) {
                    if (count >= constants::THREAT_THRESHOLD) {
                        threats.push_back(pos);
                        break;
                    }
                }
            }

            return threats;
        }

        std::vector<Position> GomokuLogic::findAttacks(PieceType piece) const {
            std::vector<Position> attacks;
            auto empty_positions = game_state_.board.getEmptyPositions();

            for (const auto& pos : empty_positions) {
                if (evaluatePosition(pos, piece) >= constants::ATTACK_SCORE_THRESHOLD) {
                    attacks.push_back(pos);
                }
            }
            
            return attacks;
        }

        std::vector<Position> GomokuLogic::getNearbyPositions(const Position& position, int radius) const {
            std::vector<Position> nearby;
            
            for (int row = std::max(0, position.row - radius);
                 row <= std::min(Board::BOARD_SIZE - 1, position.row + radius); ++row) {
                for (int col = std::max(0, position.col - radius);
                     col <= std::min(Board::BOARD_SIZE - 1, position.col + radius); ++col) {
                    Position pos(row, col);
                    if (pos != position && game_state_.board.isEmpty(pos)) {
                        nearby.push_back(pos);
                    }
                }
            }
            
            return nearby;
        }

        void GomokuLogic::resetState() {
            game_state_.reset();
            draw_offered_ = false;
            draw_offer_player_.clear();
            stopTimer();
        }

        void GomokuLogic::endGame(GameResult result) {
            game_state_.gameResult = result;
            stopTimer();
            
            // 确定获胜者
            std::vector<std::string> winners;
            if (result == GameResult::BLACK_WIN) {
                for (const auto& pair : player_pieces_) {
                    if (pair.second == PieceType::BLACK) {
                        winners.push_back(pair.first);
                        break;
                    }
                }
            } else if (result == GameResult::WHITE_WIN) {
                for (const auto& pair : player_pieces_) {
                    if (pair.second == PieceType::WHITE) {
                        winners.push_back(pair.first);
                        break;
                    }
                }
            }
            
            notifyGameEnd(winners);
        }

        void GomokuLogic::notifyGameStateUpdate() {
            if (state_update_callback_) {
                state_update_callback_(getGameSpecificState());
            }
        }

        void GomokuLogic::notifyTimeUpdate(PieceType player, int timeLeft) {
            if (time_update_callback_) {
                time_update_callback_(player, timeLeft);
            }
        }

        bool GomokuLogic::validatePlayerId(const std::string& player_id) const {
            return player_pieces_.find(player_id) != player_pieces_.end();
        }

        PieceType GomokuLogic::getPlayerPiece(const std::string& player_id) const {
            auto it = player_pieces_.find(player_id);
            return it != player_pieces_.end() ? it->second : PieceType::EMPTY;
        }

        bool GomokuLogic::isPlayerTurn(const std::string& player_id) const {
            return getPlayerPiece(player_id) == game_state_.currentPlayer;
        }

        // 静态方法实现
        std::unique_ptr<GomokuLogic> GomokuLogic::fromJson(const nlohmann::json& json,
                                                          std::shared_ptr<common::network::EventLoop> event_loop) {
            auto config = GomokuConfig::fromJson(json.value("config", nlohmann::json{}));
            return std::make_unique<GomokuLogic>(config, event_loop);
        }

        GomokuConfig GomokuLogic::getDefaultConfig() {
            return GomokuConfig();
        }

        bool GomokuLogic::validateConfig(const GomokuConfig& config) {
            return config.timeLimit > 0 && 
                   config.incrementPerMove >= 0 && 
                   config.maxSpectators >= 0;
        }

    } // namespace gomoku
} // namespace game_services
