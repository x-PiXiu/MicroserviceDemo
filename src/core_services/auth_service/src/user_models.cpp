/**
 * @file user_models.cpp
 * @brief 用户数据模型实现 - 认证服务核心数据结构实现
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 */

#include "user_models.h"
#include "common/logger/logger.h"
#include <regex>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "auth_service.h"

namespace core_services {
    namespace auth_service {

        // ==================== CoreUserInfo 实现 ====================

        /**
         * @brief CoreUserInfo构造函数
         * @param username 用户名
         * @param email 邮箱
         * @param password_hash 密码哈希
         * @param salt 密码盐值
         * @param nickname 昵称
         */
        CoreUserInfo::CoreUserInfo(const std::string& username, const std::string& email,
                                  const std::string& password_hash, const std::string& salt,
                                  const std::string& nickname)
            : username(username)
            , email(email)
            , password_hash(password_hash)
            , salt(salt)
            , nickname(nickname.empty() ? username : nickname)
            , status(UserStatus::ACTIVE)
            , online_status(OnlineStatus::OFFLINE)
            , created_at(std::chrono::system_clock::now())
            , updated_at(std::chrono::system_clock::now())
            , login_attempts(0) {
        }

        /**
         * @brief 验证用户信息有效性
         * @return 验证结果，成功返回空字符串，失败返回错误信息
         */
        std::string CoreUserInfo::validate() const {
            // 验证用户名
            if (username.empty()) {
                return "用户名不能为空";
            }
            if (!isValidUsername(username)) {
                return "用户名格式不正确";
            }

            // 验证邮箱
            if (email.empty()) {
                return "邮箱不能为空";
            }
            if (!isValidEmail(email)) {
                return "邮箱格式不正确";
            }

            // 验证密码哈希
            if (password_hash.empty()) {
                return "密码哈希不能为空";
            }

            // 验证盐值（BCrypt情况下盐值可以为空，因为盐值包含在哈希中）
            // 如果使用BCrypt，盐值可以为空；如果使用其他算法，盐值不能为空
            if (salt.empty() && !password_hash.empty() && password_hash.substr(0, 4) != "$2b$" && password_hash.substr(0, 4) != "$2a$" && password_hash.substr(0, 4) != "$2y$") {
                return "密码盐值不能为空";
            }

            // 验证昵称
            if (nickname.empty()) {
                return "昵称不能为空";
            }

            return ""; // 验证通过
        }

        /**
         * @brief 检查用户是否被锁定
         * @return 如果用户被锁定返回true
         */
        bool CoreUserInfo::isLocked() const {
            auto now = std::chrono::system_clock::now();
            return locked_until > now;
        }

        /**
         * @brief 检查用户是否活跃
         * @return 如果用户状态为活跃返回true
         */
        bool CoreUserInfo::isActive() const {
            return status == UserStatus::ACTIVE;
        }

        /**
         * @brief 检查用户是否在线
         * @return 如果用户在线返回true
         */
        bool CoreUserInfo::isOnline() const {
            return online_status == OnlineStatus::ONLINE;
        }

        /**
         * @brief 转换为JSON对象
         * @param include_sensitive 是否包含敏感信息（密码哈希等）
         * @return JSON对象
         */
        nlohmann::json CoreUserInfo::toJson(bool include_sensitive) const {
            nlohmann::json json;

            json["user_id"] = user_id;
            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            json["avatar_url"] = avatar_url;
            json["status"] = static_cast<int>(status);
            json["online_status"] = static_cast<int>(online_status);
            json["created_at"] = timePointToTimestamp(created_at);
            json["updated_at"] = timePointToTimestamp(updated_at);
            json["last_login_at"] = last_login_at.has_value() ? timePointToTimestamp(last_login_at.value()) : 0;
            json["last_login_ip"] = last_login_ip;
            json["login_attempts"] = login_attempts;
            json["locked_until"] = locked_until.has_value() ? timePointToTimestamp(locked_until.value()) : 0;
            json["roles"] = roles;
            json["metadata"] = metadata;

            // 敏感信息只在需要时包含
            if (include_sensitive) {
                json["password_hash"] = password_hash;
                json["salt"] = salt;
            }

            return json;
        }

        /**
         * @brief 从JSON对象创建用户信息
         * @param json JSON对象
         * @return 用户信息对象
         */
        CoreUserInfo CoreUserInfo::fromJson(const nlohmann::json& json) {
            CoreUserInfo user;

            if (json.contains("user_id")) user.user_id = json["user_id"];
            if (json.contains("username")) user.username = json["username"];
            if (json.contains("email")) user.email = json["email"];
            if (json.contains("password_hash")) user.password_hash = json["password_hash"];
            if (json.contains("salt")) user.salt = json["salt"];
            if (json.contains("nickname")) user.nickname = json["nickname"];
            if (json.contains("avatar_url")) user.avatar_url = json["avatar_url"];
            // 🔧 修复：支持字符串和数字类型的状态字段
            if (json.contains("status")) {
                if (json["status"].is_string()) {
                    user.status = stringToUserStatus(json["status"].get<std::string>());
                } else if (json["status"].is_number()) {
                    user.status = static_cast<UserStatus>(json["status"].get<int>());
                }
            }
            if (json.contains("online_status")) {
                if (json["online_status"].is_string()) {
                    user.online_status = stringToOnlineStatus(json["online_status"].get<std::string>());
                } else if (json["online_status"].is_number()) {
                    user.online_status = static_cast<OnlineStatus>(json["online_status"].get<int>());
                }
            }
            if (json.contains("created_at")) user.created_at = timestampToTimePoint(json["created_at"]);
            if (json.contains("updated_at")) user.updated_at = timestampToTimePoint(json["updated_at"]);
            if (json.contains("last_login_at") && json["last_login_at"] != 0) {
                user.last_login_at = timestampToTimePoint(json["last_login_at"]);
            }
            if (json.contains("last_login_ip")) user.last_login_ip = json["last_login_ip"];
            if (json.contains("login_attempts")) user.login_attempts = json["login_attempts"];
            if (json.contains("locked_until") && json["locked_until"] != 0) {
                user.locked_until = timestampToTimePoint(json["locked_until"]);
            }
            if (json.contains("roles")) user.roles = json["roles"].get<std::vector<std::string>>();
            if (json.contains("metadata")) user.metadata = json["metadata"];

            return user;
        }

