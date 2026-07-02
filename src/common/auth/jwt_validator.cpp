/**
 * @file jwt_validator.cpp
 * @brief JWT Token 验证器实现
 * @author AI Assistant
 * @date 2025-02-20
 * @version 1.0.0
 */

#include "common/auth/jwt_validator.h"
#include "common/utils/crypto.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace common {
namespace auth {

// ==================== JwtValidatorConfig ====================

JwtValidatorConfig JwtValidatorConfig::fromConfig() {
    JwtValidatorConfig config;

    auto& config_mgr = common::config::ConfigManager::getInstance();

    // 从配置文件读取
    config.secret_key = config_mgr.get<std::string>("jwt.secret_key", "");
    config.issuer = config_mgr.get<std::string>("jwt.issuer", "game-microservices-auth");
    config.audience = config_mgr.get<std::string>("jwt.audience", "game-microservices-clients");
    config.algorithm = config_mgr.get<std::string>("jwt.algorithm", "HS256");
    config.clock_skew_seconds = config_mgr.get<int>("jwt.clock_skew_seconds", 60);
    config.validate_issuer = config_mgr.get<bool>("jwt.validate_issuer", true);
    config.validate_audience = config_mgr.get<bool>("jwt.validate_audience", false);
    config.validate_expiration = config_mgr.get<bool>("jwt.validate_expiration", true);

    return config;
}

// ==================== JwtValidator ====================

JwtValidator& JwtValidator::getInstance() {
    static JwtValidator instance;
    return instance;
}

bool JwtValidator::initialize(const JwtValidatorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!config.validate()) {
        LOG_ERROR("JWT Validator 配置无效: secret_key 为空");
        return false;
    }

    config_ = config;
    initialized_ = true;

    LOG_INFO("JWT Validator 初始化成功: issuer=" + config_.issuer +
             ", algorithm=" + config_.algorithm);

    return true;
}

bool JwtValidator::initialize(const std::string& secret_key,
                              const std::string& issuer) {
    JwtValidatorConfig config;
    config.secret_key = secret_key;
    config.issuer = issuer;
    return initialize(config);
}

JwtValidationResult JwtValidator::validate(const std::string& token) {
    if (!initialized_) {
        return JwtValidationResult::failure("JWT Validator 未初始化", "NOT_INITIALIZED");
    }

    if (token.empty()) {
        return JwtValidationResult::failure("Token 为空", "EMPTY_TOKEN");
    }

    try {
        // 1. 解析 Token
        std::string header_b64, payload_b64, signature_b64;
        if (!parseToken(token, header_b64, payload_b64, signature_b64)) {
            return JwtValidationResult::failure("Token 格式无效", "INVALID_FORMAT");
        }

        // 2. 验证签名
        if (!verifySignature(header_b64, payload_b64, signature_b64)) {
            JwtValidationResult result = JwtValidationResult::failure("签名验证失败", "INVALID_SIGNATURE");
            result.signature_valid = false;
            return result;
        }

        // 3. 解码并验证 Claims
        std::string payload_json = base64UrlDecode(payload_b64);
        if (payload_json.empty()) {
            return JwtValidationResult::failure("Payload 解码失败", "DECODE_ERROR");
        }

        JwtValidationResult result = parseAndValidateClaims(payload_json);
        if (!result.valid) {
            return result;
        }

        result.signature_valid = true;

        // 4. 检查黑名单（如果配置了）
        if (blacklist_checker_ && !result.jti.empty()) {
            if (blacklist_checker_(result.jti)) {
                JwtValidationResult blacklist_result = JwtValidationResult::failure(
                    "Token 已被撤销", "TOKEN_REVOKED");
                return blacklist_result;
            }
        }

        return result;

    } catch (const std::exception& e) {
        LOG_ERROR("JWT 验证异常: " + std::string(e.what()));
        return JwtValidationResult::failure("验证过程发生异常: " + std::string(e.what()), "VALIDATION_EXCEPTION");
    }
}

std::optional<std::string> JwtValidator::validateAndGetUserId(const std::string& token) {
    auto result = validate(token);
    if (result.valid) {
        return result.user_id;
    }
    return std::nullopt;
}

void JwtValidator::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    blacklist_checker_ = nullptr;
    LOG_INFO("JWT Validator 资源已清理");
}

bool JwtValidator::parseToken(const std::string& token,
                              std::string& header_b64,
                              std::string& payload_b64,
                              std::string& signature_b64) {
    // JWT 格式: header.payload.signature
    size_t first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return false;
    }

    size_t second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return false;
    }

    // 检查是否有多余的点
    if (token.find('.', second_dot + 1) != std::string::npos) {
        return false;
    }

    header_b64 = token.substr(0, first_dot);
    payload_b64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
    signature_b64 = token.substr(second_dot + 1);

    return !header_b64.empty() && !payload_b64.empty() && !signature_b64.empty();
}

std::string JwtValidator::base64UrlDecode(const std::string& input) {
    try {
        return common::utils::Crypto::base64UrlDecode(input);
    } catch (const std::exception& e) {
        LOG_ERROR("Base64URL 解码失败: " + std::string(e.what()));
        return "";
    }
}

