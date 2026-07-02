/**
 * @file sms_service.h
 * @brief 短信服务工具类 - 支持阿里云和腾讯云
 * @author AI Assistant
 * @date 2025-12-05
 * @version 1.0
 *
 * 功能特性：
 * - 支持阿里云短信服务
 * - 支持腾讯云短信服务
 * - 自动从配置文件加载配置
 * - 线程安全的短信发送
 * - 模板消息支持
 * - 发送记录和日志
 */

#ifndef SMS_SERVICE_H
#define SMS_SERVICE_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

namespace common {
namespace utils {

/**
 * @brief 短信服务提供商枚举
 */
enum class SmsProvider {
    ALIYUN,      ///< 阿里云短信
    TENCENT,     ///< 腾讯云短信
    AUTO         ///< 自动选择（优先使用阿里云）
};

/**
 * @brief 短信发送结果
 */
struct SmsSendResult {
    bool success;                  ///< 是否成功
    std::string request_id;        ///< 请求ID
    std::string biz_id;            ///< 业务ID（阿里云）或短信发送流水号（腾讯云）
    std::string message;           ///< 返回消息
    std::string error_code;        ///< 错误代码
    SmsProvider provider;          ///< 使用的服务商
    
    SmsSendResult() : success(false), provider(SmsProvider::AUTO) {}
};

/**
 * @brief 阿里云短信配置
 */
struct AliyunSmsConfig {
    std::string access_key_id;          ///< AccessKey ID
    std::string access_key_secret;      ///< AccessKey Secret
    std::string sign_name;              ///< 短信签名
    std::string region_id = "cn-hangzhou";  ///< 地域ID，默认杭州
    std::string endpoint = "dysmsapi.aliyuncs.com";  ///< 接入点
    int timeout_ms = 10000;             ///< 超时时间（毫秒）
    bool enable = true;                 ///< 是否启用
    
    /**
     * @brief 从配置管理器加载阿里云短信配置
     * @return 阿里云短信配置实例
     */
    static AliyunSmsConfig fromConfigManager();
    
    /**
     * @brief 验证配置的有效性
     * @throws std::invalid_argument 配置无效时抛出异常
     */
    void validate() const;
};

/**
 * @brief 腾讯云短信配置
 */
struct TencentSmsConfig {
    std::string secret_id;              ///< SecretId
    std::string secret_key;             ///< SecretKey
    std::string app_id;                 ///< 短信应用ID（SDK AppID）
    std::string sign_name;              ///< 短信签名内容
    std::string region = "ap-guangzhou"; ///< 地域，默认广州
    std::string endpoint = "sms.tencentcloudapi.com"; ///< 接入点
    int timeout_ms = 10000;             ///< 超时时间（毫秒）
    bool enable = true;                 ///< 是否启用
    
    /**
     * @brief 从配置管理器加载腾讯云短信配置
     * @return 腾讯云短信配置实例
     */
    static TencentSmsConfig fromConfigManager();
    
    /**
     * @brief 验证配置的有效性
     * @throws std::invalid_argument 配置无效时抛出异常
     */
    void validate() const;
};

/**
 * @brief 短信服务统一配置
 */
struct SmsServiceConfig {
    SmsProvider default_provider = SmsProvider::AUTO;  ///< 默认服务商
    AliyunSmsConfig aliyun;         ///< 阿里云配置
    TencentSmsConfig tencent;       ///< 腾讯云配置
    bool enable_fallback = true;    ///< 是否启用降级（主服务商失败时切换）
    int max_retries = 2;            ///< 最大重试次数
    int retry_delay_ms = 1000;      ///< 重试延迟（毫秒）
    bool enable_log = true;         ///< 是否启用日志
    
    /**
     * @brief 从配置管理器加载短信服务配置
     * @return 短信服务配置实例
     */
    static SmsServiceConfig fromConfigManager();
    
    /**
     * @brief 验证配置的有效性
     * @throws std::invalid_argument 配置无效时抛出异常
     */
    void validate() const;
};

/**
 * @brief 短信服务类
 * 
 * 提供统一的短信发送接口，支持阿里云和腾讯云两种服务商。
 * 使用单例模式确保全局唯一性。
 */
class SmsService {
public:
    /**
     * @brief 获取单例实例
     * @return SmsService实例引用
     */
    static SmsService& getInstance();
    
    /**
     * @brief 初始化短信服务
     * @param config 短信服务配置
     * @return 是否初始化成功
     */
    bool initialize(const SmsServiceConfig& config);
    
    /**
     * @brief 使用默认配置初始化（从配置管理器加载）
     * @return 是否初始化成功
     */
    bool initialize();
    
