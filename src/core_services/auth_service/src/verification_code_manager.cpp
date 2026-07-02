/**
 * @file verification_code_manager.cpp
 * @brief 验证码管理器实现
 * @date 2026-04-13
 */

#include "verification_code_manager.h"
#include "common/logger/logger.h"
#include "common/utils/crypto.h"
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <chrono>
#include <regex>
#include <iomanip>
#include <thread>

namespace core_services {
namespace auth_service {

// ==================== VerifyPurpose 辅助 ====================

std::string verifyPurposeToString(VerifyPurpose purpose) {
    switch (purpose) {
        case VerifyPurpose::REGISTER:       return "register";
        case VerifyPurpose::LOGIN:          return "login";
        case VerifyPurpose::RESET_PASSWORD: return "reset_password";
        default: return "unknown";
    }
}

VerifyPurpose parseVerifyPurpose(const std::string& str) {
    if (str == "register")       return VerifyPurpose::REGISTER;
    if (str == "login")          return VerifyPurpose::LOGIN;
    if (str == "reset_password") return VerifyPurpose::RESET_PASSWORD;
    return VerifyPurpose::REGISTER; // 默认
}

// ==================== VerificationCodeConfig ====================

VerificationCodeConfig VerificationCodeConfig::fromConfigManager() {
    auto& cm = common::config::ConfigManager::getInstance();
    VerificationCodeConfig config;
    config.code_length = cm.get<int>("email.code_length", 6);
    config.code_ttl_seconds = cm.get<int>("email.code_ttl_seconds", 300);
    config.send_cooldown_seconds = cm.get<int>("email.send_cooldown_seconds", 60);
    config.max_send_per_day = cm.get<int>("email.max_send_per_day", 10);
    config.verify_token_ttl_seconds = cm.get<int>("email.verify_token_ttl_seconds", 600);
    return config;
}

// ==================== VerificationCodeManager ====================

VerificationCodeManager::VerificationCodeManager(common::database::RedisPool* redis_pool,
                                                   const VerificationCodeConfig& config)
    : redis_pool_(redis_pool), config_(config) {}

std::string VerificationCodeManager::generateCode() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9);

    std::stringstream ss;
    for (int i = 0; i < config_.code_length; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

std::string VerificationCodeManager::generateVerifyToken() {
    // 生成基于随机数的 token
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);

    return "vtoken_" + std::to_string(ts) + "_" + std::to_string(dis(gen));
}

std::string VerificationCodeManager::buildCodeKey(const std::string& email, VerifyPurpose purpose) {
    return "auth:verify_code:" + verifyPurposeToString(purpose) + ":" + email;
}

std::string VerificationCodeManager::buildCooldownKey(const std::string& email) {
    return "auth:verify_cooldown:" + email;
}

std::string VerificationCodeManager::buildDailyCountKey(const std::string& email) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::stringstream ss;
    ss << "auth:verify_daily:" << tm_now.tm_year + 1900 << "-"
       << std::setfill('0') << std::setw(2) << tm_now.tm_mon + 1 << "-"
       << std::setw(2) << tm_now.tm_mday << ":" << email;
    return ss.str();
}

std::string VerificationCodeManager::buildTokenKey(const std::string& token) {
    return "auth:verify_token:" + token;
}

std::string VerificationCodeManager::buildTokenEmailKey(const std::string& email) {
    return "auth:verify_token_email:" + email;
}

int VerificationCodeManager::checkCooldown(const std::string& email) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) return -1;

        std::string key = buildCooldownKey(email);
        std::string remaining = conn->get(key);
        if (!remaining.empty()) {
            return std::stoi(remaining);
        }
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR("检查发送冷却异常: " + std::string(e.what()));
        return -1;
    }
}

bool VerificationCodeManager::checkDailyLimit(const std::string& email) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) return false;

        std::string key = buildDailyCountKey(email);
        std::string count_str = conn->get(key);
        if (!count_str.empty()) {
            int count = std::stoi(count_str);
            if (count >= config_.max_send_per_day) {
                return false; // 超过每日上限
            }
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("检查每日发送上限异常: " + std::string(e.what()));
        return false;
    }
}

