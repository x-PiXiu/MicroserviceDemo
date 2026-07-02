/**
 * @file sms_service.cpp
 * @brief 短信服务实现
 */

#include "common/utils/sms_service.h"
#include "common/config/config_manager.h"
#include "common/logger/logger.h"
#include "common/utils/crypto.h"
#include "common/http/http_client.h"
#include <regex>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <nlohmann/json.hpp>
#include <random>
#include <chrono>
#include <algorithm>

using json = nlohmann::json;

namespace common {
namespace utils {

// ==================== AliyunSmsConfig ====================

AliyunSmsConfig AliyunSmsConfig::fromConfigManager() {
    auto& cm = config::ConfigManager::getInstance();
    
    AliyunSmsConfig config;
    config.access_key_id = cm.get<std::string>("sms.aliyun.access_key_id", "");
    config.access_key_secret = cm.get<std::string>("sms.aliyun.access_key_secret", "");
    config.sign_name = cm.get<std::string>("sms.aliyun.sign_name", "");
    config.region_id = cm.get<std::string>("sms.aliyun.region_id", "cn-hangzhou");
    config.endpoint = cm.get<std::string>("sms.aliyun.endpoint", "dysmsapi.aliyuncs.com");
    config.timeout_ms = cm.get<int>("sms.aliyun.timeout_ms", 10000);
    config.enable = cm.get<bool>("sms.aliyun.enable", true);
    
    return config;
}

void AliyunSmsConfig::validate() const {
    if (enable) {
        if (access_key_id.empty()) {
            throw std::invalid_argument("阿里云短信配置错误: access_key_id不能为空");
        }
        if (access_key_secret.empty()) {
            throw std::invalid_argument("阿里云短信配置错误: access_key_secret不能为空");
        }
        if (sign_name.empty()) {
            throw std::invalid_argument("阿里云短信配置错误: sign_name不能为空");
        }
        if (timeout_ms <= 0) {
            throw std::invalid_argument("阿里云短信配置错误: timeout_ms必须大于0");
        }
    }
}

// ==================== TencentSmsConfig ====================

TencentSmsConfig TencentSmsConfig::fromConfigManager() {
    auto& cm = config::ConfigManager::getInstance();
    
    TencentSmsConfig config;
    config.secret_id = cm.get<std::string>("sms.tencent.secret_id", "");
    config.secret_key = cm.get<std::string>("sms.tencent.secret_key", "");
    config.app_id = cm.get<std::string>("sms.tencent.app_id", "");
    config.sign_name = cm.get<std::string>("sms.tencent.sign_name", "");
    config.region = cm.get<std::string>("sms.tencent.region", "ap-guangzhou");
    config.endpoint = cm.get<std::string>("sms.tencent.endpoint", "sms.tencentcloudapi.com");
    config.timeout_ms = cm.get<int>("sms.tencent.timeout_ms", 10000);
    config.enable = cm.get<bool>("sms.tencent.enable", true);
    
    return config;
}

void TencentSmsConfig::validate() const {
    if (enable) {
        if (secret_id.empty()) {
            throw std::invalid_argument("腾讯云短信配置错误: secret_id不能为空");
        }
        if (secret_key.empty()) {
            throw std::invalid_argument("腾讯云短信配置错误: secret_key不能为空");
        }
        if (app_id.empty()) {
            throw std::invalid_argument("腾讯云短信配置错误: app_id不能为空");
        }
        if (sign_name.empty()) {
            throw std::invalid_argument("腾讯云短信配置错误: sign_name不能为空");
        }
        if (timeout_ms <= 0) {
            throw std::invalid_argument("腾讯云短信配置错误: timeout_ms必须大于0");
        }
    }
}

// ==================== SmsServiceConfig ====================

SmsServiceConfig SmsServiceConfig::fromConfigManager() {
    auto& cm = config::ConfigManager::getInstance();
    
    SmsServiceConfig config;
    
    // 读取默认服务商
    std::string provider_str = cm.get<std::string>("sms.default_provider", "auto");
    std::transform(provider_str.begin(), provider_str.end(), provider_str.begin(), ::tolower);
    
    if (provider_str == "aliyun") {
        config.default_provider = SmsProvider::ALIYUN;
    } else if (provider_str == "tencent") {
        config.default_provider = SmsProvider::TENCENT;
    } else {
        config.default_provider = SmsProvider::AUTO;
    }
    
    // 加载阿里云和腾讯云配置
    config.aliyun = AliyunSmsConfig::fromConfigManager();
    config.tencent = TencentSmsConfig::fromConfigManager();
    
    // 读取其他配置
    config.enable_fallback = cm.get<bool>("sms.enable_fallback", true);
    config.max_retries = cm.get<int>("sms.max_retries", 2);
    config.retry_delay_ms = cm.get<int>("sms.retry_delay_ms", 1000);
    config.enable_log = cm.get<bool>("sms.enable_log", true);
    
    return config;
}

void SmsServiceConfig::validate() const {
    // 至少启用一个服务商
    if (!aliyun.enable && !tencent.enable) {
        throw std::invalid_argument("短信服务配置错误: 至少需要启用一个服务商（阿里云或腾讯云）");
    }
    
    // 验证各服务商配置
    if (aliyun.enable) {
        aliyun.validate();
    }
    if (tencent.enable) {
        tencent.validate();
    }
    
    if (max_retries < 0) {
        throw std::invalid_argument("短信服务配置错误: max_retries不能为负数");
    }
    if (retry_delay_ms < 0) {
        throw std::invalid_argument("短信服务配置错误: retry_delay_ms不能为负数");
    }
}

// ==================== SmsService ====================

SmsService::SmsService() 
    : initialized_(false) {
    stats_.last_send_time = std::chrono::system_clock::now();
}

SmsService::~SmsService() {
    // 清理资源
}

SmsService& SmsService::getInstance() {
    static SmsService instance;
    return instance;
}

bool SmsService::initialize(const SmsServiceConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // 验证配置
        config.validate();
        
        config_ = config;
        initialized_ = true;
        
        LOG_INFO("短信服务初始化成功");
        LOG_INFO("  - 阿里云短信: " + std::string(config_.aliyun.enable ? "启用" : "禁用"));
        LOG_INFO("  - 腾讯云短信: " + std::string(config_.tencent.enable ? "启用" : "禁用"));
        LOG_INFO("  - 默认服务商: " + std::string(
            config_.default_provider == SmsProvider::ALIYUN ? "阿里云" :
            config_.default_provider == SmsProvider::TENCENT ? "腾讯云" : "自动"
        ));
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("短信服务初始化失败: " + std::string(e.what()));
        return false;
    }
}

bool SmsService::initialize() {
    try {
        auto config = SmsServiceConfig::fromConfigManager();
        return initialize(config);
    } catch (const std::exception& e) {
        LOG_ERROR("从配置管理器加载短信配置失败: " + std::string(e.what()));
        return false;
    }
}

SmsSendResult SmsService::sendSms(
    const std::string& phone_number,
    const std::string& template_code,
    const std::map<std::string, std::string>& template_params
) {
    return sendSms(phone_number, template_code, template_params, config_.default_provider);
}

SmsSendResult SmsService::sendSms(
    const std::string& phone_number,
    const std::string& template_code,
    const std::map<std::string, std::string>& template_params,
    SmsProvider provider
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        SmsSendResult result;
        result.success = false;
        result.message = "短信服务未初始化";
        result.error_code = "NOT_INITIALIZED";
        return result;
    }
    
