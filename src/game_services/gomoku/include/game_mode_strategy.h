/**
 * @file game_mode_strategy.h
 * @brief 游戏模式策略接口 - 策略模式实现
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 *
 * 职责：
 * - 定义游戏模式的统一接口
 * - 支持不同五子棋规则变体（自由规则、连珠规则、Swap2等）
 * - 封装禁手检测逻辑
 */

#pragma once

#include "gomoku_types.h"
#include <memory>
#include <vector>
#include <string>

namespace game_services {
namespace gomoku {

// 前向声明
class Board;

/**
 * @brief 棋型分析结果
 */
struct PatternAnalysis {
    int live_fours = 0;         // 活四数量
    int rush_fours = 0;         // 冲四数量
    int live_threes = 0;        // 活三数量
    int sleep_threes = 0;       // 眠三数量
    int live_twos = 0;          // 活二数量
    int max_consecutive = 0;    // 最大连子数

    nlohmann::json toJson() const {
        return nlohmann::json{
            {"liveFours", live_fours},
            {"rushFours", rush_fours},
            {"liveThrees", live_threes},
            {"sleepThrees", sleep_threes},
            {"liveTwos", live_twos},
            {"maxConsecutive", max_consecutive}
        };
    }
};

/**
 * @brief 游戏模式策略接口
 *
 * 使用策略模式将不同游戏规则的逻辑封装到独立的策略类中，
 * 使得游戏规则的切换不影响核心游戏逻辑。
 */
class IGameModeStrategy {
public:
    virtual ~IGameModeStrategy() = default;

    // ========== 核心规则检查 ==========

    /**
     * @brief 检查落子是否有效
     * @param board 当前棋盘状态
     * @param position 落子位置
     * @param piece 棋子类型
     * @return 是否可以落子
     */
    virtual bool isValidMove(const Board& board, const Position& position, PieceType piece) const = 0;

    /**
     * @brief 检查是否获胜
     * @param board 当前棋盘状态
     * @param position 最后落子位置
     * @param piece 棋子类型
     * @return 是否获胜
     */
    virtual bool checkWin(const Board& board, const Position& position, PieceType piece) const = 0;

    /**
     * @brief 检查禁手（如果有）
     * @param board 当前棋盘状态
     * @param position 落子位置
     * @param piece 棋子类型
     * @return 禁手类型（NONE表示无禁手）
     */
    virtual ForbiddenType checkForbidden(const Board& board, const Position& position, PieceType piece) const = 0;

    // ========== 棋型分析 ==========

    /**
     * @brief 分析指定位置的棋型
     * @param board 当前棋盘状态
     * @param position 分析位置（假设在此位置落子）
     * @param piece 棋子类型
     * @return 棋型分析结果
     */
    virtual PatternAnalysis analyzePatterns(const Board& board, const Position& position, PieceType piece) const = 0;

    /**
     * @brief 检查是否形成活三
     * @param board 当前棋盘状态
     * @param position 落子位置
     * @param dx 方向X
     * @param dy 方向Y
     * @param piece 棋子类型
     * @return 是否为活三
     */
    virtual bool isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const = 0;

    /**
     * @brief 检查是否形成四（活四或冲四）
     * @param board 当前棋盘状态
     * @param position 落子位置
     * @param dx 方向X
     * @param dy 方向Y
     * @param piece 棋子类型
     * @return 0=不是四，1=冲四，2=活四
     */
    virtual int isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const = 0;

    // ========== 辅助方法 ==========

    /**
     * @brief 获取游戏模式名称
     */
    virtual std::string getModeName() const = 0;

    /**
     * @brief 获取游戏模式枚举值
     */
    virtual GameMode getGameMode() const = 0;

    /**
     * @brief 是否启用禁手规则
     */
    virtual bool hasForbiddenRules() const = 0;

    /**
     * @brief 获取获胜连子数
     */
    virtual int getWinLength() const { return 5; }

