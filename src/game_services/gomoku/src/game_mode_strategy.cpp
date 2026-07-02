/**
 * @file game_mode_strategy.cpp
 * @brief 游戏模式策略实现
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 */

#include "game_mode_strategy.h"
#include "common/logger/logger.h"
#include <algorithm>
#include <set>

namespace game_services {
namespace gomoku {

// ==================== FreestyleStrategy ====================

bool FreestyleStrategy::isValidMove(const Board& board, const Position& position, PieceType piece) const {
    if (!position.isValid()) {
        return false;
    }
    if (!board.isEmpty(position)) {
        return false;
    }
    return true;
}

bool FreestyleStrategy::checkWin(const Board& board, const Position& position, PieceType piece) const {
    // 四个方向：水平、垂直、主对角线、副对角线
    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        int count = countDirection(board, position, piece, dir[0], dir[1]);
        if (count >= constants::WIN_CONDITION) {
            return true;
        }
    }
    return false;
}

ForbiddenType FreestyleStrategy::checkForbidden(const Board& board, const Position& position, PieceType piece) const {
    // 自由规则无禁手
    return ForbiddenType::NONE;
}

PatternAnalysis FreestyleStrategy::analyzePatterns(const Board& board, const Position& position, PieceType piece) const {
    PatternAnalysis analysis;
    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        int count = countDirection(board, position, piece, dir[0], dir[1]);
        analysis.max_consecutive = std::max(analysis.max_consecutive, count);

        int four_type = isFour(board, position, dir[0], dir[1], piece);
        if (four_type == 2) {
            analysis.live_fours++;
        } else if (four_type == 1) {
            analysis.rush_fours++;
        }

        if (isLiveThree(board, position, dir[0], dir[1], piece)) {
            analysis.live_threes++;
        }
    }
    return analysis;
}

bool FreestyleStrategy::isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const {
    // 计算该方向上的连子数
    int count = countDirection(board, position, piece, dx, dy);
    if (count != 3) {
        return false;
    }

    // 找到该方向上的起始和结束位置
    Position start = position;
    Position end = position;

    // 向负方向找到起始
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position prev(start.row - dx, start.col - dy);
        if (!prev.isValid() || board.getPiece(prev) != piece) {
            break;
        }
        start = prev;
    }

    // 向正方向找到结束
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position next(end.row + dx, end.col + dy);
        if (!next.isValid() || board.getPiece(next) != piece) {
            break;
        }
        end = next;
    }

    // 检查两端是否为空
    Position before_start(start.row - dx, start.col - dy);
    Position after_end(end.row + dx, end.col + dy);

    bool before_empty = before_start.isValid() && board.isEmpty(before_start);
    bool after_empty = after_end.isValid() && board.isEmpty(after_end);

    return before_empty && after_empty;
}

int FreestyleStrategy::isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const {
    int count = countDirection(board, position, piece, dx, dy);
    if (count != 4) {
        return 0;
    }

    // 找到四连的起始和结束位置
    Position start = position;
    Position end = position;

    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position prev(start.row - dx, start.col - dy);
        if (!prev.isValid() || board.getPiece(prev) != piece) {
            break;
        }
        start = prev;
    }

    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position next(end.row + dx, end.col + dy);
        if (!next.isValid() || board.getPiece(next) != piece) {
            break;
        }
        end = next;
    }

    // 检查两端是否为空
    Position before_start(start.row - dx, start.col - dy);
    Position after_end(end.row + dx, end.col + dy);

    bool before_empty = before_start.isValid() && board.isEmpty(before_start);
    bool after_empty = after_end.isValid() && board.isEmpty(after_end);

    if (before_empty && after_empty) {
        return 2;  // 活四
    } else if (before_empty || after_empty) {
        return 1;  // 冲四
    }
    return 0;  // 死四
}

std::vector<Position> FreestyleStrategy::getForbiddenPositions(const Board& board, PieceType piece) const {
    return {};  // 自由规则无禁手
}

int FreestyleStrategy::countDirection(const Board& board, const Position& position, PieceType piece, int dx, int dy) const {
    int count = 1;  // 包含当前位置

    // 向正方向计数
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position pos(position.row + i * dx, position.col + i * dy);
        if (!pos.isValid() || board.getPiece(pos) != piece) {
            break;
        }
        count++;
    }

    // 向负方向计数
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position pos(position.row - i * dx, position.col - i * dy);
        if (!pos.isValid() || board.getPiece(pos) != piece) {
            break;
        }
        count++;
    }

    return count;
}