    // 验证手机号
    if (!validatePhoneNumber(phone_number)) {
        SmsSendResult result;
        result.success = false;
        result.message = "手机号码格式错误";
        result.error_code = "INVALID_PHONE_NUMBER";
        return result;
    }
    
    // 获取可用的服务商
    SmsProvider actual_provider = getAvailableProvider(provider);
    
    SmsSendResult result;
    int retry_count = 0;
    
    while (retry_count <= config_.max_retries) {
        // 根据服务商发送短信
        if (actual_provider == SmsProvider::ALIYUN) {
            result = sendAliyunSms(phone_number, template_code, template_params);
        } else if (actual_provider == SmsProvider::TENCENT) {
            result = sendTencentSms(phone_number, template_code, template_params);
        } else {
            result.success = false;
            result.message = "没有可用的短信服务商";
            result.error_code = "NO_PROVIDER_AVAILABLE";
            break;
        }
        
        // 如果成功，跳出重试循环
        if (result.success) {
            break;
        }
        
        // 如果启用降级且主服务商失败，尝试备用服务商
        if (config_.enable_fallback && retry_count == 0) {
            if (actual_provider == SmsProvider::ALIYUN && config_.tencent.enable) {
                actual_provider = SmsProvider::TENCENT;
                LOG_WARNING("阿里云短信发送失败，切换到腾讯云重试");
            } else if (actual_provider == SmsProvider::TENCENT && config_.aliyun.enable) {
                actual_provider = SmsProvider::ALIYUN;
                LOG_WARNING("腾讯云短信发送失败，切换到阿里云重试");
            }
        }
        
        retry_count++;
        if (retry_count <= config_.max_retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_delay_ms));
        }
    }
    
    // 更新统计信息
    stats_.total_sent++;
    stats_.last_send_time = std::chrono::system_clock::now();
    
    if (result.success) {
        stats_.total_success++;
        if (result.provider == SmsProvider::ALIYUN) {
            stats_.aliyun_sent++;
        } else if (result.provider == SmsProvider::TENCENT) {
            stats_.tencent_sent++;
        }
    } else {
        stats_.total_failed++;
    }
    
    // 记录日志
    if (config_.enable_log) {
        logSmsRecord(phone_number, template_code, result);
    }
    
    return result;
}