        // ==================== 字符串转换辅助函数 ====================

        /**
         * @brief 将字符串转换为用户状态枚举
         * @param status_str 状态字符串
         * @return 用户状态枚举
         */
        UserStatus CoreUserInfo::stringToUserStatus(const std::string& status_str) {
            if (status_str == "active") return UserStatus::ACTIVE;
            if (status_str == "inactive") return UserStatus::INACTIVE;
            if (status_str == "suspended") return UserStatus::SUSPENDED;
            if (status_str == "banned") return UserStatus::BANNED;
            if (status_str == "deleted") return UserStatus::DELETED;
            // 默认返回活跃状态
            return UserStatus::ACTIVE;
        }

        /**
         * @brief 将字符串转换为在线状态枚举
         * @param status_str 在线状态字符串
         * @return 在线状态枚举
         */
        OnlineStatus CoreUserInfo::stringToOnlineStatus(const std::string& status_str) {
            if (status_str == "offline") return OnlineStatus::OFFLINE;
            if (status_str == "online") return OnlineStatus::ONLINE;
            if (status_str == "away") return OnlineStatus::AWAY;
            if (status_str == "busy") return OnlineStatus::BUSY;
            if (status_str == "invisible") return OnlineStatus::INVISIBLE;
            // 默认返回离线状态
            return OnlineStatus::OFFLINE;
        }

        // ==================== GameUserData 实现 ====================

        /**
         * @brief GameUserData构造函数
         * @param user_id 用户ID
         * @param game_type 游戏类型
         */
        GameUserData::GameUserData(int64_t user_id, GameType game_type)
            : user_id(user_id)
            , game_type(game_type)
            , level(1)
            , experience(0)
            , coins(1000)  // 默认给用户1000游戏币
            , gems(10)     // 默认给用户10宝石
            , total_games(0)
            , wins(0)
            , losses(0)
            , draws(0)
            , total_playtime_seconds(0)
            , best_score(0)
            , last_played_at(std::chrono::system_clock::now())  // 设置为当前时间而非默认时间
            , created_at(std::chrono::system_clock::now())
            , updated_at(std::chrono::system_clock::now()) {
        }

        /**
         * @brief 计算胜率
         * @return 胜率百分比（0-100）
         */
        double GameUserData::getWinRate() const {
            if (total_games == 0) {
                return 0.0;
            }
            return (static_cast<double>(wins) / total_games) * 100.0;
        }

        /**
         * @brief 计算平均游戏时长
         * @return 平均游戏时长（秒）
         */
        double GameUserData::getAverageGameDuration() const {
            if (total_games == 0) {
                return 0.0;
            }
            return static_cast<double>(total_playtime_seconds) / total_games;
        }