    /**
     * @brief 发送短信（使用默认服务商）
     * @param phone_number 手机号码
     * @param template_code 模板代码
     * @param template_params 模板参数（键值对）
     * @return 发送结果
     */
    SmsSendResult sendSms(
        const std::string& phone_number,
        const std::string& template_code,
        const std::map<std::string, std::string>& template_params
    );
    
    /**
     * @brief 发送短信（指定服务商）
     * @param phone_number 手机号码
     * @param template_code 模板代码
     * @param template_params 模板参数
     * @param provider 指定的服务商
     * @return 发送结果
     */
    SmsSendResult sendSms(
        const std::string& phone_number,
        const std::string& template_code,
        const std::map<std::string, std::string>& template_params,
        SmsProvider provider
    );
    
    /**
     * @brief 批量发送短信
     * @param phone_numbers 手机号码列表
     * @param template_code 模板代码
     * @param template_params 模板参数
     * @return 发送结果列表
     */
    std::vector<SmsSendResult> sendBatchSms(
        const std::vector<std::string>& phone_numbers,
        const std::string& template_code,
        const std::map<std::string, std::string>& template_params
    );
    
    /**
     * @brief 发送验证码短信（快捷方法）
     * @param phone_number 手机号码
     * @param code 验证码
     * @param template_code 模板代码（可选，使用默认模板）
     * @return 发送结果
     */
    SmsSendResult sendVerificationCode(
        const std::string& phone_number,
        const std::string& code,
        const std::string& template_code = ""
    );
    
    /**
     * @brief 获取当前配置
     * @return 当前短信服务配置
     */
    SmsServiceConfig getConfig() const;
    
    /**
     * @brief 设置默认服务商
     * @param provider 服务商类型
     */
    void setDefaultProvider(SmsProvider provider);
    
    /**
     * @brief 检查服务商是否可用
     * @param provider 服务商类型
     * @return 是否可用
     */
    bool isProviderAvailable(SmsProvider provider) const;
    
    // 禁用拷贝和移动
    SmsService(const SmsService&) = delete;
    SmsService& operator=(const SmsService&) = delete;
    SmsService(SmsService&&) = delete;
    SmsService& operator=(SmsService&&) = delete;
    
private:
    SmsService();
    ~SmsService();
    
    /**
     * @brief 使用阿里云发送短信
     * @param phone_number 手机号码
     * @param template_code 模板代码
     * @param template_params 模板参数
     * @return 发送结果
     */
    SmsSendResult sendAliyunSms(
        const std::string& phone_number,
        const std::string& template_code,
        const std::map<std::string, std::string>& template_params
    );
    
    /**
     * @brief 使用腾讯云发送短信
     * @param phone_number 手机号码
     * @param template_code 模板代码
     * @param template_params 模板参数
     * @return 发送结果
     */
    SmsSendResult sendTencentSms(
        const std::string& phone_number,
        const std::string& template_code,
        const std::map<std::string, std::string>& template_params
    );
    
    /**
     * @brief 验证手机号码格式
     * @param phone_number 手机号码
     * @return 是否有效
     */
    bool validatePhoneNumber(const std::string& phone_number) const;
    
    /**
     * @brief 记录发送日志
     * @param phone_number 手机号码
     * @param template_code 模板代码
     * @param result 发送结果
     */
    void logSmsRecord(
        const std::string& phone_number,
        const std::string& template_code,
        const SmsSendResult& result
    );
    
    /**
     * @brief 获取可用的服务商（根据配置和降级策略）
     * @param preferred_provider 优先服务商
     * @return 实际使用的服务商
     */
    SmsProvider getAvailableProvider(SmsProvider preferred_provider) const;
    
    /**
     * @brief URL编码辅助函数
     * @param str 待编码字符串
     * @return 编码后的字符串
     */
    static std::string urlEncode(const std::string& str);
    
    /**
     * @brief 计算阿里云签名
     * @param params 请求参数
     * @param access_key_secret 密钥
     * @return 签名字符串
     */
    static std::string calculateAliyunSignature(
        const std::map<std::string, std::string>& params,
        const std::string& access_key_secret
    );
    
    SmsServiceConfig config_;           ///< 服务配置
    mutable std::mutex mutex_;          ///< 线程安全锁
    bool initialized_;                  ///< 是否已初始化
    
    // 统计信息
    struct Statistics {
        uint64_t total_sent = 0;        ///< 总发送次数
        uint64_t total_success = 0;     ///< 成功次数
        uint64_t total_failed = 0;      ///< 失败次数
        uint64_t aliyun_sent = 0;       ///< 阿里云发送次数
        uint64_t tencent_sent = 0;      ///< 腾讯云发送次数
        std::chrono::system_clock::time_point last_send_time;  ///< 最后发送时间
    };
    Statistics stats_;                  ///< 统计信息
};

} // namespace utils
} // namespace common

#endif // SMS_SERVICE_H