std::vector<SmsSendResult> SmsService::sendBatchSms(
    const std::vector<std::string>& phone_numbers,
    const std::string& template_code,
    const std::map<std::string, std::string>& template_params
) {
    std::vector<SmsSendResult> results;
    results.reserve(phone_numbers.size());
    
    for (const auto& phone : phone_numbers) {
        results.push_back(sendSms(phone, template_code, template_params));
    }
    
    return results;
}

SmsSendResult SmsService::sendVerificationCode(
    const std::string& phone_number,
    const std::string& code,
    const std::string& template_code
) {
    std::map<std::string, std::string> params;
    params["code"] = code;
    
    // 如果没有指定模板，使用默认验证码模板
    std::string actual_template = template_code.empty() ? 
        config::ConfigManager::getInstance().get<std::string>("sms.verification_code_template", "") :
        template_code;
    
    if (actual_template.empty()) {
        SmsSendResult result;
        result.success = false;
        result.message = "未配置验证码短信模板";
        result.error_code = "NO_TEMPLATE";
        return result;
    }
    
    return sendSms(phone_number, actual_template, params);
}

SmsServiceConfig SmsService::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void SmsService::setDefaultProvider(SmsProvider provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.default_provider = provider;
    LOG_INFO("默认短信服务商已更改为: " + std::string(
        provider == SmsProvider::ALIYUN ? "阿里云" :
        provider == SmsProvider::TENCENT ? "腾讯云" : "自动"
    ));
}

bool SmsService::isProviderAvailable(SmsProvider provider) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    switch (provider) {
        case SmsProvider::ALIYUN:
            return config_.aliyun.enable;
        case SmsProvider::TENCENT:
            return config_.tencent.enable;
        case SmsProvider::AUTO:
            return config_.aliyun.enable || config_.tencent.enable;
        default:
            return false;
    }
}

// ==================== 私有方法 ====================

