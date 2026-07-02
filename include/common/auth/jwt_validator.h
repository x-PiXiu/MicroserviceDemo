/**
 * @file jwt_validator.h
 * @brief JWT Token 验证器 - 提供跨服务的 JWT 验证功能
 * @author AI Assistant
 * @date 2025-02-20
 * @version 1.0.0
 *
 * 功能特性:
 * - JWT Token 解析和验证
 * - 签名验证（HS256）
 * - 过期时间检查
 * - 发行者和受众验证
 * - 用户信息提取
 * - 单例模式，全局共享
 *
 * 设计原则:
 * - 无状态验证（不需要 Redis）
 * - 与 auth_service 共享相同密钥
 * - 支持黑名单检查（可选）
 *
 * 使用示例:
 * @code
 * // 初始化（服务启动时调用一次）
 * JwtValidator::getInstance().initialize("your-secret-key", "game-microservices-auth");
 *
 * // 验证 Token
 * std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...";
 * auto result = JwtValidator::getInstance().validate(token);
 *
 * if (result.valid) {
 *     std::cout << "User ID: " << result.user_id << std::endl;
 * } else {
 *     std::cout << "Error: " << result.error_message << std::endl;
 * }
 * @endcode
 */

#ifndef COMMON_AUTH_JWT_VALIDATOR_H
#define COMMON_AUTH_JWT_VALIDATOR_H

#include <string>
#include <optional>
#include <chrono>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

namespace common {
namespace auth {

/**
 * @brief JWT Token 验证结果
 */
struct JwtValidationResult {
    bool valid = false;                     ///< 是否有效
    std::string error_message;              ///< 错误消息
    std::string error_code;                 ///< 错误代码

    // 用户信息（验证成功时填充）
    std::string user_id;                    ///< 用户ID
    std::string username;                   ///< 用户名
    std::string email;                      ///< 邮箱
    std::string session_id;                 ///< 会话ID
    std::vector<std::string> roles;         ///< 用户角色
    std::string token_type;                 ///< Token类型（access_token/refresh_token）

    // Token 元数据
    int64_t issued_at = 0;                  ///< 签发时间（Unix时间戳）
    int64_t expires_at = 0;                 ///< 过期时间（Unix时间戳）
    std::string issuer;                     ///< 发行者
    std::string audience;                   ///< 受众
    std::string jti;                        ///< JWT ID

    // 验证状态
    bool signature_valid = false;           ///< 签名是否有效
    bool expired = false;                   ///< 是否过期
    bool not_active_yet = false;            ///< 是否未生效

    /**
     * @brief 创建成功结果
     */
    static JwtValidationResult success() {
        JwtValidationResult result;
        result.valid = true;
        result.signature_valid = true;
        return result;
    }

    /**
     * @brief 创建失败结果
     */
    static JwtValidationResult failure(const std::string& message,
                                       const std::string& code = "VALIDATION_ERROR") {
        JwtValidationResult result;
        result.valid = false;
        result.error_message = message;
        result.error_code = code;
        return result;
    }

    /**
     * @brief 检查用户是否有指定角色
     */
    bool hasRole(const std::string& role) const {
        return std::find(roles.begin(), roles.end(), role) != roles.end();
    }

    /**
     * @brief 获取剩余有效时间（秒）
     */
    int64_t getRemainingSeconds() const {
        if (expires_at == 0) return 0;
        auto now = std::chrono::system_clock::now();
        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        int64_t diff = expires_at - now_ts;
        return diff > 0 ? diff : 0;
    }
};

/**
 * @brief JWT 验证器配置
 */
struct JwtValidatorConfig {
    std::string secret_key;                          ///< 签名密钥（必填）
    std::string issuer = "game-microservices-auth";  ///< 期望的发行者
    std::string audience = "game-microservices-clients"; ///< 期望的受众
    std::string algorithm = "HS256";                 ///< 签名算法
    int clock_skew_seconds = 60;                     ///< 时钟偏差容忍（秒）
    bool validate_issuer = true;                     ///< 是否验证发行者
    bool validate_audience = false;                  ///< 是否验证受众
    bool validate_expiration = true;                 ///< 是否验证过期时间

    /**
     * @brief 从配置管理器加载
     */
    static JwtValidatorConfig fromConfig();

    /**
     * @brief 验证配置
     */
    bool validate() const {
        return !secret_key.empty();
    }
};

/**
 * @brief JWT Token 验证器（单例）
 * @details 提供跨服务的 JWT 验证功能，与 auth_service 共享相同的验证逻辑
 */
class JwtValidator {
public:
    /**
     * @brief 获取单例实例
     */
    static JwtValidator& getInstance();

    // 禁用拷贝和赋值
    JwtValidator(const JwtValidator&) = delete;
    JwtValidator& operator=(const JwtValidator&) = delete;

    /**
     * @brief 初始化验证器
     * @param config 配置
     * @return 成功返回 true
     */
    bool initialize(const JwtValidatorConfig& config);

    /**
     * @brief 初始化验证器（简化版本）
     * @param secret_key 签名密钥
     * @param issuer 期望的发行者
     * @return 成功返回 true
     */
    bool initialize(const std::string& secret_key,
                    const std::string& issuer = "game-microservices-auth");

    /**
     * @brief 验证 JWT Token
     * @param token JWT Token 字符串
     * @return 验证结果
     */
    JwtValidationResult validate(const std::string& token);

    /**
     * @brief 验证 Token 并返回用户ID（简化版本）
     * @param token JWT Token 字符串
     * @return 用户ID，验证失败返回空
     */
    std::optional<std::string> validateAndGetUserId(const std::string& token);

    /**
     * @brief 检查验证器是否已初始化
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief 设置黑名单检查函数（可选）
     * @param checker 黑名单检查函数，返回 true 表示在黑名单中
     */
    void setBlacklistChecker(std::function<bool(const std::string& jti)> checker) {
        blacklist_checker_ = std::move(checker);
    }

    /**
     * @brief 清理资源
     */
    void cleanup();

private:
    JwtValidator() = default;
    ~JwtValidator() = default;

    /**
     * @brief 解析 JWT Token 的各部分
     */
    bool parseToken(const std::string& token,
                   std::string& header_b64,
                   std::string& payload_b64,
                   std::string& signature_b64);

    /**
     * @brief 解码 Base64URL
     */
    std::string base64UrlDecode(const std::string& input);

    /**
     * @brief 验证签名
     */
    bool verifySignature(const std::string& header_b64,
                        const std::string& payload_b64,
                        const std::string& signature_b64);

    /**
     * @brief 解析并验证 Claims
     */
    JwtValidationResult parseAndValidateClaims(const std::string& payload_json);

    /**
     * @brief 从 Token 提取用户信息
     */
    void extractUserInfo(const std::map<std::string, std::string>& claims,
                        JwtValidationResult& result);

    JwtValidatorConfig config_;
    bool initialized_ = false;
    std::mutex mutex_;
    std::function<bool(const std::string& jti)> blacklist_checker_;
};

} // namespace auth
} // namespace common

#endif // COMMON_AUTH_JWT_VALIDATOR_H
