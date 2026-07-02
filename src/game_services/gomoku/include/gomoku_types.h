#pragma once

#include "game_types.h"
#include "common/config/config_manager.h"
#include <array>
#include <vector>
#include <nlohmann/json.hpp>

/**
 * @file gomoku_types.h
 * @brief 五子棋游戏类型定义
 * @details 定义五子棋游戏专用的枚举、结构体和常量
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 棋子类型枚举
         */
        enum class PieceType {
            EMPTY = 0,      // 空位
            BLACK = 1,      // 黑子
            WHITE = 2       // 白子
        };

        /**
         * @brief 游戏模式枚举
         * @details 支持不同的五子棋规则变体
         */
        enum class GameMode {
            FREESTYLE = 0,          // 自由模式（无禁手）
            RENJU = 1,              // 连珠模式（黑棋有禁手）
            SWAP2 = 2,              // Swap2规则
            PRO = 3,                // 职业规则
            TOURNAMENT = 4          // 锦标赛规则
        };

        /**
         * @brief 游戏结果枚举
         */
        enum class GameResult {
            ONGOING = 0,            // 游戏进行中
            BLACK_WIN = 1,          // 黑方获胜
            WHITE_WIN = 2,          // 白方获胜
            DRAW = 3,               // 平局
            BLACK_FORBIDDEN = 4,    // 黑方禁手失败
            TIMEOUT = 5,            // 超时失败
            SURRENDER = 6           // 投降
        };

        /**
         * @brief 禁手类型枚举（连珠规则）
         */
        enum class ForbiddenType {
            NONE = 0,               // 无禁手
            THREE_THREE = 1,        // 双三禁手
            FOUR_FOUR = 2,          // 双四禁手
            OVERLINE = 3            // 长连禁手（六连或以上）
        };

        /**
         * @brief 棋盘位置结构
         */
        struct Position {
            int row;                // 行坐标 (0-14)
            int col;                // 列坐标 (0-14)

            Position() : row(-1), col(-1) {}
            Position(int r, int c) : row(r), col(c) {}

            bool isValid() const {
                return row >= 0 && row < 15 && col >= 0 && col < 15;
            }

            bool operator==(const Position& other) const {
                return row == other.row && col == other.col;
            }

            bool operator!=(const Position& other) const {
                return !(*this == other);
            }

            nlohmann::json toJson() const {
                return nlohmann::json{{"row", row}, {"col", col}};
            }

            static Position fromJson(const nlohmann::json& json) {
                int row = json.value("row", -1);
                int col = json.value("col", -1);
                return Position(row, col);
            }
        };

        /**
         * @brief 落子移动结构
         */
        struct Move {
            Position position;      // 落子位置
            PieceType piece;        // 棋子类型
            int moveNumber;         // 移动序号（从1开始）
            std::chrono::system_clock::time_point timestamp; // 落子时间
            bool isForbidden;       // 是否为禁手
            ForbiddenType forbiddenType; // 禁手类型

            Move() : piece(PieceType::EMPTY), moveNumber(0), isForbidden(false), forbiddenType(ForbiddenType::NONE) {}
            
            Move(Position pos, PieceType p, int moveNum) 
                : position(pos), piece(p), moveNumber(moveNum), 
                  timestamp(std::chrono::system_clock::now()), 
                  isForbidden(false), forbiddenType(ForbiddenType::NONE) {}

            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"position", position.toJson()},
                    {"piece", static_cast<int>(piece)},
                    {"moveNumber", moveNumber},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        timestamp.time_since_epoch()).count()},
                    {"isForbidden", isForbidden},
                    {"forbiddenType", static_cast<int>(forbiddenType)}
                };
            }

            static Move fromJson(const nlohmann::json& json) {
                Move move;
                if (json.contains("position")) {
                    move.position = Position::fromJson(json["position"]);
                }
                if (json.contains("piece")) {
                    move.piece = static_cast<PieceType>(json.value("piece", 0));
                }
                if (json.contains("moveNumber")) {
                    move.moveNumber = json.value("moveNumber", 0);
                }
                if (json.contains("timestamp")) {
                    auto timestamp_ms = json["timestamp"].get<int64_t>();
                    move.timestamp = std::chrono::system_clock::time_point(
                        std::chrono::milliseconds(timestamp_ms));
                }
                if (json.contains("isForbidden")) {
                    move.isForbidden = json.value("isForbidden", false);
                }
                if (json.contains("forbiddenType")) {
                    move.forbiddenType = static_cast<ForbiddenType>(json.value("forbiddenType", 0));
                }
                return move;
            }
        };

        /**
         * @brief 五子棋棋盘类
         * @details 使用15x15的标准棋盘
         */
        class Board {
        public:
            static constexpr int BOARD_SIZE = 15;
            
            Board() {
                clear();
            }

            /**
             * @brief 清空棋盘
             */
            void clear() {
                for (int i = 0; i < BOARD_SIZE; ++i) {
                    for (int j = 0; j < BOARD_SIZE; ++j) {
                        board_[i][j] = PieceType::EMPTY;
                    }
                }
            }

            /**
             * @brief 获取指定位置的棋子
             */
            PieceType getPiece(const Position& pos) const {
                if (!pos.isValid()) return PieceType::EMPTY;
                return board_[pos.row][pos.col];
            }

            /**
             * @brief 在指定位置放置棋子
             */
            bool setPiece(const Position& pos, PieceType piece) {
                if (!pos.isValid() || board_[pos.row][pos.col] != PieceType::EMPTY) {
                    return false;
                }
                board_[pos.row][pos.col] = piece;
                return true;
            }

            /**
             * @brief 检查位置是否为空
             */
            bool isEmpty(const Position& pos) const {
                return getPiece(pos) == PieceType::EMPTY;
            }

            /**
             * @brief 检查棋盘是否已满
             */
            bool isFull() const {
                for (int i = 0; i < BOARD_SIZE; ++i) {
                    for (int j = 0; j < BOARD_SIZE; ++j) {
                        if (board_[i][j] == PieceType::EMPTY) {
                            return false;
                        }
                    }
                }
                return true;
            }

            /**
             * @brief 获取所有空位
             */
            std::vector<Position> getEmptyPositions() const {
                std::vector<Position> empty;
                for (int i = 0; i < BOARD_SIZE; ++i) {
                    for (int j = 0; j < BOARD_SIZE; ++j) {
                        if (board_[i][j] == PieceType::EMPTY) {
                            empty.emplace_back(i, j);
                        }
                    }
                }
                return empty;
            }

            /**
             * @brief 转换为JSON格式
             */
            nlohmann::json toJson() const {
                nlohmann::json json_board = nlohmann::json::array();
                for (int i = 0; i < BOARD_SIZE; ++i) {
                    nlohmann::json row = nlohmann::json::array();
                    for (int j = 0; j < BOARD_SIZE; ++j) {
                        row.push_back(static_cast<int>(board_[i][j]));
                    }
                    json_board.push_back(row);
                }
                return json_board;
            }

            /**
             * @brief 从JSON格式加载
             */
            void fromJson(const nlohmann::json& json) {
                clear();
                for (int i = 0; i < BOARD_SIZE && static_cast<size_t>(i) < json.size(); ++i) {
                    for (int j = 0; j < BOARD_SIZE && static_cast<size_t>(j) < json[i].size(); ++j) {
                        board_[i][j] = static_cast<PieceType>(json[i][j]);
                    }
                }
            }

        private:
            std::array<std::array<PieceType, BOARD_SIZE>, BOARD_SIZE> board_;
        };

        /**
         * @brief 五子棋游戏状态结构
         */
        struct GomokuGameState {
            Board board;                            // 棋盘状态
            PieceType currentPlayer;                // 当前玩家
            GameMode gameMode;                      // 游戏模式
            GameResult gameResult;                  // 游戏结果
            std::vector<Move> moveHistory;          // 移动历史
            Position lastMove;                      // 最后一步
            bool canUndo;                           // 是否可以悔棋
            int totalMoves;                         // 总移动数
            
            // 时间控制
            int blackTimeLeft;                      // 黑方剩余时间(秒)
            int whiteTimeLeft;                      // 白方剩余时间(秒)
            int incrementPerMove;                   // 每步增加时间(秒)
            
            // 连珠规则相关
            std::vector<Position> forbiddenPositions; // 禁手位置
            
            GomokuGameState() {
                reset();
            }

            void reset() {
                board.clear();
                currentPlayer = PieceType::BLACK;
                gameMode = GameMode::FREESTYLE;
                gameResult = GameResult::ONGOING;
                moveHistory.clear();
                lastMove = Position();
                canUndo = false;
                totalMoves = 0;
                blackTimeLeft = 900;        // 15分钟
                whiteTimeLeft = 900;        // 15分钟
                incrementPerMove = 10;      // 每步加10秒
                forbiddenPositions.clear();
            }

            nlohmann::json toJson() const {
                nlohmann::json moves_json = nlohmann::json::array();
                for (const auto& move : moveHistory) {
                    moves_json.push_back(move.toJson());
                }

                nlohmann::json forbidden_json = nlohmann::json::array();
                for (const auto& pos : forbiddenPositions) {
                    forbidden_json.push_back(pos.toJson());
                }

                return nlohmann::json{
                    {"board", board.toJson()},
                    {"currentPlayer", static_cast<int>(currentPlayer)},
                    {"gameMode", static_cast<int>(gameMode)},
                    {"gameResult", static_cast<int>(gameResult)},
                    {"moveHistory", moves_json},
                    {"lastMove", lastMove.toJson()},
                    {"canUndo", canUndo},
                    {"totalMoves", totalMoves},
                    {"blackTimeLeft", blackTimeLeft},
                    {"whiteTimeLeft", whiteTimeLeft},
                    {"incrementPerMove", incrementPerMove},
                    {"forbiddenPositions", forbidden_json}
                };
            }
        };

        /**
         * @brief 五子棋游戏配置
         */
        struct GomokuConfig {
            GameMode gameMode = GameMode::FREESTYLE;
            int timeLimit = 900;                    // 时间限制(秒)
            int incrementPerMove = 10;              // 每步增加时间(秒)
            bool allowUndo = false;                 // 是否允许悔棋
            bool allowSpectators = true;            // 是否允许观战
            int maxSpectators = 50;                 // 最大观战人数
            bool rankingEnabled = true;             // 是否启用排名

            // 游戏开始配置 (P0 修复)
            bool requireReadyToStart = false;       // 是否要求所有玩家准备才能开始
            bool autoStartWhenFull = false;         // 人满时是否自动开始
            int autoStartDelayMs = 3000;            // 自动开始延迟(毫秒)

            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"gameMode", static_cast<int>(gameMode)},
                    {"timeLimit", timeLimit},
                    {"incrementPerMove", incrementPerMove},
                    {"allowUndo", allowUndo},
                    {"allowSpectators", allowSpectators},
                    {"maxSpectators", maxSpectators},
                    {"rankingEnabled", rankingEnabled},
                    {"requireReadyToStart", requireReadyToStart},
                    {"autoStartWhenFull", autoStartWhenFull},
                    {"autoStartDelayMs", autoStartDelayMs}
                };
            }

            static GomokuConfig fromJson(const nlohmann::json& json) {
                GomokuConfig config;
                
                // [FIX] 支持字符串类型的gameMode
                if (json.contains("gameMode")) {
                    if (json["gameMode"].is_string()) {
                        std::string mode_str = json.value("gameMode", "");
                        config.gameMode = stringToGameMode(mode_str);
                    } else if (json["gameMode"].is_number()) {
                        config.gameMode = static_cast<GameMode>(json.value("gameMode", 0));
                    }
                } else {
                    config.gameMode = GameMode::FREESTYLE;  // 默认值
                }
                
                config.timeLimit = json.value("timeLimit", 900);
                config.incrementPerMove = json.value("incrementPerMove", 10);
                config.allowUndo = json.value("allowUndo", false);
                config.allowSpectators = json.value("allowSpectators", true);
                config.maxSpectators = json.value("maxSpectators", 50);
                config.rankingEnabled = json.value("rankingEnabled", true);
                config.requireReadyToStart = json.value("requireReadyToStart", false);
                config.autoStartWhenFull = json.value("autoStartWhenFull", false);
                config.autoStartDelayMs = json.value("autoStartDelayMs", 3000);
                return config;
            }
            
            /**
             * @brief 字符串转游戏模式
             */
            static GameMode stringToGameMode(const std::string& mode_str) {
                if (mode_str == "freestyle") return GameMode::FREESTYLE;
                if (mode_str == "renju") return GameMode::RENJU;
                if (mode_str == "swap2") return GameMode::SWAP2;
                if (mode_str == "pro") return GameMode::PRO;
                if (mode_str == "tournament") return GameMode::TOURNAMENT;
                return GameMode::FREESTYLE;  // 默认值
            }
            
            /**
             * @brief 从ConfigManager加载配置
             */
            static GomokuConfig fromConfigManager() {
                auto& config_manager = common::config::ConfigManager::getInstance();
                GomokuConfig config;

                config.gameMode = static_cast<GameMode>(
                    config_manager.get<int>("gomoku.default_game_mode", static_cast<int>(GameMode::FREESTYLE))
                );
                config.timeLimit = config_manager.get<int>("gomoku.time_limit", 900);
                config.incrementPerMove = config_manager.get<int>("gomoku.increment_per_move", 10);
                config.allowUndo = config_manager.get<bool>("gomoku.allow_undo", false);
                config.allowSpectators = config_manager.get<bool>("gomoku.allow_spectators", true);
                config.maxSpectators = config_manager.get<int>("gomoku.max_spectators", 50);
                config.rankingEnabled = config_manager.get<bool>("gomoku.ranking_enabled", true);

                // P0 修复：游戏开始配置
                config.requireReadyToStart = config_manager.get<bool>("gomoku.require_ready_to_start", false);
                config.autoStartWhenFull = config_manager.get<bool>("gomoku.auto_start_when_full", false);
                config.autoStartDelayMs = config_manager.get<int>("gomoku.auto_start_delay_ms", 3000);

                return config;
            }
        };

        /**
         * @brief 游戏结束上下文 - 包含结算所需的所有数据
         * @details 用于在游戏结束时收集数据并传递给 Game Data Service 结算 API
         */
        struct GameEndContext {
            std::string room_id;
            std::string game_type = "gomoku";
            std::string game_mode;              // "casual" 或 "ranked"

            // 时间信息
            std::chrono::system_clock::time_point started_at;
            std::chrono::system_clock::time_point ended_at;
            int duration_seconds = 0;
            int total_moves = 0;

            /**
             * @brief 玩家上下文 - 单个玩家的结算数据
             */
            struct PlayerContext {
                std::string user_id;
                PieceType piece;                // BLACK 或 WHITE
                std::string result;             // "win", "loss", "draw", "surrender", "timeout"

                // 游戏前数据（从 Game Data Service 获取）
                int rating_before = 1200;
                int games_played = 0;
                int win_streak = 0;
                int tier_level = 5;
                bool is_first_win_today = false;
                int games_today = 0;
            };

            std::vector<PlayerContext> players;
            std::string winner_id;
            nlohmann::json metadata;            // 棋谱、获胜方式等

            nlohmann::json toJson() const {
                nlohmann::json players_json = nlohmann::json::array();
                for (const auto& player : players) {
                    // 内联棋子类型转换（pieceTypeToString 定义在此结构之后）
                    std::string piece_str = "empty";
                    switch (player.piece) {
                        case PieceType::BLACK: piece_str = "black"; break;
                        case PieceType::WHITE: piece_str = "white"; break;
                        default: piece_str = "empty"; break;
                    }

                    players_json.push_back({
                        {"user_id", player.user_id},
                        {"piece", piece_str},
                        {"result", player.result},
                        {"rating_before", player.rating_before},
                        {"games_played", player.games_played},
                        {"win_streak", player.win_streak},
                        {"tier_level", player.tier_level},
                        {"is_first_win_today", player.is_first_win_today},
                        {"games_today", player.games_today}
                    });
                }

                return {
                    {"room_id", room_id},
                    {"game_type", game_type},
                    {"game_mode", game_mode},
                    {"duration_seconds", duration_seconds},
                    {"total_moves", total_moves},
                    {"players", players_json},
                    {"winner_id", winner_id},
                    {"metadata", metadata}
                };
            }
        };

        // 常量定义
        namespace constants {
            constexpr int WIN_CONDITION = 5;                    // 获胜条件：连成5个
            constexpr int DEFAULT_TIME_LIMIT = 900;             // 默认时间限制：15分钟
            constexpr int DEFAULT_INCREMENT = 10;               // 默认每步增加10秒
            constexpr int MAX_MOVES = 225;                      // 最大移动数（15x15棋盘）
            constexpr int THINKING_TIME_WARNING = 30;           // 思考时间警告：30秒
            constexpr int OVERLINE_LENGTH = 6;                  // 长连禁手长度

            // 位置评估分数常量（P3: 魔法数字常量化）
            constexpr int SCORE_FIVE = 100000;                  // 五连得分
            constexpr int SCORE_FOUR = 10000;                   // 四连得分
            constexpr int SCORE_THREE = 1000;                   // 三连得分
            constexpr int SCORE_TWO = 100;                      // 二连得分
            constexpr int SCORE_BLOCK_FOUR = 50000;             // 阻止对手四连得分
            constexpr int SCORE_BLOCK_THREE = 5000;             // 阻止对手三连得分
            constexpr int SCORE_BLOCK_TWO = 500;                // 阻止对手二连得分
            constexpr int THREAT_THRESHOLD = 4;                 // 威胁检测阈值（四连以上）
            constexpr int ATTACK_SCORE_THRESHOLD = 1000;        // 攻击位置得分阈值（三连或以上）

            // 消息类型
            constexpr const char* MSG_PLACE_PIECE = "place_piece";
            constexpr const char* MSG_UNDO_MOVE = "undo_move";
            constexpr const char* MSG_UNDO_REQUEST = "undo_request";
            constexpr const char* MSG_UNDO_RESPONSE = "undo_response";
            constexpr const char* MSG_SURRENDER = "surrender";
            constexpr const char* MSG_DRAW_OFFER = "draw_offer";
            constexpr const char* MSG_DRAW_RESPONSE = "draw_response";
            constexpr const char* MSG_GAME_STATE = "game_state";
            constexpr const char* MSG_MOVE_RESULT = "move_result";
            constexpr const char* MSG_GAME_END = "game_end";
            constexpr const char* MSG_TIME_UPDATE = "time_update";
        }

        /**
         * @brief 工具函数：获取对手棋子类型
         */
        inline PieceType getOpponent(PieceType piece) {
            switch (piece) {
                case PieceType::BLACK: return PieceType::WHITE;
                case PieceType::WHITE: return PieceType::BLACK;
                default: return PieceType::EMPTY;
            }
        }

        /**
         * @brief 工具函数：棋子类型转字符串
         */
        inline std::string pieceTypeToString(PieceType piece) {
            switch (piece) {
                case PieceType::BLACK: return "black";
                case PieceType::WHITE: return "white";
                default: return "empty";
            }
        }

        /**
         * @brief 工具函数：游戏模式转字符串
         */
        inline std::string gameModeToString(GameMode mode) {
            switch (mode) {
                case GameMode::FREESTYLE: return "freestyle";
                case GameMode::RENJU: return "renju";
                case GameMode::SWAP2: return "swap2";
                case GameMode::PRO: return "pro";
                case GameMode::TOURNAMENT: return "tournament";
                default: return "unknown";
            }
        }

    } // namespace gomoku
} // namespace game_services