SmsSendResult SmsService::sendAliyunSms(
    const std::string& phone_number,
    const std::string& template_code,
    const std::map<std::string, std::string>& template_params
) {
    SmsSendResult result;
    result.provider = SmsProvider::ALIYUN;
    
    try {
        // 1. 构建阿里云通用参数
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream timestamp_ss;
        timestamp_ss << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%dT%H:%M:%SZ");
        std::string timestamp = timestamp_ss.str();
        
        // 生成随机Nonce
        std::string nonce = Crypto::randomString(16);
        
        // 构建所有参数
        std::map<std::string, std::string> all_params;
        all_params["AccessKeyId"] = config_.aliyun.access_key_id;
        all_params["Action"] = "SendSms";
        all_params["Format"] = "JSON";
        all_params["RegionId"] = config_.aliyun.region_id;
        all_params["SignatureMethod"] = "HMAC-SHA1";
        all_params["SignatureNonce"] = nonce;
        all_params["SignatureVersion"] = "1.0";
        all_params["Timestamp"] = timestamp;
        all_params["Version"] = "2017-05-25";
        all_params["PhoneNumbers"] = phone_number;
        all_params["SignName"] = config_.aliyun.sign_name;
        all_params["TemplateCode"] = template_code;
        
        if (!template_params.empty()) {
            json template_param_json = json::object();
            for (const auto& param : template_params) {
                template_param_json[param.first] = param.second;
            }
            all_params["TemplateParam"] = template_param_json.dump();
        }
        
        // 2. 计算签名
        std::string signature = calculateAliyunSignature(all_params, config_.aliyun.access_key_secret);
        all_params["Signature"] = signature;
        
        // 3. 构建请求URL
        std::stringstream url_ss;
        url_ss << "https://" << config_.aliyun.endpoint << "/?";
        bool first = true;
        for (const auto& param : all_params) {
            if (!first) url_ss << "&";
            url_ss << urlEncode(param.first) << "=" << urlEncode(param.second);
            first = false;
        }
        std::string url = url_ss.str();
        
        // 4. 发送HTTP请求
        common::http::HttpClient client;
        auto response = client.get(url, config_.aliyun.timeout_ms);
        
        // 5. 解析响应
        if (!response.success) {
            result.success = false;
            result.message = "HTTP请求失败: " + response.error_message;
            result.error_code = "HTTP_ERROR";
            return result;
        }
        
        json response_json = json::parse(response.body);
        
        // 检查是否有Code字段（阿里云错误标识）
        if (response_json.contains("Code")) {
            std::string code = response_json["Code"];
            if (code == "OK") {
                // 发送成功
                result.success = true;
                result.request_id = response_json.value("RequestId", "");
                result.biz_id = response_json.value("BizId", "");
                result.message = "短信发送成功";
            } else {
                // 发送失败
                result.success = false;
                result.error_code = code;
                result.message = response_json.value("Message", "未知错误");
                result.request_id = response_json.value("RequestId", "");
            }
        } else {
            result.success = false;
            result.message = "响应格式错误";
            result.error_code = "INVALID_RESPONSE";
        }
        
    } catch (const json::exception& e) {
        result.success = false;
        result.message = "JSON解析失败: " + std::string(e.what());
        result.error_code = "JSON_ERROR";
        LOG_ERROR("阿里云短信发送JSON错误: " + std::string(e.what()));
    } catch (const std::exception& e) {
        result.success = false;
        result.message = "发送异常: " + std::string(e.what());
        result.error_code = "EXCEPTION";
        LOG_ERROR("阿里云短信发送异常: " + std::string(e.what()));
    }
    
    return result;
}

