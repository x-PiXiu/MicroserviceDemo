/**
 * @file email_service.cpp
 * @brief 邮件服务实现 - 基于 libcurl SMTP
 * @details 使用 libcurl 的 SMTP 协议支持实现邮件发送，
 *          自动处理 SSL/TLS、DNS 解析、超时控制等底层细节
 * @date 2026-04-24
 */

#include "common/utils/email_service.h"
#include "common/config/config_manager.h"
#include "common/logger/logger.h"

#include <curl/curl.h>

#include <regex>
#include <sstream>
#include <random>
#include <cstring>
#include <chrono>

namespace common {
namespace utils {

// ==================== libcurl Callbacks (file-scope static) ====================

namespace {

struct EmailUploadData {
    const std::string* data;
    size_t pos;
};

size_t emailReadCallback(char* ptr, size_t size, size_t nmemb, void* userp) {
    auto* upload = static_cast<EmailUploadData*>(userp);
    if (!upload || !upload->data) return 0;

    size_t remaining = upload->data->size() - upload->pos;
    size_t max_bytes = size * nmemb;
    if (remaining == 0) return 0;

    size_t to_copy = (remaining < max_bytes) ? remaining : max_bytes;
    memcpy(ptr, upload->data->data() + upload->pos, to_copy);
    upload->pos += to_copy;
    return to_copy;
}

int emailDebugCallback(CURL*, curl_infotype type, char* data, size_t size, void*) {
    if (!data || size == 0) return 0;
    if (type == CURLINFO_HEADER_IN || type == CURLINFO_DATA_IN) {
        std::string msg(data, size);
        while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) {
            msg.pop_back();
        }
        if (!msg.empty()) {
            LOG_DEBUG("SMTP < " + msg);
        }
    }
    return 0;
}

} // anonymous namespace

// ==================== EmailServiceConfig ====================

bool EmailServiceConfig::validate() const {
    if (!enable) return true;

    if (smtp_host.empty()) {
        LOG_ERROR("EmailService: smtp_host cannot be empty");
        return false;
    }
    if (smtp_port <= 0 || smtp_port > 65535) {
        LOG_ERROR("EmailService: smtp_port invalid");
        return false;
    }
    if (smtp_user.empty()) {
        LOG_ERROR("EmailService: smtp_user cannot be empty");
        return false;
    }
    if (smtp_password.empty()) {
        LOG_ERROR("EmailService: smtp_password cannot be empty");
        return false;
    }
    return true;
}

EmailServiceConfig EmailServiceConfig::fromConfigManager() {
    auto& cm = config::ConfigManager::getInstance();

    EmailServiceConfig config;
    config.smtp_host = cm.get<std::string>("email.smtp_host", "smtp.163.com");
    config.smtp_port = cm.get<int>("email.smtp_port", 465);
    config.smtp_user = cm.get<std::string>("email.smtp_user", "");
    config.smtp_password = cm.get<std::string>("email.smtp_password", "");
    config.from_address = cm.get<std::string>("email.from_address", "");
    config.from_name = cm.get<std::string>("email.from_name", "Game Platform");
    config.connection_timeout_ms = cm.get<int>("email.connection_timeout_ms", 10000);
    config.read_timeout_ms = cm.get<int>("email.read_timeout_ms", 10000);
    config.total_timeout_ms = cm.get<int>("email.total_timeout_ms", 30000);
    config.enable = cm.get<bool>("email.enable", true);

    if (config.from_address.empty()) {
        config.from_address = config.smtp_user;
    }

    return config;
}

// ==================== EmailService ====================

EmailService::EmailService() = default;

EmailService::~EmailService() {
    if (curl_initialized_) {
        curl_global_cleanup();
        curl_initialized_ = false;
    }
};

EmailService& EmailService::getInstance() {
    static EmailService instance;
    return instance;
}

bool EmailService::initialize(const EmailServiceConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!config.validate()) {
        LOG_ERROR("EmailService: config validation failed");
        return false;
    }

