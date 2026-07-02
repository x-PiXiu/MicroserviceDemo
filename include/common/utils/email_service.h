/**
 * @file email_service.h
 * @brief 邮件服务工具类 - 基于 SMTP 协议
 * @details 基于 libcurl SMTP 实现邮件发送，支持 SSL/TLS 加密连接
 * @date 2026-04-13
 * @version 1.0
 */

#ifndef EMAIL_SERVICE_H
#define EMAIL_SERVICE_H

#include <string>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>

namespace common {
namespace utils {

/**
 * @brief 邮件发送结果
 */
struct EmailSendResult {
    bool success = false;
    std::string message;
    std::string error_code;

    EmailSendResult() = default;
    EmailSendResult(bool s, const std::string& msg, const std::string& code = "")
        : success(s), message(msg), error_code(code) {}
};

/**
 * @brief 邮件服务配置
 */
struct EmailServiceConfig {
    std::string smtp_host = "smtp.163.com";
    int smtp_port = 465;                // SSL 端口
    std::string smtp_user;               // 完整邮箱地址
    std::string smtp_password;           // 授权码
    std::string from_address;            // 发件人地址
    std::string from_name;               // 发件人名称
    int connection_timeout_ms = 10000;
    int read_timeout_ms = 10000;
    int total_timeout_ms = 30000;        // SMTP 整体超时（含 DNS + TCP + SSL + SMTP 交互）
    bool enable = true;

    /**
     * @brief 验证配置有效性
     */
    bool validate() const;

    /**
     * @brief 从 ConfigManager 加载配置
     */
    static EmailServiceConfig fromConfigManager();
};

/**
 * @brief 邮件服务类
 * @details 基于 libcurl SMTP 的邮件发送服务，单例模式
 */
class EmailService {
public:
    /**
     * @brief 获取单例实例
     */
    static EmailService& getInstance();

    /**
     * @brief 初始化邮件服务
     */
    bool initialize(const EmailServiceConfig& config);

    /**
     * @brief 使用默认配置初始化
     */
    bool initialize();

    /**
     * @brief 发送纯文本邮件
     */
    EmailSendResult sendEmail(const std::string& to,
                              const std::string& subject,
                              const std::string& body);

    /**
     * @brief 发送 HTML 邮件
     */
    EmailSendResult sendHtmlEmail(const std::string& to,
                                  const std::string& subject,
                                  const std::string& html_body);

    /**
     * @brief 发送验证码邮件（快捷方法）
     * @param to 收件人邮箱
     * @param code 验证码
     * @param purpose 用途描述（如"注册"、"登录"、"重置密码"）
     * @param ttl_minutes 验证码有效期（分钟）
     */
    EmailSendResult sendVerificationCodeEmail(const std::string& to,
                                               const std::string& code,
                                               const std::string& purpose = "验证",
                                               int ttl_minutes = 5);

    bool isInitialized() const { return initialized_; }
    EmailServiceConfig getConfig() const;

    EmailService(const EmailService&) = delete;
    EmailService& operator=(const EmailService&) = delete;

private:
    EmailService();
    ~EmailService();

    /**
     * @brief SMTP 连接和发送核心实现
     */
    EmailSendResult sendSmtp(const std::string& to,
                             const std::string& subject,
                             const std::string& body,
                             bool is_html);

    /**
     * @brief Base64 编码
     */
    static std::string base64Encode(const std::string& input);

    /**
     * @brief 构建验证码邮件 HTML 内容
     */
    static std::string buildVerificationCodeHtml(const std::string& code,
                                                  const std::string& purpose,
                                                  int ttl_minutes);

    EmailServiceConfig config_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool curl_initialized_ = false;

    // 统计信息
    std::atomic<uint64_t> total_sent_{0};
    std::atomic<uint64_t> total_success_{0};
    std::atomic<uint64_t> total_failed_{0};
};

} // namespace utils
} // namespace common

#endif // EMAIL_SERVICE_H