SmsSendResult SmsService::sendTencentSms(
    const std::string& phone_number,
    const std::string& template_code,
    const std::map<std::string, std::string>& template_params
) {
    SmsSendResult result;
    result.provider = SmsProvider::TENCENT;
    
    try {
        // 1. 准备请求体
        json request_body;
        
        // 手机号需要加+86前缀
        std::string formatted_phone = phone_number;
        if (formatted_phone[0] != '+') {
            formatted_phone = "+86" + formatted_phone;
        }
        request_body["PhoneNumberSet"] = json::array({formatted_phone});
        request_body["SmsSdkAppId"] = config_.tencent.app_id;
        request_body["SignName"] = config_.tencent.sign_name;
        request_body["TemplateId"] = template_code;
        
        // 转换模板参数（腾讯云使用数组格式）
        if (!template_params.empty()) {
            json template_param_array = json::array();
            // 按照参数的数字键排序（如果有的话）
            std::vector<std::pair<std::string, std::string>> sorted_params(
                template_params.begin(), template_params.end()
            );
            std::sort(sorted_params.begin(), sorted_params.end());
            
            for (const auto& param : sorted_params) {
                template_param_array.push_back(param.second);
            }
            request_body["TemplateParamSet"] = template_param_array;
        }
        
        std::string body = request_body.dump();
        
        // 2. 构建腾讯云签名（TC3-HMAC-SHA256）
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        
        // 日期字符串（YYYY-MM-DD）
        std::stringstream date_ss;
        date_ss << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%d");
        std::string date = date_ss.str();
        
        // 时间戳（秒）
        std::string timestamp = std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()
            ).count()
        );
        
        // CanonicalRequest
        std::string canonical_headers = "content-type:application/json\nhost:" + 
                                       config_.tencent.endpoint + "\n";
        std::string signed_headers = "content-type;host";
        std::string hashed_request_payload = Crypto::sha256(body);
        
        std::stringstream canonical_request_ss;
        canonical_request_ss << "POST\n"
                            << "/\n"
                            << "\n"
                            << canonical_headers << "\n"
                            << signed_headers << "\n"
                            << hashed_request_payload;
        std::string canonical_request = canonical_request_ss.str();
        
        // StringToSign
        std::string algorithm = "TC3-HMAC-SHA256";
        std::string credential_scope = date + "/sms/tc3_request";
        std::string hashed_canonical_request = Crypto::sha256(canonical_request);
        
        std::stringstream string_to_sign_ss;
        string_to_sign_ss << algorithm << "\n"
                         << timestamp << "\n"
                         << credential_scope << "\n"
                         << hashed_canonical_request;
        std::string string_to_sign = string_to_sign_ss.str();
        
        // 计算签名
        std::string secret_date = Crypto::hmacSha256Raw(
            date, "TC3" + config_.tencent.secret_key
        );
        std::string secret_service = Crypto::hmacSha256Raw("sms", secret_date);
        std::string secret_signing = Crypto::hmacSha256Raw("tc3_request", secret_service);
        std::string signature = Crypto::hexEncode(
            Crypto::hmacSha256Raw(string_to_sign, secret_signing)
        );
        
        // 3. 构建Authorization
        std::stringstream authorization_ss;
        authorization_ss << algorithm << " "
                        << "Credential=" << config_.tencent.secret_id << "/" << credential_scope << ", "
                        << "SignedHeaders=" << signed_headers << ", "
                        << "Signature=" << signature;
        std::string authorization = authorization_ss.str();
        
        // 4. 设置请求头
        std::unordered_map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        headers["Host"] = config_.tencent.endpoint;
        headers["X-TC-Action"] = "SendSms";
        headers["X-TC-Version"] = "2021-01-11";
        headers["X-TC-Timestamp"] = timestamp;
        headers["X-TC-Region"] = config_.tencent.region;
        headers["Authorization"] = authorization;
        
        // 5. 发送HTTP请求
        common::http::HttpClient client;
        std::string url = "https://" + config_.tencent.endpoint + "/";
        auto response = client.post(url, body, headers, config_.tencent.timeout_ms);
        
        // 6. 解析响应
        if (!response.success) {
            result.success = false;
            result.message = "HTTP请求失败: " + response.error_message;
            result.error_code = "HTTP_ERROR";
            return result;
        }
        
        json response_json = json::parse(response.body);
        
        // 检查响应
        if (response_json.contains("Response")) {
            json response_data = response_json["Response"];
            
            if (response_data.contains("Error")) {
                // 发送失败
                result.success = false;
                result.error_code = response_data["Error"].value("Code", "UNKNOWN");
                result.message = response_data["Error"].value("Message", "未知错误");
                result.request_id = response_data.value("RequestId", "");
            } else if (response_data.contains("SendStatusSet")) {
                // 发送成功
                auto send_status_set = response_data["SendStatusSet"];
                if (!send_status_set.empty()) {
                    auto first_status = send_status_set[0];
                    std::string code = first_status.value("Code", "");
                    
                    if (code == "Ok") {
                        result.success = true;
                        result.request_id = response_data.value("RequestId", "");
                        result.biz_id = first_status.value("SerialNo", "");
                        result.message = "短信发送成功";
                    } else {
                        result.success = false;
                        result.error_code = code;
                        result.message = first_status.value("Message", "发送失败");
                        result.request_id = response_data.value("RequestId", "");
                    }
                } else {
                    result.success = false;
                    result.message = "SendStatusSet为空";
                    result.error_code = "EMPTY_RESPONSE";
                }
            } else {
                result.success = false;
                result.message = "响应格式错误";
                result.error_code = "INVALID_RESPONSE";
            }
        } else {
            result.success = false;
            result.message = "响应格式错误：缺少Response字段";
            result.error_code = "INVALID_RESPONSE";
        }
        
    } catch (const json::exception& e) {
        result.success = false;
        result.message = "JSON解析失败: " + std::string(e.what());
        result.error_code = "JSON_ERROR";
        LOG_ERROR("腾讯云短信发送JSON错误: " + std::string(e.what()));
    } catch (const std::exception& e) {
        result.success = false;
        result.message = "发送异常: " + std::string(e.what());
        result.error_code = "EXCEPTION";
        LOG_ERROR("腾讯云短信发送异常: " + std::string(e.what()));
    }
    
    return result;
}