    /**
     * @brief 获取所有禁手位置
     * @param board 当前棋盘状态
     * @param piece 棋子类型（通常只有黑棋有禁手）
     * @return 禁手位置列表
     */
    virtual std::vector<Position> getForbiddenPositions(const Board& board, PieceType piece) const = 0;
};

/**
 * @brief 自由规则策略
 * @details 无禁手规则，五连即胜，长连也算胜
 */
class FreestyleStrategy : public IGameModeStrategy {
public:
    bool isValidMove(const Board& board, const Position& position, PieceType piece) const override;
    bool checkWin(const Board& board, const Position& position, PieceType piece) const override;
    ForbiddenType checkForbidden(const Board& board, const Position& position, PieceType piece) const override;
    PatternAnalysis analyzePatterns(const Board& board, const Position& position, PieceType piece) const override;
    bool isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const override;
    int isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const override;
    std::string getModeName() const override { return "freestyle"; }
    GameMode getGameMode() const override { return GameMode::FREESTYLE; }
    bool hasForbiddenRules() const override { return false; }
    std::vector<Position> getForbiddenPositions(const Board& board, PieceType piece) const override;

private:
    int countDirection(const Board& board, const Position& position, PieceType piece, int dx, int dy) const;
};

/**
 * @brief 连珠规则策略（Renju）
 * @details 黑棋有禁手（三三、四四、长连），白棋无禁手
 */
class RenjuStrategy : public IGameModeStrategy {
public:
    bool isValidMove(const Board& board, const Position& position, PieceType piece) const override;
    bool checkWin(const Board& board, const Position& position, PieceType piece) const override;
    ForbiddenType checkForbidden(const Board& board, const Position& position, PieceType piece) const override;
    PatternAnalysis analyzePatterns(const Board& board, const Position& position, PieceType piece) const override;
    bool isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const override;
    int isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const override;
    std::string getModeName() const override { return "renju"; }
    GameMode getGameMode() const override { return GameMode::RENJU; }
    bool hasForbiddenRules() const override { return true; }
    std::vector<Position> getForbiddenPositions(const Board& board, PieceType piece) const override;

private:
    // 禁手检测辅助方法
    bool checkThreeThreeForbidden(const Board& board, const Position& position) const;
    bool checkFourFourForbidden(const Board& board, const Position& position) const;
    bool checkOverlineForbidden(const Board& board, const Position& position) const;

    // 棋型检测辅助方法
    int countDirection(const Board& board, const Position& position, PieceType piece, int dx, int dy) const;
    int countConsecutiveWithGaps(const Board& board, const Position& position, PieceType piece, int dx, int dy) const;

    // 获取方向上的线段信息
    struct LineSegment {
        int consecutive;    // 连续棋子数
        int spaces;         // 空位数
        int blocked;        // 被堵端数（0, 1, 2）
        bool is_live;       // 是否为活棋型
    };
    LineSegment analyzeLineSegment(const Board& board, const Position& position, PieceType piece, int dx, int dy) const;
};

/**
 * @brief Swap2规则策略
 * @details 开局特殊规则，之后按连珠规则
 */
class Swap2Strategy : public IGameModeStrategy {
public:
    Swap2Strategy() : renju_strategy_(std::make_unique<RenjuStrategy>()) {}

    bool isValidMove(const Board& board, const Position& position, PieceType piece) const override;
    bool checkWin(const Board& board, const Position& position, PieceType piece) const override;
    ForbiddenType checkForbidden(const Board& board, const Position& position, PieceType piece) const override;
    PatternAnalysis analyzePatterns(const Board& board, const Position& position, PieceType piece) const override;
    bool isLiveThree(const Board& board, const Position& position, int dx, int dy, PieceType piece) const override;
    int isFour(const Board& board, const Position& position, int dx, int dy, PieceType piece) const override;
    std::string getModeName() const override { return "swap2"; }
    GameMode getGameMode() const override { return GameMode::SWAP2; }
    bool hasForbiddenRules() const override { return true; }
    std::vector<Position> getForbiddenPositions(const Board& board, PieceType piece) const override;

    // Swap2特有方法
    bool isInSwapPhase(const Board& board) const;
    int getMoveCount(const Board& board) const;

private:
    std::unique_ptr<RenjuStrategy> renju_strategy_;
    static constexpr int SWAP_PHASE_MOVES = 3;  // Swap2阶段结束于第3步后
};

/**
 * @brief 游戏模式策略工厂
 */
class GameModeStrategyFactory {
public:
    /**
     * @brief 创建游戏模式策略
     * @param mode 游戏模式
     * @return 策略实例
     */
    static std::unique_ptr<IGameModeStrategy> create(GameMode mode) {
        switch (mode) {
            case GameMode::FREESTYLE:
                return std::make_unique<FreestyleStrategy>();
            case GameMode::RENJU:
                return std::make_unique<RenjuStrategy>();
            case GameMode::SWAP2:
                return std::make_unique<Swap2Strategy>();
            case GameMode::PRO:
            case GameMode::TOURNAMENT:
                // 职业规则和锦标赛规则使用连珠规则
                return std::make_unique<RenjuStrategy>();
            default:
                return std::make_unique<FreestyleStrategy>();
        }
    }
};

} // namespace gomoku
} // namespace game_services
