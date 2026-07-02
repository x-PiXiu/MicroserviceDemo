#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>

/**
 * @file game_types.h
 * @brief 游戏基类模块的通用类型定义
 * @details 定义所有游戏共用的枚举、结构体和常量
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 游戏类型枚举
         * @details 用于标识不同类型的游戏
         */
        enum class GameType {
            UNKNOWN = 0,
            SNAKE = 1,      // 贪吃蛇游戏
            CARD = 2,       // 卡牌游戏
            CHESS = 3,      // 棋类游戏
            PUZZLE = 4      // 益智游戏
        };

        /**
         * @brief 房间状态枚举
         * @details 描述游戏房间的当前状态
         */
        enum class RoomState {
            WAITING = 0,    // 等待玩家加入
            PLAYING = 1,    // 游戏进行中
            PAUSED = 2,     // 游戏暂停
            FINISHED = 3    // 游戏结束
        };

        /**
         * @brief 玩家状态枚举
         * @details 描述玩家在游戏中的状态
         */
        enum class PlayerState {
            DISCONNECTED = 0,   // 断开连接
            CONNECTED = 1,      // 已连接
            IN_ROOM = 2,        // 在房间中
            PLAYING = 3,        // 游戏中
            SPECTATING = 4      // 观战中
        };

        /**
         * @brief 消息类型枚举
         * @details WebSocket消息的类型分类
         */
        enum class MessageType {
            // 系统消息
            SYSTEM_INFO = 0,
            SYSTEM_ERROR = 1,
            
            // 连接管理
            CLIENT_CONNECT = 10,
            CLIENT_DISCONNECT = 11,
            HEARTBEAT = 12,
            
            // 房间管理
            ROOM_JOIN = 20,
            ROOM_LEAVE = 21,
            ROOM_STATE_UPDATE = 22,
            
            // 游戏控制
            GAME_START = 30,
            GAME_PAUSE = 31,
            GAME_RESUME = 32,
            GAME_END = 33,
            
            // 游戏数据
            GAME_STATE_UPDATE = 40,
            PLAYER_ACTION = 41,
            PLAYER_STATE_UPDATE = 42
        };

        /**
         * @brief 基础配置结构
         * @details 所有游戏服务器的通用配置项
         */
        struct BaseConfig {
            std::string host = "0.0.0.0";          // 服务器监听地址
            int port = 8084;                       // HTTP服务端口
            int websocket_port = 8085;             // WebSocket端口
            int max_rooms = 1000;                  // 最大房间数
            int max_players_per_room = 4;          // 每房间最大玩家数
            bool enable_websocket = true;          // 是否启用WebSocket
            bool enable_leaderboard = true;        // 是否启用排行榜
            GameType game_type = GameType::UNKNOWN; // 游戏类型
            std::string config_file = "";          // 配置文件路径

            // Qt6客户端支持
            bool qt6_optimizations = true;         // 是否启用Qt6优化
            bool enable_compression = false;       // 是否启用消息压缩
            int heartbeat_interval_ms = 30000;     // 心跳间隔(毫秒)

            // 线程池配置
            int thread_pool_core_size = 4;         // 线程池核心线程数
            int thread_pool_max_size = 16;         // 线程池最大线程数
            int thread_pool_keep_alive_ms = 60000; // 线程保活时间(毫秒)
            int thread_pool_queue_capacity = 1000; // 任务队列容量
            bool thread_pool_allow_core_timeout = false; // 是否允许核心线程超时
            std::string thread_pool_rejection_policy = "abort"; // 拒绝策略
            bool thread_pool_prestart_core_threads = false; // 是否预启动核心线程
            std::string thread_pool_name_prefix = "game-worker-"; // 线程名前缀
            bool thread_pool_enable_monitoring = true; // 是否启用监控

            // 数据库配置
            std::string mysql_host = "dev-mysql";
            int mysql_port = 3306;
            std::string mysql_database = "gomoku_game_db";
            std::string mysql_username = "root";
            std::string mysql_password = "123456";
            int mysql_max_connections = 20;
            int mysql_connection_timeout = 5000;

            std::string redis_host = "redis";
            int redis_port = 6379;
            int redis_database = 0;
            std::string redis_password = "123456";
            int redis_max_connections = 10;
            int redis_connection_timeout = 3000;

            // 服务间通信配置
            std::string auth_service_url = "http://127.0.0.1:8083";    // 认证服务地址
            std::string api_gateway_url = "http://127.0.0.1:8081";     // API Gateway地址
            
            /**
             * @brief 验证配置有效性
             * @return 配置是否有效
             */
            virtual bool validate() const {
                return port > 0 && port < 65536 &&
                       websocket_port > 0 && websocket_port < 65536 &&
                       max_rooms > 0 && max_players_per_room > 0;
            }
            
            /**
             * @brief 转换为JSON格式
             * @return JSON对象
             */
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"host", host},
                    {"port", port},
                    {"websocket_port", websocket_port},
                    {"max_rooms", max_rooms},
                    {"max_players_per_room", max_players_per_room},
                    {"enable_websocket", enable_websocket},
                    {"enable_leaderboard", enable_leaderboard},
                    {"game_type", static_cast<int>(game_type)},
                    {"qt6_optimizations", qt6_optimizations},
                    {"enable_compression", enable_compression},
                    {"heartbeat_interval_ms", heartbeat_interval_ms},
                    {"thread_pool_core_size", thread_pool_core_size},
                    {"thread_pool_max_size", thread_pool_max_size},
                    {"thread_pool_keep_alive_ms", thread_pool_keep_alive_ms},
                    {"thread_pool_queue_capacity", thread_pool_queue_capacity},
                    {"thread_pool_allow_core_timeout", thread_pool_allow_core_timeout},
                    {"thread_pool_rejection_policy", thread_pool_rejection_policy},
                    {"thread_pool_prestart_core_threads", thread_pool_prestart_core_threads},
                    {"thread_pool_name_prefix", thread_pool_name_prefix},
                    {"thread_pool_enable_monitoring", thread_pool_enable_monitoring},
                    {"mysql_host", mysql_host},
                    {"mysql_port", mysql_port},
                    {"mysql_database", mysql_database},
                    {"redis_host", redis_host},
                    {"redis_port", redis_port},
                    {"redis_database", redis_database},
                    {"auth_service_url", auth_service_url},
                    {"api_gateway_url", api_gateway_url}
                };
            }
        };

        /**
         * @brief 玩家信息结构
         * @details 存储玩家的基本信息
         */
        struct PlayerInfo {
            // 基本玩家信息
            std::string player_id;                          // 玩家ID
            std::string username;                           // 用户名
            std::string nickname;                           // 昵称 (player_name的别名)
            std::string avatar_url;                         // 头像URL
            
            // 游戏统计信息
            int level = 1;                                  // 等级
            int rating = 1000;                              // 积分/评级
            int total_games = 0;                            // 总游戏数
            int wins = 0;                                   // 胜局数
            int losses = 0;                                 // 败局数
            int win_streak = 0;                             // 连胜数
            
            // 认证和会话信息
            bool authenticated = false;                     // 是否已认证
            std::string auth_token;                         // 认证令牌
            std::string session_id;                         // 会话ID
            
            // 游戏状态信息
            PlayerState state = PlayerState::DISCONNECTED;  // 玩家状态
            std::string room_id;                            // 所在房间ID
            std::chrono::system_clock::time_point join_time; // 加入时间
            std::chrono::system_clock::time_point last_activity; // 最后活动时间
            
            // Qt6客户端信息
            std::string qt_version;                         // Qt版本
            std::string client_version;                     // 客户端版本
            bool supports_compression = false;              // 是否支持压缩
            int preferred_fps = 60;                         // 首选帧率
            
            /**
             * @brief 转换为JSON格式
             * @return JSON对象
             */
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"player_id", player_id},
                    {"username", username},
                    {"nickname", nickname},
                    {"avatar_url", avatar_url},
                    {"level", level},
                    {"rating", rating},
                    {"total_games", total_games},
                    {"wins", wins},
                    {"losses", losses},
                    {"win_streak", win_streak},
                    {"authenticated", authenticated},
                    {"state", static_cast<int>(state)},
                    {"room_id", room_id},
                    {"qt_version", qt_version},
                    {"client_version", client_version},
                    {"supports_compression", supports_compression},
                    {"preferred_fps", preferred_fps}
                };
            }
        };

        /**
         * @brief 房间信息结构
         * @details 存储游戏房间的基本信息
         */
        struct RoomInfo {
            std::string room_id;                            // 房间ID
            std::string creator_id;                         // 创建者ID
            RoomState state = RoomState::WAITING;           // 房间状态
            GameType game_type = GameType::UNKNOWN;         // 游戏类型
            int current_players = 0;                        // 当前玩家数
            int max_players = 4;                            // 最大玩家数
            std::chrono::system_clock::time_point created_at; // 创建时间
            std::chrono::system_clock::time_point last_activity; // 最后活动时间
            
            /**
             * @brief 转换为JSON格式
             * @return JSON对象
             */
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"room_id", room_id},
                    {"creator_id", creator_id},
                    {"state", static_cast<int>(state)},
                    {"game_type", static_cast<int>(game_type)},
                    {"current_players", current_players},
                    {"max_players", max_players},
                    {"created_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                        created_at.time_since_epoch()).count()},
                    {"last_activity", std::chrono::duration_cast<std::chrono::milliseconds>(
                        last_activity.time_since_epoch()).count()}
                };
            }
        };

        /**
         * @brief 游戏统计信息结构
         * @details 用于性能监控和统计
         */
        struct GameStats {
            int total_rooms = 0;                    // 总房间数
            int active_rooms = 0;                   // 活跃房间数
            int total_players = 0;                  // 总玩家数
            int active_players = 0;                 // 活跃玩家数
            double avg_response_time_ms = 0.0;      // 平均响应时间
            double cpu_usage_percent = 0.0;         // CPU使用率
            double memory_usage_mb = 0.0;           // 内存使用量
            
            /**
             * @brief 转换为JSON格式
             * @return JSON对象
             */
            nlohmann::json toJson() const {
                // 🔧 安全的数值验证函数 - 防止NaN/Inf导致JSON错误
                auto safe_double = [](double val) -> double {
                    if (std::isnan(val) || std::isinf(val)) {
                        return 0.0;  // 替换无效值为安全默认值
                    }
                    return val;
                };
                
                return nlohmann::json{
                    {"total_rooms", total_rooms},
                    {"active_rooms", active_rooms},
                    {"total_players", total_players},
                    {"active_players", active_players},
                    {"avg_response_time_ms", safe_double(avg_response_time_ms)},
                    {"cpu_usage_percent", safe_double(cpu_usage_percent)},
                    {"memory_usage_mb", safe_double(memory_usage_mb)}
                };
            }
        };

        // 常量定义
        namespace constants {
            constexpr int DEFAULT_ROOM_CLEANUP_INTERVAL_MS = 300000;  // 5分钟
            constexpr int DEFAULT_PLAYER_TIMEOUT_MS = 60000;          // 1分钟
            constexpr int DEFAULT_HEARTBEAT_TIMEOUT_MS = 90000;       // 1.5分钟
            constexpr int MAX_MESSAGE_SIZE = 65536;                   // 64KB
            constexpr int MAX_ROOM_NAME_LENGTH = 100;
            constexpr int MAX_PLAYER_NAME_LENGTH = 50;
        }

    } // namespace game_base
} // namespace game_services