        /**
         * @brief 添加游戏结果
         * @param won 是否胜利
         * @param score 得分
         * @param duration_seconds 游戏时长（秒）
         */
        void GameUserData::addGameResult(bool won, int64_t score, int64_t duration_seconds) {
            total_games++;
            total_playtime_seconds += duration_seconds;

            if (won) {
                wins++;
            } else {
                losses++;
            }

            if (score > best_score) {
                best_score = score;
            }

            // 更新经验值（简单算法：每局游戏获得10经验，胜利额外获得20经验）
            experience += 10;
            if (won) {
                experience += 20;
            }

            // 更新等级（每1000经验升一级）
            int new_level = static_cast<int>(experience / 1000) + 1;
            if (new_level > level) {
                level = new_level;
                // 升级奖励
                coins += 100 * level;
                gems += 10;
            }

            last_played_at = std::chrono::system_clock::now();
            updated_at = std::chrono::system_clock::now();
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json GameUserData::toJson() const {
            nlohmann::json json;

            json["user_id"] = user_id;
            json["game_type"] = static_cast<int>(game_type);
            json["level"] = level;
            json["experience"] = experience;
            json["coins"] = coins;
            json["gems"] = gems;
            json["total_games"] = total_games;
            json["wins"] = wins;
            json["losses"] = losses;
            json["draws"] = draws;
            json["total_playtime_seconds"] = total_playtime_seconds;
            json["best_score"] = best_score;
            json["last_played_at"] = timePointToTimestamp(last_played_at);
            json["created_at"] = timePointToTimestamp(created_at);
            json["updated_at"] = timePointToTimestamp(updated_at);
            json["game_data"] = game_data;
            json["achievements"] = achievements;
            json["win_rate"] = getWinRate();
            json["average_game_duration"] = getAverageGameDuration();

            return json;
        }

        /**
         * @brief 从JSON对象创建游戏用户数据
         * @param json JSON对象
         * @return 游戏用户数据对象
         */
        GameUserData GameUserData::fromJson(const nlohmann::json& json) {
            GameUserData data;

            if (json.contains("user_id")) data.user_id = json["user_id"];
            if (json.contains("game_type")) data.game_type = static_cast<GameType>(json["game_type"]);
            if (json.contains("level")) data.level = json["level"];
            if (json.contains("experience")) data.experience = json["experience"];
            if (json.contains("coins")) data.coins = json["coins"];
            if (json.contains("gems")) data.gems = json["gems"];
            if (json.contains("total_games")) data.total_games = json["total_games"];
            if (json.contains("wins")) data.wins = json["wins"];
            if (json.contains("losses")) data.losses = json["losses"];
            if (json.contains("draws")) data.draws = json["draws"];
            if (json.contains("total_playtime_seconds")) data.total_playtime_seconds = json["total_playtime_seconds"];
            if (json.contains("best_score")) data.best_score = json["best_score"];
            if (json.contains("last_played_at")) data.last_played_at = timestampToTimePoint(json["last_played_at"]);
            if (json.contains("created_at")) data.created_at = timestampToTimePoint(json["created_at"]);
            if (json.contains("updated_at")) data.updated_at = timestampToTimePoint(json["updated_at"]);
            if (json.contains("game_data")) data.game_data = json["game_data"];
            if (json.contains("achievements")) data.achievements = json["achievements"].get<std::vector<std::string>>();

            return data;
        }

        // ==================== UserSession 实现 ====================

        /**
         * @brief UserSession构造函数
         * @param user_id 用户ID
         * @param device_type 设备类型
         * @param client_ip 客户端IP
         */
        UserSession::UserSession(const std::string& user_id, const std::string& device_type, const std::string& client_ip)
            : session_id(generateUUID())
            , user_id(user_id)
            , device_type(device_type)
            , client_ip(client_ip)
            , created_at(std::chrono::system_clock::now())
            , last_activity_at(std::chrono::system_clock::now())
            , expires_at(std::chrono::system_clock::now() + std::chrono::hours(24)) // 默认24小时过期
            , is_active(true) {
        }

        /**
         * @brief 检查会话是否有效
         * @return 如果会话有效返回true
         */
        bool UserSession::isValid() const {
            return is_active && !isExpired();
        }

        /**
         * @brief 检查会话是否过期
         * @return 如果会话过期返回true
         */
        bool UserSession::isExpired() const {
            auto now = std::chrono::system_clock::now();
            return expires_at <= now;
        }

        /**
         * @brief 更新最后活动时间
         */
        void UserSession::updateActivity() {
            last_activity_at = std::chrono::system_clock::now();
        }

        /**
         * @brief 转换为JSON对象
         * @param include_tokens 是否包含令牌信息
         * @return JSON对象
         */
        nlohmann::json UserSession::toJson(bool include_tokens) const {
            nlohmann::json json;

            json["session_id"] = session_id;
            json["user_id"] = user_id;
            json["device_type"] = device_type;
            json["device_id"] = device_id;
            json["client_ip"] = client_ip;
            json["user_agent"] = user_agent;
            json["created_at"] = timePointToTimestamp(created_at);
            json["last_activity_at"] = timePointToTimestamp(last_activity_at);
            json["expires_at"] = timePointToTimestamp(expires_at);
            json["is_active"] = is_active;
            json["metadata"] = metadata;

            // 令牌信息只在需要时包含
            if (include_tokens) {
                json["access_token"] = access_token;
                json["refresh_token"] = refresh_token;
            }

            return json;
        }

        /**
         * @brief 从JSON对象创建会话
         * @param json JSON对象
         * @return 会话对象
         */
        UserSession UserSession::fromJson(const nlohmann::json& json) {
            UserSession session;

            if (json.contains("session_id")) session.session_id = json["session_id"];
            if (json.contains("user_id")) session.user_id = json["user_id"];
            if (json.contains("access_token")) session.access_token = json["access_token"];
            if (json.contains("refresh_token")) session.refresh_token = json["refresh_token"];
            if (json.contains("device_type")) session.device_type = json["device_type"];
            if (json.contains("device_id")) session.device_id = json["device_id"];
            if (json.contains("client_ip")) session.client_ip = json["client_ip"];
            if (json.contains("user_agent")) session.user_agent = json["user_agent"];
            if (json.contains("created_at")) session.created_at = timestampToTimePoint(json["created_at"]);
            if (json.contains("last_activity_at")) session.last_activity_at = timestampToTimePoint(json["last_activity_at"]);
            if (json.contains("expires_at")) session.expires_at = timestampToTimePoint(json["expires_at"]);
            if (json.contains("is_active")) session.is_active = json["is_active"];
            if (json.contains("metadata")) session.metadata = json["metadata"];

            return session;
        }

        // ==================== AuthResult 实现 ====================

        /**
         * @brief AuthResult成功结果构造函数
         * @param user 用户信息
         * @param session 会话信息
         * @param access_token 访问令牌
         * @param refresh_token 刷新令牌
         */
        AuthResult::AuthResult(const CoreUserInfo& user, const UserSession& session,
                              const std::string& access_token, const std::string& refresh_token)
            : success(true)
            , message("认证成功")
            , user_info(user)
            , session(session)
            , access_token(access_token)
            , refresh_token(refresh_token)
            , token_expires_at(std::chrono::system_clock::now() + std::chrono::hours(1)) {
        }

        /**
         * @brief AuthResult失败结果构造函数
         * @param error_message 错误消息
         * @param error_code 错误代码
         */
        AuthResult::AuthResult(const std::string& error_message, const std::string& error_code)
            : success(false)
            , message(error_message)
            , error_code(error_code) {
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json AuthResult::toJson() const {
            nlohmann::json json;

            json["success"] = success;
            json["message"] = message;
            json["error_code"] = error_code;
            json["access_token"] = access_token;
            json["refresh_token"] = refresh_token;
            json["token_expires_at"] = timePointToTimestamp(token_expires_at);
            json["metadata"] = metadata;

            if (user_info.has_value()) {
                json["user_info"] = user_info->toJson(false); // 不包含敏感信息
            }

            if (session.has_value()) {
                json["session"] = session->toJson(false); // 不包含令牌信息
            }

            return json;
        }

        /**
         * @brief 从JSON对象创建认证结果
         * @param json JSON对象
         * @return 认证结果对象
         */
        AuthResult AuthResult::fromJson(const nlohmann::json& json) {
            AuthResult result;

            if (json.contains("success")) result.success = json["success"];
            if (json.contains("message")) result.message = json["message"];
            if (json.contains("error_code")) result.error_code = json["error_code"];
            if (json.contains("access_token")) result.access_token = json["access_token"];
            if (json.contains("refresh_token")) result.refresh_token = json["refresh_token"];
            if (json.contains("token_expires_at")) result.token_expires_at = timestampToTimePoint(json["token_expires_at"]);
            if (json.contains("metadata")) result.metadata = json["metadata"];

            if (json.contains("user_info")) {
                result.user_info = CoreUserInfo::fromJson(json["user_info"]);
            }

            if (json.contains("session")) {
                result.session = UserSession::fromJson(json["session"]);
            }

            return result;
        }

        // ==================== GameLoginResult 实现 ====================

        /**
         * @brief GameLoginResult成功结果构造函数
         * @param game_type 游戏类型
         * @param game_data 游戏用户数据
         * @param server_host 服务器主机
         * @param server_port 服务器端口
         */
        GameLoginResult::GameLoginResult(GameType game_type, const GameUserData& game_data,
                                        const std::string& server_host, int server_port)
            : success(true)
            , message("游戏登录成功")
            , game_type(game_type)
            , game_data(game_data)
            , game_server_host(server_host)
            , game_server_port(server_port)
            , game_session_token(generateUUID())
            , session_expires_at(std::chrono::system_clock::now() + std::chrono::hours(2)) { // 游戏会话2小时过期
        }



        /**
         * @brief GameLoginResult失败结果构造函数
         * @param error_message 错误消息
         * @param error_code 错误代码
         */
        GameLoginResult::GameLoginResult(const std::string& error_message, const std::string& error_code)
            : success(false)
            , message(error_message)
            , error_code(error_code) {
        }

        /**
         * @brief 转换为JSON对象（传统格式）
         * @return JSON对象
         */
        nlohmann::json GameLoginResult::toJson() const {
            nlohmann::json json;

            json["success"] = success;
            json["message"] = message;
            json["error_code"] = error_code;
            json["game_type"] = static_cast<int>(game_type);
            json["game_server_id"] = game_server_id;
            json["game_server_host"] = game_server_host;
            json["game_server_port"] = game_server_port;
            json["game_session_token"] = game_session_token;
            json["session_expires_at"] = timePointToTimestamp(session_expires_at);
            json["metadata"] = metadata;

            if (game_data.has_value()) {
                json["game_data"] = game_data->toJson();
            }

            return json;
        }



        /**
         * @brief 从JSON对象创建游戏登录结果
         * @param json JSON对象
         * @return 游戏登录结果对象
         */
        GameLoginResult GameLoginResult::fromJson(const nlohmann::json& json) {
            GameLoginResult result;

            if (json.contains("success")) result.success = json["success"];
            if (json.contains("message")) result.message = json["message"];
            if (json.contains("error_code")) result.error_code = json["error_code"];
            if (json.contains("game_type")) result.game_type = static_cast<GameType>(json["game_type"]);
            if (json.contains("game_server_id")) result.game_server_id = json["game_server_id"];
            if (json.contains("game_server_host")) result.game_server_host = json["game_server_host"];
            if (json.contains("game_server_port")) result.game_server_port = json["game_server_port"];
            if (json.contains("game_session_token")) result.game_session_token = json["game_session_token"];
            if (json.contains("session_expires_at")) result.session_expires_at = timestampToTimePoint(json["session_expires_at"]);
            if (json.contains("metadata")) result.metadata = json["metadata"];

            if (json.contains("game_data")) {
                result.game_data = GameUserData::fromJson(json["game_data"]);
            }

            return result;
        }



        // ==================== GameServerInfo 实现 ====================

        /**
         * @brief GameServerInfo构造函数
         * @param server_id 服务器ID
         * @param host 主机地址
         * @param port 端口号
         * @param game_type 游戏类型
         */
        GameServerInfo::GameServerInfo(const std::string& server_id, const std::string& host,
                                      int port, GameType game_type)
            : server_id(server_id)
            , server_name(server_id)
            , host(host)
            , port(port)
            , game_type(game_type)
            , region("default")
            , status(ServerStatus::ONLINE)
            , current_players(0)
            , max_players(100)
            , cpu_usage(0.0)
            , memory_usage(0.0)
            , is_healthy(true)
            , is_accepting_players(true)
            , last_heartbeat(std::chrono::system_clock::now())
            , created_at(std::chrono::system_clock::now())
            , updated_at(std::chrono::system_clock::now()) {
        }

        /**
         * @brief 检查服务器是否可用
         * @return 如果服务器可用返回true
         */
        bool GameServerInfo::isAvailable() const {
            // 检查健康状态
            if (!is_healthy || !is_accepting_players) {
                return false;
            }

            // 检查是否已满
            if (current_players >= max_players) {
                return false;
            }

            // 检查心跳时间（超过5分钟没有心跳认为不可用）
            auto now = std::chrono::system_clock::now();
            auto heartbeat_timeout = std::chrono::minutes(5);
            if (now - last_heartbeat > heartbeat_timeout) {
                return false;
            }

            return true;
        }

        /**
         * @brief 计算服务器负载率
         * @return 负载率（0-1）
         */
        double GameServerInfo::getLoadRatio() const {
            if (max_players == 0) {
                return 1.0; // 避免除零错误
            }
            return static_cast<double>(current_players) / max_players;
        }

        /**
         * @brief 计算服务器评分（用于负载均衡）
         * @return 服务器评分（越高越好）
         */
        double GameServerInfo::calculateScore() const {
            if (!isAvailable()) {
                return 0.0;
            }

            double score = 100.0; // 基础分数

            // 负载因子（权重：40%）
            double load_factor = getLoadRatio();
            score -= load_factor * 40.0;

            // CPU使用率因子（权重：25%）
            score -= (cpu_usage / 100.0) * 25.0;

            // 内存使用率因子（权重：25%）
            score -= (memory_usage / 100.0) * 25.0;

            // 心跳新鲜度因子（权重：10%）
            auto now = std::chrono::system_clock::now();
            auto heartbeat_age = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count();
            double heartbeat_factor = std::min(1.0, heartbeat_age / 300.0); // 5分钟内为最佳
            score -= heartbeat_factor * 10.0;

            return std::max(0.0, score);
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json GameServerInfo::toJson() const {
            nlohmann::json json;

            json["server_id"] = server_id;
            json["server_name"] = server_name;
            json["host"] = host;
            json["port"] = port;
            json["game_type"] = static_cast<int>(game_type);
            json["region"] = region;
            json["status"] = serverStatusToString(status);
            json["current_players"] = current_players;
            json["max_players"] = max_players;
            json["cpu_usage"] = cpu_usage;
            json["memory_usage"] = memory_usage;
            json["is_healthy"] = is_healthy;
            json["is_accepting_players"] = is_accepting_players;
            json["last_heartbeat"] = timePointToTimestamp(last_heartbeat);
            json["created_at"] = timePointToTimestamp(created_at);
            json["updated_at"] = timePointToTimestamp(updated_at);
            json["metadata"] = metadata;
            json["load_ratio"] = getLoadRatio();
            json["score"] = calculateScore();
            json["is_available"] = isAvailable();

            return json;
        }

        /**
         * @brief 从JSON对象创建服务器信息
         * @param json JSON对象
         * @return 服务器信息对象
         */
        GameServerInfo GameServerInfo::fromJson(const nlohmann::json& json) {
            GameServerInfo info;

            if (json.contains("server_id")) info.server_id = json["server_id"];
            if (json.contains("server_name")) info.server_name = json["server_name"];
            if (json.contains("host")) info.host = json["host"];
            if (json.contains("port")) info.port = json["port"];
            if (json.contains("game_type")) info.game_type = static_cast<GameType>(json["game_type"]);
            if (json.contains("region")) info.region = json["region"];
            if (json.contains("status")) info.status = stringToServerStatus(json["status"]);
            if (json.contains("current_players")) info.current_players = json["current_players"];
            if (json.contains("max_players")) info.max_players = json["max_players"];
            if (json.contains("cpu_usage")) info.cpu_usage = json["cpu_usage"];
            if (json.contains("memory_usage")) info.memory_usage = json["memory_usage"];
            if (json.contains("is_healthy")) info.is_healthy = json["is_healthy"];
            if (json.contains("is_accepting_players")) info.is_accepting_players = json["is_accepting_players"];
            if (json.contains("last_heartbeat")) info.last_heartbeat = timestampToTimePoint(json["last_heartbeat"]);
            if (json.contains("created_at")) info.created_at = timestampToTimePoint(json["created_at"]);
            if (json.contains("updated_at")) info.updated_at = timestampToTimePoint(json["updated_at"]);
            if (json.contains("metadata")) info.metadata = json["metadata"];

            return info;
        }

        // ==================== 辅助函数实现 ====================

        /**
         * @brief 游戏类型转换为字符串
         * @param game_type 游戏类型
         * @return 游戏类型字符串
         */
        std::string gameTypeToString(GameType game_type) {
            switch (game_type) {
                case GameType::SNAKE: return "snake";
                case GameType::TETRIS: return "tetris";
                case GameType::CHESS: return "chess";
                case GameType::POKER: return "poker";
                case GameType::RPG: return "rpg";
                case GameType::MOBA: return "moba";
                case GameType::FPS: return "fps";
                case GameType::STRATEGY: return "strategy";
                default: return "unknown";
            }
        }

        /**
         * @brief 字符串转换为游戏类型
         * @param type_str 游戏类型字符串
         * @return 游戏类型
         */
        GameType stringToGameType(const std::string& type_str) {
            std::string lower_str = type_str;
            std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);

            if (lower_str == "snake") return GameType::SNAKE;
            if (lower_str == "tetris") return GameType::TETRIS;
            if (lower_str == "chess") return GameType::CHESS;
            if (lower_str == "poker") return GameType::POKER;
            if (lower_str == "rpg") return GameType::RPG;
            if (lower_str == "moba") return GameType::MOBA;
            if (lower_str == "fps") return GameType::FPS;
            if (lower_str == "strategy") return GameType::STRATEGY;

            return GameType::UNKNOWN;
        }

        /**
         * @brief 用户状态转换为字符串
         * @param status 用户状态
         * @return 状态字符串
         */
        std::string userStatusToString(UserStatus status) {
            switch (status) {
                case UserStatus::INACTIVE: return "inactive";
                case UserStatus::ACTIVE: return "active";
                case UserStatus::SUSPENDED: return "suspended";
                case UserStatus::BANNED: return "banned";
                case UserStatus::DELETED: return "deleted";
                default: return "unknown";
            }
        }

        /**
         * @brief 字符串转换为用户状态
         * @param status_str 状态字符串
         * @return 用户状态
         */
        UserStatus stringToUserStatus(const std::string& status_str) {
            std::string lower_str = status_str;
            std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);

            if (lower_str == "inactive") return UserStatus::INACTIVE;
            if (lower_str == "active") return UserStatus::ACTIVE;
            if (lower_str == "suspended") return UserStatus::SUSPENDED;
            if (lower_str == "banned") return UserStatus::BANNED;
            if (lower_str == "deleted") return UserStatus::DELETED;

            return UserStatus::INACTIVE;
        }

        /**
         * @brief 在线状态转换为字符串
         * @param status 在线状态
         * @return 状态字符串
         */
        std::string onlineStatusToString(OnlineStatus status) {
            switch (status) {
                case OnlineStatus::OFFLINE: return "offline";
                case OnlineStatus::ONLINE: return "online";
                case OnlineStatus::AWAY: return "away";
                case OnlineStatus::BUSY: return "busy";
                case OnlineStatus::INVISIBLE: return "invisible";
                default: return "offline";
            }
        }

        std::string serverStatusToString(ServerStatus status) {
            switch (status) {
                case ServerStatus::OFFLINE: return "offline";
                case ServerStatus::ONLINE: return "online";
                case ServerStatus::MAINTENANCE: return "maintenance";
                case ServerStatus::OVERLOADED: return "overloaded";
                default: return "offline";
            }
        }

        ServerStatus stringToServerStatus(const std::string& status_str) {
            if (status_str == "online") return ServerStatus::ONLINE;
            if (status_str == "offline") return ServerStatus::OFFLINE;
            if (status_str == "maintenance") return ServerStatus::MAINTENANCE;
            if (status_str == "overloaded") return ServerStatus::OVERLOADED;
            return ServerStatus::OFFLINE; // 默认为离线
        }

        /**
         * @brief 字符串转换为在线状态
         * @param status_str 状态字符串
         * @return 在线状态
         */
        OnlineStatus stringToOnlineStatus(const std::string& status_str) {
            std::string lower_str = status_str;
            std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);

            if (lower_str == "offline") return OnlineStatus::OFFLINE;
            if (lower_str == "online") return OnlineStatus::ONLINE;
            if (lower_str == "away") return OnlineStatus::AWAY;
            if (lower_str == "busy") return OnlineStatus::BUSY;
            if (lower_str == "invisible") return OnlineStatus::INVISIBLE;

            return OnlineStatus::OFFLINE;
        }