// ==================== RenjuStrategy ====================

bool RenjuStrategy::isValidMove(const Board& board, const Position& position, PieceType piece) const {
    if (!position.isValid()) {
        return false;
    }
    if (!board.isEmpty(position)) {
        return false;
    }

    // 黑棋需要额外检查禁手
    if (piece == PieceType::BLACK && checkForbidden(board, position, piece) != ForbiddenType::NONE) {
        return false;
    }

    return true;
}

bool RenjuStrategy::checkWin(const Board& board, const Position& position, PieceType piece) const {
    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        int count = countDirection(board, position, piece, dir[0], dir[1]);

        // 黑棋：恰好5连才获胜
        if (piece == PieceType::BLACK) {
            if (count == constants::WIN_CONDITION) {
                return true;
            }
        }
        // 白棋：5连或以上都获胜
        else if (piece == PieceType::WHITE) {
            if (count >= constants::WIN_CONDITION) {
                return true;
            }
        }
    }
    return false;
}

ForbiddenType RenjuStrategy::checkForbidden(const Board& board, const Position& position, PieceType piece) const {
    // 只有黑棋有禁手
    if (piece != PieceType::BLACK) {
        return ForbiddenType::NONE;
    }

    // 优先级：长连 > 双四 > 双三

    // 检查长连禁手（六子或以上）
    if (checkOverlineForbidden(board, position)) {
        return ForbiddenType::OVERLINE;
    }

    // 检查双四禁手
    if (checkFourFourForbidden(board, position)) {
        return ForbiddenType::FOUR_FOUR;
    }

    // 检查双三禁手
    if (checkThreeThreeForbidden(board, position)) {
        return ForbiddenType::THREE_THREE;
    }

    return ForbiddenType::NONE;
}

PatternAnalysis RenjuStrategy::analyzePatterns(const Board& board, const Position& position, PieceType piece) const {
    PatternAnalysis analysis;
    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        int count = countDirection(board, position, piece, dir[0], dir[1]);
        analysis.max_consecutive = std::max(analysis.max_consecutive, count);

        int four_type = isFour(board, position, dir[0], dir[1], piece);
        if (four_type == 2) {
            analysis.live_fours++;
        } else if (four_type == 1) {
            analysis.rush_fours++;
        }

        if (isLiveThree(board, position, dir[0], dir[1], piece)) {
            analysis.live_threes++;
        }
    }
    return analysis;
}

bool RenjuStrategy::isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const {
    /**
     * 活三的精确定义：
     * 在某个方向上形成恰好三个连续的同色棋子，
     * 且两端都是空位，且在任一空位再落一子能形成活四。
     *
     * 需要考虑的活三形式：
     * 1. _OOO_ （标准活三）
     * 2. _O_OO_ （跳活三）
     * 3. _OO_O_ （跳活三）
     */

    // 获取该方向上的线段分析
    auto segment = analyzeLineSegment(board, position, piece, dx, dy);

    // 活三条件：恰好3子，两端开放
    if (segment.consecutive == 3 && segment.blocked == 0) {
        return true;
    }

    // 检查跳活三：形如 _O_OO_ 或 _OO_O_
    // 这里简化处理，主要检测 _X_XX_ 或 _XX_X_ 形式
    int total_in_line = countConsecutiveWithGaps(board, position, piece, dx, dy);
    if (total_in_line >= 3 && segment.blocked == 0 && segment.spaces <= 1) {
        // 进一步验证：能否形成活四
        // 在两端空位测试落子
        Position left(position.row - 4 * dx, position.col - 4 * dy);
        Position right(position.row + 4 * dx, position.col + 4 * dy);

        // 简化判断：如果两端有空位且能形成四连
        for (int offset = -3; offset <= 3; ++offset) {
            Position test_pos(position.row + offset * dx, position.col + offset * dy);
            if (test_pos.isValid() && board.isEmpty(test_pos)) {
                // 模拟在此落子
                int four_type = isFour(board, test_pos, dx, dy, piece);
                if (four_type == 2) {  // 能形成活四
                    return true;
                }
            }
        }
    }

    return false;
}