void VerificationCodeManager::recordSend(const std::string& email) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) return;

        // 设置冷却时间
        std::string cooldown_key = buildCooldownKey(email);
        conn->set(cooldown_key, std::to_string(config_.send_cooldown_seconds),
                  config_.send_cooldown_seconds);

        // 递增每日计数
        std::string daily_key = buildDailyCountKey(email);
        int64_t new_count = conn->incr(daily_key);
        if (new_count == 1) {
            // 首次发送，设置过期时间为到当天结束
            // 简单处理：设置 24 小时过期
            conn->expire(daily_key, 86400);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("记录发送次数异常: " + std::string(e.what()));
    }
}

SendCodeResult VerificationCodeManager::sendCode(const std::string& email, VerifyPurpose purpose) {
    std::string code;
    std::string code_key;

    // ---- 同步阶段：快速校验 + 验证码生成 + Redis 存储 ----
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 验证邮箱格式
        std::regex email_regex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
        if (!std::regex_match(email, email_regex)) {
            return {false, "邮箱格式无效", "INVALID_EMAIL"};
        }

        // 2. 检查冷却时间
        int cooldown = checkCooldown(email);
        if (cooldown > 0) {
            return {false, "发送过于频繁，请稍后再试", "COOLDOWN", cooldown};
        }

        // 3. 检查每日发送上限
        if (!checkDailyLimit(email)) {
            return {false, "今日发送次数已达上限", "DAILY_LIMIT"};
        }

        // 4. 生成验证码
        code = generateCode();
        code_key = buildCodeKey(email, purpose);

        // 5. 存储到 Redis
        try {
            common::database::RedisConnectionGuard guard(*redis_pool_);
            auto conn = guard.get();
            if (!conn || !conn->isConnected()) {
                return {false, "Redis 连接失败", "REDIS_ERROR"};
            }

            bool stored = conn->set(code_key, code, config_.code_ttl_seconds);
            if (!stored) {
                return {false, "验证码存储失败", "REDIS_ERROR"};
            }
        } catch (const std::exception& e) {
            LOG_ERROR("存储验证码异常: " + std::string(e.what()));
            return {false, "验证码存储失败", "REDIS_ERROR"};
        }

        // 6. 记录发送（在邮件发送前记录，防止无限重试）
        recordSend(email);

    } // mutex_ 释放，后续邮件发送不再持有锁

    // ---- 异步阶段：后台发送邮件，不阻塞 HTTP 响应 ----
    std::string purpose_desc;
    switch (purpose) {
        case VerifyPurpose::REGISTER:       purpose_desc = "注册"; break;
        case VerifyPurpose::LOGIN:          purpose_desc = "登录"; break;
        case VerifyPurpose::RESET_PASSWORD: purpose_desc = "重置密码"; break;
    }
    int ttl_minutes = config_.code_ttl_seconds / 60;

    // 捕获 code 和 code_key 的副本到异步线程
    std::string async_code = code;
    std::string async_code_key = code_key;
    std::string async_email = email;

    std::thread([async_email, async_code, purpose_desc, ttl_minutes, async_code_key, this]() {
        try {
            LOG_INFO("异步邮件线程启动: 目标=" + async_email);
            auto& email_service = common::utils::EmailService::getInstance();
            if (!email_service.isInitialized()) {
                LOG_ERROR("异步邮件发送失败: 邮件服务未初始化");
                return;
            }
            auto send_result = email_service.sendVerificationCodeEmail(
                async_email, async_code, purpose_desc, ttl_minutes);

            if (!send_result.success) {
                // 邮件发送失败，清除 Redis 中的验证码
                LOG_ERROR("验证码邮件异步发送失败: " + send_result.message);
                try {
                    common::database::RedisConnectionGuard guard(*redis_pool_);
                    auto conn = guard.get();
                    if (conn) conn->del(async_code_key);
                } catch (...) {}
            } else {
                LOG_INFO("验证码邮件异步发送成功: " + async_email);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("异步邮件发送线程异常: " + std::string(e.what()));
        }
    }).detach();

    LOG_INFO("验证码已生成，邮件异步发送中: " + email + ", 用途: " + verifyPurposeToString(purpose));

    return {true, "验证码已发送", "", config_.send_cooldown_seconds};
}

