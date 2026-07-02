#pragma once

/**
 * @file auth_service.h
 * @brief 认证服务头文件 - 重构版本
 * @details 重构后的认证服务，使用用户服务客户端架构
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

#include "common/config/config_manager.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include "common/http/http_server.h"
#include "common/thread_pool/thread_pool.h"
#include "common/network/event_loop.h"
#include "common/monitoring/performance_monitor.h"
#include "auth_timer_manager.h"
// Kafka integration - 可选功能，需要时可启用
// 如果启用Kafka集成，需要取消注释以下行：
// #include "common/kafka/kafka_producer.h"
// #include "common/kafka/kafka_consumer.h"

// 认证服务组件
#include "jwt_manager.h"
#include "password_manager.h"
#include "session_manager.h"
#include "database_initializer.h"
#include "user_models.h"
#include "verification_code_manager.h"

// ✅ 新架构：用户服务客户端
#include "user_service_client.h"

#include <memory>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "kafka/kafka_consumer.h"
#include "kafka/kafka_producer.h"

namespace core_services {
namespace auth_service {

// ==================== 认证相关数据结构 ====================

// 注意：AuthRequest 和 AuthResult 已在 user_models.h 中定义，这里不重复定义

/**
 * @brief 注册请求
 */
struct RegisterRequest {
    std::string username;        // 用户名
    std::string email;          // 邮箱
    std::string password;       // 密码
    std::string nickname;       // 昵称（可选）
    std::string client_ip;      // 客户端IP
    std::string user_agent;     // 用户代理
    std::string device_type;    // 设备类型
    std::string device_id;      // 设备ID
    
    std::string validate() const;
    nlohmann::json toJson() const;
    static RegisterRequest fromJson(const nlohmann::json& json);
};

// AuthResult 已在 user_models.h 中定义

// ==================== 认证服务主类 ====================

/**
 * @brief 认证服务主类 (重构版)
 * @details 提供用户认证、注册、JWT管理等功能，使用用户服务客户端架构
 */
class AuthService {
public:
    /**
     * @brief 认证服务配置 (重构版)
     */
    struct Config {
        // 服务基本信息
        std::string service_name = "auth_service";
        std::string service_version = "2.0.0";
        
        // 使用预定义的配置结构体
        common::config::ServerConfig server_config;
        common::config::NetworkConfig network_config;
        common::config::DatabaseConfig database_config;
        common::config::RedisConfig redis_config;
        common::config::ThreadPoolConfig thread_pool_config;
        common::config::KafkaConfig kafka_config;

        
        // ✅ 新架构：用户服务配置
        std::string user_service_base_url = "http://localhost:8082";
        int user_service_timeout_seconds = 5;
        int user_service_retry_count = 3;
        int user_service_cache_ttl_seconds = 300;
        bool enable_user_service_cache = true;
        
        // 安全配置
        int max_login_attempts = 5;
        int account_lockout_duration_minutes = 15;
        
        // JWT配置
        std::string jwt_secret = "your_jwt_secret_key_here";
        int jwt_access_expiry = 3600;  // 1小时
        int jwt_refresh_expiry = 604800;  // 7天
        
        // 功能开关
        bool enable_kafka = true;
        bool enable_performance_monitoring = true;

        // 清理任务配置
        bool enable_session_cleanup = true;           // 启用会话清理
        bool enable_token_cleanup = true;             // 启用Token清理  
        int session_cleanup_interval_minutes = 30;    // 会话清理间隔（分钟）
        int token_cleanup_interval_minutes = 60;      // Token清理间隔（分钟）
        int expired_session_retention_hours = 24;     // 过期会话保留时间（小时）
        int expired_token_retention_hours = 48;       // 过期Token保留时间（小时）
        
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
        static Config fromConfigManager();
        
        /**
         * @brief 从配置文件加载配置
         */
        static Config fromConfigFile(const std::string& config_file_path);
    };

    // ==================== 构造和初始化 ====================
    
    /**
     * @brief 构造函数
     * @param config 服务配置
     */
    explicit AuthService(const Config& config);
    
    /**
     * @brief 析构函数
     */
    ~AuthService();
    
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
    
    // ==================== 核心认证接口 ====================
    