    // Initialize libcurl globally (must be called from main thread, NOT thread-safe)
    if (!curl_initialized_) {
        CURLcode global_res = curl_global_init(CURL_GLOBAL_ALL);
        if (global_res != CURLE_OK) {
            LOG_ERROR("EmailService: curl_global_init failed: " + std::string(curl_easy_strerror(global_res)));
            return false;
        }
        curl_initialized_ = true;
        LOG_INFO("EmailService: libcurl global init OK");
    }

    config_ = config;
    initialized_ = true;

    LOG_INFO("EmailService initialized");
    LOG_INFO("  - SMTP: " + config_.smtp_host + ":" + std::to_string(config_.smtp_port));
    LOG_INFO("  - From: " + config_.from_name + " <" + config_.from_address + ">");

    return true;
}

bool EmailService::initialize() {
    try {
        auto config = EmailServiceConfig::fromConfigManager();
        return initialize(config);
    } catch (const std::exception& e) {
        LOG_ERROR("EmailService: load config failed: " + std::string(e.what()));
        return false;
    }
}

EmailSendResult EmailService::sendEmail(const std::string& to,
                                         const std::string& subject,
                                         const std::string& body) {
    return sendSmtp(to, subject, body, false);
}

EmailSendResult EmailService::sendHtmlEmail(const std::string& to,
                                            const std::string& subject,
                                            const std::string& html_body) {
    return sendSmtp(to, subject, html_body, true);
}

EmailSendResult EmailService::sendVerificationCodeEmail(const std::string& to,
                                                         const std::string& code,
                                                         const std::string& purpose,
                                                         int ttl_minutes) {
    std::string from_name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        from_name = config_.from_name;
    }
    std::string subject = "[" + from_name + "] " + purpose + " verification code";
    std::string html = buildVerificationCodeHtml(code, purpose, ttl_minutes);
    return sendSmtp(to, subject, html, true);
}

EmailServiceConfig EmailService::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

// ==================== SMTP via libcurl ====================

EmailSendResult EmailService::sendSmtp(const std::string& to,
                                        const std::string& subject,
                                        const std::string& body,
                                        bool is_html) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return {false, "Email service not initialized", "NOT_INITIALIZED"};
    }

    total_sent_++;

    LOG_INFO("EmailService: sending email to " + to + ", subject: " + subject);

    // Build SMTP URL: smtp://host:port or smtps://host:port
    std::string smtp_url;
    bool use_implicit_ssl = (config_.smtp_port == 465);
    if (use_implicit_ssl) {
        smtp_url = "smtps://" + config_.smtp_host + ":" + std::to_string(config_.smtp_port);
    } else {
        smtp_url = "smtp://" + config_.smtp_host + ":" + std::to_string(config_.smtp_port);
    }

    // Build email payload (RFC 5322)
    std::string from_header = config_.from_name + " <" + config_.from_address + ">";
    std::string payload;
    payload += "From: " + from_header + "\r\n";
    payload += "To: <" + to + ">\r\n";
    payload += "Subject: =?UTF-8?B?" + base64Encode(subject) + "?=\r\n";
    payload += "MIME-Version: 1.0\r\n";

    if (is_html) {
        payload += "Content-Type: text/html; charset=\"UTF-8\"\r\n";
    } else {
        payload += "Content-Type: text/plain; charset=\"UTF-8\"\r\n";
    }

    payload += "Content-Transfer-Encoding: base64\r\n";
    payload += "\r\n";

    // Split base64 encoded body into 76-char lines (RFC requirement)
    std::string encoded_body = base64Encode(body);
    for (size_t i = 0; i < encoded_body.size(); i += 76) {
        payload += encoded_body.substr(i, 76) + "\r\n";
    }
    payload += "\r\n";

    // Prepare recipients list
    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, to.c_str());

    // Upload data for read callback
    EmailUploadData upload{&payload, 0};

    CURL* curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(recipients);
        total_failed_++;
        return {false, "Failed to init libcurl", "CURL_INIT_ERROR"};
    }

    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, smtp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, config_.from_address.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, emailReadCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(payload.size()));

    // Authentication
    curl_easy_setopt(curl, CURLOPT_USERNAME, config_.smtp_user.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, config_.smtp_password.c_str());

    // SSL/TLS configuration
    if (use_implicit_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        // STARTTLS for port 25/587
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    }

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(config_.connection_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.total_timeout_ms));

    // Debug
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, emailDebugCallback);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    // Perform the SMTP transfer
    CURLcode res = curl_easy_perform(curl);

    // Check result
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    std::string curl_error = curl_easy_strerror(res);

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(recipients);

    if (res != CURLE_OK) {
        total_failed_++;
        std::string error_detail = "SMTP send failed: " + curl_error;

        // Map curl errors to specific error codes
        std::string error_code;
        switch (res) {
            case CURLE_COULDNT_RESOLVE_HOST:
                error_code = "DNS_ERROR";
                error_detail = "DNS resolution failed: " + config_.smtp_host;
                break;
            case CURLE_COULDNT_CONNECT:
                error_code = "CONNECT_ERROR";
                error_detail = "Cannot connect to " + config_.smtp_host + ":" + std::to_string(config_.smtp_port);
                break;
            case CURLE_SSL_CONNECT_ERROR:
                error_code = "SSL_ERROR";
                error_detail = "SSL handshake failed: " + curl_error;
                break;
            case CURLE_LOGIN_DENIED:
                error_code = "AUTH_ERROR";
                error_detail = "SMTP authentication failed";
                break;
            case CURLE_OPERATION_TIMEDOUT:
                error_code = "TIMEOUT";
                error_detail = "SMTP operation timed out (" + std::to_string(config_.total_timeout_ms) + "ms)";
                break;
            default:
                error_code = "SMTP_ERROR";
                break;
        }

        LOG_ERROR("EmailService: " + error_detail);
        return {false, error_detail, error_code};
    }

    total_success_++;
    std::string masked_to = to.length() > 5
        ? to.substr(0, 3) + "***" + to.substr(to.find('@'))
        : to;
    LOG_INFO("Email sent successfully -> " + masked_to + ", subject: " + subject);
    return {true, "Email sent successfully"};
}