        /**
         * @brief 生成UUID字符串
         * @return UUID字符串
         */
        std::string generateUUID() {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(0, 15);
            static std::uniform_int_distribution<> dis2(8, 11);

            std::stringstream ss;
            int i;
            ss << std::hex;
            for (i = 0; i < 8; i++) {
                ss << dis(gen);
            }
            ss << "-";
            for (i = 0; i < 4; i++) {
                ss << dis(gen);
            }
            ss << "-4";
            for (i = 0; i < 3; i++) {
                ss << dis(gen);
            }
            ss << "-";
            ss << dis2(gen);
            for (i = 0; i < 3; i++) {
                ss << dis(gen);
            }
            ss << "-";
            for (i = 0; i < 12; i++) {
                ss << dis(gen);
            }
            return ss.str();
        }

        /**
         * @brief 生成随机字符串
         * @param length 字符串长度
         * @return 随机字符串
         */
        std::string generateRandomString(size_t length) {
            static const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(0, charset.size() - 1);

            std::string result;
            result.reserve(length);
            for (size_t i = 0; i < length; ++i) {
                result += charset[dis(gen)];
            }
            return result;
        }

        /**
         * @brief 验证邮箱格式
         * @param email 邮箱地址
         * @return 如果格式正确返回true
         */
        bool isValidEmail(const std::string& email) {
            // 简单的邮箱格式验证正则表达式
            static const std::regex email_regex(
                R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)"
            );
            return std::regex_match(email, email_regex);
        }