    /**
     * @brief 用户登录 (重构版)
     * @param request 登录请求
     * @return 认证结果
     */
    AuthResult loginUser(const AuthRequest& request);
    
    /**
     * @brief 用户注册 (重构版)
     * @param request 注册请求
     * @return 认证结果
     */
    AuthResult registerUser(const RegisterRequest& request);
    
    /**
     * @brief 刷新Token
     * @param refresh_token 刷新令牌
     * @param client_ip 客户端IP
     * @param user_agent 用户代理
     * @return 认证结果
     */
    AuthResult refreshToken(const std::string& refresh_token,
                           const std::string& client_ip,
                           const std::string& user_agent);
    
    /**
     * @brief 验证Token
     * @param access_token 访问令牌
     * @return 验证结果
     */
    TokenValidationResult validateToken(const std::string& access_token);
    
    /**
     * @brief 用户登出
     * @param access_token 访问令牌
     * @param session_id 会话ID
     * @return 登出成功返回true
     */
    bool logoutUser(const std::string& access_token, const std::string& session_id);
    
    // ==================== 状态查询接口 ====================
    
    /**
     * @brief 检查服务是否运行
     * @return 运行状态
     */
    bool isRunning() const { return running_; }
    
    /**
     * @brief 检查服务是否已初始化
     * @return 初始化状态
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief 获取服务统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getStatistics();
    
    /**
     * @brief 获取健康状态
     * @return 健康状态JSON
     */
    nlohmann::json getHealthStatus();

private:
    // ==================== 配置和状态 ====================
    
    Config config_;                                    // 服务配置
    std::atomic<bool> initialized_{false};             // 初始化状态
    std::atomic<bool> running_{false};                 // 运行状态
    
    // 统计信息
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::atomic<uint64_t> total_logins_{0};              // 总登录次数
    std::atomic<uint64_t> total_registrations_{0};       // 总注册次数
    std::chrono::steady_clock::time_point start_time_;    // 服务启动时间
    
    // ==================== 核心组件 ====================
    
    // 数据库连接池
    std::shared_ptr<common::database::MySQLPool> mysql_pool_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    
    // 线程池（用于异步任务处理）
    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;
    
    // 时间轮定时任务（使用独立的定时器管理器）
    std::shared_ptr<common::network::EventLoop> event_loop_; // 指向HttpServer的EventLoop
    std::unique_ptr<AuthServiceTimerManager> timer_manager_;  // 独立定时器管理器
    
    // 保留原始定时器ID为兼容性
    uint64_t session_cleanup_timer_id_{0};
    uint64_t token_cleanup_timer_id_{0};
    
    // HTTP服务器
    std::unique_ptr<common::http::HttpServer> http_server_;
    
    // 业务组件
    std::shared_ptr<JwtManager> jwt_manager_;
    std::unique_ptr<PasswordManager> password_manager_;
    std::unique_ptr<SessionManager> session_manager_;
    std::unique_ptr<DatabaseInitializer> database_initializer_;
    
    // 新架构：用户服务客户端
    std::unique_ptr<UserServiceClient> user_service_client_;

    // 验证码管理器
    std::unique_ptr<VerificationCodeManager> verification_code_manager_;
    
    // Kafka组件
    std::shared_ptr<common::messaging::KafkaProducer> kafka_producer_;
    std::shared_ptr<common::messaging::KafkaConsumer> kafka_consumer_;
    
    // 监控组件
    std::unique_ptr<common::monitoring::PerformanceMonitor> performance_monitor_;
    
    // HTTP服务器线程管理
    std::thread http_server_thread_;                    // HTTP服务器线程
    std::atomic<bool> http_server_running_{false};      // HTTP服务器运行状态
    
    // ==================== 初始化方法 ====================
    
    /**
     * @brief 初始化认证专用数据库连接池
     * @return 初始化成功返回true
     */
    bool initializeAuthDatabasePools();
    
    /**
     * @brief 初始化认证专用数据库表结构
     * @return 初始化成功返回true
     */
    bool initializeAuthDatabaseSchema();
    
    /**
     * @brief 初始化用户服务客户端
     * @return 初始化成功返回true
     */
    bool initializeUserServiceClient();
    
    /**
     * @brief 初始化业务组件
     * @return 初始化成功返回true
     */
    bool initializeBusinessComponents();
    
