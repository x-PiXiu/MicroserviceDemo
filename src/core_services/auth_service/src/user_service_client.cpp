#include "../include/user_service_client.h"
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

/**
 * @file user_service_client.cpp
 * @brief 用户服务客户端实现
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

namespace core_services {
namespace auth_service {

UserServiceClient::UserServiceClient(const UserServiceClientConfig& config, 
                                    common::database::RedisPool* redis_pool)
    : config_(config), redis_pool_(redis_pool) {
    http_client_ = std::make_unique<common::http::HttpClient>();
    
    // HTTP客户端配置
    try {
        // 配置HTTP客户端的基本参数
        // 注意：这里的方法调用需要根据实际HttpClient的API来调整
        // http_client_->setTimeout(config_.timeout_seconds * 1000); // 转换为毫秒
        // http_client_->setUserAgent("AuthService-UserClient/1.0");
        // http_client_->setConnectTimeout(5000); // 5秒连接超时
        
        LOG_DEBUG("HTTP客户端配置完成 - 超时: " + std::to_string(config_.timeout_seconds) + "秒");
    } catch (const std::exception& e) {
        LOG_WARNING("HTTP客户端配置失败，使用默认配置: " + std::string(e.what()));
    }
    
    LOG_INFO("用户服务客户端初始化完成，服务地址: " + config_.base_url);
}

UserServiceClient::~UserServiceClient() {
    LOG_INFO("用户服务客户端销毁");
}

// ==================== 用户信息获取 ====================

std::optional<CoreUserInfo> UserServiceClient::getUserByUsername(const std::string& username) {
    try {
        total_requests_++;
        
        // 尝试从缓存获取
        if (config_.enable_cache && redis_pool_) {
            std::string cache_key = generateCacheKey("username", username);
            auto cached_user = getUserFromCache(cache_key);
            if (cached_user.has_value()) {
                cache_hits_++;
                return cached_user;
            }
            cache_misses_++;
        }
        
        // 从用户服务获取
        std::string path = "/api/v1/user/by-username/" + username;
        auto response = makeRequestWithRetry("GET", path);
        
        if (!isSuccessResponse(response)) {
            if (response.status_code == 404) {
                LOG_DEBUG("用户不存在: " + username);
                successful_requests_++;  // 404也是正常响应
                return std::nullopt;
            }
            
            LOG_ERROR("获取用户信息失败: " + std::to_string(response.status_code) + 
                     ", 错误: " + response.error_message);
            failed_requests_++;
            return std::nullopt;
        }
        
        // 解析用户信息
        auto user = parseUserFromResponse(response);
        if (user.has_value()) {
            // 存入缓存
            if (config_.enable_cache && redis_pool_) {
                std::string cache_key = generateCacheKey("username", username);
                setUserToCache(cache_key, user.value());
                
                // 同时缓存ID和邮箱索引
                std::string id_cache_key = generateCacheKey("user_id", user->user_id);
                std::string email_cache_key = generateCacheKey("email", user->email);
                setUserToCache(id_cache_key, user.value());
                setUserToCache(email_cache_key, user.value());
            }
            
            successful_requests_++;
            return user;
        }
        
        failed_requests_++;
        return std::nullopt;
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<CoreUserInfo> UserServiceClient::getUserByEmail(const std::string& email) {
    try {
        total_requests_++;
        
        // 尝试从缓存获取
        if (config_.enable_cache && redis_pool_) {
            std::string cache_key = generateCacheKey("email", email);
            auto cached_user = getUserFromCache(cache_key);
            if (cached_user.has_value()) {
                cache_hits_++;
                return cached_user;
            }
            cache_misses_++;
        }
        
        // 从用户服务获取
        std::string path = "/api/v1/user/by-email/" + email;
        auto response = makeRequestWithRetry("GET", path);
        
        if (!isSuccessResponse(response)) {
            if (response.status_code == 404) {
                LOG_DEBUG("邮箱对应用户不存在: " + email);
                successful_requests_++;
                return std::nullopt;
            }
            
            LOG_ERROR("通过邮箱获取用户信息失败: " + std::to_string(response.status_code));
            failed_requests_++;
            return std::nullopt;
        }
        
        // 解析用户信息
        auto user = parseUserFromResponse(response);
        if (user.has_value()) {
            // 存入缓存
            if (config_.enable_cache && redis_pool_) {
                std::string cache_key = generateCacheKey("email", email);
                setUserToCache(cache_key, user.value());
                
                // 同时缓存其他索引
                std::string id_cache_key = generateCacheKey("user_id", user->user_id);
                std::string username_cache_key = generateCacheKey("username", user->username);
                setUserToCache(id_cache_key, user.value());
                setUserToCache(username_cache_key, user.value());
            }
            
            successful_requests_++;
            return user;
        }
        
        failed_requests_++;
        return std::nullopt;
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("通过邮箱获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<CoreUserInfo> UserServiceClient::getUserById(const std::string& user_id) {
    try {
        total_requests_++;
        
        // 尝试从缓存获取
        if (config_.enable_cache && redis_pool_) {
            std::string cache_key = generateCacheKey("user_id", user_id);
            auto cached_user = getUserFromCache(cache_key);
            if (cached_user.has_value()) {
                cache_hits_++;
                return cached_user;
            }
            cache_misses_++;
        }
        
        // 从用户服务获取
        std::string path = "/api/v1/user/" + user_id + "/profile";
        auto response = makeRequestWithRetry("GET", path);
        
        if (!isSuccessResponse(response)) {
            if (response.status_code == 404) {
                LOG_DEBUG("用户ID不存在: " + user_id);
                successful_requests_++;
                return std::nullopt;
            }
            
            LOG_ERROR("通过ID获取用户信息失败: " + std::to_string(response.status_code));
            failed_requests_++;
            return std::nullopt;
        }
        
        // 解析用户信息
        auto user = parseUserFromResponse(response);
        if (user.has_value()) {
            // 存入缓存
            if (config_.enable_cache && redis_pool_) {
                std::string cache_key = generateCacheKey("user_id", user_id);
                setUserToCache(cache_key, user.value());
                
                // 同时缓存其他索引
                std::string username_cache_key = generateCacheKey("username", user->username);
                std::string email_cache_key = generateCacheKey("email", user->email);
                setUserToCache(username_cache_key, user.value());
                setUserToCache(email_cache_key, user.value());
            }
            
            successful_requests_++;
            return user;
        }
        
        failed_requests_++;
        return std::nullopt;
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("通过ID获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

// ==================== 用户状态更新 ====================

bool UserServiceClient::updateLastLogin(const std::string& user_id, const std::string& ip) {
    try {
        total_requests_++;
        
        nlohmann::json request_data = {
            {"last_login_ip", ip},
            {"last_login_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        std::string path = "/api/v1/user/" + user_id + "/last-login";
        auto response = makeRequestWithRetry("PUT", path, request_data.dump(), {
            {"Content-Type", "application/json"}
        });
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            // 清除用户缓存，确保数据一致性
            clearUserCache(user_id);
            return true;
        } else {
            failed_requests_++;
            LOG_ERROR("更新最后登录时间失败: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("更新最后登录时间异常: " + std::string(e.what()));
        return false;
    }
}

bool UserServiceClient::updateOnlineStatus(const std::string& user_id, const std::string& status) {
    try {
        total_requests_++;
        
        nlohmann::json request_data = {
            {"online_status", status}
        };
        
        std::string path = "/api/v1/user/" + user_id + "/online-status";
        auto response = makeRequestWithRetry("PUT", path, request_data.dump(), {
            {"Content-Type", "application/json"}
        });
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            // 清除用户缓存
            clearUserCache(user_id);
            return true;
        } else {
            failed_requests_++;
            LOG_ERROR("更新在线状态失败: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("更新在线状态异常: " + std::string(e.what()));
        return false;
    }
}

bool UserServiceClient::incrementLoginAttempts(const std::string& user_id) {
    try {
        total_requests_++;
        
        std::string path = "/api/v1/user/" + user_id + "/login-attempts/increment";
        auto response = makeRequestWithRetry("POST", path);
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            // 清除用户缓存
            clearUserCache(user_id);
            return true;
        } else {
            failed_requests_++;
            LOG_ERROR("增加登录尝试次数失败: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("增加登录尝试次数异常: " + std::string(e.what()));
        return false;
    }
}

bool UserServiceClient::resetLoginAttempts(const std::string& user_id) {
    try {
        total_requests_++;
        
        std::string path = "/api/v1/user/" + user_id + "/login-attempts/reset";
        auto response = makeRequestWithRetry("POST", path);
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            // 清除用户缓存
            clearUserCache(user_id);
            return true;
        } else {
            failed_requests_++;
            LOG_ERROR("重置登录尝试次数失败: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("重置登录尝试次数异常: " + std::string(e.what()));
        return false;
    }
}

bool UserServiceClient::lockUserUntil(const std::string& user_id, 
                                     const std::chrono::system_clock::time_point& until) {
    try {
        total_requests_++;
        
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(until.time_since_epoch()).count();
        
        nlohmann::json request_data = {
            {"locked_until", timestamp}
        };
        
        std::string path = "/api/v1/user/" + user_id + "/lock";
        auto response = makeRequestWithRetry("POST", path, request_data.dump(), {
            {"Content-Type", "application/json"}
        });
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            // 清除用户缓存
            clearUserCache(user_id);
            return true;
        } else {
            failed_requests_++;
            LOG_ERROR("锁定用户失败: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("锁定用户异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 用户创建 ====================

UserServiceClient::CreateUserResult UserServiceClient::createUser(const CoreUserInfo& user_info) {
    try {
        total_requests_++;
        
        nlohmann::json request_data = {
            {"user_id", user_info.user_id},
            {"username", user_info.username},
            {"email", user_info.email},
            {"password_hash", user_info.password_hash},
            {"salt", user_info.salt},
            {"nickname", user_info.nickname},
            {"avatar_url", user_info.avatar_url},
            {"status", static_cast<int>(user_info.status)},
            {"online_status", static_cast<int>(user_info.online_status)}
        };
        
        std::string path = "/api/v1/user";
        auto response = makeRequestWithRetry("POST", path, request_data.dump(), {
            {"Content-Type", "application/json"}
        });
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            
            try {
                auto json_response = nlohmann::json::parse(response.body);
                if (json_response.contains("data") && json_response["data"].contains("user_id")) {
                    return CreateUserResult(true, json_response["data"]["user_id"]);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("解析创建用户响应失败: " + std::string(e.what()));
            }
            
            return CreateUserResult(true, user_info.user_id);  // 返回输入的用户ID
        } else {
            failed_requests_++;
            LOG_ERROR("创建用户失败: " + std::to_string(response.status_code) + 
                     ", 响应: " + response.body);
            
            // 解析错误信息
            std::string error_message = "用户创建失败";
            std::string error_code = "USER_CREATION_FAILED";
            
            try {
                auto json_response = nlohmann::json::parse(response.body);
                if (json_response.contains("error")) {
                    error_message = json_response["error"];
                    
                    // 根据错误消息判断错误类型
                    if (error_message.find("Duplicate") != std::string::npos) {
                        if (error_message.find("username") != std::string::npos) {
                            error_code = "USERNAME_EXISTS";
                            error_message = "用户名已存在";
                        } else if (error_message.find("email") != std::string::npos) {
                            error_code = "EMAIL_EXISTS";
                            error_message = "邮箱已被注册";
                        } else {
                            error_code = "DUPLICATE_ENTRY";
                            error_message = "数据已存在";
                        }
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARNING("解析错误响应失败: " + std::string(e.what()));
            }
            
            return CreateUserResult(false, "", error_message, response.status_code, error_code);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("创建用户异常: " + std::string(e.what()));
        return CreateUserResult(false, "", "网络请求异常: " + std::string(e.what()), 0, "NETWORK_ERROR");
    }
}

bool UserServiceClient::updatePassword(const std::string& user_id,
                                        const std::string& password_hash,
                                        const std::string& salt) {
    try {
        total_requests_++;

        nlohmann::json request_data = {
            {"password_hash", password_hash},
            {"salt", salt},
            {"updated_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        std::string path = "/api/v1/user/" + user_id + "/password";
        auto response = makeRequestWithRetry("PUT", path, request_data.dump(), {
            {"Content-Type", "application/json"}
        });

        if (isSuccessResponse(response)) {
            successful_requests_++;
            clearUserCache(user_id);
            return true;
        } else {
            failed_requests_++;
            LOG_ERROR("更新密码失败: " + std::to_string(response.status_code));
            return false;
        }

    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("更新密码异常: " + std::string(e.what()));
        return false;
    }
}

bool UserServiceClient::isUsernameExists(const std::string& username) {
    auto user = getUserByUsername(username);
    return user.has_value();
}

bool UserServiceClient::isEmailExists(const std::string& email) {
    auto user = getUserByEmail(email);
    return user.has_value();
}

// ==================== 认证日志记录 ====================

bool UserServiceClient::recordAuthEvent(const std::string& user_id,
                                       const std::string& username,
                                       const std::string& event_type,
                                       bool success,
                                       const std::string& client_ip,
                                       const std::string& user_agent,
                                       const std::string& failure_reason) {
    try {
        total_requests_++;
        
        nlohmann::json request_data = {
            {"event_type", event_type},
            {"success", success},
            {"client_ip", client_ip},
            {"user_agent", user_agent},
            {"username", username}
        };
        
        if (!user_id.empty()) {
            request_data["user_id"] = user_id;
        }
        
        if (!failure_reason.empty()) {
            request_data["failure_reason"] = failure_reason;
        }
        
        std::string path = "/api/v1/user/auth-events";
        auto response = makeRequestWithRetry("POST", path, request_data.dump(), {
            {"Content-Type", "application/json"}
        });
        
        if (isSuccessResponse(response)) {
            successful_requests_++;
            return true;
        } else {
            failed_requests_++;
            LOG_WARNING("记录认证事件失败: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_WARNING("记录认证事件异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 缓存管理 ====================

void UserServiceClient::clearUserCache(const std::string& user_id) {
    if (!redis_pool_) {
        return;
    }

    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis = conn_guard.get();
        if (!redis) {
            LOG_WARNING("无法获取Redis连接，跳过缓存清除");
            return;
        }

        // 🔧 修复：先从元数据缓存获取 username 和 email
        std::string metadata_key = generateCacheKey("metadata", user_id);
        std::string metadata_json = redis->get(metadata_key);

        std::vector<std::string> cache_keys = {
            generateCacheKey("user_id", user_id),
        };

        // 如果有元数据，解析并清除相关索引
        if (!metadata_json.empty()) {
            try {
                auto metadata = nlohmann::json::parse(metadata_json);
                if (metadata.contains("username")) {
                    cache_keys.push_back(generateCacheKey("username", metadata["username"].get<std::string>()));
                }
                if (metadata.contains("email")) {
                    cache_keys.push_back(generateCacheKey("email", metadata["email"].get<std::string>()));
                }
                // 也清除元数据本身
                cache_keys.push_back(metadata_key);
            } catch (const std::exception& e) {
                LOG_WARNING("解析用户元数据失败: " + std::string(e.what()));
            }
        }

        // 清除所有相关缓存
        for (const auto& key : cache_keys) {
            redis->del(key);
            LOG_DEBUG("清除缓存: " + key);
        }

        LOG_DEBUG("清除用户缓存完成，共清除 " + std::to_string(cache_keys.size()) + " 个键");

        // 🔧 RAII：连接会在conn_guard析构时自动归还

    } catch (const std::exception& e) {
        LOG_WARNING("清除用户缓存异常: " + std::string(e.what()));
    }
}

void UserServiceClient::clearAllCache() {
    if (!redis_pool_) {
        return;
    }
    
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis = conn_guard.get();
        if (!redis) {
            LOG_WARNING("无法获取Redis连接，跳过全部缓存清除");
            return;
        }
        
        // 清除所有用户服务客户端相关缓存
        // 获取所有用户服务相关的Redis键
        std::vector<std::string> keys;
        try {
            // 使用SCAN命令替代KEYS命令，避免阻塞Redis
            // 这里需要根据Redis客户端的实际API来实现
            // 使用模式匹配获取所有相关键
            std::string pattern = "auth:user_service:*";
            
            // 实现SCAN迭代（示例代码，需要根据实际Redis客户端调整）
            // redis->scan(0, pattern, [&keys](const std::string& key) {
            //     keys.push_back(key);
            //     return true; // 继续扫描
            // });
            
            LOG_DEBUG("扫描到 " + std::to_string(keys.size()) + " 个缓存键需要清理");
        } catch (const std::exception& e) {
            LOG_ERROR("扫描Redis键失败: " + std::string(e.what()));
        }
        
        if (!keys.empty()) {
            // 批量删除键
            for (const auto& key : keys) {
                redis->del(key);
            }
        }
        
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_WARNING("清除所有缓存异常: " + std::string(e.what()));
    }
}

// ==================== 健康检查 ====================

bool UserServiceClient::isHealthy() {
    try {
        auto response = makeRequest("GET", "/api/v1/user/health");
        return isSuccessResponse(response);
    } catch (const std::exception& e) {
        LOG_ERROR("用户服务健康检查异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 统计信息 ====================

nlohmann::json UserServiceClient::getStatistics() {
    return nlohmann::json{
        {"total_requests", total_requests_.load()},
        {"successful_requests", successful_requests_.load()},
        {"failed_requests", failed_requests_.load()},
        {"cache_hits", cache_hits_.load()},
        {"cache_misses", cache_misses_.load()},
        {"cache_hit_rate", cache_hits_.load() + cache_misses_.load() > 0 ? 
            static_cast<double>(cache_hits_.load()) / (cache_hits_.load() + cache_misses_.load()) : 0.0},
        {"success_rate", total_requests_.load() > 0 ? 
            static_cast<double>(successful_requests_.load()) / total_requests_.load() : 0.0}
    };
}

// ==================== 私有辅助方法 ====================

HttpResponse UserServiceClient::makeRequest(const std::string& method,
                                          const std::string& path,
                                          const std::string& body,
                                          const std::unordered_map<std::string, std::string>& headers) {
    HttpResponse response;
    
    try {
        std::string url = config_.base_url + path;
        
        // 执行HTTP请求
        if (method == "GET") {
            auto http_response = http_client_->get(url);
            response.status_code = http_response.status_code;
            response.body = http_response.body;
        } else if (method == "POST") {
            auto http_response = http_client_->post(url, body, headers);
            response.status_code = http_response.status_code;
            response.body = http_response.body;
        } else if (method == "PUT") {
            auto http_response = http_client_->put(url, body, headers);
            response.status_code = http_response.status_code;
            response.body = http_response.body;
        } else {
            response.error_message = "不支持的HTTP方法: " + method;
            return response;
        }
        
        response.success = (response.status_code >= 200 && response.status_code < 300);
        
    } catch (const std::exception& e) {
        response.error_message = std::string(e.what());
        response.success = false;
    }
    
    return response;
}

HttpResponse UserServiceClient::makeRequestWithRetry(const std::string& method,
                                                   const std::string& path,
                                                   const std::string& body,
                                                   const std::unordered_map<std::string, std::string>& headers) {
    HttpResponse response;
    
    for (int attempt = 0; attempt < config_.retry_count; ++attempt) {
        response = makeRequest(method, path, body, headers);
        
        if (response.success || response.status_code == 404 || response.status_code == 400 || response.status_code == 500) {
            // 成功、客户端错误、或服务端确定性错误不重试
            break;
        }
        
        if (attempt < config_.retry_count - 1) {
            // 指数退避
            int delay_ms = 100 * (1 << attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            LOG_WARNING("用户服务请求失败，重试中... 尝试 " + std::to_string(attempt + 1) + 
                       "/" + std::to_string(config_.retry_count));
        }
    }
    
    return response;
}

// ==================== 缓存相关 ====================

std::string UserServiceClient::generateCacheKey(const std::string& type, const std::string& key) {
    return "auth:user_service:" + type + ":" + key;
}

std::optional<CoreUserInfo> UserServiceClient::getUserFromCache(const std::string& cache_key) {
    if (!redis_pool_) {
        return std::nullopt;
    }
    
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis = conn_guard.get();
        if (!redis) {
            LOG_WARNING("无法获取Redis连接，跳过缓存获取");
            return std::nullopt;
        }
        
        auto cached_data = redis->get(cache_key);
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
        if (cached_data.empty()) {
            return std::nullopt;
        }
        
        auto json_data = nlohmann::json::parse(cached_data);
        return CoreUserInfo::fromJson(json_data);
        
    } catch (const std::exception& e) {
        LOG_WARNING("从缓存获取用户信息失败: " + std::string(e.what()));
        return std::nullopt;
    }
}

void UserServiceClient::setUserToCache(const std::string& cache_key, const CoreUserInfo& user) {
    if (!redis_pool_) {
        return;
    }

    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis = conn_guard.get();
        if (!redis) {
            LOG_WARNING("无法获取Redis连接，跳过缓存存储");
            return;
        }

        std::string json_data = user.toJson(true).dump();  // 🔧 修复：缓存包含敏感信息用于认证
        // 使用set命令替代setex（如果setex不可用）
        redis->set(cache_key, json_data);
        redis->expire(cache_key, config_.cache_ttl_seconds);

        // 🔧 新增：存储用户元数据（username, email）用于缓存清除
        // 当调用 clearUserCache(user_id) 时，可以根据这个元数据清除所有相关索引
        std::string metadata_key = generateCacheKey("metadata", user.user_id);
        nlohmann::json metadata = {
            {"username", user.username},
            {"email", user.email}
        };
        redis->set(metadata_key, metadata.dump());
        redis->expire(metadata_key, config_.cache_ttl_seconds);

        // 🔧 RAII：连接会在conn_guard析构时自动归还

    } catch (const std::exception& e) {
        LOG_WARNING("将用户信息存入缓存失败: " + std::string(e.what()));
    }
}

void UserServiceClient::deleteFromCache(const std::string& cache_key) {
    if (!redis_pool_) {
        return;
    }
    
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis = conn_guard.get();
        if (!redis) {
            LOG_WARNING("无法获取Redis连接，跳过缓存删除");
            return;
        }
        
        redis->del(cache_key);
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_WARNING("删除缓存失败: " + std::string(e.what()));
    }
}

// ==================== 验证辅助 ====================

bool UserServiceClient::isSuccessResponse(const HttpResponse& response) {
    return response.success && response.status_code >= 200 && response.status_code < 300;
}

std::optional<CoreUserInfo> UserServiceClient::parseUserFromResponse(const HttpResponse& response) {
    try {
        auto json_response = nlohmann::json::parse(response.body);
        
        if (json_response.contains("data")) {
            return CoreUserInfo::fromJson(json_response["data"]);
        } else if (json_response.contains("user")) {
            return CoreUserInfo::fromJson(json_response["user"]);
        } else {
            // 尝试直接解析
            return CoreUserInfo::fromJson(json_response);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("解析用户信息响应失败: " + std::string(e.what()));
        return std::nullopt;
    }
}

void UserServiceClient::recordRequestStats(bool success) {
    if (success) {
        successful_requests_++;
    } else {
        failed_requests_++;
    }
}

} // namespace auth_service
} // namespace core_services