        /**
         * @brief 验证用户名格式
         * @param username 用户名
         * @return 如果格式正确返回true
         */
        bool isValidUsername(const std::string& username) {
            // 用户名规则：3-20个字符，只能包含字母、数字、下划线
            if (username.length() < 3 || username.length() > 20) {
                return false;
            }

            static const std::regex username_regex(R"(^[a-zA-Z0-9_]+$)");
            return std::regex_match(username, username_regex);
        }

        /**
         * @brief 获取当前时间戳（毫秒）
         * @return 时间戳
         */
        int64_t getCurrentTimestamp() {
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        }

        /**
         * @brief 时间点转换为时间戳
         * @param time_point 时间点
         * @return 时间戳（毫秒）
         */
        int64_t timePointToTimestamp(const std::chrono::system_clock::time_point& time_point) {
            auto duration = time_point.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        }

        /**
         * @brief 时间戳转换为时间点
         * @param timestamp 时间戳（毫秒）
         * @return 时间点
         */
        std::chrono::system_clock::time_point timestampToTimePoint(int64_t timestamp) {
            auto duration = std::chrono::milliseconds(timestamp);
            return std::chrono::system_clock::time_point(duration);
        }

        /**
         * @brief 时间点转换为MySQL DATETIME格式字符串
         * @param time_point 时间点
         * @return MySQL DATETIME格式字符串 (YYYY-MM-DD HH:MM:SS)
         */
        std::string timePointToMySQLDateTime(const std::chrono::system_clock::time_point& time_point) {
            auto time_t = std::chrono::system_clock::to_time_t(time_point);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                time_point.time_since_epoch()) % 1000;