    /**
     * @brief 初始化定时器管理器
     * @return 初始化成功返回true
     */
    bool initializeTimerManager();
    
    /**
     * @brief 初始化HTTP服务器
     * @return 初始化成功返回true
     */
    bool initializeHttpServer();
    
    /**
     * @brief 初始化Kafka组件
     * @return 初始化成功返回true
     */
    bool initializeKafka();
    
    /**
     * @brief 初始化性能监控
     * @return 初始化成功返回true
     */
    bool initializePerformanceMonitor();
    
    /**
     * @brief 执行外部服务健康检查
     * @return 健康检查通过返回true
     */
    bool performExternalServicesHealthCheck();

    // ==================== HTTP处理方法 ====================

    /**
     * @brief 设置 CORS 中间件
     */
    void setupCorsMiddleware();

    /**
     * @brief 设置HTTP路由
     */
    void setupHttpRoutes();
    
    /**
     * @brief 处理登录请求
     */
    void handleLoginRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理注册请求
     */
    void handleRegisterRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理Token刷新请求
     */
    void handleRefreshTokenRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理Token验证请求
     */
    void handleValidateTokenRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理登出请求
     */
    void handleLogoutRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);

    /**
     * @brief 处理发送邮箱验证码请求
     */
    void handleSendCodeRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);

    /**
     * @brief 处理验证邮箱验证码请求
     */
    void handleVerifyCodeRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);

    /**
     * @brief 处理重设密码请求
     */
    void handleResetPasswordRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理健康检查请求
     */
    void handleHealthCheckRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理统计信息请求
     */
    void handleStatisticsRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    /**
     * @brief 处理服务端点发现请求 (API Gateway使用)
     */
    void handleServiceEndpointsRequest(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // ==================== 辅助方法 ====================
    
    /**
     * @brief 启动清理任务（时间轮实现）
     */
    void startCleanupTasks();
    
    /**
     * @brief 停止清理任务（时间轮实现）
     */
    void stopCleanupTasks();
    
    /**
     * @brief 获取HttpServer的EventLoop并设置定时任务
     */
    void setupTimerTasks();
    
    /**
     * @brief 执行会话清理任务
     */
    void performSessionCleanup();
    
    /**
     * @brief 执行Token清理任务
     */
    void performTokenCleanup();
    
    /**
     * @brief 记录认证事件
     * @param user_id 用户ID
     * @param username 用户名
     * @param event_type 事件类型
     * @param success 是否成功
     * @param client_ip 客户端IP
     * @param user_agent 用户代理
     * @param failure_reason 失败原因
     */
    void recordAuthEvent(const std::string& user_id,
                        const std::string& username,
                        const std::string& event_type,
                        bool success,
                        const std::string& client_ip,
                        const std::string& user_agent,
                        const std::string& failure_reason = "");
    
    /**
     * @brief 检查并锁定账户
     * @param user 用户信息
     */
    void checkAndLockAccount(const CoreUserInfo& user);
    
    /**
     * @brief 生成唯一用户ID
     * @return 用户ID
     */
    std::string generateUserId();
    
    /**
     * @brief 获取基础统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getBaseStatistics();
    bool verifyApiEndpoints();

    /**
     * @brief 验证HTTP请求
     * @param request HTTP请求
     * @return 验证结果
     */
    std::string validateHttpRequest(const common::http::HttpRequest& request);
    
    /**
     * @brief 创建错误响应
     * @param response HTTP响应
     * @param error_code 错误代码
     * @param error_message 错误消息
     * @param status_code HTTP状态码
     */
    void createErrorResponse(common::http::HttpResponse& response,
                           const std::string& error_code,
                           const std::string& error_message,
                           int status_code = 400);
    
    /**
     * @brief 创建成功响应
     * @param response HTTP响应
     * @param data 响应数据
     * @param status_code HTTP状态码
     */
    void createSuccessResponse(common::http::HttpResponse& response,
                             const nlohmann::json& data,
                             int status_code = 200);

    // 禁用拷贝构造和赋值
    AuthService(const AuthService&) = delete;
    AuthService& operator=(const AuthService&) = delete;
};

// ==================== 工厂函数 ====================

// createAuthService 函数在main.cpp中定义，不在头文件中声明以避免重载冲突

} // namespace auth_service
} // namespace core_services