int RenjuStrategy::isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const {
    /**
     * 四的定义：
     * 1. 活四：_XXXX_ - 两端开放的四连，再下一子就五连
     * 2. 冲四：OXXXX_ 或 _XXXXO - 一端被堵的四连
     *
     * 返回值：0=不是四，1=冲四，2=活四
     */

    auto segment = analyzeLineSegment(board, position, piece, dx, dy);

    // 检查各种四的形式
    if (segment.consecutive >= 4) {
        if (segment.blocked == 0) {
            return 2;  // 活四
        } else if (segment.blocked == 1) {
            return 1;  // 冲四
        }
        return 0;  // 死四（两端都被堵）
    }

    // 检查跳四：形如 XX_XX 或 X_XXX 或 XXX_X
    if (segment.consecutive + segment.spaces >= 4 && segment.consecutive >= 3) {
        // 检查是否存在可以形成五连的位置
        for (int offset = -4; offset <= 4; ++offset) {
            Position test_pos(position.row + offset * dx, position.col + offset * dy);
            if (test_pos.isValid() && board.isEmpty(test_pos)) {
                // 模拟落子，检查是否能形成五连
                int count = 1;
                // 正方向
                for (int i = 1; i < 5; ++i) {
                    Position p(test_pos.row + i * dx, test_pos.col + i * dy);
                    if (!p.isValid()) break;
                    auto p_piece = board.getPiece(p);
                    if (p_piece == piece) count++;
                    else break;
                }
                // 负方向
                for (int i = 1; i < 5; ++i) {
                    Position p(test_pos.row - i * dx, test_pos.col - i * dy);
                    if (!p.isValid()) break;
                    auto p_piece = board.getPiece(p);
                    if (p_piece == piece) count++;
                    else break;
                }
                if (count >= 4) {
                    // 检查这个四的开放性
                    if (segment.blocked == 0) {
                        return 2;  // 跳活四
                    } else if (segment.blocked == 1) {
                        return 1;  // 跳冲四
                    }
                }
            }
        }
    }

    return 0;
}

std::vector<Position> RenjuStrategy::getForbiddenPositions(const Board& board, PieceType piece) const {
    std::vector<Position> forbidden;

    if (piece != PieceType::BLACK) {
        return forbidden;  // 只有黑棋有禁手
    }

    // 检查所有空位
    auto empty_positions = board.getEmptyPositions();
    for (const auto& pos : empty_positions) {
        if (checkForbidden(board, pos, piece) != ForbiddenType::NONE) {
            forbidden.push_back(pos);
        }
    }

    return forbidden;
}

bool RenjuStrategy::checkThreeThreeForbidden(const Board& board, const Position& position) const {
    /**
     * 三三禁手：同时形成两个或以上的活三
     *
     * 注意：只有真正的活三才计入，眠三（一端被堵的三）不计入
     */

    int live_three_count = 0;
    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        if (isLiveThree(board, position, dir[0], dir[1], PieceType::BLACK)) {
            live_three_count++;
        }
    }

    return live_three_count >= 2;
}

bool RenjuStrategy::checkFourFourForbidden(const Board& board, const Position& position) const {
    /**
     * 四四禁手：同时形成两个或以上的四（活四或冲四）
     */

    int four_count = 0;
    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        int four_type = isFour(board, position, dir[0], dir[1], PieceType::BLACK);
        if (four_type > 0) {  // 活四或冲四都计入
            four_count++;
        }
    }

    return four_count >= 2;
}

bool RenjuStrategy::checkOverlineForbidden(const Board& board, const Position& position) const {
    /**
     * 长连禁手：形成六子或以上的连线
     */

    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

    for (const auto& dir : directions) {
        int count = countDirection(board, position, PieceType::BLACK, dir[0], dir[1]);
        if (count >= constants::OVERLINE_LENGTH) {
            return true;
        }
    }

    return false;
}

int RenjuStrategy::countDirection(const Board& board, const Position& position, PieceType piece, int dx, int dy) const {
    int count = 1;  // 包含当前位置

    // 向正方向计数
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position pos(position.row + i * dx, position.col + i * dy);
        if (!pos.isValid() || board.getPiece(pos) != piece) {
            break;
        }
        count++;
    }

    // 向负方向计数
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position pos(position.row - i * dx, position.col - i * dy);
        if (!pos.isValid() || board.getPiece(pos) != piece) {
            break;
        }
        count++;
    }

    return count;
}