            std::tm local_tm;
            localtime_r(&time_t, &local_tm);

            std::ostringstream oss;
            oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");

            // 如果需要毫秒精度，可以添加：
            // oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

            return oss.str();
        }

        // ==================== AuthRequest 实现 ====================

        /**
         * @brief 从JSON创建认证请求
         * @param json JSON对象
         * @return 认证请求对象
         */
        AuthRequest AuthRequest::fromJson(const nlohmann::json& json) {
            AuthRequest request;

            if (json.contains("username")) request.username = json["username"];
            if (json.contains("email")) request.email = json["email"];
            if (json.contains("password")) request.password = json["password"];
            if (json.contains("nickname")) request.nickname = json["nickname"];
            if (json.contains("client_ip")) request.client_ip = json["client_ip"];
            if (json.contains("user_agent")) request.user_agent = json["user_agent"];
            if (json.contains("device_id")) request.device_id = json["device_id"];
            if (json.contains("device_type")) request.device_type = json["device_type"];
            if (json.contains("metadata")) request.metadata = json["metadata"];

            return request;
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json AuthRequest::toJson() const {
            nlohmann::json json;

            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            // 注意：不包含密码字段以确保安全
            json["client_ip"] = client_ip;
            json["user_agent"] = user_agent;
            json["device_id"] = device_id;
            json["device_type"] = device_type;
            json["metadata"] = metadata;

            return json;
        }

        /**
         * @brief 验证请求有效性
         * @return 验证结果，成功返回空字符串
         */
        std::string AuthRequest::validate() const {
            if (username.empty() && email.empty()) {
                return "用户名或邮箱不能为空";
            }

            if (password.empty()) {
                return "密码不能为空";
            }

            if (!username.empty() && !isValidUsername(username)) {
                return "用户名格式不正确";
            }

            if (!email.empty() && !isValidEmail(email)) {
                return "邮箱格式不正确";
            }

            return ""; // 验证通过
        }

        /**
         * @brief 验证用户名格式
         * @param username 用户名
         * @return 格式正确返回true
         */
        bool AuthRequest::isValidUsername(const std::string& username) {
            return ::core_services::auth_service::isValidUsername(username);
        }

        /**
         * @brief 验证邮箱格式
         * @param email 邮箱地址
         * @return 格式正确返回true
         */
        bool AuthRequest::isValidEmail(const std::string& email) {
            return ::core_services::auth_service::isValidEmail(email);
        }

        // ==================== GameLoginResult 增强方法实现 ====================

        /**
         * @brief GameLoginResult增强的成功结果构造函数（支持前端期望格式）
         * @param game_type 游戏类型
         * @param game_data 游戏用户数据
         * @param server_info 完整的服务器信息
         * @param user_info 用户信息
         * @param game_session_token 游戏会话令牌
         */
        GameLoginResult::GameLoginResult(GameType game_type, const GameUserData& game_data,
                                        const GameServerInfo& server_info, const CoreUserInfo& user_info,
                                        const std::string& game_session_token)
            : success(true)
            , message("Game authentication successful")
            , game_type(game_type)
            , game_data(game_data)
            , game_server_id(server_info.server_id)
            , game_server_host(server_info.host)
            , game_server_port(server_info.port)
            , game_session_token(game_session_token)
            , session_expires_at(std::chrono::system_clock::now() + std::chrono::hours(2))
            , server_info(server_info)
            , user_info(user_info) {
        }

        /**
         * @brief 转换为前端期望的JSON格式
         * @return 前端期望的JSON对象
         */
        nlohmann::json GameLoginResult::toFrontendJson() const {
            nlohmann::json json;

            // 基础响应信息
            json["success"] = success;
            json["message"] = message;

            if (!success) {
                json["error_code"] = error_code;
                return json;
            }

            // 会话令牌
            json["session_token"] = game_session_token;

            // 服务器信息
            if (server_info.has_value()) {
                const auto& server = server_info.value();
                json["server_info"] = {
                    {"server_id", server.server_id},
                    {"name", server.server_name},
                    {"game_type", gameTypeToString(server.game_type)},
                    {"host", server.host},
                    {"port", server.port},
                    {"region", server.region},
                    {"version", "1.2.0"}
                };

                // 设置websocket_port，优先从metadata获取，否则使用默认值
                if (server.metadata.contains("websocket_port")) {
                    json["server_info"]["websocket_port"] = server.metadata.at("websocket_port");
                } else {
                    // 默认websocket端口为HTTP端口+1
                    json["server_info"]["websocket_port"] = server.port + 1;
                }

                // 从metadata中提取其他额外信息
                if (server.metadata.contains("display_name")) {
                    json["server_info"]["name"] = server.metadata.at("display_name");
                }
                if (server.metadata.contains("version")) {
                    json["server_info"]["version"] = server.metadata.at("version");
                }
            } else {
                // 回退到基础服务器信息
                json["server_info"] = {
                    {"server_id", game_server_id},
                    {"name", "Game Server"},
                    {"game_type", gameTypeToString(game_type)},
                    {"host", game_server_host},
                    {"port", game_server_port},
                    {"websocket_port", game_server_port + 1},
                    {"region", "default"},
                    {"version", "1.0.0"}
                };
            }

            // 玩家信息
            if (user_info.has_value()) {
                const auto& user = user_info.value();
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                json["player_info"] = {
                    {"player_id", generatePlayerId(user.user_id, timestamp)},
                    {"nickname", user.nickname.empty() ? user.username : user.nickname},
                    {"user_id", user.user_id}
                };
            }

            // 游戏配置
            if (server_info.has_value()) {
                json["game_config"] = extractGameConfig(server_info.value());
            } else {
                // 默认游戏配置
                json["game_config"] = {
                    {"max_players_per_room", 4},
                    {"default_game_speed", 100},
                    {"supported_game_modes", nlohmann::json::array({"classic", "battle", "survival"})}
                };
            }

            return json;
        }


        /**
         * @brief 生成玩家ID
         * @param user_id 用户ID
         * @param timestamp 时间戳
         * @return 玩家ID字符串
         */
        std::string GameLoginResult::generatePlayerId(const std::string& user_id, int64_t timestamp) const {
            return user_id + "_" + std::to_string(timestamp);
        }

        /**
         * @brief 从服务器信息中提取游戏配置
         * @param server_info 服务器信息
         * @return 游戏配置JSON对象
         */
        nlohmann::json GameLoginResult::extractGameConfig(const GameServerInfo& server_info) const {
            nlohmann::json game_config;

            // 默认值
            game_config["max_players_per_room"] = 4;
            game_config["default_game_speed"] = 100;
            game_config["supported_game_modes"] = nlohmann::json::array({"classic", "battle", "survival"});

            // 从metadata中提取配置信息
            if (server_info.metadata.contains("game_config")) {
                const auto& config = server_info.metadata.at("game_config");

                if (config.contains("max_players_per_room")) {
                    game_config["max_players_per_room"] = config["max_players_per_room"];
                }
                if (config.contains("default_game_speed")) {
                    game_config["default_game_speed"] = config["default_game_speed"];
                }
                if (config.contains("supported_game_modes")) {
                    game_config["supported_game_modes"] = config["supported_game_modes"];
                }
            } else {
                // 从单独的metadata字段中提取
                if (server_info.metadata.contains("max_players_per_room")) {
                    game_config["max_players_per_room"] = server_info.metadata.at("max_players_per_room");
                }
                if (server_info.metadata.contains("default_game_speed")) {
                    game_config["default_game_speed"] = server_info.metadata.at("default_game_speed");
                }
                if (server_info.metadata.contains("supported_game_modes")) {
                    game_config["supported_game_modes"] = server_info.metadata.at("supported_game_modes");
                }
            }

            return game_config;
        }




    } // namespace auth_service
} // namespace core_services