bool SmsService::validatePhoneNumber(const std::string& phone_number) const {
    // 中国大陆手机号：1开头，11位数字
    std::regex cn_mobile_pattern("^1[3-9]\\d{9}$");
    
    // 支持国际格式：+86开头
    std::regex cn_mobile_with_code("^\\+86[1][3-9]\\d{9}$");
    
    return std::regex_match(phone_number, cn_mobile_pattern) ||
           std::regex_match(phone_number, cn_mobile_with_code);
}

void SmsService::logSmsRecord(
    const std::string& phone_number,
    const std::string& template_code,
    const SmsSendResult& result
) {
    std::string provider_name = 
        result.provider == SmsProvider::ALIYUN ? "阿里云" :
        result.provider == SmsProvider::TENCENT ? "腾讯云" : "未知";
    
    std::string masked_phone = phone_number.length() > 7 ?
        phone_number.substr(0, 3) + "****" + phone_number.substr(phone_number.length() - 4) :
        phone_number;
    
    if (result.success) {
        LOG_INFO("短信发送成功 - 手机: " + masked_phone + 
                ", 模板: " + template_code +
                ", 服务商: " + provider_name +
                ", 请求ID: " + result.request_id);
    } else {
        LOG_ERROR("短信发送失败 - 手机: " + masked_phone +
                 ", 模板: " + template_code +
                 ", 服务商: " + provider_name +
                 ", 错误: " + result.message +
                 ", 错误码: " + result.error_code);
    }
}

SmsProvider SmsService::getAvailableProvider(SmsProvider preferred_provider) const {
    // 如果指定了AUTO，自动选择
    if (preferred_provider == SmsProvider::AUTO) {
        // 优先使用阿里云
        if (config_.aliyun.enable) {
            return SmsProvider::ALIYUN;
        } else if (config_.tencent.enable) {
            return SmsProvider::TENCENT;
        }
        return SmsProvider::AUTO; // 没有可用的
    }
    
    // 检查指定的服务商是否可用
    if (preferred_provider == SmsProvider::ALIYUN && config_.aliyun.enable) {
        return SmsProvider::ALIYUN;
    }
    if (preferred_provider == SmsProvider::TENCENT && config_.tencent.enable) {
        return SmsProvider::TENCENT;
    }
    
    // 指定的服务商不可用，如果启用降级，使用备用服务商
    if (config_.enable_fallback) {
        if (preferred_provider == SmsProvider::ALIYUN && config_.tencent.enable) {
            LOG_WARNING("阿里云短信不可用，降级使用腾讯云");
            return SmsProvider::TENCENT;
        }
        if (preferred_provider == SmsProvider::TENCENT && config_.aliyun.enable) {
            LOG_WARNING("腾讯云短信不可用，降级使用阿里云");
            return SmsProvider::ALIYUN;
        }
    }
    
    return SmsProvider::AUTO; // 没有可用的
}

        std::string SmsService::urlEncode(const std::string &str) {
            std::ostringstream escaped;
            escaped.fill('0');
            escaped << std::hex;

            for (char c : str) {
                if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    escaped << c;
                } else {
                    escaped << std::uppercase;
                    escaped << '%' << std::setw(2) << int((unsigned char)c);
                    escaped << std::nouppercase;
                }
            }

            return escaped.str();
        }

        std::string SmsService::calculateAliyunSignature(const std::map<std::string, std::string> &params,
                                                         const std::string &access_key_secret) {
            // 1. 排序参数
            std::map<std::string, std::string> sorted_params(params.begin(), params.end());

            // 2. 构建待签名字符串
            std::stringstream canonical_query_string;
            bool first = true;
            for (const auto& param : sorted_params) {
                if (!first) canonical_query_string << "&";
                canonical_query_string << urlEncode(param.first) << "=" << urlEncode(param.second);
                first = false;
            }

            // 3. 构建StringToSign
            std::stringstream string_to_sign;
            string_to_sign << "GET&" << urlEncode("/") << "&" << urlEncode(canonical_query_string.str());

            // 4. 计算HMAC-SHA1签名
            std::string signature_key = access_key_secret + "&";
            std::string signature = Crypto::base64Encode(
                    Crypto::hmacSha256Raw(string_to_sign.str(), signature_key)
            );

            return signature;
        }

    } // namespace utils
} // namespace common
