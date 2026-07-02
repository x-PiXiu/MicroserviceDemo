#pragma once

/**
 * @file user_service.h
 * @brief 用户核心服务 - 符合推荐分层架构
 * @details 管理user_service_db，专注于用户核心数据
 * @author AI Assistant
 * @date 2025-09-19
 * @version 1.0.0
 */

#include "common/http/http_server.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"
#include "user_models.h"
#include "user_repository.h"
#include "user_timer_manager.h"
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace core_services {
namespace user_service {

/**
 * @brief 用户服务配置
 */
struct UserServiceConfig {
    // 服务基本信息
    common::config::ServerConfig server_config;
    common::http::HttpServerConfig http_config;    ///< 服务器配置

    common::config::DatabaseConfig database_config;
    common::config::RedisConfig redis_config;
    common::config::ThreadPoolConfig thread_pool_config;

    /**
     * @brief 验证配置
     */
    bool validate() const;
    
    /**
     * @brief 转换为JSON格式
     */
    nlohmann::json toJson() const;
    
    /**
     * @brief 从配置管理器加载配置
     */
    static UserServiceConfig fromConfigManager();
    
    /**
     * @brief 从配置文件加载配置
     */
    static UserServiceConfig fromConfigFile(const std::string& config_file);
};

/**
 * @brief 用户核心服务类
 * @details 管理用户核心数据：users, user_profiles, user_sessions, user_preferences
 */
class UserService {
public:
    /**
     * @brief 构造函数
     * @param config 服务配置
     */
    explicit UserService(const UserServiceConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~UserService();
    
    /**
     * @brief 初始化服务
     * @return 初始化成功返回true
     */
    bool initialize();
    
    /**
     * @brief 启动服务
     * @return 启动成功返回true
     */
    bool start();
    
    /**
     * @brief 停止服务
     */
    void shutdown();
    
    // ==================== 用户管理API ====================
    
    /**
     * @brief 创建用户
     * @param user_info 用户信息
     * @return 用户ID，失败返回空字符串
     */
    std::string createUser(const UserInfo& user_info);
    
    /**
     * @brief 根据用户ID获取用户信息
     * @param user_id 用户ID
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getUserById(const std::string& user_id);
    
    /**
     * @brief 根据用户名获取用户信息
     * @param username 用户名
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getUserByUsername(const std::string& username);
    
    /**
     * @brief 根据邮箱获取用户信息
     * @param email 邮箱
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getUserByEmail(const std::string& email);
    
    /**
     * @brief 更新用户信息
     * @param user_id 用户ID
     * @param user_info 新的用户信息
     * @return 更新成功返回true
     */
    bool updateUser(const std::string& user_id, const UserInfo& user_info);
    
    /**
     * @brief 检查用户名是否存在
     * @param username 用户名
     * @return 存在返回true
     */
    bool isUsernameExists(const std::string& username);
    
    /**
     * @brief 检查邮箱是否存在
     * @param email 邮箱
     * @return 存在返回true
     */
    bool isEmailExists(const std::string& email);
    
    // ==================== 用户档案管理API ====================
    
    /**
     * @brief 获取用户档案
     * @param user_id 用户ID
     * @return 用户档案，不存在返回空
     */
    std::optional<UserProfile> getUserProfile(const std::string& user_id);
    
    /**
     * @brief 更新用户档案
     * @param user_id 用户ID
     * @param profile 用户档案
     * @return 更新成功返回true
     */
    bool updateUserProfile(const std::string& user_id, const UserProfile& profile);
    
    // ==================== 用户偏好设置API ====================
    
    /**
     * @brief 获取用户偏好设置
     * @param user_id 用户ID
     * @return 偏好设置，不存在返回空
     */
    std::optional<UserPreferences> getUserPreferences(const std::string& user_id);
    
    /**
     * @brief 更新用户偏好设置
     * @param user_id 用户ID
     * @param preferences 偏好设置
     * @return 更新成功返回true
     */
    bool updateUserPreferences(const std::string& user_id, const UserPreferences& preferences);
    
    // ==================== 用户状态管理API ====================
    
    /**
     * @brief 更新用户在线状态
     * @param user_id 用户ID
     * @param status 在线状态
     * @return 更新成功返回true
     */
    bool updateOnlineStatus(const std::string& user_id, const std::string& status);
    
    /**
     * @brief 更新最后登录时间
     * @param user_id 用户ID
     * @param login_ip 登录IP
     * @return 更新成功返回true
     */
    bool updateLastLogin(const std::string& user_id, const std::string& login_ip);
    
    /**
     * @brief 增加登录失败次数
     * @param user_id 用户ID
     * @return 当前失败次数
     */
    int incrementLoginAttempts(const std::string& user_id);
    
    /**
     * @brief 重置登录失败次数
     * @param user_id 用户ID
     * @return 重置成功返回true
     */
    bool resetLoginAttempts(const std::string& user_id);
    
    /**
     * @brief 锁定用户直到指定时间
     * @param user_id 用户ID
     * @param lock_until 锁定截止时间
     * @return 锁定成功返回true
     */
    bool lockUserUntil(const std::string& user_id, const std::chrono::system_clock::time_point& lock_until);
    
    // ==================== 统计和健康检查API ====================
    
    /**
     * @brief 获取服务统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getStatistics() const;
    
    /**
     * @brief 获取健康状态
     * @return 健康状态JSON
     */
    nlohmann::json getHealthStatus();
    
    /**
     * @brief 检查服务是否运行
     * @return 运行状态
     */
    bool isRunning() const { return running_; }
    
    // ==================== 定时器管理API ====================
    
    /**
     * @brief 获取定时器统计信息
     * @return 定时器统计JSON
     */
    nlohmann::json getTimerStatistics() const;
    
    /**
     * @brief 暂停定时任务
     * @param task_id 任务ID
     * @return 暂停成功返回true
     */
    bool pauseTimerTask(const std::string& task_id);
    
    /**
     * @brief 恢复定时任务
     * @param task_id 任务ID
     * @return 恢复成功返回true
     */
    bool resumeTimerTask(const std::string& task_id);
    
    /**
     * @brief 取消定时任务
     * @param task_id 任务ID
     * @return 取消成功返回true
     */
    bool cancelTimerTask(const std::string& task_id);

private:
    UserServiceConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    
    // 核心组件
    std::shared_ptr<common::database::MySQLPool> mysql_pool_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    std::unique_ptr<common::http::HttpServer> http_server_;
    std::unique_ptr<UserRepository> user_repository_;
    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;
    std::unique_ptr<UserServiceTimerManager> timer_manager_;
    
    // 统计信息
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::chrono::steady_clock::time_point start_time_;

    // HTTP服务器线程管理
    std::thread http_server_thread_;                    // HTTP服务器线程
    std::atomic<bool> http_server_running_{false};      // HTTP服务器运行状态


    // 初始化方法
    bool initializeDatabasePools();
    bool initializeDatabaseSchema();
    bool initializeHttpServer();

    // HTTP处理方法
    void setupHttpRoutes();
    void setupCorsMiddleware();  // 🔧 新增：CORS 中间件设置
    void setupRoutes();
    void handleCreateUser(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUser(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateUser(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdatePassword(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserPreferences(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateUserPreferences(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateOnlineStatus(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateLastLogin(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleIncrementLoginAttempts(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleResetLoginAttempts(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleHealthCheck(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleStatistics(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleTimerStatistics(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleServiceInfo(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleServiceEndpoints(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserByUsername(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserByEmail(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleCheckUsername(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleCheckEmail(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // 时间轮定时任务方法
    void setupTimerTasks();
    void performStatisticsTask();
    void performCacheCleanupTask();
    void performUserStatusCheckTask();
    void performHealthCheckTask();

    // 辅助方法
    void createErrorResponse(common::http::HttpResponse& response, const std::string& error_code, 
                           const std::string& error_message, int status_code = 400);
    void createSuccessResponse(common::http::HttpResponse& response, const nlohmann::json& data, 
                             int status_code = 200);
    std::string extractUserIdFromPath(const std::string& path);
    
    // 禁用拷贝构造和赋值
    UserService(const UserService&) = delete;
    UserService& operator=(const UserService&) = delete;
};

// createUserService 函数在 main.cpp 中定义，不在此处声明以避免冲突

} // namespace user_service
} // namespace core_services




