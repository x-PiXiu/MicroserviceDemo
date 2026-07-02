#pragma once

#include "common/http/http_client.h"
#include "user_models.h"
#include "common/database/redis_pool.h"
#include <optional>
#include <memory>
#include <chrono>
#include <string>

/**
 * @file user_service_client.h
 * @brief 用户服务客户端
 * @details 认证服务通过此客户端与用户服务通信，获取用户信息
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

namespace core_services {
namespace auth_service {

/**
 * @brief 用户服务客户端配置
 */
struct UserServiceClientConfig {
    std::string base_url = "http://localhost:8082";
    int timeout_seconds = 5;
    int retry_count = 3;
    int cache_ttl_seconds = 300;  // 5分钟缓存
    bool enable_cache = true;
    std::string user_agent = "AuthService/2.0";
};

/**
 * @brief HTTP响应结果
 */
struct HttpResponse {
    int status_code = 0;
    std::string body;
    bool success = false;
    std::string error_message;
};

/**
 * @brief 用户服务客户端
 * @details 提供与用户服务的通信接口，支持缓存和重试机制
 */
class UserServiceClient {
public:
    /**
     * @brief 构造函数
     * @param config 客户端配置
     * @param redis_pool Redis连接池（用于缓存）
     */
    explicit UserServiceClient(const UserServiceClientConfig& config, 
                              common::database::RedisPool* redis_pool = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~UserServiceClient();
    
    // ==================== 用户信息获取 ====================
    
    /**
     * @brief 根据用户名获取用户信息
     * @param username 用户名
     * @return 用户信息，不存在返回nullopt
     */
    std::optional<CoreUserInfo> getUserByUsername(const std::string& username);
    
    /**
     * @brief 根据邮箱获取用户信息
     * @param email 邮箱地址
     * @return 用户信息，不存在返回nullopt
     */
    std::optional<CoreUserInfo> getUserByEmail(const std::string& email);
    
    /**
     * @brief 根据用户ID获取用户信息
     * @param user_id 用户ID
     * @return 用户信息，不存在返回nullopt
     */
    std::optional<CoreUserInfo> getUserById(const std::string& user_id);
    
    // ==================== 用户状态更新 ====================
    
    /**
     * @brief 更新用户最后登录信息
     * @param user_id 用户ID
     * @param ip 登录IP
     * @return 更新是否成功
     */
    bool updateLastLogin(const std::string& user_id, const std::string& ip);
    
    /**
     * @brief 更新用户在线状态
     * @param user_id 用户ID
     * @param status 在线状态 (online/offline/away/busy)
     * @return 更新是否成功
     */
    bool updateOnlineStatus(const std::string& user_id, const std::string& status);
    
    /**
     * @brief 增加登录尝试次数
     * @param user_id 用户ID
     * @return 更新是否成功
     */
    bool incrementLoginAttempts(const std::string& user_id);
    
    /**
     * @brief 重置登录尝试次数
     * @param user_id 用户ID
     * @return 更新是否成功
     */
    bool resetLoginAttempts(const std::string& user_id);
    
    /**
     * @brief 锁定用户到指定时间
     * @param user_id 用户ID
     * @param until 锁定到期时间
     * @return 更新是否成功
     */
    bool lockUserUntil(const std::string& user_id, const std::chrono::system_clock::time_point& until);
    
    // ==================== 用户创建（注册）====================
    
    /**
     * @brief 用户创建结果
     */
    struct CreateUserResult {
        bool success = false;           ///< 是否成功
        std::string user_id;           ///< 用户ID
        std::string error_message;     ///< 错误消息
        int status_code = 0;           ///< HTTP状态码
        std::string error_code;        ///< 错误代码
        
        CreateUserResult() = default;
        CreateUserResult(bool success, const std::string& user_id = "", 
                        const std::string& error_message = "", int status_code = 0,
                        const std::string& error_code = "")
            : success(success), user_id(user_id), error_message(error_message), 
              status_code(status_code), error_code(error_code) {}
    };
    
    /**
     * @brief 创建新用户
     * @param user_info 用户信息
     * @return 创建结果，包含成功状态、用户ID和错误信息
     */
    CreateUserResult createUser(const CoreUserInfo& user_info);
    
    /**
     * @brief 更新用户密码
     * @param user_id 用户ID
     * @param password_hash 新密码哈希
     * @param salt 新密码盐值
     * @return 更新是否成功
     */
    bool updatePassword(const std::string& user_id,
                        const std::string& password_hash,
                        const std::string& salt);

    /**
     * @brief 检查用户名是否存在
     * @param username 用户名
     * @return 存在返回true
     */
    bool isUsernameExists(const std::string& username);
    
    /**
     * @brief 检查邮箱是否存在
     * @param email 邮箱地址
     * @return 存在返回true
     */
    bool isEmailExists(const std::string& email);
    
    // ==================== 认证日志记录 ====================
    
    /**
     * @brief 记录认证事件
     * @param user_id 用户ID（可为空）
     * @param username 用户名
     * @param event_type 事件类型
     * @param success 是否成功
     * @param client_ip 客户端IP
     * @param user_agent 用户代理
     * @param failure_reason 失败原因（可选）
     * @return 记录是否成功
     */
    bool recordAuthEvent(const std::string& user_id,
                        const std::string& username,
                        const std::string& event_type,
                        bool success,
                        const std::string& client_ip,
                        const std::string& user_agent,
                        const std::string& failure_reason = "");
    
    // ==================== 缓存管理 ====================
    
    /**
     * @brief 清除用户缓存
     * @param user_id 用户ID
     */
    void clearUserCache(const std::string& user_id);
    
    /**
     * @brief 清除所有缓存
     */
    void clearAllCache();
    
    // ==================== 健康检查 ====================
    
    /**
     * @brief 检查用户服务是否健康
     * @return 健康返回true
     */
    bool isHealthy();
    
    // ==================== 统计信息 ====================
    
    /**
     * @brief 获取客户端统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getStatistics();

private:
    UserServiceClientConfig config_;
    std::unique_ptr<common::http::HttpClient> http_client_;
    common::database::RedisPool* redis_pool_;
    
    // 统计信息
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};
    
    // ==================== 私有辅助方法 ====================
    
    /**
     * @brief 执行HTTP请求
     * @param method HTTP方法
     * @param path 请求路径
     * @param body 请求体（可选）
     * @param headers 额外请求头（可选）
     * @return HTTP响应
     */
    HttpResponse makeRequest(const std::string& method,
                           const std::string& path,
                           const std::string& body = "",
                           const std::unordered_map<std::string, std::string>& headers = {});
    
    /**
     * @brief 带重试的HTTP请求
     * @param method HTTP方法
     * @param path 请求路径
     * @param body 请求体
     * @param headers 请求头
     * @return HTTP响应
     */
    HttpResponse makeRequestWithRetry(const std::string& method,
                                    const std::string& path,
                                    const std::string& body = "",
                                    const std::unordered_map<std::string, std::string>& headers = {});
    
    // ==================== 缓存相关 ====================
    
    /**
     * @brief 生成缓存键
     * @param type 缓存类型
     * @param key 键值
     * @return 缓存键
     */
    std::string generateCacheKey(const std::string& type, const std::string& key);
    
    /**
     * @brief 从缓存获取用户信息
     * @param cache_key 缓存键
     * @return 用户信息，不存在返回nullopt
     */
    std::optional<CoreUserInfo> getUserFromCache(const std::string& cache_key);
    
    /**
     * @brief 将用户信息存入缓存
     * @param cache_key 缓存键
     * @param user 用户信息
     */
    void setUserToCache(const std::string& cache_key, const CoreUserInfo& user);
    
    /**
     * @brief 删除缓存项
     * @param cache_key 缓存键
     */
    void deleteFromCache(const std::string& cache_key);
    
    // ==================== 验证辅助 ====================
    
    /**
     * @brief 验证HTTP响应
     * @param response HTTP响应
     * @return 是否为成功响应
     */
    bool isSuccessResponse(const HttpResponse& response);
    
    /**
     * @brief 从响应中解析用户信息
     * @param response HTTP响应
     * @return 用户信息，解析失败返回nullopt
     */
    std::optional<CoreUserInfo> parseUserFromResponse(const HttpResponse& response);
    
    /**
     * @brief 记录请求统计
     * @param success 请求是否成功
     */
    void recordRequestStats(bool success);
};

} // namespace auth_service
} // namespace core_services