// ==================== RegisterRequest 实现 ====================

/**
 * @brief 验证注册请求有效性
 * @return 验证结果，成功返回空字符串，失败返回错误信息
 */
std::string core_services::auth_service::RegisterRequest::validate() const {
    // 验证用户名
    if (username.empty()) {
        return "用户名不能为空";
    }

    if (username.length() < 3) {
        return "用户名长度至少3个字符";
    }

    if (username.length() > 50) {
        return "用户名长度不能超过50个字符";
    }

    // 验证用户名格式（只允许字母、数字、下划线）
    std::regex username_regex("^[a-zA-Z0-9_]+$");
    if (!std::regex_match(username, username_regex)) {
        return "用户名只能包含字母、数字和下划线";
    }

    // 验证邮箱
    if (email.empty()) {
        return "邮箱不能为空";
    }

    std::regex email_regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
    if (!std::regex_match(email, email_regex)) {
        return "邮箱格式不正确";
    }

    // 验证密码
    if (password.empty()) {
        return "密码不能为空";
    }

    if (password.length() < 6) {
        return "密码长度至少6个字符";
    }

    if (password.length() > 128) {
        return "密码长度不能超过128个字符";
    }

    // 验证昵称（可选）
    if (!nickname.empty()) {
        if (nickname.length() > 50) {
            return "昵称长度不能超过50个字符";
        }
    }

    // 验证客户端IP（可选）
    if (!client_ip.empty()) {
        // 简单的IP格式验证
        std::regex ipv4_regex("^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
        std::regex ipv6_regex("^(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$");

        if (!std::regex_match(client_ip, ipv4_regex) && !std::regex_match(client_ip, ipv6_regex)) {
            // 允许localhost等特殊地址
            if (client_ip != "localhost" && client_ip != "127.0.0.1" && client_ip != "::1") {
                return "客户端IP格式不正确";
            }
        }
    }

    // 验证设备类型（可选）
    if (!device_type.empty()) {
        if (device_type.length() > 20) {
            return "设备类型长度不能超过20个字符";
        }
    }

    // 验证设备ID（可选）
    if (!device_id.empty()) {
        if (device_id.length() > 100) {
            return "设备ID长度不能超过100个字符";
        }
    }

    return ""; // 验证通过
}

/**
 * @brief 转换为JSON
 * @return JSON对象
 */
nlohmann::json core_services::auth_service::RegisterRequest::toJson() const {
    nlohmann::json json;
    json["username"] = username;
    json["email"] = email;
    // 注意：不包含密码字段，出于安全考虑
    json["nickname"] = nickname;
    json["client_ip"] = client_ip;
    json["device_type"] = device_type;
    json["device_id"] = device_id;
    return json;
}

/**
 * @brief 从JSON创建注册请求对象
 * @param json JSON对象
 * @return 注册请求对象
 */
core_services::auth_service::RegisterRequest core_services::auth_service::RegisterRequest::fromJson(const nlohmann::json& json) {
    RegisterRequest request;

    if (json.contains("username") && json["username"].is_string()) {
        request.username = json["username"];
    }

    if (json.contains("email") && json["email"].is_string()) {
        request.email = json["email"];
    }

    if (json.contains("password") && json["password"].is_string()) {
        request.password = json["password"];
    }

    if (json.contains("nickname") && json["nickname"].is_string()) {
        request.nickname = json["nickname"];
    }

    if (json.contains("client_ip") && json["client_ip"].is_string()) {
        request.client_ip = json["client_ip"];
    }

    if (json.contains("device_type") && json["device_type"].is_string()) {
        request.device_type = json["device_type"];
    }

    if (json.contains("device_id") && json["device_id"].is_string()) {
        request.device_id = json["device_id"];
    }

    return request;
}



