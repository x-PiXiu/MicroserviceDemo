/**
 * @file jwt_manager.cpp
 * @brief JWT令牌管理器实现 - 负责JWT令牌的生成、验证和管理
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 */

#include "jwt_manager.h"
#include "common/logger/logger.h"
#include "common/security/secret_manager.h"
#include <jwt-cpp/jwt.h>
#include <regex>
#include <sstream>

namespace core_services {
    namespace auth_service {

        // ==================== UserClaims 实现 ====================

        /**
         * @brief 从用户信息创建声明
         * @param user_info 用户信息
         * @param session_id 会话ID
         * @param device_type 设备类型
         * @param client_ip 客户端IP
         */
        UserClaims::UserClaims(const CoreUserInfo& user_info, const std::string& session_id,
                              const std::string& device_type, const std::string& client_ip)
            : user_id(user_info.user_id)
            , username(user_info.username)
            , email(user_info.email)
            , nickname(user_info.nickname)
            , roles(user_info.roles)
            , session_id(session_id)
            , device_type(device_type)
            , client_ip(client_ip)
            , issued_at(std::chrono::system_clock::now())
            , not_before(std::chrono::system_clock::now())
            , jti(generateUUID()) {
        }

        /**
         * @brief 检查令牌是否有效
         * @return 如果令牌有效返回true
         */
        bool UserClaims::isValid() const {
            auto now = std::chrono::system_clock::now();
            return now >= not_before && now < expires_at;
        }

        /**
         * @brief 检查令牌是否过期
         * @return 如果令牌过期返回true
         */
        bool UserClaims::isExpired() const {
            auto now = std::chrono::system_clock::now();
            return now >= expires_at;
        }

        /**
         * @brief 检查用户是否有指定角色
         * @param role 角色名称
         * @return 如果用户有该角色返回true
         */
        bool UserClaims::hasRole(const std::string& role) const {
            return std::find(roles.begin(), roles.end(), role) != roles.end();
        }