bool JwtValidator::verifySignature(const std::string& header_b64,
                                   const std::string& payload_b64,
                                   const std::string& signature_b64) {
    try {
        // 构造签名输入: header.payload（不带签名部分）
        std::string signing_input = header_b64 + "." + payload_b64;

        // 使用 HMAC-SHA256 计算签名
        std::string expected_signature = common::utils::Crypto::hmacSha256Raw(
            signing_input, config_.secret_key);

        // Base64URL 编码期望的签名
        std::string expected_signature_b64 = common::utils::Crypto::base64UrlEncode(
            expected_signature);

        // 比较签名（常量时间比较，防止时序攻击）
        if (expected_signature_b64.length() != signature_b64.length()) {
            return false;
        }

        int result = 0;
        for (size_t i = 0; i < signature_b64.length(); ++i) {
            result |= (static_cast<unsigned char>(expected_signature_b64[i]) ^
                      static_cast<unsigned char>(signature_b64[i]));
        }

        return result == 0;

    } catch (const std::exception& e) {
        LOG_ERROR("签名验证异常: " + std::string(e.what()));
        return false;
    }
}

JwtValidationResult JwtValidator::parseAndValidateClaims(const std::string& payload_json) {
    try {
        nlohmann::json payload = nlohmann::json::parse(payload_json);

        JwtValidationResult result = JwtValidationResult::success();

        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        // 1. 验证过期时间 (exp)
        if (config_.validate_expiration && payload.contains("exp")) {
            int64_t exp = payload["exp"].get<int64_t>();
            result.expires_at = exp;

            if (exp < now_ts - config_.clock_skew_seconds) {
                result.expired = true;
                return JwtValidationResult::failure("Token 已过期", "TOKEN_EXPIRED");
            }
        }

        // 2. 验证生效时间 (nbf)
        if (payload.contains("nbf")) {
            int64_t nbf = payload["nbf"].get<int64_t>();
            if (nbf > now_ts + config_.clock_skew_seconds) {
                JwtValidationResult nbf_result = JwtValidationResult::failure(
                    "Token 尚未生效", "TOKEN_NOT_ACTIVE");
                nbf_result.not_active_yet = true;
                return nbf_result;
            }
        }

        // 3. 验证发行者 (iss)
        if (config_.validate_issuer && payload.contains("iss")) {
            std::string iss = payload["iss"].get<std::string>();
            result.issuer = iss;

            if (iss != config_.issuer) {
                return JwtValidationResult::failure(
                    "发行者不匹配: 期望 " + config_.issuer + ", 实际 " + iss,
                    "INVALID_ISSUER");
            }
        }

        // 4. 验证受众 (aud)
        if (config_.validate_audience && payload.contains("aud")) {
            if (payload["aud"].is_string()) {
                std::string aud = payload["aud"].get<std::string>();
                result.audience = aud;

                if (aud != config_.audience) {
                    return JwtValidationResult::failure("受众不匹配", "INVALID_AUDIENCE");
                }
            } else if (payload["aud"].is_array()) {
                // aud 可能是数组
                bool found = false;
                for (const auto& aud_item : payload["aud"]) {
                    if (aud_item.get<std::string>() == config_.audience) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return JwtValidationResult::failure("受众不匹配", "INVALID_AUDIENCE");
                }
            }
        }

        // 5. 提取标准 Claims
        if (payload.contains("iat")) {
            result.issued_at = payload["iat"].get<int64_t>();
        }

        if (payload.contains("jti")) {
            result.jti = payload["jti"].get<std::string>();
        }

        // 6. 提取用户信息
        if (payload.contains("sub")) {
            result.user_id = payload["sub"].get<std::string>();
        } else if (payload.contains("user_id")) {
            result.user_id = payload["user_id"].get<std::string>();
        }

        if (payload.contains("username")) {
            result.username = payload["username"].get<std::string>();
        }

        if (payload.contains("email")) {
            result.email = payload["email"].get<std::string>();
        }

        if (payload.contains("session_id")) {
            result.session_id = payload["session_id"].get<std::string>();
        }

        if (payload.contains("token_type")) {
            result.token_type = payload["token_type"].get<std::string>();
        }

        if (payload.contains("roles")) {
            if (payload["roles"].is_array()) {
                for (const auto& role : payload["roles"]) {
                    result.roles.push_back(role.get<std::string>());
                }
            }
        }

        // 7. 检查必要的字段
        if (result.user_id.empty()) {
            return JwtValidationResult::failure("Token 缺少用户标识", "MISSING_USER_ID");
        }

        return result;

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JSON 解析错误: " + std::string(e.what()));
        return JwtValidationResult::failure("Payload JSON 解析失败", "JSON_PARSE_ERROR");
    } catch (const std::exception& e) {
        LOG_ERROR("Claims 解析异常: " + std::string(e.what()));
        return JwtValidationResult::failure("Claims 解析异常", "CLAIMS_PARSE_ERROR");
    }
}

} // namespace auth
} // namespace common