int RenjuStrategy::countConsecutiveWithGaps(const Board& board, const Position& position, PieceType piece, int dx, int dy) const {
    /**
     * 计算方向上包含空格的连续棋子数
     * 用于检测跳三、跳四等棋型
     */
    int count = 0;
    int gaps = 0;

    // 向正方向扫描
    for (int i = 0; i < 5; ++i) {
        Position pos(position.row + i * dx, position.col + i * dy);
        if (!pos.isValid()) break;

        auto p = board.getPiece(pos);
        if (p == piece) {
            count++;
        } else if (p == PieceType::EMPTY && gaps < 1) {
            gaps++;
            // 继续扫描
        } else {
            break;
        }
    }

    return count;
}

RenjuStrategy::LineSegment RenjuStrategy::analyzeLineSegment(const Board& board, const Position& position, PieceType piece, int dx, int dy) const {
    /**
     * 分析某个方向上的线段信息
     */
    LineSegment segment{0, 0, 0, false};

    // 找到连续棋子的范围
    Position start = position;
    Position end = position;

    // 向负方向找起始
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position prev(start.row - dx, start.col - dy);
        if (!prev.isValid()) {
            segment.blocked++;
            break;
        }
        auto p = board.getPiece(prev);
        if (p == piece) {
            start = prev;
        } else if (p == PieceType::EMPTY) {
            break;
        } else {
            segment.blocked++;
            break;
        }
    }

    // 向正方向找结束
    for (int i = 1; i < Board::BOARD_SIZE; ++i) {
        Position next(end.row + dx, end.col + dy);
        if (!next.isValid()) {
            segment.blocked++;
            break;
        }
        auto p = board.getPiece(next);
        if (p == piece) {
            end = next;
        } else if (p == PieceType::EMPTY) {
            break;
        } else {
            segment.blocked++;
            break;
        }
    }

    // 计算连续棋子数
    segment.consecutive = 1;
    for (int i = 1; start.row + i * dx <= end.row || start.col + i * dy <= end.col; ++i) {
        Position p(start.row + i * dx, start.col + i * dy);
        if (p == end) break;
        if (board.getPiece(p) == piece) {
            segment.consecutive++;
        }
    }

    // 判断是否为活棋型
    segment.is_live = (segment.blocked == 0);

    return segment;
}

// ==================== Swap2Strategy ====================

bool Swap2Strategy::isValidMove(const Board& board, const Position& position, PieceType piece) const {
    // Swap2阶段：前3步无限制
    if (isInSwapPhase(board)) {
        if (!position.isValid()) return false;
        if (!board.isEmpty(position)) return false;
        return true;
    }

    // Swap2阶段结束后，按连珠规则
    return renju_strategy_->isValidMove(board, position, piece);
}

bool Swap2Strategy::checkWin(const Board& board, const Position& position, PieceType piece) const {
    return renju_strategy_->checkWin(board, position, piece);
}

ForbiddenType Swap2Strategy::checkForbidden(const Board& board, const Position& position, PieceType piece) const {
    // Swap2阶段：前3步无禁手
    if (isInSwapPhase(board)) {
        return ForbiddenType::NONE;
    }

    // Swap2阶段结束后，按连珠规则
    return renju_strategy_->checkForbidden(board, position, piece);
}

PatternAnalysis Swap2Strategy::analyzePatterns(const Board& board, const Position& position, PieceType piece) const {
    return renju_strategy_->analyzePatterns(board, position, piece);
}

bool Swap2Strategy::isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const {
    return renju_strategy_->isLiveThree(board, position, dx, dy, piece);
}

int Swap2Strategy::isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const {
    return renju_strategy_->isFour(board, position, dx, dy, piece);
}

std::vector<Position> Swap2Strategy::getForbiddenPositions(const Board& board, PieceType piece) const {
    if (isInSwapPhase(board)) {
        return {};
    }
    return renju_strategy_->getForbiddenPositions(board, piece);
}

bool Swap2Strategy::isInSwapPhase(const Board& board) const {
    return getMoveCount(board) < SWAP_PHASE_MOVES;
}

int Swap2Strategy::getMoveCount(const Board& board) const {
    int count = 0;
    for (int i = 0; i < Board::BOARD_SIZE; ++i) {
        for (int j = 0; j < Board::BOARD_SIZE; ++j) {
            if (board.getPiece(Position(i, j)) != PieceType::EMPTY) {
                count++;
            }
        }
    }
    return count;
}

} // namespace gomoku
} // namespace game_services
