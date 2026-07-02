/**
 * @file user_models.h
 * @brief 用户数据模型定义 - 认证服务核心数据结构
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 *
 * 功能特性:
 * - 核心用户信息模型
 * - 游戏用户数据模型
 * - 用户会话模型
 * - 认证结果模型
 * - 游戏服务器信息模型
 * - 支持JSON序列化/反序列化
 * - 数据验证和安全性检查
 *
 * 设计原则:
 * - 数据模型与业务逻辑分离
 * - 支持多游戏平台扩展
 * - 线程安全的数据访问
 * - 完整的数据验证机制
 */

#ifndef USER_MODELS_H
#define USER_MODELS_H

// 解决DEBUG宏冲突
#ifdef DEBUG
#undef DEBUG
#endif

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace core_services {
    namespace auth_service {

        /**
         * @brief 游戏类型枚举
         * @details 支持的游戏类型，可扩展
         */
        enum class GameType {
            UNKNOWN = 0,    ///< 未知游戏类型
            SNAKE = 1,      ///< 贪吃蛇游戏
            TETRIS = 2,     ///< 俄罗斯方块
            CHESS = 3,      ///< 象棋游戏
            POKER = 4,      ///< 扑克游戏
            RPG = 5,        ///< 角色扮演游戏
            MOBA = 6,       ///< 多人在线战术竞技游戏
            FPS = 7,        ///< 第一人称射击游戏
            STRATEGY = 8    ///< 策略游戏
        };

        /**
         * @brief 认证请求结构体
         */
        struct AuthRequest {
            std::string username;                                   ///< 用户名
            std::string email;                                      ///< 邮箱
            std::string password;                                   ///< 密码
            std::string nickname;                                   ///< 昵称
            std::string client_ip;                                  ///< 客户端IP
            std::string user_agent;                                 ///< 用户代理字符串
            std::string device_id;                                  ///< 设备ID
            std::string device_type;                                ///< 设备类型
            std::unordered_map<std::string, std::string> metadata;  ///< 附加元数据

            /**
             * @brief 从JSON创建认证请求
             * @param json JSON对象
             * @return 认证请求对象
             */
            static AuthRequest fromJson(const nlohmann::json& json);

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 验证请求有效性
             * @return 验证结果，成功返回空字符串
             */
            std::string validate() const;

            /**
             * @brief 验证用户名格式
             * @param username 用户名
             * @return 格式正确返回true
             */
            static bool isValidUsername(const std::string& username);

            /**
             * @brief 验证邮箱格式
             * @param email 邮箱地址
             * @return 格式正确返回true
             */
            static bool isValidEmail(const std::string& email);
        };

        /**
         * @brief 游戏登录请求结构体
         */
        struct GameLoginRequest {
            std::string access_token;                               ///< 访问令牌
            GameType game_type;                                     ///< 游戏类型
            std::string preferred_server_region;                    ///< 首选服务器区域
            std::string client_ip;                                  ///< 客户端IP
            std::string user_agent;                                 ///< 用户代理字符串
            nlohmann::json metadata;  ///< 附加元数据（支持复杂数据类型）

            /**
             * @brief 从JSON创建游戏登录请求
             * @param json JSON对象
             * @return 游戏登录请求对象
             */
            static GameLoginRequest fromJson(const nlohmann::json& json);

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 验证请求有效性
             * @return 验证结果，成功返回空字符串
             */
            std::string validate() const;
        };

        /**
         * @brief 用户状态枚举
         */
        enum class UserStatus {
            INACTIVE = 0,   ///< 未激活
            ACTIVE = 1,     ///< 活跃
            SUSPENDED = 2,  ///< 暂停
            BANNED = 3,     ///< 封禁
            DELETED = 4     ///< 已删除
        };

        /**
         * @brief 在线状态枚举
         */
        enum class OnlineStatus {
            OFFLINE = 0,    ///< 离线
            ONLINE = 1,     ///< 在线
            AWAY = 2,       ///< 离开
            BUSY = 3,       ///< 忙碌
            INVISIBLE = 4   ///< 隐身
        };

        /**
         * @brief 服务器状态枚举
         */
        enum class ServerStatus {
            OFFLINE = 0,    ///< 离线
            ONLINE = 1,     ///< 在线
            MAINTENANCE = 2, ///< 维护中
            OVERLOADED = 3  ///< 过载
        };

        /**
         * @brief 核心用户信息模型
         * @details 存储用户的基本信息和认证相关数据
         */
        struct CoreUserInfo {
            std::string user_id;                                    ///< 用户ID（主键）
            std::string username;                                   ///< 用户名（唯一）
            std::string email;                                      ///< 邮箱地址（唯一）
            std::string password_hash;                              ///< 密码哈希值
            std::string salt;                                       ///< 密码盐值
            std::string nickname;                                   ///< 昵称
            std::string avatar_url;                                 ///< 头像URL
            UserStatus status = UserStatus::ACTIVE;                 ///< 用户状态
            OnlineStatus online_status = OnlineStatus::OFFLINE;     ///< 在线状态
            std::chrono::system_clock::time_point created_at;       ///< 创建时间
            std::chrono::system_clock::time_point updated_at;       ///< 更新时间
            std::optional<std::chrono::system_clock::time_point> last_login_at;    ///< 最后登录时间
            std::string last_login_ip;                              ///< 最后登录IP
            int login_attempts = 0;                                 ///< 登录尝试次数
            std::optional<std::chrono::system_clock::time_point> locked_until;     ///< 锁定到期时间
            std::vector<std::string> roles;                         ///< 用户角色列表
            std::unordered_map<std::string, std::string> metadata;  ///< 扩展元数据

            /**
             * @brief 默认构造函数
             */
            CoreUserInfo() = default;

            /**
             * @brief 构造函数
             * @param username 用户名
             * @param email 邮箱
             * @param password_hash 密码哈希
             * @param salt 密码盐值
             * @param nickname 昵称
             */
            CoreUserInfo(const std::string& username, const std::string& email,
                        const std::string& password_hash, const std::string& salt,
                        const std::string& nickname = "");

            /**
             * @brief 验证用户信息有效性
             * @return 验证结果，成功返回空字符串，失败返回错误信息
             */
            std::string validate() const;

            /**
             * @brief 检查用户是否被锁定
             * @return 如果用户被锁定返回true
             */
            bool isLocked() const;

            /**
             * @brief 检查用户是否活跃
             * @return 如果用户状态为活跃返回true
             */
            bool isActive() const;

            /**
             * @brief 检查用户是否在线
             * @return 如果用户在线返回true
             */
            bool isOnline() const;

            /**
             * @brief 转换为JSON对象
             * @param include_sensitive 是否包含敏感信息（密码哈希等）
             * @return JSON对象
             */
            nlohmann::json toJson(bool include_sensitive = false) const;

            /**
             * @brief 从JSON对象创建用户信息
             * @param json JSON对象
             * @return 用户信息对象
             */
            static CoreUserInfo fromJson(const nlohmann::json& json);

            /**
             * @brief 将字符串转换为用户状态枚举
             * @param status_str 状态字符串
             * @return 用户状态枚举
             */
            static UserStatus stringToUserStatus(const std::string& status_str);

            /**
             * @brief 将字符串转换为在线状态枚举
             * @param status_str 在线状态字符串
             * @return 在线状态枚举
             */
            static OnlineStatus stringToOnlineStatus(const std::string& status_str);
        };

        /**
         * @brief 游戏用户数据模型
         * @details 存储用户在特定游戏中的数据和统计信息
         */
        struct GameUserData {
            int64_t user_id = 0;                                    ///< 用户ID
            GameType game_type = GameType::UNKNOWN;                 ///< 游戏类型
            int level = 1;                                          ///< 游戏等级
            int64_t experience = 0;                                 ///< 经验值
            int64_t coins = 0;                                      ///< 游戏币
            int64_t gems = 0;                                       ///< 宝石数量
            int total_games = 0;                                    ///< 总游戏次数
            int wins = 0;                                           ///< 胜利次数
            int losses = 0;                                         ///< 失败次数
            int draws = 0;                                          ///< 平局次数
            int64_t total_playtime_seconds = 0;                     ///< 总游戏时间（秒）
            int64_t best_score = 0;                                 ///< 最高分数
            std::chrono::system_clock::time_point last_played_at;   ///< 最后游戏时间
            std::chrono::system_clock::time_point created_at;       ///< 创建时间
            std::chrono::system_clock::time_point updated_at;       ///< 更新时间
            std::unordered_map<std::string, std::string> game_data; ///< 游戏特定数据
            std::vector<std::string> achievements;                  ///< 成就列表

            /**
             * @brief 默认构造函数
             */
            GameUserData() = default;

            /**
             * @brief 构造函数
             * @param user_id 用户ID
             * @param game_type 游戏类型
             */
            GameUserData(int64_t user_id, GameType game_type);

            /**
             * @brief 计算胜率
             * @return 胜率百分比（0-100）
             */
            double getWinRate() const;

            /**
             * @brief 计算平均游戏时长
             * @return 平均游戏时长（秒）
             */
            double getAverageGameDuration() const;

            /**
             * @brief 添加游戏结果
             * @param won 是否胜利
             * @param score 得分
             * @param duration_seconds 游戏时长（秒）
             */
            void addGameResult(bool won, int64_t score, int64_t duration_seconds);

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 从JSON对象创建游戏用户数据
             * @param json JSON对象
             * @return 游戏用户数据对象
             */
            static GameUserData fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 用户会话模型
         * @details 管理用户登录会话信息
         */
        struct UserSession {
            std::string session_id;                                 ///< 会话ID（UUID）
            std::string user_id;                                    ///< 用户ID
            std::string access_token;                               ///< 访问令牌
            std::string refresh_token;                              ///< 刷新令牌
            std::string device_type;                                ///< 设备类型（web/mobile/desktop）
            std::string device_id;                                  ///< 设备唯一标识
            std::string client_ip;                                  ///< 客户端IP地址
            std::string user_agent;                                 ///< 用户代理字符串
            std::chrono::system_clock::time_point created_at;       ///< 创建时间
            std::chrono::system_clock::time_point last_activity_at; ///< 最后活动时间
            std::chrono::system_clock::time_point expires_at;       ///< 过期时间
            bool is_active = true;                                  ///< 是否活跃
            std::unordered_map<std::string, std::string> metadata;  ///< 会话元数据

            /**
             * @brief 默认构造函数
             */
            UserSession() = default;

            /**
             * @brief 构造函数
             * @param user_id 用户ID
             * @param device_type 设备类型
             * @param client_ip 客户端IP
             */
            UserSession(const std::string& user_id, const std::string& device_type, const std::string& client_ip);

            /**
             * @brief 检查会话是否有效
             * @return 如果会话有效返回true
             */
            bool isValid() const;

            /**
             * @brief 检查会话是否过期
             * @return 如果会话过期返回true
             */
            bool isExpired() const;

            /**
             * @brief 更新最后活动时间
             */
            void updateActivity();

            /**
             * @brief 转换为JSON对象
             * @param include_tokens 是否包含令牌信息
             * @return JSON对象
             */
            nlohmann::json toJson(bool include_tokens = false) const;

            /**
             * @brief 从JSON对象创建会话
             * @param json JSON对象
             * @return 会话对象
             */
            static UserSession fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 认证结果模型
         * @details 封装认证操作的结果信息
         */
        struct AuthResult {
            bool success = false;                                   ///< 认证是否成功
            std::string message;                                    ///< 结果消息
            std::string error_code;                                 ///< 错误代码
            std::optional<CoreUserInfo> user_info;                  ///< 用户信息（成功时）
            std::optional<UserSession> session;                     ///< 会话信息（成功时）
            std::string access_token;                               ///< 访问令牌
            std::string refresh_token;                              ///< 刷新令牌
            std::chrono::system_clock::time_point token_expires_at; ///< 令牌过期时间
            std::unordered_map<std::string, std::string> metadata;  ///< 额外元数据

            /**
             * @brief 默认构造函数
             */
            AuthResult() = default;

            /**
             * @brief 成功结果构造函数
             * @param user 用户信息
             * @param session 会话信息
             * @param access_token 访问令牌
             * @param refresh_token 刷新令牌
             */
            AuthResult(const CoreUserInfo& user, const UserSession& session,
                      const std::string& access_token, const std::string& refresh_token);

            /**
             * @brief 失败结果构造函数
             * @param error_message 错误消息
             * @param error_code 错误代码
             */
            AuthResult(const std::string& error_message, const std::string& error_code = "");

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 从JSON对象创建认证结果
             * @param json JSON对象
             * @return 认证结果对象
             */
            static AuthResult fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 游戏服务器信息模型
         * @details 存储游戏服务器的状态和配置信息
         */
        struct GameServerInfo {
            std::string server_id;                                  ///< 服务器唯一标识
            std::string server_name;                                ///< 服务器名称
            std::string host;                                       ///< 主机地址
            int port = 0;                                           ///< 端口号
            GameType game_type = GameType::UNKNOWN;                 ///< 支持的游戏类型
            std::string region = "default";                         ///< 服务器区域
            ServerStatus status = ServerStatus::OFFLINE;            ///< 服务器状态
            int current_players = 0;                                ///< 当前玩家数
            int max_players = 100;                                  ///< 最大玩家数
            double cpu_usage = 0.0;                                 ///< CPU使用率（0-100）
            double memory_usage = 0.0;                              ///< 内存使用率（0-100）
            bool is_healthy = true;                                 ///< 健康状态
            bool is_accepting_players = true;                       ///< 是否接受新玩家
            std::chrono::system_clock::time_point last_heartbeat;   ///< 最后心跳时间
            std::chrono::system_clock::time_point created_at;       ///< 创建时间
            std::chrono::system_clock::time_point updated_at;       ///< 更新时间
            nlohmann::json metadata;  ///< 服务器元数据（支持复杂数据类型）

            /**
             * @brief 默认构造函数
             */
            GameServerInfo() = default;

            /**
             * @brief 构造函数
             * @param server_id 服务器ID
             * @param host 主机地址
             * @param port 端口号
             * @param game_type 游戏类型
             */
            GameServerInfo(const std::string& server_id, const std::string& host,
                          int port, GameType game_type);

            /**
             * @brief 检查服务器是否可用
             * @return 如果服务器可用返回true
             */
            bool isAvailable() const;

            /**
             * @brief 计算服务器负载率
             * @return 负载率（0-1）
             */
            double getLoadRatio() const;

            /**
             * @brief 计算服务器评分（用于负载均衡）
             * @return 服务器评分（越高越好）
             */
            double calculateScore() const;

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 从JSON对象创建服务器信息
             * @param json JSON对象
             * @return 服务器信息对象
             */
            static GameServerInfo fromJson(const nlohmann::json& json);
        };

        /**
         * @brief 游戏登录结果模型
         * @details 封装游戏登录操作的结果信息
         */
        struct GameLoginResult {
            bool success = false;                                   ///< 登录是否成功
            std::string message;                                    ///< 结果消息
            std::string error_code;                                 ///< 错误代码
            GameType game_type = GameType::UNKNOWN;                 ///< 游戏类型
            std::optional<GameUserData> game_data;                  ///< 游戏用户数据
            std::string game_server_id;                             ///< 分配的游戏服务器ID
            std::string game_server_host;                           ///< 游戏服务器主机
            int game_server_port = 0;                               ///< 游戏服务器端口
            std::string game_session_token;                         ///< 游戏会话令牌
            std::chrono::system_clock::time_point session_expires_at; ///< 游戏会话过期时间
            std::unordered_map<std::string, std::string> metadata;  ///< 额外元数据

            // === 新增字段以支持前端期望格式 ===
            std::optional<GameServerInfo> server_info;              ///< 完整的服务器信息
            std::optional<CoreUserInfo> user_info;                  ///< 用户信息（用于构建player_info）

            /**
             * @brief 默认构造函数
             */
            GameLoginResult() = default;

            /**
             * @brief 成功结果构造函数
             * @param game_type 游戏类型
             * @param game_data 游戏用户数据
             * @param server_host 服务器主机
             * @param server_port 服务器端口
             */
            GameLoginResult(GameType game_type, const GameUserData& game_data,
                           const std::string& server_host, int server_port);

            /**
             * @brief 增强的成功结果构造函数（支持前端期望格式）
             * @param game_type 游戏类型
             * @param game_data 游戏用户数据
             * @param server_info 完整的服务器信息
             * @param user_info 用户信息
             * @param game_session_token 游戏会话令牌
             */
            GameLoginResult(GameType game_type, const GameUserData& game_data,
                           const GameServerInfo& server_info, const CoreUserInfo& user_info,
                           const std::string& game_session_token);

            /**
             * @brief 失败结果构造函数
             * @param error_message 错误消息
             * @param error_code 错误代码
             */
            GameLoginResult(const std::string& error_message, const std::string& error_code = "");

            /**
             * @brief 转换为JSON对象（传统格式）
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 转换为前端期望的JSON格式
             * @return 前端期望的JSON对象
             */
            nlohmann::json toFrontendJson() const;

            /**
             * @brief 从JSON对象创建游戏登录结果
             * @param json JSON对象
             * @return 游戏登录结果对象
             */
            static GameLoginResult fromJson(const nlohmann::json& json);

        private:
            /**
             * @brief 生成玩家ID
             * @param user_id 用户ID
             * @param timestamp 时间戳
             * @return 玩家ID字符串
             */
            std::string generatePlayerId(const std::string& user_id, int64_t timestamp) const;

            /**
             * @brief 从服务器信息中提取游戏配置
             * @param server_info 服务器信息
             * @return 游戏配置JSON对象
             */
            nlohmann::json extractGameConfig(const GameServerInfo& server_info) const;
        };



        // ==================== 辅助函数 ====================

        /**
         * @brief 游戏类型转换为字符串
         * @param game_type 游戏类型
         * @return 游戏类型字符串
         */
        std::string gameTypeToString(GameType game_type);

        /**
         * @brief 字符串转换为游戏类型
         * @param type_str 游戏类型字符串
         * @return 游戏类型
         */
        GameType stringToGameType(const std::string& type_str);

        /**
         * @brief 用户状态转换为字符串
         * @param status 用户状态
         * @return 状态字符串
         */
        std::string userStatusToString(UserStatus status);

        /**
         * @brief 字符串转换为用户状态
         * @param status_str 状态字符串
         * @return 用户状态
         */
        UserStatus stringToUserStatus(const std::string& status_str);

        /**
         * @brief 在线状态转换为字符串
         * @param status 在线状态
         * @return 状态字符串
         */
        std::string onlineStatusToString(OnlineStatus status);

        /**
         * @brief 服务器状态转换为字符串
         * @param status 服务器状态
         * @return 状态字符串
         */
        std::string serverStatusToString(ServerStatus status);

        /**
         * @brief 字符串转换为服务器状态
         * @param status_str 状态字符串
         * @return 服务器状态
         */
        ServerStatus stringToServerStatus(const std::string& status_str);

        /**
         * @brief 字符串转换为在线状态
         * @param status_str 状态字符串
         * @return 在线状态
         */
        OnlineStatus stringToOnlineStatus(const std::string& status_str);

        /**
         * @brief 生成UUID字符串
         * @return UUID字符串
         */
        std::string generateUUID();

        /**
         * @brief 生成随机字符串
         * @param length 字符串长度
         * @return 随机字符串
         */
        std::string generateRandomString(size_t length);

        /**
         * @brief 验证邮箱格式
         * @param email 邮箱地址
         * @return 如果格式正确返回true
         */
        bool isValidEmail(const std::string& email);

        /**
         * @brief 验证用户名格式
         * @param username 用户名
         * @return 如果格式正确返回true
         */
        bool isValidUsername(const std::string& username);

        /**
         * @brief 获取当前时间戳（毫秒）
         * @return 时间戳
         */
        int64_t getCurrentTimestamp();

        /**
         * @brief 时间点转换为时间戳
         * @param time_point 时间点
         * @return 时间戳（毫秒）
         */
        int64_t timePointToTimestamp(const std::chrono::system_clock::time_point& time_point);

        /**
         * @brief 时间戳转换为时间点
         * @param timestamp 时间戳（毫秒）
         * @return 时间点
         */
        std::chrono::system_clock::time_point timestampToTimePoint(int64_t timestamp);

        /**
         * @brief 时间点转换为MySQL DATETIME格式字符串
         * @param time_point 时间点
         * @return MySQL DATETIME格式字符串 (YYYY-MM-DD HH:MM:SS)
         */
        std::string timePointToMySQLDateTime(const std::chrono::system_clock::time_point& time_point);

    } // namespace auth_service
} // namespace core_services

#endif //USER_MODELS_H