VerifyCodeResult VerificationCodeManager::verifyCode(const std::string& email,
                                                      const std::string& code,
                                                      VerifyPurpose purpose) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) {
            return {false, "Redis 连接失败", "REDIS_ERROR"};
        }

        // 1. 从 Redis 获取存储的验证码
        std::string key = buildCodeKey(email, purpose);
        std::string stored_code = conn->get(key);

        if (stored_code.empty()) {
            return {false, "验证码已过期或不存在", "CODE_EXPIRED"};
        }

        // 2. 比对验证码
        if (stored_code != code) {
            return {false, "验证码错误", "CODE_MISMATCH"};
        }

        // 3. 验证成功，删除已用的验证码
        conn->del(key);

        // 4. 生成 verify_token 并存储
        std::string verify_token = generateVerifyToken();

        // 存储 token -> email 映射
        std::string token_key = buildTokenKey(verify_token);
        nlohmann::json token_data;
        token_data["email"] = email;
        token_data["purpose"] = verifyPurposeToString(purpose);
        conn->set(token_key, token_data.dump(), config_.verify_token_ttl_seconds);

        // 存储 email -> token 映射（防止重复验证）
        std::string email_token_key = buildTokenEmailKey(email);
        conn->set(email_token_key, verify_token, config_.verify_token_ttl_seconds);

        LOG_INFO("验证码校验成功: " + email + ", 用途: " + verifyPurposeToString(purpose));

        return {true, "验证码正确", "", verify_token};
    } catch (const std::exception& e) {
        LOG_ERROR("验证码校验异常: " + std::string(e.what()));
        return {false, "验证码校验失败", "INTERNAL_ERROR"};
    }
}

VerifyCodeResult VerificationCodeManager::verifyCodeDirect(const std::string& email,
                                                            const std::string& code,
                                                            VerifyPurpose purpose) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) {
            return {false, "Redis 连接失败", "REDIS_ERROR"};
        }

        std::string key = buildCodeKey(email, purpose);
        std::string stored_code = conn->get(key);

        if (stored_code.empty()) {
            return {false, "验证码已过期或不存在", "CODE_EXPIRED"};
        }

        if (stored_code != code) {
            return {false, "验证码错误", "CODE_MISMATCH"};
        }

        // 验证成功，删除已用的验证码
        conn->del(key);

        LOG_INFO("验证码直接校验成功: " + email + ", 用途: " + verifyPurposeToString(purpose));
        return {true, "验证码正确"};
    } catch (const std::exception& e) {
        LOG_ERROR("验证码校验异常: " + std::string(e.what()));
        return {false, "验证码校验失败", "INTERNAL_ERROR"};
    }
}

std::string VerificationCodeManager::verifyToken(const std::string& verify_token) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) return "";

        std::string key = buildTokenKey(verify_token);
        std::string data_str = conn->get(key);
        if (data_str.empty()) return "";

        auto data = nlohmann::json::parse(data_str);
        return data.value("email", "");
    } catch (const std::exception& e) {
        LOG_ERROR("验证 token 异常: " + std::string(e.what()));
        return "";
    }
}

void VerificationCodeManager::consumeToken(const std::string& verify_token) {
    try {
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn || !conn->isConnected()) return;

        std::string token_key = buildTokenKey(verify_token);
        std::string data_str = conn->get(token_key);
        if (!data_str.empty()) {
            auto data = nlohmann::json::parse(data_str);
            std::string email = data.value("email", "");

            // 删除 token -> email 映射
            conn->del(token_key);

            // 删除 email -> token 映射
            if (!email.empty()) {
                conn->del(buildTokenEmailKey(email));
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("消费 token 异常: " + std::string(e.what()));
    }
}

} // namespace auth_service
} // namespace core_services