// ==================== Verification Code HTML Template ====================

std::string EmailService::buildVerificationCodeHtml(const std::string& code,
                                                      const std::string& purpose,
                                                      int ttl_minutes) {
    std::stringstream ss;
    ss << "<!DOCTYPE html>"
       << "<html><head><meta charset=\"UTF-8\">"
       << "<style>"
       << "body{font-family:Arial,sans-serif;background:#f5f5f5;margin:0;padding:20px;}"
       << ".container{max-width:600px;margin:0 auto;background:#fff;border-radius:8px;"
       << "overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,0.1);}"
       << ".header{background:#4A90D9;color:#fff;padding:30px;text-align:center;}"
       << ".header h1{margin:0;font-size:24px;}"
       << ".content{padding:30px;}"
       << ".code{font-size:36px;font-weight:bold;color:#4A90D9;text-align:center;"
       << "letter-spacing:8px;padding:20px;margin:20px 0;background:#f0f7ff;"
       << "border-radius:8px;border:2px dashed #4A90D9;}"
       << ".footer{text-align:center;color:#999;padding:20px;font-size:12px;}"
       << "</style></head><body>"
       << "<div class=\"container\">"
       << "<div class=\"header\"><h1>" << purpose << " verification code</h1></div>"
       << "<div class=\"content\">"
       << "<p>Hello, you are performing <strong>" << purpose << "</strong> operation.</p>"
       << "<p>Your verification code is:</p>"
       << "<div class=\"code\">" << code << "</div>"
       << "<p>This code expires in <strong>" << ttl_minutes << " minutes</strong>.</p>"
       << "<p>If you did not request this, please ignore this email.</p>"
       << "</div>"
       << "<div class=\"footer\">"
       << "<p>This is an automated message, please do not reply.</p>"
       << "</div></div></body></html>";
    return ss.str();
}

// ==================== Base64 Encoding ====================

std::string EmailService::base64Encode(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t in_len = input.size();
    const unsigned char* bytes_to_encode = reinterpret_cast<const unsigned char*>(input.data());

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++) {
                output += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++) {
            output += base64_chars[char_array_4[j]];
        }

        while (i++ < 3) {
            output += '=';
        }
    }

    return output;
}

} // namespace utils
} // namespace common