        /**
         * @brief 检查用户是否有任意指定角色
         * @param roles 角色列表
         * @return 如果用户有任意一个角色返回true
         */
        bool UserClaims::hasAnyRole(const std::vector<std::string>& roles) const {
            for (const auto& role : roles) {
                if (hasRole(role)) {
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json UserClaims::toJson() const {
            nlohmann::json json;

            json["user_id"] = user_id;
            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            json["roles"] = roles;
            json["session_id"] = session_id;
            json["device_type"] = device_type;
            json["client_ip"] = client_ip;
            json["issued_at"] = timePointToTimestamp(issued_at);
            json["expires_at"] = timePointToTimestamp(expires_at);
            json["not_before"] = timePointToTimestamp(not_before);
            json["issuer"] = issuer;
            json["audience"] = audience;
            json["jti"] = jti;
            json["custom_claims"] = custom_claims;

            return json;
        }

        /**
         * @brief 从JSON对象创建声明
         * @param json JSON对象
         * @return 用户声明对象
         */
        UserClaims UserClaims::fromJson(const nlohmann::json& json) {
            UserClaims claims;

            if (json.contains("user_id")) claims.user_id = json["user_id"];
            if (json.contains("username")) claims.username = json["username"];
            if (json.contains("email")) claims.email = json["email"];
            if (json.contains("nickname")) claims.nickname = json["nickname"];
            if (json.contains("roles")) claims.roles = json["roles"].get<std::vector<std::string>>();
            if (json.contains("session_id")) claims.session_id = json["session_id"];
            if (json.contains("device_type")) claims.device_type = json["device_type"];
            if (json.contains("client_ip")) claims.client_ip = json["client_ip"];
            if (json.contains("issued_at")) claims.issued_at = timestampToTimePoint(json["issued_at"]);
            if (json.contains("expires_at")) claims.expires_at = timestampToTimePoint(json["expires_at"]);
            if (json.contains("not_before")) claims.not_before = timestampToTimePoint(json["not_before"]);
            if (json.contains("issuer")) claims.issuer = json["issuer"];
            if (json.contains("audience")) claims.audience = json["audience"];
            if (json.contains("jti")) claims.jti = json["jti"];
            if (json.contains("custom_claims")) claims.custom_claims = json["custom_claims"];

            return claims;
        }

        // ==================== TokenValidationResult 实现 ====================

        /**
         * @brief 成功结果构造函数
         * @param claims 用户声明
         */
        TokenValidationResult::TokenValidationResult(const UserClaims& claims)
            : valid(true)
            , claims(claims)
            , signature_valid(true)
            , format_valid(true) {
        }

        /**
         * @brief 失败结果构造函数
         * @param error_message 错误消息
         * @param error_code 错误代码
         */
        TokenValidationResult::TokenValidationResult(const std::string& error_message, const std::string& error_code)
            : valid(false)
            , error_message(error_message)
            , error_code(error_code) {
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json TokenValidationResult::toJson() const {
            nlohmann::json json;

            json["valid"] = valid;
            json["error_message"] = error_message;
            json["error_code"] = error_code;
            json["expired"] = expired;
            json["blacklisted"] = blacklisted;
            json["signature_valid"] = signature_valid;
            json["format_valid"] = format_valid;

            if (claims.has_value()) {
                json["claims"] = claims->toJson();
            }

            return json;
        }

        // ==================== JwtManager::Config 实现 ====================

        /**
         * @brief 从配置管理器加载配置
         * @return JWT配置对象
         *
         * 安全改进：优先从 SecretManager 获取敏感密钥
         */
        JwtManager::Config JwtManager::Config::fromConfigManager() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            Config config;

            // 安全改进：优先从 SecretManager 获取 JWT 密钥
            // 1. 首先尝试从 SecretManager 获取（环境变量或文件）
            // 2. 如果没有，回退到 ConfigManager（配置文件）
            // 3. 强制要求密钥长度 >= 32
            if (common::security::SecretManager::isInitialized()) {
                auto secret = common::security::SecretManager::getInstance().getSecret("JWT_SECRET_KEY");
                if (secret.has_value() && !secret->empty()) {
                    config.secret_key = secret.value();
                    LOG_INFO("JWT密钥已从SecretManager加载");
                } else {
                    // 回退到 ConfigManager
                    config.secret_key = config_manager.get<std::string>("jwt.secret_key", "");
                    if (!config.secret_key.empty()) {
                        LOG_WARNING("JWT密钥从ConfigManager加载，建议迁移到SecretManager");
                    }
                }
            } else {
                // SecretManager 未初始化，使用 ConfigManager
                config.secret_key = config_manager.get<std::string>("jwt.secret_key", "");
                if (!config.secret_key.empty()) {
                    LOG_WARNING("SecretManager未初始化，JWT密钥从ConfigManager加载");
                }
            }

            // 验证密钥安全性
            if (config.secret_key.empty()) {
                LOG_ERROR("JWT密钥未配置！请设置环境变量 JWT_SECRET_KEY 或在配置文件中配置 jwt.secret_key");
                throw std::runtime_error("JWT secret key is not configured. "
                    "Please set JWT_SECRET_KEY environment variable or configure jwt.secret_key");
            }

            if (config.secret_key.length() < 32) {
                LOG_ERROR("JWT密钥长度不足（当前: " + std::to_string(config.secret_key.length()) +
                         "），要求至少32个字符");
                throw std::runtime_error("JWT secret key must be at least 32 characters long");
            }

            // 其他非敏感配置从 ConfigManager 加载
            config.issuer = config_manager.get<std::string>("jwt.issuer", config.issuer);
            config.audience = config_manager.get<std::string>("jwt.audience", config.audience);
            config.access_token_expire_seconds = config_manager.get<int>("jwt.access_token_expire_seconds", config.access_token_expire_seconds);
            config.refresh_token_expire_seconds = config_manager.get<int>("jwt.refresh_token_expire_seconds", config.refresh_token_expire_seconds);
            config.algorithm = config_manager.get<std::string>("jwt.algorithm", config.algorithm);
            config.enable_token_blacklist = config_manager.get<bool>("jwt.enable_token_blacklist", config.enable_token_blacklist);
            config.blacklist_cleanup_interval_hours = config_manager.get<int>("jwt.blacklist_cleanup_interval_hours", config.blacklist_cleanup_interval_hours);
            config.enable_jti = config_manager.get<bool>("jwt.enable_jti", config.enable_jti);
            config.strict_audience_check = config_manager.get<bool>("jwt.strict_audience_check", config.strict_audience_check);
            config.strict_issuer_check = config_manager.get<bool>("jwt.strict_issuer_check", config.strict_issuer_check);

            LOG_INFO("JWT配置加载完成 - Issuer: " + config.issuer + ", Algorithm: " + config.algorithm);
            return config;
        }

        /**
         * @brief 验证配置有效性
         * @return 验证结果，成功返回空字符串
         */
        std::string JwtManager::Config::validate() const {
            if (secret_key.empty()) {
                return "JWT密钥不能为空";
            }

            if (secret_key.length() < 32) {
                return "JWT密钥长度不能少于32个字符";
            }

            if (issuer.empty()) {
                return "JWT发行者不能为空";
            }

            if (audience.empty()) {
                return "JWT受众不能为空";
            }

            if (access_token_expire_seconds <= 0) {
                return "访问令牌过期时间必须大于0";
            }

            if (refresh_token_expire_seconds <= 0) {
                return "刷新令牌过期时间必须大于0";
            }

            if (algorithm != "HS256" && algorithm != "HS384" && algorithm != "HS512") {
                return "不支持的签名算法: " + algorithm;
            }

            return ""; // 验证通过
        }

        // ==================== JwtManager 实现 ====================

        /**
         * @brief 构造函数
         * @param config JWT配置
         * @param redis_pool Redis连接池（用于黑名单存储）
         */
        JwtManager::JwtManager(const Config& config, std::shared_ptr<common::database::RedisPool> redis_pool)
            : config_(config)
            , redis_pool_(redis_pool) {

            // 验证配置
            std::string validation_error = config_.validate();
            if (!validation_error.empty()) {
                throw std::invalid_argument("JWT配置无效: " + validation_error);
            }

            LOG_INFO("JWT管理器初始化完成");
        }

        /**
         * @brief 析构函数
         */
        JwtManager::~JwtManager() {
        }

        /**
         * @brief 生成访问令牌
         * @param user_info 用户信息
         * @param session_id 会话ID
         * @param device_type 设备类型
         * @param client_ip 客户端IP
         * @return 访问令牌字符串
         */
        std::string JwtManager::generateAccessToken(const CoreUserInfo& user_info, const std::string& session_id,
                                                   const std::string& device_type, const std::string& client_ip) {
            try {
                auto claims = createUserClaims(user_info, session_id, device_type, client_ip,
                                             "access_token", std::chrono::seconds(config_.access_token_expire_seconds));

                std::string token = generateToken(claims, "access_token", std::chrono::seconds(config_.access_token_expire_seconds));

                tokens_generated_++;

                return token;
            } catch (const std::exception& e) {
                LOG_ERROR("生成访问令牌失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 生成刷新令牌
         * @param user_info 用户信息
         * @param session_id 会话ID
         * @param device_type 设备类型
         * @param client_ip 客户端IP
         * @return 刷新令牌字符串
         */
        std::string JwtManager::generateRefreshToken(const CoreUserInfo& user_info, const std::string& session_id,
                                                    const std::string& device_type, const std::string& client_ip) {
            try {
                auto claims = createUserClaims(user_info, session_id, device_type, client_ip,
                                             "refresh_token", std::chrono::seconds(config_.refresh_token_expire_seconds));

                std::string token = generateToken(claims, "refresh_token", std::chrono::seconds(config_.refresh_token_expire_seconds));

                tokens_generated_++;

                return token;
            } catch (const std::exception& e) {
                LOG_ERROR("生成刷新令牌失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 生成游戏会话令牌
         * @param user_info 用户信息
         * @param game_type 游戏类型
         * @param server_id 游戏服务器ID
         * @param session_duration 会话持续时间
         * @return 游戏会话令牌字符串
         */
        std::string JwtManager::generateGameSessionToken(const CoreUserInfo& user_info, GameType game_type,
                                                        const std::string& server_id,
                                                        std::chrono::seconds session_duration) {
            try {
                auto claims = createUserClaims(user_info, generateUUID(), "game", "",
                                             "game_session", session_duration);

                // 添加游戏特定的声明
                claims.custom_claims["game_type"] = gameTypeToString(game_type);
                claims.custom_claims["server_id"] = server_id;

                std::string token = generateToken(claims, "game_session", session_duration);

                tokens_generated_++;

                return token;
            } catch (const std::exception& e) {
                LOG_ERROR("生成游戏会话令牌失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 验证访问令牌
         * @param token 令牌字符串
         * @return 验证结果
         */
        TokenValidationResult JwtManager::validateAccessToken(const std::string& token) {
            tokens_validated_++;
            return validateToken(token, "access_token");
        }

        /**
         * @brief 验证刷新令牌
         * @param token 令牌字符串
         * @return 验证结果
         */
        TokenValidationResult JwtManager::validateRefreshToken(const std::string& token) {
            tokens_validated_++;
            return validateToken(token, "refresh_token");
        }

        /**
         * @brief 验证游戏会话令牌
         * @param token 令牌字符串
         * @return 验证结果
         */
        TokenValidationResult JwtManager::validateGameSessionToken(const std::string& token) {
            tokens_validated_++;
            return validateToken(token, "game_session");
        }

        /**
         * @brief 解析令牌获取用户声明（不验证签名）
         * @param token 令牌字符串
         * @return 用户声明，解析失败返回nullopt
         */
        std::optional<UserClaims> JwtManager::parseTokenClaims(const std::string& token) {
            try {
                if (!isValidTokenFormat(token)) {
                    return std::nullopt;
                }

                // 使用jwt-cpp库解析令牌（不验证签名）
                auto decoded = jwt::decode(token);

                UserClaims claims;

                // 解析标准声明
                if (decoded.has_payload_claim("sub")) {
                    claims.user_id = decoded.get_payload_claim("sub").as_string();
                }
                if (decoded.has_payload_claim("username")) {
                    claims.username = decoded.get_payload_claim("username").as_string();
                }
                if (decoded.has_payload_claim("email")) {
                    claims.email = decoded.get_payload_claim("email").as_string();
                }
                if (decoded.has_payload_claim("nickname")) {
                    claims.nickname = decoded.get_payload_claim("nickname").as_string();
                }
                if (decoded.has_payload_claim("session_id")) {
                    claims.session_id = decoded.get_payload_claim("session_id").as_string();
                }
                if (decoded.has_payload_claim("device_type")) {
                    claims.device_type = decoded.get_payload_claim("device_type").as_string();
                }
                if (decoded.has_payload_claim("client_ip")) {
                    claims.client_ip = decoded.get_payload_claim("client_ip").as_string();
                }
                if (decoded.has_payload_claim("iat")) {
                    claims.issued_at = decoded.get_payload_claim("iat").as_date();
                }
                if (decoded.has_payload_claim("exp")) {
                    claims.expires_at = decoded.get_payload_claim("exp").as_date();
                }
                if (decoded.has_payload_claim("nbf")) {
                    claims.not_before = decoded.get_payload_claim("nbf").as_date();
                }
                if (decoded.has_payload_claim("iss")) {
                    claims.issuer = decoded.get_payload_claim("iss").as_string();
                }
                if (decoded.has_payload_claim("aud")) {
                    claims.audience = decoded.get_payload_claim("aud").as_string();
                }
                if (decoded.has_payload_claim("jti")) {
                    claims.jti = decoded.get_payload_claim("jti").as_string();
                }

                // 解析角色信息
                if (decoded.has_payload_claim("roles")) {
                    auto roles_claim = decoded.get_payload_claim("roles");
                    if (roles_claim.get_type() == jwt::json::type::array) {
                        // 简化处理：使用默认角色
                        claims.roles.push_back("user");

                    }
                }

                return claims;
            } catch (const std::exception& e) {
                LOG_WARNING("解析令牌失败: " + std::string(e.what()));
                return std::nullopt;
            }
        }

        /**
         * @brief 刷新访问令牌
         * @param refresh_token 刷新令牌
         * @return 新的访问令牌和刷新令牌对
         */
        std::pair<std::string, std::string> JwtManager::refreshAccessToken(const std::string& refresh_token) {
            try {
                // 验证刷新令牌
                auto validation_result = validateRefreshToken(refresh_token);
                if (!validation_result.valid) {
                    throw std::invalid_argument("无效的刷新令牌: " + validation_result.error_message);
                }

                auto claims = validation_result.claims.value();

                // 创建用户信息对象（从声明中重建）
                CoreUserInfo user_info;
                user_info.user_id = claims.user_id;
                user_info.username = claims.username;
                user_info.email = claims.email;
                user_info.nickname = claims.nickname;
                user_info.roles = claims.roles;

                // 生成新的访问令牌和刷新令牌
                std::string new_access_token = generateAccessToken(user_info, claims.session_id,
                                                                  claims.device_type, claims.client_ip);
                std::string new_refresh_token = generateRefreshToken(user_info, claims.session_id,
                                                                    claims.device_type, claims.client_ip);

                // 撤销旧的刷新令牌
                revokeToken(refresh_token);

                LOG_INFO("令牌刷新成功，用户ID: " + claims.user_id);

                return std::make_pair(new_access_token, new_refresh_token);
            } catch (const std::exception& e) {
                LOG_ERROR("刷新令牌失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 延长令牌有效期
         * @param token 原令牌
         * @param extend_duration 延长时间
         * @return 新令牌
         */
        std::string JwtManager::extendTokenExpiry(const std::string& token, std::chrono::seconds extend_duration) {
            try {
                auto claims_opt = parseTokenClaims(token);
                if (!claims_opt.has_value()) {
                    throw std::invalid_argument("无法解析令牌");
                }

                auto claims = claims_opt.value();

                // 延长过期时间
                claims.expires_at += extend_duration;

                // 重新生成令牌
                std::string new_token = generateToken(claims, "access_token",
                    std::chrono::duration_cast<std::chrono::seconds>(claims.expires_at - claims.issued_at));

                // 撤销旧令牌
                revokeToken(token);

                LOG_INFO("令牌延期成功，用户ID: " + claims.user_id);

                return new_token;
            } catch (const std::exception& e) {
                LOG_ERROR("延长令牌有效期失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 撤销令牌（加入黑名单）
         * @param token 令牌字符串
         * @return 撤销成功返回true
         */
        bool JwtManager::revokeToken(const std::string& token) {
            try {
                if (!config_.enable_token_blacklist) {
                    LOG_WARNING("令牌黑名单功能未启用，无法撤销令牌");
                    return false;
                }

                auto claims_opt = parseTokenClaims(token);
                if (!claims_opt.has_value()) {
                    LOG_WARNING("无法解析令牌，撤销失败");
                    return false;
                }

                auto claims = claims_opt.value();

                // 添加到黑名单
                bool success = addToBlacklist(claims.jti, claims.expires_at);
                if (success) {
                    tokens_revoked_++;
                }

                return success;
            } catch (const std::exception& e) {
                LOG_ERROR("撤销令牌失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 撤销用户的所有令牌
         * @param user_id 用户ID
         * @return 撤销成功返回true
         */
        bool JwtManager::revokeAllUserTokens(int64_t user_id) {
            try {
                if (!config_.enable_token_blacklist || !redis_pool_) {
                    LOG_WARNING("令牌黑名单功能未启用或Redis连接池未配置");
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return false;
                }

                // 在Redis中标记用户的所有令牌为已撤销
                std::string key = "jwt_blacklist:user:" + std::to_string(user_id);
                auto now = std::chrono::system_clock::now();
                auto timestamp = timePointToTimestamp(now);

                bool success = redis_conn->set(key, std::to_string(timestamp), config_.refresh_token_expire_seconds);

                return success;
            } catch (const std::exception& e) {
                LOG_ERROR("撤销用户所有令牌失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 撤销会话的所有令牌
         * @param session_id 会话ID
         * @return 撤销成功返回true
         */
        bool JwtManager::revokeSessionTokens(const std::string& session_id) {
            try {
                if (!config_.enable_token_blacklist || !redis_pool_) {
                    LOG_WARNING("令牌黑名单功能未启用或Redis连接池未配置");
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return false;
                }

                // 在Redis中标记会话的所有令牌为已撤销
                std::string key = "jwt_blacklist:session:" + session_id;
                auto now = std::chrono::system_clock::now();
                auto timestamp = timePointToTimestamp(now);

                bool success = redis_conn->set(key, std::to_string(timestamp), config_.refresh_token_expire_seconds);

                return success;
            } catch (const std::exception& e) {
                LOG_ERROR("撤销会话所有令牌失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 检查令牌是否在黑名单中
         * @param token 令牌字符串
         * @return 如果在黑名单中返回true
         */
        bool JwtManager::isTokenBlacklisted(const std::string& token) {
            try {
                if (!config_.enable_token_blacklist) {
                    return false;
                }

                auto claims_opt = parseTokenClaims(token);
                if (!claims_opt.has_value()) {
                    return true; // 无法解析的令牌视为黑名单
                }

                auto claims = claims_opt.value();

                // 检查JTI是否在黑名单中
                if (isJtiBlacklisted(claims.jti)) {
                    blacklist_hits_++;
                    return true;
                }

                // 检查用户是否被全局撤销
                if (redis_pool_) {
                    // 使用 RAII 连接管理器，确保连接自动归还
                    common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                    auto redis_conn = conn_guard.get();
                    if (redis_conn) {
                        std::string user_key = "jwt_blacklist:user:" + claims.user_id;
                        auto user_revoke_time_str = redis_conn->get(user_key);
                        if (!user_revoke_time_str.empty()) {
                            int64_t user_revoke_time = std::stoll(user_revoke_time_str);
                            int64_t token_issued_time = timePointToTimestamp(claims.issued_at);
                            if (token_issued_time < user_revoke_time) {
                                blacklist_hits_++;
                                return true;
                            }
                        }

                        // 检查会话是否被撤销
                        std::string session_key = "jwt_blacklist:session:" + claims.session_id;
                        auto session_revoke_time_str = redis_conn->get(session_key);
                        if (!session_revoke_time_str.empty()) {
                            int64_t session_revoke_time = std::stoll(session_revoke_time_str);
                            int64_t token_issued_time = timePointToTimestamp(claims.issued_at);
                            if (token_issued_time < session_revoke_time) {
                                blacklist_hits_++;
                                return true;
                            }
                        }
                    }
                }

                return false;
            } catch (const std::exception& e) {
                LOG_ERROR("检查令牌黑名单状态失败: " + std::string(e.what()));
                return true; // 出错时保守处理，视为黑名单
            }
        }

        /**
         * @brief 清理过期的黑名单条目
         * @return 清理的条目数量
         */
        size_t JwtManager::cleanupExpiredBlacklistEntries() {
            size_t cleaned_count = 0;

            try {
                if (!config_.enable_token_blacklist || !redis_pool_) {
                    return 0;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接进行黑名单清理");
                    return 0;
                }

                // 清理本地黑名单缓存中的过期条目
                {
                    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
                    auto it = local_blacklist_.begin();
                    while (it != local_blacklist_.end()) {
                        // 这里简化处理，实际应该检查过期时间
                        // 由于本地缓存没有存储过期时间，我们定期清空整个缓存
                        it = local_blacklist_.erase(it);
                        cleaned_count++;
                    }
                }



            } catch (const std::exception& e) {
                LOG_ERROR("清理黑名单失败: " + std::string(e.what()));
            }

            return cleaned_count;
        }

        /**
         * @brief 获取统计信息
         * @return 统计信息JSON对象
         */
        nlohmann::json JwtManager::getStatistics() const {
            nlohmann::json stats;

            stats["tokens_generated"] = tokens_generated_.load();
            stats["tokens_validated"] = tokens_validated_.load();
            stats["tokens_revoked"] = tokens_revoked_.load();
            stats["validation_failures"] = validation_failures_.load();
            stats["blacklist_hits"] = blacklist_hits_.load();

            // 计算成功率
            uint64_t total_validations = tokens_validated_.load();
            if (total_validations > 0) {
                double success_rate = 1.0 - (static_cast<double>(validation_failures_.load()) / total_validations);
                stats["validation_success_rate"] = success_rate;
            } else {
                stats["validation_success_rate"] = 0.0;
            }

            // 配置信息
            stats["config"] = {
                {"issuer", config_.issuer},
                {"audience", config_.audience},
                {"access_token_expire_seconds", config_.access_token_expire_seconds},
                {"refresh_token_expire_seconds", config_.refresh_token_expire_seconds},
                {"algorithm", config_.algorithm},
                {"enable_token_blacklist", config_.enable_token_blacklist},
                {"enable_jti", config_.enable_jti}
            };

            return stats;
        }

        /**
         * @brief 重置统计信息
         */
        void JwtManager::resetStatistics() {
            tokens_generated_ = 0;
            tokens_validated_ = 0;
            tokens_revoked_ = 0;
            validation_failures_ = 0;
            blacklist_hits_ = 0;


        }

        /**
         * @brief 从令牌中提取JWT ID
         * @param token 令牌字符串
         * @return JWT ID，提取失败返回空字符串
         */
        std::string JwtManager::extractJwtId(const std::string& token) {
            auto claims_opt = parseTokenClaims(token);
            if (claims_opt.has_value()) {
                return claims_opt->jti;
            }
            return "";
        }

        /**
         * @brief 从令牌中提取用户ID
         * @param token 令牌字符串
         * @return 用户ID，提取失败返回空字符串
         */
        std::string JwtManager::extractUserId(const std::string& token) {
            auto claims_opt = parseTokenClaims(token);
            if (claims_opt.has_value()) {
                return claims_opt->user_id;
            }
            return "";
        }

        /**
         * @brief 从令牌中提取会话ID
         * @param token 令牌字符串
         * @return 会话ID，提取失败返回空字符串
         */
        std::string JwtManager::extractSessionId(const std::string& token) {
            auto claims_opt = parseTokenClaims(token);
            if (claims_opt.has_value()) {
                return claims_opt->session_id;
            }
            return "";
        }

        /**
         * @brief 检查令牌格式是否正确
         * @param token 令牌字符串
         * @return 格式正确返回true
         */
        bool JwtManager::isValidTokenFormat(const std::string& token) {
            // JWT令牌应该有三个部分，用点号分隔
            std::regex jwt_regex(R"(^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$)");
            return std::regex_match(token, jwt_regex);
        }

        // ==================== 内部方法实现 ====================

        /**
         * @brief 生成JWT令牌
         * @param claims 用户声明
         * @param token_type 令牌类型
         * @param expire_duration 过期时间
         * @return JWT令牌字符串
         */
        std::string JwtManager::generateToken(const UserClaims& claims, const std::string& token_type,
                                            std::chrono::seconds expire_duration) {
            try {
                // 创建JWT令牌
                auto token = jwt::create()
                    .set_issuer(config_.issuer)
                    .set_audience(config_.audience)
                    .set_subject(claims.user_id)
                    .set_issued_at(std::chrono::system_clock::now())
                    .set_expires_at(std::chrono::system_clock::now() + expire_duration)
                    .set_not_before(std::chrono::system_clock::now());

                // 添加JWT ID
                if (config_.enable_jti) {
                    token.set_id(claims.jti);
                }

                // 添加自定义声明
                token.set_payload_claim("username", jwt::claim(claims.username));
                token.set_payload_claim("email", jwt::claim(claims.email));
                token.set_payload_claim("nickname", jwt::claim(claims.nickname));
                token.set_payload_claim("session_id", jwt::claim(claims.session_id));
                token.set_payload_claim("device_type", jwt::claim(claims.device_type));
                token.set_payload_claim("client_ip", jwt::claim(claims.client_ip));
                token.set_payload_claim("token_type", jwt::claim(token_type));

                // 添加角色信息
                if (!claims.roles.empty()) {
                    nlohmann::json roles_json = claims.roles;
                    token.set_payload_claim("roles", jwt::claim(roles_json.dump()));
                }

                // 添加自定义声明
                for (const auto& [key, value] : claims.custom_claims) {
                    token.set_payload_claim(key, jwt::claim(value));
                }

                // 根据算法签名
                if (config_.algorithm == "HS256") {
                    return token.sign(jwt::algorithm::hs256{config_.secret_key});
                } else if (config_.algorithm == "HS384") {
                    return token.sign(jwt::algorithm::hs384{config_.secret_key});
                } else if (config_.algorithm == "HS512") {
                    return token.sign(jwt::algorithm::hs512{config_.secret_key});
                } else {
                    throw std::invalid_argument("不支持的签名算法: " + config_.algorithm);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("生成JWT令牌失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 验证JWT令牌
         * @param token 令牌字符串
         * @param expected_type 期望的令牌类型
         * @return 验证结果
         */
        TokenValidationResult JwtManager::validateToken(const std::string& token, const std::string& expected_type) {
            try {
                // 检查令牌格式
                if (!isValidTokenFormat(token)) {
                    validation_failures_++;
                    return TokenValidationResult("令牌格式无效", "INVALID_FORMAT");
                }

                // 检查黑名单
                if (isTokenBlacklisted(token)) {
                    validation_failures_++;
                    auto result = TokenValidationResult("令牌已被撤销", "TOKEN_REVOKED");
                    result.blacklisted = true;
                    return result;
                }

                // 创建验证器
                auto verifier = jwt::verify()
                    .allow_algorithm(jwt::algorithm::hs256{config_.secret_key})
                    .with_issuer(config_.issuer);

                if (config_.strict_audience_check) {
                    verifier.with_audience(config_.audience);
                }

                // 验证令牌
                auto decoded = jwt::decode(token);
                verifier.verify(decoded);

                // 解析用户声明
                auto claims_opt = parseTokenClaims(token);
                if (!claims_opt.has_value()) {
                    validation_failures_++;
                    return TokenValidationResult("无法解析令牌声明", "PARSE_ERROR");
                }

                auto claims = claims_opt.value();

                // 检查令牌类型
                if (decoded.has_payload_claim("token_type")) {
                    std::string token_type = decoded.get_payload_claim("token_type").as_string();
                    if (token_type != expected_type) {
                        validation_failures_++;
                        return TokenValidationResult("令牌类型不匹配", "TYPE_MISMATCH");
                    }
                }

                // 检查过期时间
                if (claims.isExpired()) {
                    validation_failures_++;
                    auto result = TokenValidationResult("令牌已过期", "TOKEN_EXPIRED");
                    result.expired = true;
                    return result;
                }

                // 验证成功
                auto result = TokenValidationResult(claims);
                result.signature_valid = true;
                result.format_valid = true;
                return result;

            } catch (const std::exception& e) {
                validation_failures_++;
                std::string error_msg = e.what();

                if (error_msg.find("signature") != std::string::npos) {
                    auto result = TokenValidationResult("令牌签名验证失败", "SIGNATURE_INVALID");
                    result.signature_valid = false;
                    return result;
                } else if (error_msg.find("expired") != std::string::npos) {
                    auto result = TokenValidationResult("令牌已过期", "TOKEN_EXPIRED");
                    result.expired = true;
                    return result;
                } else {
                    validation_failures_++;
                    LOG_ERROR("验证JWT令牌失败: " + std::string(e.what()));
                    return TokenValidationResult("令牌验证失败: " + std::string(e.what()), "VALIDATION_ERROR");
                }
            }
        }

        /**
         * @brief 添加令牌到黑名单
         * @param jti JWT ID
         * @param expire_time 过期时间
         * @return 添加成功返回true
         */
        bool JwtManager::addToBlacklist(const std::string& jti, std::chrono::system_clock::time_point expire_time) {
            try {
                // 添加到本地缓存
                {
                    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
                    local_blacklist_.insert(jti);
                }

                // 添加到Redis
                if (redis_pool_) {
                    // 使用 RAII 连接管理器，确保连接自动归还
                    common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                    auto redis_conn = conn_guard.get();
                    if (redis_conn) {
                        std::string key = "jwt_blacklist:jti:" + jti;
                        auto now = std::chrono::system_clock::now();
                        auto ttl = std::chrono::duration_cast<std::chrono::seconds>(expire_time - now).count();

                        if (ttl > 0) {
                            redis_conn->set(key, "1", static_cast<int>(ttl));
                        }
                    }
                }

                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("添加令牌到黑名单失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 检查JWT ID是否在黑名单中
         * @param jti JWT ID
         * @return 如果在黑名单中返回true
         */
        bool JwtManager::isJtiBlacklisted(const std::string& jti) {
            try {
                // 检查本地缓存
                {
                    std::shared_lock<std::shared_mutex> lock(blacklist_mutex_);
                    if (local_blacklist_.find(jti) != local_blacklist_.end()) {
                        return true;
                    }
                }

                // 检查Redis
                if (redis_pool_) {
                    // 使用 RAII 连接管理器，确保连接自动归还
                    common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                    auto redis_conn = conn_guard.get();
                    if (redis_conn) {
                        std::string key = "jwt_blacklist:jti:" + jti;
                        std::string value = redis_conn->get(key);
                        if (!value.empty()) {
                            // 同时添加到本地缓存以提高性能
                            std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
                            local_blacklist_.insert(jti);
                            return true;
                        }
                    }
                }

                return false;
            } catch (const std::exception& e) {
                LOG_ERROR("检查JTI黑名单状态失败: " + std::string(e.what()));
                return true; // 出错时保守处理
            }
        }

        /**
         * @brief 生成JWT ID
         * @return JWT ID字符串
         */
        std::string JwtManager::generateJwtId() {
            return generateUUID();
        }

        /**
         * @brief 创建用户声明
         * @param user_info 用户信息
         * @param session_id 会话ID
         * @param device_type 设备类型
         * @param client_ip 客户端IP
         * @param token_type 令牌类型
         * @param expire_duration 过期时间
         * @return 用户声明对象
         */
        UserClaims JwtManager::createUserClaims(const CoreUserInfo& user_info, const std::string& session_id,
                                              const std::string& device_type, const std::string& client_ip,
                                              const std::string& token_type, std::chrono::seconds expire_duration) {
            UserClaims claims(user_info, session_id, device_type, client_ip);

            // 设置时间相关声明
            auto now = std::chrono::system_clock::now();
            claims.issued_at = now;
            claims.not_before = now;
            claims.expires_at = now + expire_duration;

            // 设置发行者和受众
            claims.issuer = config_.issuer;
            claims.audience = config_.audience;

            // 生成JWT ID
            if (config_.enable_jti) {
                claims.jti = generateJwtId();
            }

            // 添加令牌类型到自定义声明
            claims.custom_claims["token_type"] = token_type;

            return claims;
        }

        /**
         * @brief 更新配置
         * @param new_config 新配置
         * @return 更新成功返回true
         */
        bool JwtManager::updateConfig(const Config& new_config) {
            try {
                // 验证新配置的有效性
                if (new_config.access_token_expire_seconds <= 0 ||
                    new_config.refresh_token_expire_seconds <= 0) {
                    LOG_ERROR("JWT令牌过期时间配置无效");
                    return false;
                }

                if (new_config.secret_key.empty()) {
                    LOG_ERROR("JWT密钥不能为空");
                    return false;
                }

                // 更新配置
                config_ = new_config;


                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("更新JWT管理器配置失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 从黑名单中移除令牌
         * @param jti JWT ID
         * @return 移除成功返回true
         */
        bool JwtManager::removeFromBlacklist(const std::string& jti) {
            try {
                // 从本地缓存移除
                {
                    std::unique_lock<std::shared_mutex> lock(blacklist_mutex_);
                    local_blacklist_.erase(jti);
                }

                // 从Redis移除
                if (redis_pool_) {
                    // 使用 RAII 连接管理器，确保连接自动归还
                    common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                    auto redis_conn = conn_guard.get();
                    if (redis_conn) {
                        std::string key = "jwt:blacklist:" + jti;
                        redis_conn->del(key);
                    }
                }


                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("从黑名单移除JWT ID失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 验证令牌签名
         * @param token 令牌字符串
         * @return 签名有效返回true
         */
        bool JwtManager::verifyTokenSignature(const std::string& token) {
            try {
                auto verifier = jwt::verify()
                    .allow_algorithm(jwt::algorithm::hs256{config_.secret_key})
                    .with_issuer(config_.issuer);

                auto decoded = jwt::decode(token);
                verifier.verify(decoded);

                return true;

            } catch (const std::exception& e) {

                return false;
            }
        }

    } // namespace auth_service
} // namespace core_services
