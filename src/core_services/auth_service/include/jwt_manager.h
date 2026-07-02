/**
 * @file jwt_manager.h
 * @brief JWT令牌管理器 - 负责JWT令牌的生成、验证和管理
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 *
 * 功能特性:
 * - JWT令牌生成和验证
 * - 访问令牌和刷新令牌管理
 * - 令牌黑名单机制
 * - 令牌刷新和撤销
 * - 多种签名算法支持
 * - 令牌过期时间管理
 * - 用户声明信息封装
 *
 * 安全特性:
 * - 密钥安全管理
 * - 令牌防伪造验证
 * - 时间戳验证
 * - 发行者和受众验证
 * - 令牌撤销黑名单
 *
 * 与现有模块集成:
 * - 使用Redis进行令牌黑名单存储
 * - 使用ConfigManager进行配置管理
 * - 使用Logger进行日志记录
 * - 集成ThreadPool进行异步处理
 */

#ifndef JWT_MANAGER_H
#define JWT_MANAGER_H

#include "user_models.h"
#include "common/database/redis_pool.h"
#include "common/config/config_manager.h"
#include "common/logger/logger.h"
#include <string>
#include <chrono>
#include <optional>
#include <memory>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace core_services {
    namespace auth_service {

        /**
         * @brief JWT用户声明信息
         * @details 封装JWT令牌中的用户相关声明
         */
        struct UserClaims {
            std::string user_id;                                    ///< 用户ID（字符串标识符）
            std::string username;                                   ///< 用户名
            std::string email;                                      ///< 邮箱
            std::string nickname;                                   ///< 昵称
            std::vector<std::string> roles;                         ///< 用户角色
            std::string session_id;                                 ///< 会话ID
            std::string device_type;                                ///< 设备类型
            std::string client_ip;                                  ///< 客户端IP
            std::chrono::system_clock::time_point issued_at;        ///< 签发时间
            std::chrono::system_clock::time_point expires_at;       ///< 过期时间
            std::chrono::system_clock::time_point not_before;       ///< 生效时间
            std::string issuer;                                     ///< 发行者
            std::string audience;                                   ///< 受众
            std::string jti;                                        ///< JWT ID（唯一标识）
            std::unordered_map<std::string, std::string> custom_claims; ///< 自定义声明

            /**
             * @brief 默认构造函数
             */
            UserClaims() = default;

            /**
             * @brief 从用户信息创建声明
             * @param user_info 用户信息
             * @param session_id 会话ID
             * @param device_type 设备类型
             * @param client_ip 客户端IP
             */
            UserClaims(const CoreUserInfo& user_info, const std::string& session_id,
                      const std::string& device_type, const std::string& client_ip);

            /**
             * @brief 检查令牌是否有效
             * @return 如果令牌有效返回true
             */
            bool isValid() const;

            /**
             * @brief 检查令牌是否过期
             * @return 如果令牌过期返回true
             */
            bool isExpired() const;

            /**
             * @brief 检查用户是否有指定角色
             * @param role 角色名称
             * @return 如果用户有该角色返回true
             */
            bool hasRole(const std::string& role) const;

            /**
             * @brief 检查用户是否有任意指定角色
             * @param roles 角色列表
             * @return 如果用户有任意一个角色返回true
             */
            bool hasAnyRole(const std::vector<std::string>& roles) const;

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;

            /**
             * @brief 从JSON对象创建声明
             * @param json JSON对象
             * @return 用户声明对象
             */
            static UserClaims fromJson(const nlohmann::json& json);
        };

        /**
         * @brief JWT令牌验证结果
         */
        struct TokenValidationResult {
            bool valid = false;                                     ///< 是否有效
            std::string error_message;                              ///< 错误消息
            std::string error_code;                                 ///< 错误代码
            std::optional<UserClaims> claims;                       ///< 用户声明（有效时）
            bool expired = false;                                   ///< 是否过期
            bool blacklisted = false;                               ///< 是否在黑名单中
            bool signature_valid = false;                           ///< 签名是否有效
            bool format_valid = false;                              ///< 格式是否有效

            /**
             * @brief 默认构造函数
             */
            TokenValidationResult() = default;

            /**
             * @brief 成功结果构造函数
             * @param claims 用户声明
             */
            explicit TokenValidationResult(const UserClaims& claims);

            /**
             * @brief 失败结果构造函数
             * @param error_message 错误消息
             * @param error_code 错误代码
             */
            TokenValidationResult(const std::string& error_message, const std::string& error_code);

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;
        };

        /**
         * @brief JWT管理器类
         * @details 负责JWT令牌的完整生命周期管理
         */
        class JwtManager {
        public:
            /**
             * @brief JWT配置结构体
             */
            struct Config {
                std::string secret_key;  ///< JWT签名密钥（必须配置，不再有默认值）
                std::string issuer = "game-microservices-auth";                 ///< 令牌发行者
                std::string audience = "game-microservices-clients";            ///< 令牌受众
                int access_token_expire_seconds = 3600;                         ///< Access Token过期时间(1小时)
                int refresh_token_expire_seconds = 604800;                      ///< Refresh Token过期时间(7天)
                std::string algorithm = "HS256";                                ///< 签名算法
                bool enable_token_blacklist = true;                             ///< 是否启用令牌黑名单
                int blacklist_cleanup_interval_hours = 24;                      ///< 黑名单清理间隔（小时）
                bool enable_jti = true;                                         ///< 是否启用JWT ID
                bool strict_audience_check = true;                              ///< 是否严格检查受众
                bool strict_issuer_check = true;                                ///< 是否严格检查发行者

                /**
                 * @brief 从配置管理器加载配置
                 * @return JWT配置对象
                 */
                static Config fromConfigManager();

                /**
                 * @brief 验证配置有效性
                 * @return 验证结果，成功返回空字符串
                 */
                std::string validate() const;
            };

            /**
             * @brief 构造函数
             * @param config JWT配置
             * @param redis_pool Redis连接池（用于黑名单存储）
             */
            explicit JwtManager(const Config& config, std::shared_ptr<common::database::RedisPool> redis_pool = nullptr);

            /**
             * @brief 析构函数
             */
            ~JwtManager();

            // ==================== 令牌生成 ====================

            /**
             * @brief 生成访问令牌
             * @param user_info 用户信息
             * @param session_id 会话ID
             * @param device_type 设备类型
             * @param client_ip 客户端IP
             * @return 访问令牌字符串
             */
            std::string generateAccessToken(const CoreUserInfo& user_info, const std::string& session_id,
                                          const std::string& device_type, const std::string& client_ip);

            /**
             * @brief 生成刷新令牌
             * @param user_info 用户信息
             * @param session_id 会话ID
             * @param device_type 设备类型
             * @param client_ip 客户端IP
             * @return 刷新令牌字符串
             */
            std::string generateRefreshToken(const CoreUserInfo& user_info, const std::string& session_id,
                                           const std::string& device_type, const std::string& client_ip);

            /**
             * @brief 生成游戏会话令牌
             * @param user_info 用户信息
             * @param game_type 游戏类型
             * @param server_id 游戏服务器ID
             * @param session_duration 会话持续时间
             * @return 游戏会话令牌字符串
             */
            std::string generateGameSessionToken(const CoreUserInfo& user_info, GameType game_type,
                                               const std::string& server_id,
                                               std::chrono::seconds session_duration = std::chrono::hours(2));

            // ==================== 令牌验证 ====================

            /**
             * @brief 验证访问令牌
             * @param token 令牌字符串
             * @return 验证结果
             */
            TokenValidationResult validateAccessToken(const std::string& token);

            /**
             * @brief 验证刷新令牌
             * @param token 令牌字符串
             * @return 验证结果
             */
            TokenValidationResult validateRefreshToken(const std::string& token);

            /**
             * @brief 验证游戏会话令牌
             * @param token 令牌字符串
             * @return 验证结果
             */
            TokenValidationResult validateGameSessionToken(const std::string& token);

            /**
             * @brief 解析令牌获取用户声明（不验证签名）
             * @param token 令牌字符串
             * @return 用户声明，解析失败返回nullopt
             */
            std::optional<UserClaims> parseTokenClaims(const std::string& token);

            // ==================== 令牌刷新 ====================

            /**
             * @brief 刷新访问令牌
             * @param refresh_token 刷新令牌
             * @return 新的访问令牌和刷新令牌对
             */
            std::pair<std::string, std::string> refreshAccessToken(const std::string& refresh_token);

            /**
             * @brief 延长令牌有效期
             * @param token 原令牌
             * @param extend_duration 延长时间
             * @return 新令牌
             */
            std::string extendTokenExpiry(const std::string& token, std::chrono::seconds extend_duration);

            // ==================== 令牌撤销和黑名单 ====================

            /**
             * @brief 撤销令牌（加入黑名单）
             * @param token 令牌字符串
             * @return 撤销成功返回true
             */
            bool revokeToken(const std::string& token);

            /**
             * @brief 撤销用户的所有令牌
             * @param user_id 用户ID
             * @return 撤销成功返回true
             */
            bool revokeAllUserTokens(int64_t user_id);

            /**
             * @brief 撤销会话的所有令牌
             * @param session_id 会话ID
             * @return 撤销成功返回true
             */
            bool revokeSessionTokens(const std::string& session_id);

            /**
             * @brief 检查令牌是否在黑名单中
             * @param token 令牌字符串
             * @return 如果在黑名单中返回true
             */
            bool isTokenBlacklisted(const std::string& token);

            /**
             * @brief 清理过期的黑名单条目
             * @return 清理的条目数量
             */
            size_t cleanupExpiredBlacklistEntries();

            // ==================== 配置和状态管理 ====================

            /**
             * @brief 更新配置
             * @param new_config 新配置
             * @return 更新成功返回true
             */
            bool updateConfig(const Config& new_config);

            /**
             * @brief 获取当前配置
             * @return 当前配置
             */
            const Config& getConfig() const { return config_; }

            /**
             * @brief 获取统计信息
             * @return 统计信息JSON对象
             */
            nlohmann::json getStatistics() const;

            /**
             * @brief 重置统计信息
             */
            void resetStatistics();

            // ==================== 工具方法 ====================

            /**
             * @brief 从令牌中提取JWT ID
             * @param token 令牌字符串
             * @return JWT ID，提取失败返回空字符串
             */
            std::string extractJwtId(const std::string& token);

            /**
             * @brief 从令牌中提取用户ID
             * @param token 令牌字符串
             * @return 用户ID，提取失败返回空字符串
             */
            std::string extractUserId(const std::string& token);

            /**
             * @brief 从令牌中提取会话ID
             * @param token 令牌字符串
             * @return 会话ID，提取失败返回空字符串
             */
            std::string extractSessionId(const std::string& token);

            /**
             * @brief 检查令牌格式是否正确
             * @param token 令牌字符串
             * @return 格式正确返回true
             */
            static bool isValidTokenFormat(const std::string& token);

        private:
            Config config_;                                                     ///< JWT配置
            std::shared_ptr<common::database::RedisPool> redis_pool_;          ///< Redis连接池
            std::unordered_set<std::string> local_blacklist_;                  ///< 本地黑名单缓存
            mutable std::shared_mutex blacklist_mutex_;                        ///< 黑名单互斥锁

            // 统计信息
            mutable std::atomic<uint64_t> tokens_generated_{0};                ///< 生成的令牌数量
            mutable std::atomic<uint64_t> tokens_validated_{0};                ///< 验证的令牌数量
            mutable std::atomic<uint64_t> tokens_revoked_{0};                  ///< 撤销的令牌数量
            mutable std::atomic<uint64_t> validation_failures_{0};             ///< 验证失败次数
            mutable std::atomic<uint64_t> blacklist_hits_{0};                  ///< 黑名单命中次数

            // ==================== 内部方法 ====================

            /**
             * @brief 生成JWT令牌
             * @param claims 用户声明
             * @param token_type 令牌类型
             * @param expire_duration 过期时间
             * @return JWT令牌字符串
             */
            std::string generateToken(const UserClaims& claims, const std::string& token_type,
                                    std::chrono::seconds expire_duration);

            /**
             * @brief 验证JWT令牌
             * @param token 令牌字符串
             * @param expected_type 期望的令牌类型
             * @return 验证结果
             */
            TokenValidationResult validateToken(const std::string& token, const std::string& expected_type);

            /**
             * @brief 添加令牌到黑名单
             * @param jti JWT ID
             * @param expire_time 过期时间
             * @return 添加成功返回true
             */
            bool addToBlacklist(const std::string& jti, std::chrono::system_clock::time_point expire_time);

            /**
             * @brief 从黑名单中移除令牌
             * @param jti JWT ID
             * @return 移除成功返回true
             */
            bool removeFromBlacklist(const std::string& jti);

            /**
             * @brief 检查JWT ID是否在黑名单中
             * @param jti JWT ID
             * @return 如果在黑名单中返回true
             */
            bool isJtiBlacklisted(const std::string& jti);

            /**
             * @brief 生成JWT ID
             * @return JWT ID字符串
             */
            std::string generateJwtId();

            /**
             * @brief 验证令牌签名
             * @param token 令牌字符串
             * @return 签名有效返回true
             */
            bool verifyTokenSignature(const std::string& token);

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
            UserClaims createUserClaims(const CoreUserInfo& user_info, const std::string& session_id,
                                      const std::string& device_type, const std::string& client_ip,
                                      const std::string& token_type, std::chrono::seconds expire_duration);
        };

    } // namespace auth_service
} // namespace core_services

#endif //JWT_MANAGER_H
