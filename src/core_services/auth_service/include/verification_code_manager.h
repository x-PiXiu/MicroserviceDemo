/**
 * @file verification_code_manager.h
 * @brief 验证码管理器 - 管理邮箱验证码的生成、存储、校验
 * @details 使用 Redis 存储验证码（带 TTL），集成 EmailService 发送邮件
 * @date 2026-04-13
 * @version 1.0
 */

#ifndef VERIFICATION_CODE_MANAGER_H
#define VERIFICATION_CODE_MANAGER_H

#include "common/database/redis_pool.h"
#include "common/utils/email_service.h"
#include <string>
#include <mutex>
#include <random>
#include <chrono>

namespace core_services {
namespace auth_service {

/**
 * @brief 验证码用途
 */
enum class VerifyPurpose {
    REGISTER,       // 注册
    LOGIN,          // 登录
    RESET_PASSWORD  // 重置密码
};

/**
 * @brief 将 VerifyPurpose 转为字符串
 */
std::string verifyPurposeToString(VerifyPurpose purpose);

/**
 * @brief 从字符串解析 VerifyPurpose
 */
VerifyPurpose parseVerifyPurpose(const std::string& str);

/**
 * @brief 验证码发送结果
 */
struct SendCodeResult {
    bool success = false;
    std::string message;
    std::string error_code;
    int cooldown_seconds = 0;  // 剩余冷却时间

    SendCodeResult() = default;
    SendCodeResult(bool s, const std::string& msg, const std::string& code = "", int cd = 0)
        : success(s), message(msg), error_code(code), cooldown_seconds(cd) {}
};

/**
 * @brief 验证码校验结果
 */
struct VerifyCodeResult {
    bool success = false;
    std::string message;
    std::string error_code;
    std::string verify_token;  // 验证成功后的临时 token

    VerifyCodeResult() = default;
    VerifyCodeResult(bool s, const std::string& msg, const std::string& code = "",
                     const std::string& token = "")
        : success(s), message(msg), error_code(code), verify_token(token) {}
};

/**
 * @brief 验证码管理器配置
 */
struct VerificationCodeConfig {
    int code_length = 6;                // 验证码位数
    int code_ttl_seconds = 300;         // 验证码有效期（5 分钟）
    int send_cooldown_seconds = 60;     // 发送冷却时间（60 秒）
    int max_send_per_day = 10;          // 每个邮箱每天最多发送次数
    int verify_token_ttl_seconds = 600; // 验证 token 有效期（10 分钟）

    static VerificationCodeConfig fromConfigManager();
};

/**
 * @brief 验证码管理器
 * @details 负责验证码的完整生命周期管理
 */
class VerificationCodeManager {
public:
    /**
     * @brief 构造函数
     * @param redis_pool Redis 连接池
     * @param config 配置
     */
    VerificationCodeManager(common::database::RedisPool* redis_pool,
                            const VerificationCodeConfig& config);

    /**
     * @brief 发送验证码
     * @param email 目标邮箱
     * @param purpose 用途
     * @return 发送结果
     */
    SendCodeResult sendCode(const std::string& email, VerifyPurpose purpose);

    /**
     * @brief 校验验证码
     * @param email 邮箱
     * @param code 用户输入的验证码
     * @param purpose 用途
     * @return 校验结果，成功时包含 verify_token
     */
    VerifyCodeResult verifyCode(const std::string& email,
                                const std::string& code,
                                VerifyPurpose purpose);

    /**
     * @brief 验证 verify_token 有效性（注册时使用）
     * @param verify_token 临时 token
     * @return 验证成功返回关联的邮箱，失败返回空
     */
    std::string verifyToken(const std::string& verify_token);

    /**
     * @brief 消费 verify_token（注册成功后调用，使其失效）
     */
    void consumeToken(const std::string& verify_token);

    /**
     * @brief 校验验证码但不生成 token（重置密码用，直接校验后执行操作）
     * @param email 邮箱
     * @param code 验证码
     * @param purpose 用途
     * @return 校验结果
     */
    VerifyCodeResult verifyCodeDirect(const std::string& email,
                                      const std::string& code,
                                      VerifyPurpose purpose);

private:
    /**
     * @brief 生成随机数字验证码
     */
    std::string generateCode();

    /**
     * @brief 生成 verify_token
     */
    std::string generateVerifyToken();

    /**
     * @brief 构建 Redis key
     */
    std::string buildCodeKey(const std::string& email, VerifyPurpose purpose);
    std::string buildCooldownKey(const std::string& email);
    std::string buildDailyCountKey(const std::string& email);
    std::string buildTokenKey(const std::string& token);
    std::string buildTokenEmailKey(const std::string& email);

    /**
     * @brief 检查发送频率限制
     * @return 0 表示可以发送，>0 表示剩余冷却秒数
     */
    int checkCooldown(const std::string& email);

    /**
     * @brief 检查每日发送上限
     */
    bool checkDailyLimit(const std::string& email);

    /**
     * @brief 记录发送次数
     */
    void recordSend(const std::string& email);

    common::database::RedisPool* redis_pool_;
    VerificationCodeConfig config_;
    mutable std::mutex mutex_;
};

} // namespace auth_service
} // namespace core_services

#endif // VERIFICATION_CODE_MANAGER_H
