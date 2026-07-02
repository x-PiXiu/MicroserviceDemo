/**
 * @file user_service.cpp
 * @brief 用户核心服务实现 - 符合推荐分层架构
 * @details 实现用户管理的核心业务逻辑，管理user_service_db
 * @author AI Assistant
 * @date 2025-09-19
 * @version 1.0.0
 */

#include "user_service.h"
#include "common/logger/logger.h"
#include "common/http/http_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

namespace core_services {
namespace user_service {

/**
 * @brief 构造函数
 * @param config 服务配置
 */
UserService::UserService(const UserServiceConfig& config)
    : config_(config)
    , running_(false)
    , mysql_pool_(nullptr)
    , redis_pool_(nullptr)
    , http_server_(nullptr)
    , user_repository_(nullptr)
    , thread_pool_(nullptr)
    , timer_manager_(nullptr)
    , start_time_(std::chrono::steady_clock::now()) {
    
    LOG_INFO("初始化用户核心服务: " + config_.server_config.serviceName + " v" + config_.server_config.version);
}

/**
 * @brief 析构函数
 */
UserService::~UserService() {
    if (running_) {
        shutdown();
    }
    LOG_INFO("用户核心服务已销毁");
}

/**
 * @brief 初始化服务
 * @return 成功返回true
 */
bool UserService::initialize() {
    try {
        LOG_INFO("=== 开始初始化用户核心服务 ===");
        
        // 1. 初始化数据库连接池
        if (!initializeDatabasePools()) {
            LOG_ERROR("数据库连接池初始化失败");
            return false;
        }
        
        // 2. 初始化数据访问层
        user_repository_ = std::make_unique<UserRepository>(mysql_pool_, redis_pool_);
        if (!user_repository_->initialize()) {
            LOG_ERROR("数据访问层初始化失败");
            return false;
        }
        
        // 3. 初始化数据库表结构（自动创建表）
        if (!initializeDatabaseSchema()) {
            LOG_ERROR("数据库表结构初始化失败");
            return false;
        }
        
        // 4. 初始化线程池
        // common::config::ThreadPoolConfig thread_pool_config;
        thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(config_.thread_pool_config);
        
        // 5. 初始化定时器管理器（基于时间轮）
        timer_manager_ = std::make_unique<UserServiceTimerManager>(true); // 启用监控
        if (!timer_manager_->start()) {
            LOG_ERROR("定时器管理器启动失败");
            return false;
        }
        
        // 6. 初始化HTTP服务器
        if (!initializeHttpServer()) {
            LOG_ERROR("HTTP服务器初始化失败");
            return false;
        }
        
        initialized_ = true;
        LOG_INFO("=== 用户核心服务初始化完成 ===");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("用户核心服务初始化异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 启动服务
 * @return 成功返回true
 */
bool UserService::start() {
    try {
        LOG_INFO("=== 启动用户核心服务 ===");
        
        if (running_) {
            LOG_WARNING("用户核心服务已在运行");
            return true;
        }

        if (!initialized_) {
            LOG_ERROR("认证服务尚未初始化，无法启动");
            return false;
        }

        if (!http_server_running_ && !http_server_)
        {
            LOG_ERROR("http_server_ 初始化失败");
            return false;
        }

        running_ = true;

        // 设置时间轮定时任务
        setupTimerTasks();

        LOG_INFO("用户核心服务启动成功");
        LOG_INFO("监听地址: " + config_.http_config.network_config.bind_address + ":" + std::to_string(config_.http_config.network_config.listen_port));
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("用户核心服务启动异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 停止服务
 */
void UserService::shutdown() {
    // 原子操作：检查并设置 running_ 为 false，防止重复调用
    if (!running_.exchange(false)) {
        LOG_INFO("UserService已停止或正在停止中，跳过重复关闭");
        return;
    }

    try {
        LOG_INFO("=== 开始停止用户核心服务 ===");

        http_server_running_ = false;

        // 停止定时器管理器
        if (timer_manager_) {
            timer_manager_->stop();
        }

        // 停止HTTP服务器
        if (http_server_) {
            http_server_->stop();
        }

        // 等待HTTP服务器线程结束
        if (http_server_thread_.joinable()) {
            http_server_thread_.join();
        }

        // 停止线程池
        if (thread_pool_) {
            thread_pool_->shutdown();
        }

        // 数据库连接池使用RAII管理，不需要手动关闭
        // Redis和MySQL连接池会在析构时自动清理

        LOG_INFO("=== 用户核心服务已停止 ===");

    } catch (const std::exception& e) {
        LOG_ERROR("用户核心服务停止异常: " + std::string(e.what()));
    }
}

// isRunning() 方法已在头文件中内联定义，删除重复定义

/**
 * @brief 获取服务统计信息
 * @return JSON格式的统计信息
 */
nlohmann::json UserService::getStatistics() const {
    nlohmann::json stats;
    
    stats["service_name"] = config_.server_config.serviceName;
    stats["service_version"] = config_.server_config.version;
    stats["running"] = running_.load();
    
    // 运行时间统计
    if (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        stats["uptime_seconds"] = uptime;
    }
    
    // 数据库连接池统计
    if (mysql_pool_) {
        stats["mysql_pool"]["total_connections"] = mysql_pool_->getTotalConnections();
        stats["mysql_pool"]["active_connections"] = mysql_pool_->getActiveConnections();
        stats["mysql_pool"]["idle_connections"] = mysql_pool_->getIdleConnections();
        stats["mysql_pool"]["running"] = mysql_pool_->isRunning();
    }
    if (redis_pool_) {
        stats["redis_pool"]["total_connections"] = redis_pool_->getTotalConnections();
        stats["redis_pool"]["active_connections"] = redis_pool_->getActiveConnections();
        stats["redis_pool"]["idle_connections"] = redis_pool_->getIdleConnections();
        stats["redis_pool"]["running"] = redis_pool_->isRunning();
    }
    
    // HTTP服务器统计
    if (http_server_) {
        stats["http_server"]["running"] = true;  // 简化的状态信息
    }
    
    // 请求统计
    stats["requests"]["total"] = total_requests_.load();
    stats["requests"]["successful"] = successful_requests_.load();
    stats["requests"]["failed"] = failed_requests_.load();
    
    // 定时器统计
    if (timer_manager_ && timer_manager_->isRunning()) {
        auto timer_stats = timer_manager_->getStatistics();
        stats["timer_manager"] = timer_stats;
    } else {
        stats["timer_manager"]["status"] = "not_running";
    }
    
    return stats;
}

/**
 * @brief 初始化数据库连接池
 * @return 成功返回true
 */
bool UserService::initializeDatabasePools() {
    try {
        LOG_INFO("初始化数据库连接池...");
        // common::config::DatabaseConfig db_config = common::config::DatabaseConfig::fromConfigManager();
        mysql_pool_ = std::make_shared<common::database::MySQLPool>(config_.database_config);
        mysql_pool_->start();
        if (!mysql_pool_->isRunning()) {
            LOG_ERROR("MySQL连接池启动失败");
            return false;
        }

        // common::config::RedisConfig redis_config = common::config::RedisConfig::fromConfigManager();
        redis_pool_ = std::make_shared<common::database::RedisPool>(config_.redis_config);
        redis_pool_->start();
        if (!redis_pool_->isRunning()) {
            LOG_ERROR("Redis连接池启动失败");
            return false;
        }
        
        LOG_INFO("数据库连接池初始化完成");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库连接池初始化异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 初始化数据库表结构
 * @return 成功返回true
 */
bool UserService::initializeDatabaseSchema() {
    try {
        LOG_INFO("🔧 开始初始化数据库表结构...");
        
        if (!user_repository_) {
            LOG_ERROR("数据访问层未初始化，无法初始化数据库表结构");
            return false;
        }
        
        // 🎯 调用UserRepository的initializeTables方法自动创建表
        if (!user_repository_->initializeTables()) {
            LOG_ERROR("数据库表结构初始化失败");
            return false;
        }
        
        LOG_INFO("✅ 数据库表结构初始化完成");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库表结构初始化异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 初始化HTTP服务器
 * @return 成功返回true
 */
bool UserService::initializeHttpServer() {
    http_server_running_ = true;
    std::promise<void> server_started_promise_;

    http_server_thread_ = std::thread([this, &server_started_promise_]()
    {
        try
        {
            // 创建HTTP服务器配置
            // common::http::HttpServerConfig server_config;

            // 创建HTTP服务器
            http_server_ = std::make_unique<common::http::HttpServer>(config_.http_config, thread_pool_);

            // 🔧 新增：设置 CORS 中间件
            setupCorsMiddleware();

            setupRoutes();

            // 启动前通知：服务器对象已准备好
            server_started_promise_.set_value();

            // 阻塞运行
            http_server_->start();
        }
        catch (...)
        {
            server_started_promise_.set_exception(std::current_exception());
            http_server_running_ = false;
            http_server_.reset();
        }
    });

    // 等待服务器对象构造完成（可选，用于同步）
    auto future = server_started_promise_.get_future();
    if (future.wait_for(std::chrono::seconds(300)) != std::future_status::ready)
    {
        LOG_ERROR("HttpServer failed to initialize within timeout");
        return false;
    }

    return true;
}

/**
 * @brief 设置HTTP路由
 */
void UserService::setupRoutes() {
    LOG_INFO("设置HTTP路由...");

    // ========== 系统级 API (统一前缀 /api/v1/user/) ==========
    // 健康检查
    http_server_->get("/api/v1/user/health", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleHealthCheck(req, res);
    });

    // 服务信息
    http_server_->get("/api/v1/user/info", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleServiceInfo(req, res);
    });

    // 统计信息
    http_server_->get("/api/v1/user/stats", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleStatistics(req, res);
    });

    // 定时器统计信息
    http_server_->get("/api/v1/user/timer-stats", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleTimerStatistics(req, res);
    });

    // 服务端点发现
    http_server_->get("/api/v1/user/service/endpoints", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleServiceEndpoints(req, res);
    });

    // ========== 用户管理 API ==========
    http_server_->get("/api/v1/user/:user_id", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleGetUser(req, res);
    });

    http_server_->post("/api/v1/user", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleCreateUser(req, res);
    });

    http_server_->put("/api/v1/user/:user_id", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleUpdateUser(req, res);
    });

    http_server_->put("/api/v1/user/:user_id/password", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleUpdatePassword(req, res);
    });

    // ========== 用户查询 API ==========
    http_server_->get("/api/v1/user/by-username/:username", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleGetUserByUsername(req, res);
    });

    http_server_->get("/api/v1/user/by-email/:email", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleGetUserByEmail(req, res);
    });

    // ========== 用户档案 API ==========
    http_server_->get("/api/v1/user/:user_id/profile", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleGetUserProfile(req, res);
    });

    http_server_->put("/api/v1/user/:user_id/profile", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleUpdateUserProfile(req, res);
    });

    // ========== 用户偏好设置 API ==========
    http_server_->get("/api/v1/user/:user_id/preferences", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleGetUserPreferences(req, res);
    });

    http_server_->put("/api/v1/user/:user_id/preferences", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleUpdateUserPreferences(req, res);
    });

    // ========== 用户状态管理 API ==========
    http_server_->put("/api/v1/user/:user_id/online-status", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleUpdateOnlineStatus(req, res);
    });

    http_server_->put("/api/v1/user/:user_id/last-login", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleUpdateLastLogin(req, res);
    });

    http_server_->post("/api/v1/user/:user_id/login-attempts/increment", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleIncrementLoginAttempts(req, res);
    });

    http_server_->post("/api/v1/user/:user_id/login-attempts/reset", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleResetLoginAttempts(req, res);
    });

    // ========== 用户验证 API ==========
    http_server_->post("/api/v1/user/check-username", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleCheckUsername(req, res);
    });

    http_server_->post("/api/v1/user/check-email", [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        this->handleCheckEmail(req, res);
    });

    LOG_INFO("HTTP路由设置完成，共注册 18 个路由");
}

/**
 * @brief 设置定时任务
 */
void UserService::setupTimerTasks() {
    LOG_INFO("📅 [USER_SERVICE] 正在设置基于时间轮的定时任务...");
    
    if (!timer_manager_) {
        LOG_ERROR("❌ [USER_SERVICE] 定时器管理器未初始化");
        return;
    }
    
    // 数据统计任务 - 每小时执行一次 (3600000ms)
    bool success1 = timer_manager_->addPeriodicTask(
        "user_statistics_task",
        "用户服务统计任务",
        TaskType::STATISTICS,
        3600000, // 1小时 = 3600000毫秒
        [this]() {
            this->performStatisticsTask();
        }
    );
    
    // 缓存清理任务 - 每30分钟执行一次 (1800000ms)
    bool success2 = timer_manager_->addPeriodicTask(
        "cache_cleanup_task",
        "缓存清理任务",
        TaskType::CACHE_CLEANUP,
        1800000, // 30分钟 = 1800000毫秒
        [this]() {
            this->performCacheCleanupTask();
        }
    );
    
    // 用户状态检查任务 - 每10分钟执行一次 (600000ms)
    bool success3 = timer_manager_->addPeriodicTask(
        "user_status_check_task",
        "用户状态检查任务",
        TaskType::USER_STATUS_CHECK,
        600000, // 10分钟 = 600000毫秒
        [this]() {
            this->performUserStatusCheckTask();
        }
    );
    
    // 健康检查任务 - 每5分钟执行一次 (300000ms)
    bool success4 = timer_manager_->addPeriodicTask(
        "health_check_task",
        "健康检查任务",
        TaskType::HEALTH_CHECK,
        300000, // 5分钟 = 300000毫秒
        [this]() {
            this->performHealthCheckTask();
        }
    );

    // 统计添加结果
    int successful_tasks = (success1 ? 1 : 0) + (success2 ? 1 : 0) +
                          (success3 ? 1 : 0) + (success4 ? 1 : 0);

    if (successful_tasks == 4) {
        LOG_INFO("✅ [USER_SERVICE] 所有定时任务设置完成 (4/4)");
    } else {
        LOG_WARNING("⚠️ [USER_SERVICE] 部分定时任务设置失败 (" +
                   std::to_string(successful_tasks) + "/4)");
    }
    
    // 打印当前定时器统计
    if (timer_manager_->isRunning()) {
        auto stats = timer_manager_->getStatistics();
        LOG_INFO("📊 [USER_SERVICE] 定时器统计: 活跃任务数 = " + 
                std::to_string(stats["active_tasks"].get<int>()));
    }
}

/**
 * @brief 执行统计任务
 */
void UserService::performStatisticsTask() {
    try {
        LOG_DEBUG("执行数据统计任务...");
        
        // 获取并记录服务统计信息
        auto stats = getStatistics();
        LOG_INFO("服务统计: " + stats.dump(2));
        
        // 执行数据库统计
        if (user_repository_) {
            // TODO: 添加数据库统计逻辑
            // user_repository_->performStatistics();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据统计任务异常: " + std::string(e.what()));
    }
}

/**
 * @brief 执行缓存清理任务
 */
void UserService::performCacheCleanupTask() {
    try {
        LOG_DEBUG("执行缓存清理任务...");
        // TODO: 添加缓存清理逻辑
        
        // 这里可以实现缓存清理逻辑
        
    } catch (const std::exception& e) {
        LOG_ERROR("缓存清理任务异常: " + std::string(e.what()));
    }
}

/**
 * @brief 执行用户状态检查任务
 */
void UserService::performUserStatusCheckTask() {
    try {
        LOG_DEBUG("📊 [USER_SERVICE] 执行用户状态检查任务...");
        
        // 获取用户统计信息
        if (user_repository_) {
            int total_users = user_repository_->getUserCount();
            int online_users = user_repository_->getOnlineUserCount();
            int today_registered = user_repository_->getTodayRegisteredUserCount();
            
            LOG_INFO("👥 [USER_SERVICE] 用户状态统计 - 总用户数: " + std::to_string(total_users) + 
                    ", 在线用户: " + std::to_string(online_users) + 
                    ", 今日注册: " + std::to_string(today_registered));

            if (total_users > 0) {
                // 只有当有用户时才进行比例检查
                double online_ratio = static_cast<double>(online_users) / total_users;
                if (online_ratio > 0.8) {
                    LOG_WARNING("⚠️ [USER_SERVICE] 在线用户比例过高: " + 
                               std::to_string(static_cast<int>(online_ratio * 100)) + "%, 可能存在异常");
                }
            } else if (online_users > 0) {
                LOG_ERROR("❌ [USER_SERVICE] 数据不一致：总用户数为0但在线用户数为" + 
                         std::to_string(online_users) + "，请检查数据库数据或查询逻辑");
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [USER_SERVICE] 用户状态检查任务异常: " + std::string(e.what()));
    }
}

/**
 * @brief 执行健康检查任务
 */
void UserService::performHealthCheckTask() {
    try {
        LOG_DEBUG("🏥 [USER_SERVICE] 执行健康检查任务...");
        
        // 检查数据库连接健康状态
        bool mysql_healthy = mysql_pool_ && mysql_pool_->isRunning();
        bool redis_healthy = redis_pool_ && redis_pool_->isRunning();
        bool http_healthy = http_server_ && true; // HTTP服务器健康检查
        bool timer_healthy = timer_manager_ && timer_manager_->isRunning();
        
        if (mysql_healthy && redis_healthy && http_healthy && timer_healthy) {
            LOG_DEBUG("✅ [USER_SERVICE] 所有组件健康检查通过");
        } else {
            LOG_WARNING(std::string("⚠️ [USER_SERVICE] 健康检查发现问题 - MySQL: ") + 
                       (mysql_healthy ? "健康" : "异常") + 
                       ", Redis: " + (redis_healthy ? "健康" : "异常") + 
                       ", HTTP: " + (http_healthy ? "健康" : "异常") +
                       ", Timer: " + (timer_healthy ? "健康" : "异常"));
        }
        
        // TODO 可以在这里添加自动修复逻辑或告警通知

    } catch (const std::exception& e) {
        LOG_ERROR("❌ [USER_SERVICE] 健康检查任务异常: " + std::string(e.what()));
    }
}

/**
 * @brief 处理健康检查
 */
void UserService::handleHealthCheck(const common::http::HttpRequest& request, 
                                  common::http::HttpResponse& response) {
    (void)request;  // 消除未使用参数警告
    nlohmann::json health_status;
    health_status["status"] = "healthy";
    health_status["service"] = config_.server_config.serviceName;
    health_status["version"] = config_.server_config.version;
    health_status["timestamp"] = std::time(nullptr);
    
    // 检查数据库连接状态
    bool mysql_healthy = mysql_pool_ && mysql_pool_->isRunning();
    bool redis_healthy = redis_pool_ && redis_pool_->isRunning();
    
    health_status["database"]["mysql"] = mysql_healthy ? "healthy" : "unhealthy";
    health_status["database"]["redis"] = redis_healthy ? "healthy" : "unhealthy";
    
    // 设置HTTP状态码
    int status_code = (mysql_healthy && redis_healthy) ? 200 : 503;
    
    response.setStatus(status_code);
    response.setHeader("Content-Type", "application/json");
    response.setBody(health_status.dump(2));
}

/**
 * @brief 处理服务信息
 */
void UserService::handleServiceInfo(const common::http::HttpRequest& request, 
                                  common::http::HttpResponse& response) {
    (void)request;  // 消除未使用参数警告
    nlohmann::json info = getStatistics();
    
    response.setStatus(200);
    response.setHeader("Content-Type", "application/json");
    response.setBody(info.dump(2));
}

/**
 * @brief 处理服务端点信息请求
 */
void UserService::handleServiceEndpoints(const common::http::HttpRequest& request, 
                                        common::http::HttpResponse& response) {
    try {
        total_requests_++;
        (void)request;  // 消除未使用参数警告
        
        LOG_INFO("🔍 [USER_SERVICE] 收到服务端点查询请求");
        
        // 🔧 API网关兼容修复：构造符合API网关期望格式的端点信息
        nlohmann::json endpoints_info;
        
        // 🔧 关键：API网关期望success字段在顶层
        endpoints_info["success"] = true;
        
        // 服务基本信息
        endpoints_info["service_name"] = config_.server_config.serviceName;
        endpoints_info["service_version"] = config_.server_config.version;
        endpoints_info["host"] = config_.http_config.network_config.bind_address;
        endpoints_info["port"] = config_.http_config.network_config.listen_port;
        endpoints_info["timestamp"] = std::time(nullptr);
        
        // 🔧 关键：API网关期望endpoints数组在顶层（不在data里）
        endpoints_info["endpoints"] = nlohmann::json::array({
            // 健康检查和服务信息
            {{"path", "/health"}, {"method", "GET"}, {"description", "健康检查"}, {"requires_auth", false}},
            {{"path", "/info"}, {"method", "GET"}, {"description", "服务信息"}, {"requires_auth", false}},
            {{"path", "/stats"}, {"method", "GET"}, {"description", "服务统计"}, {"requires_auth", false}},
            {{"path", "/timer-stats"}, {"method", "GET"}, {"description", "定时器统计"}, {"requires_auth", false}},
            {{"path", "/api/v1/service/endpoints"}, {"method", "GET"}, {"description", "服务端点列表"}, {"requires_auth", false}},

            // 用户管理API
            {{"path", "/api/v1/user"}, {"method", "POST"}, {"description", "创建用户"}, {"requires_auth", false}},
            {{"path", "/api/v1/user/{user_id}"}, {"method", "GET"}, {"description", "获取用户信息"}, {"requires_auth", true}},
            {{"path", "/api/v1/user/{user_id}"}, {"method", "PUT"}, {"description", "更新用户信息"}, {"requires_auth", true}},
            {{"path", "/api/v1/user/by-username/{username}"}, {"method", "GET"}, {"description", "根据用户名获取用户"}, {"requires_auth", true}},
            {{"path", "/api/v1/user/by-email/{email}"}, {"method", "GET"}, {"description", "根据邮箱获取用户"}, {"requires_auth", true}},

            // 用户档案API
            {{"path", "/api/v1/user/{user_id}/profile"}, {"method", "GET"}, {"description", "获取用户档案"}, {"requires_auth", true}},
            {{"path", "/api/v1/user/{user_id}/profile"}, {"method", "PUT"}, {"description", "更新用户档案"}, {"requires_auth", true}},

            // 用户偏好设置API
            {{"path", "/api/v1/user/{user_id}/preferences"}, {"method", "GET"}, {"description", "获取用户偏好设置"}, {"requires_auth", true}},
            {{"path", "/api/v1/user/{user_id}/preferences"}, {"method", "PUT"}, {"description", "更新用户偏好设置"}, {"requires_auth", true}},

            // 用户状态管理API
            {{"path", "/api/v1/user/{user_id}/online-status"}, {"method", "PUT"}, {"description", "更新用户在线状态"}, {"requires_auth", true}},

            // 用户验证API
            {{"path", "/api/v1/user/check-username"}, {"method", "POST"}, {"description", "检查用户名是否存在"}, {"requires_auth", false}},
            {{"path", "/api/v1/user/check-email"}, {"method", "POST"}, {"description", "检查邮箱是否存在"}, {"requires_auth", false}}
        });
        
        // 添加服务能力信息
        endpoints_info["capabilities"] = {
            {"user_management", true},
            {"user_profiles", true},
            {"user_preferences", true},
            {"user_validation", true},
            {"health_check", true},
            {"metrics", true}
        };
        
        // 服务元数据
        endpoints_info["metadata"] = {
            {"description", "User Management Microservice"},
            {"architecture", "repository_pattern"},
            {"team", "core-services"},
            {"features", nlohmann::json::array({"user_management", "profiles", "preferences", "validation"})}
        };
        
        successful_requests_++;

        // 因为API网关期望success和endpoints在顶层，而createSuccessResponse会包装在data里
        response.setStatus(200);
        response.setHeader("Content-Type", "application/json");
        response.setBody(endpoints_info.dump(2));

        LOG_INFO("✅ API发现端点请求处理完成，返回 " + std::to_string(endpoints_info["endpoints"].size()) + " 个端点");
        LOG_INFO("✅ [USER_SERVICE] 服务端点信息返回成功");
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("❌ [USER_SERVICE] 处理服务端点信息异常: " + std::string(e.what()));
        createErrorResponse(response, "ENDPOINTS_ERROR", "获取服务端点信息失败", 500);
    }
}

/**
 * @brief 处理统计信息
 */
void UserService::handleStatistics(const common::http::HttpRequest& request, 
                                   common::http::HttpResponse& response) {
    try {
        total_requests_++;
        (void)request;  // 消除未使用参数警告
        
        // 获取服务统计信息
        nlohmann::json stats = getStatistics();
        
        successful_requests_++;
        createSuccessResponse(response, stats);
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("❌ [USER_SERVICE] 处理统计信息异常: " + std::string(e.what()));
        createErrorResponse(response, "STATS_ERROR", "获取统计信息失败", 500);
    }
}

/**
 * @brief 处理定时器统计信息
 */
void UserService::handleTimerStatistics(const common::http::HttpRequest& request,
                                        common::http::HttpResponse& response) {
    try {
        total_requests_++;
        (void)request;  // 消除未使用参数警告
        
        // 获取定时器统计信息
        nlohmann::json timer_stats = getTimerStatistics();
        
        // 添加时间轮相关信息
        nlohmann::json result;
        result["success"] = true;
        result["timestamp"] = std::time(nullptr);
        result["timer_statistics"] = timer_stats;
        
        // 如果定时器管理器正常运行，添加任务列表信息
        if (timer_manager_ && timer_manager_->isRunning()) {
            auto all_tasks = timer_manager_->getAllTasks();
            nlohmann::json task_list = nlohmann::json::array();
            
            for (const auto& task : all_tasks) {
                nlohmann::json task_info;
                task_info["task_id"] = task->task_id;
                task_info["task_name"] = task->task_name;
                // 内联类型转换，避免函数作用域问题
                switch (task->type) {
                    case TaskType::STATISTICS: task_info["type"] = "statistics"; break;
                    case TaskType::CACHE_CLEANUP: task_info["type"] = "cache_cleanup"; break;
                    case TaskType::USER_STATUS_CHECK: task_info["type"] = "user_status_check"; break;
                    case TaskType::HEALTH_CHECK: task_info["type"] = "health_check"; break;
                    case TaskType::HEARTBEAT: task_info["type"] = "heartbeat"; break;
                    case TaskType::SESSION_CLEANUP: task_info["type"] = "session_cleanup"; break;
                    case TaskType::CUSTOM: task_info["type"] = "custom"; break;
                    default: task_info["type"] = "unknown"; break;
                }
                task_info["is_periodic"] = task->is_periodic;
                task_info["interval_ms"] = task->interval.count();
                task_info["execution_count"] = task->execution_count.load();
                task_info["is_running"] = task->is_running.load();
                
                // 计算上次执行时间（如果有）
                if (task->last_executed != std::chrono::steady_clock::time_point::min()) {
                    auto last_exec_time_t = std::chrono::duration_cast<std::chrono::seconds>(
                        task->last_executed.time_since_epoch()
                    ).count();
                    task_info["last_executed"] = last_exec_time_t;
                }
                
                task_list.push_back(task_info);
            }
            
            result["active_tasks"] = task_list;
        }
        
        successful_requests_++;
        createSuccessResponse(response, result);
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("❌ [USER_SERVICE] 处理定时器统计信息异常: " + std::string(e.what()));
        createErrorResponse(response, "TIMER_STATS_ERROR", "获取定时器统计信息失败", 500);
    }
}


// 这里只提供一些关键API处理方法的示例实现
// 实际项目中应该根据具体需求完整实现所有API

/**
 * @brief 处理获取用户信息
 */
void UserService::handleGetUser(const common::http::HttpRequest& request,
                              common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");

        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }

        // 调用数据访问层获取用户信息
        auto user = user_repository_->getUserById(user_id);
        if (user.has_value()) {
            successful_requests_++;
            // 使用 createSuccessResponse 包装响应，确保格式统一
            // 注意：内部服务调用，包含敏感字段用于认证（password_hash, salt）
            nlohmann::json user_json = user->toJson(true);

            // 🔧 修复：同时获取用户档案，合并 nickname 和 avatar_url 字段
            // 这些字段在 UserProfile 表中，但其他服务（如 gomoku）期望在用户信息中获取
            try {
                auto profile = user_repository_->getUserProfile(user_id);
                if (profile.has_value()) {
                    user_json["nickname"] = profile->nickname;
                    user_json["avatar_url"] = profile->avatar_url;
                    // LOG_DEBUG("用户档案合并成功: " + user_id + ", nickname=" + profile->nickname);
                } else {
                    // 如果没有档案，使用用户名作为默认昵称
                    user_json["nickname"] = user->username;
                    user_json["avatar_url"] = "";
                    // LOG_DEBUG("用户档案不存在，使用默认值: " + user_id);
                }
            } catch (const std::exception& profile_e) {
                // 档案获取失败不影响主流程，使用默认值
                user_json["nickname"] = user->username;
                user_json["avatar_url"] = "";
                LOG_WARNING("获取用户档案失败，使用默认值: " + user_id + ", 错误: " + std::string(profile_e.what()));
            }

            createSuccessResponse(response, user_json);
        } else {
            failed_requests_++;
            createErrorResponse(response, "USER_NOT_FOUND", "User not found", 404);
        }

    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理获取用户信息异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理创建用户
 */
void UserService::handleCreateUser(const common::http::HttpRequest& request, 
                                 common::http::HttpResponse& response) {
    try {
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            response.setStatus(400);
            response.setBody("{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // 创建用户对象
        UserInfo user = UserInfo::fromJson(json);
        
        // 🔧 修复: 提取额外的档案信息（nickname等）
        std::string nickname = json.contains("nickname") && json["nickname"].is_string() 
                             ? json["nickname"].get<std::string>() 
                             : user.username; // 默认使用username
        
        std::string avatar_url = json.contains("avatar_url") && json["avatar_url"].is_string() 
                               ? json["avatar_url"].get<std::string>() 
                               : "";
        
        // 验证用户信息
        std::string validation_error = user.validate();
        if (!validation_error.empty()) {
            response.setStatus(400);
            response.setBody("{\"error\":\"" + validation_error + "\"}");
            return;
        }
        
        // 🎯 调用增强的用户创建方法（传递档案信息）
        if (user_repository_->createCompleteUser(user, nickname, avatar_url)) {
            response.setStatus(201);
            response.setHeader("Content-Type", "application/json");
            response.setBody("{\"message\":\"User created successfully\"}");
        } else {
            response.setStatus(500);
            response.setBody("{\"error\":\"Failed to create user\"}");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("处理创建用户异常: " + std::string(e.what()));
        response.setStatus(500);
        response.setBody("{\"error\":\"Internal server error\"}");
    }
}

/**
 * @brief 处理根据用户名获取用户信息
 */
void UserService::handleGetUserByUsername(const common::http::HttpRequest& request, 
                                         common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string username = request.getParam("username");
        
        // 🔍 调试：记录提取的用户名参数
        LOG_DEBUG("处理用户名查询: 提取的username参数='" + username + "'");
        
        if (username.empty()) {
            failed_requests_++;
            LOG_ERROR("用户名参数为空，请求路径: " + request.getPath());
            createErrorResponse(response, "MISSING_USERNAME", "Missing username parameter", 400);
            return;
        }
        
        auto user = user_repository_->getUserByUsername(username);
        if (user.has_value()) {
            successful_requests_++;
            // 内部服务调用，包含敏感字段（password_hash, salt）用于认证
            createSuccessResponse(response, user->toJson(true));
        } else {
            failed_requests_++;
            createErrorResponse(response, "USER_NOT_FOUND", "User not found", 404);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理根据用户名获取用户信息异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理根据邮箱获取用户信息
 */
void UserService::handleGetUserByEmail(const common::http::HttpRequest& request, 
                                      common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string email = request.getParam("email");
        
        // 🔍 调试：记录提取的邮箱参数
        LOG_DEBUG("处理邮箱查询: 提取的email参数='" + email + "'");
        
        if (email.empty()) {
            failed_requests_++;
            LOG_ERROR("邮箱参数为空，请求路径: " + request.getPath());
            createErrorResponse(response, "MISSING_EMAIL", "Missing email parameter", 400);
            return;
        }
        
        auto user = user_repository_->getUserByEmail(email);
        if (user.has_value()) {
            successful_requests_++;
            // 内部服务调用，包含敏感字段（password_hash, salt）用于认证
            createSuccessResponse(response, user->toJson(true));
        } else {
            failed_requests_++;
            createErrorResponse(response, "USER_NOT_FOUND", "User not found", 404);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理根据邮箱获取用户信息异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理更新用户信息
 */
void UserService::handleUpdateUser(const common::http::HttpRequest& request, 
                                  common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");
        
        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }
        
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }
        
        // 先获取现有用户信息
        auto existing_user = user_repository_->getUserById(user_id);
        if (!existing_user.has_value()) {
            failed_requests_++;
            createErrorResponse(response, "USER_NOT_FOUND", "User not found", 404);
            return;
        }
        
        // 更新字段
        UserInfo updated_user = existing_user.value();
        if (json.contains("email") && json["email"].is_string()) {
            updated_user.email = json["email"].get<std::string>();
        }
        if (json.contains("phone") && json["phone"].is_string()) {
            updated_user.phone = json["phone"].get<std::string>();
        }
        // 更新时间
        updated_user.updated_at = std::chrono::system_clock::now();
        
        // 验证更新后的用户信息
        std::string validation_error = updated_user.validate();
        if (!validation_error.empty()) {
            failed_requests_++;
            createErrorResponse(response, "VALIDATION_ERROR", validation_error, 400);
            return;
        }
        
        // 执行更新
        if (user_repository_->updateUser(updated_user)) {
            successful_requests_++;
            createSuccessResponse(response, updated_user.toJson());
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to update user", 500);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理更新用户信息异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

void UserService::handleUpdatePassword(const common::http::HttpRequest& request,
                                        common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");

        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }

        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception&) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }

        if (!json.contains("password_hash") || !json["password_hash"].is_string()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_PASSWORD_HASH", "Missing password_hash field", 400);
            return;
        }

        std::string password_hash = json["password_hash"].get<std::string>();
        std::string salt = json.value("salt", "");

        if (user_repository_->updatePassword(user_id, password_hash, salt)) {
            successful_requests_++;
            nlohmann::json result;
            result["success"] = true;
            result["message"] = "Password updated successfully";
            createSuccessResponse(response, result);
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to update password", 500);
        }

    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理更新密码异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理获取用户档案
 */
void UserService::handleGetUserProfile(const common::http::HttpRequest& request, 
                                      common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");
        
        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }
        
        auto profile = user_repository_->getUserProfile(user_id);
        if (profile.has_value()) {
            successful_requests_++;
            createSuccessResponse(response, profile->toJson());
        } else {
            failed_requests_++;
            createErrorResponse(response, "PROFILE_NOT_FOUND", "User profile not found", 404);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理获取用户档案异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理更新用户档案
 */
void UserService::handleUpdateUserProfile(const common::http::HttpRequest& request, 
                                         common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");
        
        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }
        
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }
        
        // 创建或更新用户档案
        UserProfile profile = UserProfile::fromJson(json);
        profile.user_id = user_id;
        profile.updated_at = std::chrono::system_clock::now();
        
        // 验证档案信息
        std::string validation_error = profile.validate();
        if (!validation_error.empty()) {
            failed_requests_++;
            createErrorResponse(response, "VALIDATION_ERROR", validation_error, 400);
            return;
        }
        
        // 先尝试获取现有档案
        auto existing_profile = user_repository_->getUserProfile(user_id);
        bool success = false;
        
        if (existing_profile.has_value()) {
            // 更新现有档案
            success = user_repository_->updateUserProfile(profile);
        } else {
            // 创建新档案
            profile.created_at = std::chrono::system_clock::now();
            success = user_repository_->createUserProfile(profile);
        }
        
        if (success) {
            successful_requests_++;
            createSuccessResponse(response, profile.toJson());
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to update user profile", 500);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理更新用户档案异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理获取用户偏好设置
 */
void UserService::handleGetUserPreferences(const common::http::HttpRequest& request, 
                                          common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");
        
        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }
        
        auto preferences = user_repository_->getUserPreferences(user_id);
        if (preferences.has_value()) {
            successful_requests_++;
            createSuccessResponse(response, preferences->toJson());
        } else {
            failed_requests_++;
            createErrorResponse(response, "PREFERENCES_NOT_FOUND", "User preferences not found", 404);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理获取用户偏好设置异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理更新用户偏好设置
 */
void UserService::handleUpdateUserPreferences(const common::http::HttpRequest& request, 
                                             common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");
        
        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }
        
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }
        
        // 创建或更新用户偏好设置
        UserPreferences preferences = UserPreferences::fromJson(json);
        preferences.user_id = user_id;
        preferences.updated_at = std::chrono::system_clock::now();
        
        // 验证偏好设置
        std::string validation_error = preferences.validate();
        if (!validation_error.empty()) {
            failed_requests_++;
            createErrorResponse(response, "VALIDATION_ERROR", validation_error, 400);
            return;
        }
        
        // 先尝试获取现有偏好设置
        auto existing_preferences = user_repository_->getUserPreferences(user_id);
        bool success = false;
        
        if (existing_preferences.has_value()) {
            // 更新现有偏好设置
            success = user_repository_->updateUserPreferences(preferences);
        } else {
            // 创建新偏好设置
            preferences.created_at = std::chrono::system_clock::now();
            success = user_repository_->createUserPreferences(preferences);
        }
        
        if (success) {
            successful_requests_++;
            createSuccessResponse(response, preferences.toJson());
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to update user preferences", 500);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理更新用户偏好设置异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理更新用户在线状态
 */
void UserService::handleUpdateOnlineStatus(const common::http::HttpRequest& request, 
                                          common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");
        
        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }
        
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }
        
        if (!json.contains("online_status") || !json["online_status"].is_string()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_STATUS", "Missing online_status field", 400);
            return;
        }
        
        std::string status_str = json["online_status"].get<std::string>();
        OnlineStatus status = UserInfo::stringToOnlineStatus(status_str);
        
        if (user_repository_->updateOnlineStatus(user_id, status)) {
            successful_requests_++;
            nlohmann::json result;
            result["message"] = "Online status updated successfully";
            result["user_id"] = user_id;
            result["online_status"] = status_str;
            createSuccessResponse(response, result);
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to update online status", 500);
        }
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理更新用户在线状态异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理更新最后登录信息
 */
void UserService::handleUpdateLastLogin(const common::http::HttpRequest& request,
                                        common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");

        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }

        // 解析请求体
        std::string login_ip;
        if (!request.getBody().empty()) {
            try {
                nlohmann::json json = nlohmann::json::parse(request.getBody());
                if (json.contains("last_login_ip")) {
                    login_ip = json["last_login_ip"].get<std::string>();
                }
            } catch (const std::exception& e) {
                // 忽略 JSON 解析错误，使用默认值
            }
        }

        if (user_repository_->updateLastLogin(user_id, login_ip)) {
            successful_requests_++;
            nlohmann::json result;
            result["message"] = "Last login updated successfully";
            result["user_id"] = user_id;
            result["last_login_ip"] = login_ip;
            createSuccessResponse(response, result);
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to update last login", 500);
        }

    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理更新最后登录信息异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理增加登录尝试次数
 */
void UserService::handleIncrementLoginAttempts(const common::http::HttpRequest& request,
                                               common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");

        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }

        int attempts = user_repository_->incrementLoginAttempts(user_id);
        successful_requests_++;

        nlohmann::json result;
        result["message"] = "Login attempts incremented";
        result["user_id"] = user_id;
        result["login_attempts"] = attempts;
        createSuccessResponse(response, result);

    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理增加登录尝试次数异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理重置登录尝试次数
 */
void UserService::handleResetLoginAttempts(const common::http::HttpRequest& request,
                                           common::http::HttpResponse& response) {
    try {
        total_requests_++;
        std::string user_id = request.getParam("user_id");

        if (user_id.empty()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USER_ID", "Missing user_id parameter", 400);
            return;
        }

        if (user_repository_->resetLoginAttempts(user_id)) {
            successful_requests_++;
            nlohmann::json result;
            result["message"] = "Login attempts reset successfully";
            result["user_id"] = user_id;
            result["login_attempts"] = 0;
            createSuccessResponse(response, result);
        } else {
            failed_requests_++;
            createErrorResponse(response, "UPDATE_FAILED", "Failed to reset login attempts", 500);
        }

    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理重置登录尝试次数异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理检查用户名
 */
void UserService::handleCheckUsername(const common::http::HttpRequest& request, 
                                     common::http::HttpResponse& response) {
    try {
        total_requests_++;
        
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }
        
        if (!json.contains("username") || !json["username"].is_string()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_USERNAME", "Missing username field", 400);
            return;
        }
        
        std::string username = json["username"].get<std::string>();
        
        // 🔧 修复：正确处理数据库异常
        bool exists = false;
        bool db_error = false;
        
        try {
            exists = user_repository_->usernameExists(username);
            LOG_INFO("🔍 [API_CHECK] 用户名检查完成: username=" + username + ", exists=" + (exists ? "true" : "false") + ", available=" + (exists ? "false" : "true"));
        } catch (const std::exception& db_e) {
            LOG_ERROR("数据库检查用户名异常: " + std::string(db_e.what()) + ", username=" + username);
            db_error = true;
        }
        
        if (db_error) {
            failed_requests_++;
            createErrorResponse(response, "DATABASE_ERROR", "Unable to check username availability due to database error", 503);
            return;
        }
        
        nlohmann::json result;
        result["username"] = username;
        result["exists"] = exists;
        result["available"] = !exists;
        
        successful_requests_++;
        createSuccessResponse(response, result);
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理检查用户名异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

/**
 * @brief 处理检查邮箱
 */
void UserService::handleCheckEmail(const common::http::HttpRequest& request, 
                                  common::http::HttpResponse& response) {
    try {
        total_requests_++;
        
        // 解析请求体
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(request.getBody());
        } catch (const std::exception& e) {
            failed_requests_++;
            createErrorResponse(response, "INVALID_JSON", "Invalid JSON", 400);
            return;
        }
        
        if (!json.contains("email") || !json["email"].is_string()) {
            failed_requests_++;
            createErrorResponse(response, "MISSING_EMAIL", "Missing email field", 400);
            return;
        }
        
        std::string email = json["email"].get<std::string>();
        
        // 🔧 修复：正确处理数据库异常
        bool exists = false;
        bool db_error = false;
        
        try {
            exists = user_repository_->emailExists(email);
        } catch (const std::exception& db_e) {
            LOG_ERROR("数据库检查邮箱异常: " + std::string(db_e.what()) + ", email=" + email);
            db_error = true;
        }
        
        if (db_error) {
            failed_requests_++;
            createErrorResponse(response, "DATABASE_ERROR", "Unable to check email availability due to database error", 503);
            return;
        }
        
        nlohmann::json result;
        result["email"] = email;
        result["exists"] = exists;
        result["available"] = !exists;
        
        successful_requests_++;
        createSuccessResponse(response, result);
        
    } catch (const std::exception& e) {
        failed_requests_++;
        LOG_ERROR("处理检查邮箱异常: " + std::string(e.what()));
        createErrorResponse(response, "INTERNAL_ERROR", "Internal server error", 500);
    }
}

// ==================== 辅助方法实现 ====================

/**
 * @brief 创建错误响应
 */
void UserService::createErrorResponse(common::http::HttpResponse& response, 
                                     const std::string& error_code,
                                     const std::string& error_message, 
                                     int status_code) {
    nlohmann::json error_json;
    error_json["error"] = true;
    error_json["error_code"] = error_code;
    error_json["error_message"] = error_message;
    error_json["timestamp"] = std::time(nullptr);
    
    response.setStatus(status_code);
    response.setHeader("Content-Type", "application/json");
    response.setBody(error_json.dump(2));
}

/**
 * @brief 创建成功响应
 */
void UserService::createSuccessResponse(common::http::HttpResponse& response, 
                                       const nlohmann::json& data, 
                                       int status_code) {
    nlohmann::json result;
    result["success"] = true;
    result["data"] = data;
    result["timestamp"] = std::time(nullptr);
    
    response.setStatus(status_code);
    response.setHeader("Content-Type", "application/json");
    response.setBody(result.dump(2));
}

/**
 * @brief 从路径中提取用户ID
 */
std::string UserService::extractUserIdFromPath(const std::string& path) {
    // 这个方法可以根据实际的路径参数提取逻辑来实现
    // 现在返回空字符串作为占位符
    (void)path; // 消除未使用参数警告
    return "";
}

// ==================== 定时器管理API实现 ====================

/**
 * @brief 获取定时器统计信息
 */
nlohmann::json UserService::getTimerStatistics() const {
    if (!timer_manager_) {
        return nlohmann::json{{"error", "定时器管理器未初始化"}};
    }
    
    return timer_manager_->getStatistics();
}

/**
 * @brief 暂停定时任务
 */
bool UserService::pauseTimerTask(const std::string& task_id) {
    if (!timer_manager_) {
        LOG_ERROR("❌ [USER_SERVICE] 定时器管理器未初始化，无法暂停任务: " + task_id);
        return false;
    }
    
    return timer_manager_->pauseTask(task_id);
}

/**
 * @brief 恢复定时任务
 */
bool UserService::resumeTimerTask(const std::string& task_id) {
    if (!timer_manager_) {
        LOG_ERROR("❌ [USER_SERVICE] 定时器管理器未初始化，无法恢复任务: " + task_id);
        return false;
    }
    
    return timer_manager_->resumeTask(task_id);
}

/**
 * @brief 取消定时任务
 */
bool UserService::cancelTimerTask(const std::string& task_id) {
    if (!timer_manager_) {
        LOG_ERROR("❌ [USER_SERVICE] 定时器管理器未初始化，无法取消任务: " + task_id);
        return false;
    }
    
    return timer_manager_->cancelTask(task_id);
}

// ==================== 用户管理公共API实现 ====================

/**
 * @brief 创建用户
 */
std::string UserService::createUser(const UserInfo& user_info) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return "";
        }
        
        // 验证用户信息
        std::string validation_error = user_info.validate();
        if (!validation_error.empty()) {
            LOG_ERROR("用户信息验证失败: " + validation_error);
            return "";
        }
        
        // 检查用户名和邮箱是否已存在
        if (user_repository_->usernameExists(user_info.username)) {
            LOG_ERROR("用户名已存在: " + user_info.username);
            return "";
        }
        
        if (user_repository_->emailExists(user_info.email)) {
            LOG_ERROR("邮箱已存在: " + user_info.email);
            return "";
        }
        
        // 创建用户
        if (user_repository_->createUser(user_info)) {
            LOG_INFO("用户创建成功: " + user_info.user_id);
            return user_info.user_id;
        }
        
        return "";
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建用户异常: " + std::string(e.what()));
        return "";
    }
}

/**
 * @brief 根据用户ID获取用户信息
 */
std::optional<UserInfo> UserService::getUserById(const std::string& user_id) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return std::nullopt;
        }
        
        return user_repository_->getUserById(user_id);
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 根据用户名获取用户信息
 */
std::optional<UserInfo> UserService::getUserByUsername(const std::string& username) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return std::nullopt;
        }
        
        return user_repository_->getUserByUsername(username);
        
    } catch (const std::exception& e) {
        LOG_ERROR("根据用户名获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 根据邮箱获取用户信息
 */
std::optional<UserInfo> UserService::getUserByEmail(const std::string& email) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return std::nullopt;
        }
        
        return user_repository_->getUserByEmail(email);
        
    } catch (const std::exception& e) {
        LOG_ERROR("根据邮箱获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 更新用户信息
 */
bool UserService::updateUser(const std::string& user_id, const UserInfo& user_info) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        // 验证用户信息
        std::string validation_error = user_info.validate();
        if (!validation_error.empty()) {
            LOG_ERROR("用户信息验证失败: " + validation_error);
            return false;
        }
        
        // 更新用户信息
        UserInfo updated_user = user_info;
        updated_user.user_id = user_id;  // 确保用户ID正确
        updated_user.updated_at = std::chrono::system_clock::now();
        
        return user_repository_->updateUser(updated_user);
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新用户信息异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 获取用户档案
 */
std::optional<UserProfile> UserService::getUserProfile(const std::string& user_id) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return std::nullopt;
        }
        
        return user_repository_->getUserProfile(user_id);
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户档案异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 更新用户档案
 */
bool UserService::updateUserProfile(const std::string& user_id, const UserProfile& profile) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        // 验证档案信息
        std::string validation_error = profile.validate();
        if (!validation_error.empty()) {
            LOG_ERROR("用户档案验证失败: " + validation_error);
            return false;
        }
        
        // 更新档案
        UserProfile updated_profile = profile;
        updated_profile.user_id = user_id;
        updated_profile.updated_at = std::chrono::system_clock::now();
        
        // 先尝试获取现有档案
        auto existing_profile = user_repository_->getUserProfile(user_id);
        if (existing_profile.has_value()) {
            return user_repository_->updateUserProfile(updated_profile);
        } else {
            updated_profile.created_at = std::chrono::system_clock::now();
            return user_repository_->createUserProfile(updated_profile);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新用户档案异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 获取用户偏好设置
 */
std::optional<UserPreferences> UserService::getUserPreferences(const std::string& user_id) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return std::nullopt;
        }
        
        return user_repository_->getUserPreferences(user_id);
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户偏好设置异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 更新用户偏好设置
 */
bool UserService::updateUserPreferences(const std::string& user_id, const UserPreferences& preferences) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        // 验证偏好设置
        std::string validation_error = preferences.validate();
        if (!validation_error.empty()) {
            LOG_ERROR("用户偏好设置验证失败: " + validation_error);
            return false;
        }
        
        // 更新偏好设置
        UserPreferences updated_preferences = preferences;
        updated_preferences.user_id = user_id;
        updated_preferences.updated_at = std::chrono::system_clock::now();
        
        // 先尝试获取现有偏好设置
        auto existing_preferences = user_repository_->getUserPreferences(user_id);
        if (existing_preferences.has_value()) {
            return user_repository_->updateUserPreferences(updated_preferences);
        } else {
            updated_preferences.created_at = std::chrono::system_clock::now();
            return user_repository_->createUserPreferences(updated_preferences);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新用户偏好设置异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 更新用户在线状态
 */
bool UserService::updateOnlineStatus(const std::string& user_id, const std::string& status) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        OnlineStatus online_status = UserInfo::stringToOnlineStatus(status);
        return user_repository_->updateOnlineStatus(user_id, online_status);
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新用户在线状态异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 更新最后登录信息
 */
bool UserService::updateLastLogin(const std::string& user_id, const std::string& login_ip) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        return user_repository_->updateLastLogin(user_id, login_ip);
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新最后登录信息异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 增加登录失败次数
 */
int UserService::incrementLoginAttempts(const std::string& user_id) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return 0;
        }
        
        return user_repository_->incrementLoginAttempts(user_id);
        
    } catch (const std::exception& e) {
        LOG_ERROR("增加登录失败次数异常: " + std::string(e.what()));
        return 0;
    }
}

/**
 * @brief 重置登录失败次数
 */
bool UserService::resetLoginAttempts(const std::string& user_id) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        return user_repository_->resetLoginAttempts(user_id);
        
    } catch (const std::exception& e) {
        LOG_ERROR("重置登录失败次数异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 锁定用户直到指定时间
 */
bool UserService::lockUserUntil(const std::string& user_id, const std::chrono::system_clock::time_point& lock_until) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        return user_repository_->lockUserUntil(user_id, lock_until);
        
    } catch (const std::exception& e) {
        LOG_ERROR("锁定用户异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 获取健康状态
 */
nlohmann::json UserService::getHealthStatus() {
    nlohmann::json health;
    
    health["service_name"] = config_.server_config.serviceName;
    health["service_version"] = config_.server_config.version;
    health["status"] = running_.load() ? "healthy" : "unhealthy";
    health["timestamp"] = std::time(nullptr);
    
    // 数据库连接状态
    health["database"]["mysql"] = mysql_pool_ && mysql_pool_->isRunning() ? "healthy" : "unhealthy";
    health["database"]["redis"] = redis_pool_ && redis_pool_->isRunning() ? "healthy" : "unhealthy";
    
    // 组件状态
    health["components"]["http_server"] = http_server_ ? "healthy" : "unhealthy";
    health["components"]["timer_manager"] = timer_manager_ && timer_manager_->isRunning() ? "healthy" : "unhealthy";
    health["components"]["thread_pool"] = thread_pool_ ? "healthy" : "unhealthy";
    
    return health;
}

// ==================== 用户验证API实现 ====================

/**
 * @brief 检查用户名是否存在
 */
bool UserService::isUsernameExists(const std::string& username) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        return user_repository_->usernameExists(username);
        
    } catch (const std::exception& e) {
        LOG_ERROR("检查用户名是否存在异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 检查邮箱是否存在
 */
bool UserService::isEmailExists(const std::string& email) {
    try {
        if (!user_repository_) {
            LOG_ERROR("用户数据访问层未初始化");
            return false;
        }
        
        return user_repository_->emailExists(email);
        
    } catch (const std::exception& e) {
        LOG_ERROR("检查邮箱是否存在异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== UserServiceConfig 方法实现 ====================

/**
 * @brief 从配置管理器加载配置
 */
UserServiceConfig UserServiceConfig::fromConfigManager() {
    UserServiceConfig config;
    
    auto& config_manager = common::config::ConfigManager::getInstance();
    
    config.server_config = common::config::ServerConfig::fromConfigManager();
    config.http_config = common::http::HttpServerConfig::fromConfigManager();

    config.database_config = common::config::DatabaseConfig::fromConfigManager();
    config.redis_config = common::config::RedisConfig::fromConfigManager();
    config.thread_pool_config = common::config::ThreadPoolConfig::fromConfigManager();

    return config;
}

/**
 * @brief 验证配置
 */
bool UserServiceConfig::validate() const {
    try {
        // 验证各个配置模块
        server_config.validate();
        database_config.validate();
        redis_config.validate();
        thread_pool_config.validate();

        if (!http_config.validate())
        {
            LOG_ERROR("http Server validate error");
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("配置验证失败: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 从配置文件加载配置
 */
UserServiceConfig UserServiceConfig::fromConfigFile(const std::string& config_file_path) {
    try {
        // 先加载配置文件
        auto& config_manager = common::config::ConfigManager::getInstance();
        if (!config_manager.loadFromFile(config_file_path)) {
            LOG_ERROR("无法加载配置文件: " + config_file_path);
            return UserServiceConfig{}; // 返回默认配置
        }

        LOG_WARNING("这里");
        
        // 然后使用ConfigManager加载配置
        return fromConfigManager();
        
    } catch (const std::exception& e) {
        LOG_ERROR("解析配置文件异常: " + std::string(e.what()));
        return UserServiceConfig{}; // 返回默认配置
    }
}

/**
 * @brief 转换为JSON
 */
nlohmann::json UserServiceConfig::toJson() const {
    nlohmann::json json;
    
    // 服务基本信息
    json["service_name"] = server_config.serviceName;
    json["service_version"] = server_config.version;
    
    // 网络配置
    json["network"]["bind_address"] = http_config.network_config.bind_address;
    json["network"]["listen_port"] = http_config.network_config.listen_port;
    json["network"]["worker_threads"] = http_config.network_config.worker_threads;
    json["network"]["max_connections"] = http_config.network_config.max_connections;
    
    // 数据库配置
    json["database"]["mysql"]["host"] = database_config.mysql_host;
    json["database"]["mysql"]["port"] = database_config.mysql_port;
    json["database"]["mysql"]["user"] = database_config.mysql_user;
    json["database"]["mysql"]["password"] = "***";  // 不显示密码
    json["database"]["mysql"]["database"] = database_config.mysql_database;
    json["database"]["mysql"]["charset"] = database_config.mysql_charset;
    json["database"]["mysql"]["pool_size"] = database_config.mysql_pool_size;
    
    // Redis配置
    json["redis"]["host"] = redis_config.redis_host;
    json["redis"]["port"] = redis_config.redis_port;
    json["redis"]["password"] = "***";  // 不显示密码
    json["redis"]["database"] = redis_config.redis_database;
    json["redis"]["pool_size"] = redis_config.redis_pool_size;
    
    // 线程池配置
    json["thread_pool"]["core_pool_size"] = thread_pool_config.core_pool_size;
    json["thread_pool"]["maximum_pool_size"] = thread_pool_config.maximum_pool_size;
    json["thread_pool"]["queue_capacity"] = thread_pool_config.queue_capacity;

    return json;
}

/**
 * @brief 设置 CORS 中间件
 */
void UserService::setupCorsMiddleware() {
    LOG_DEBUG("设置 CORS 中间件...");

    // 创建 CORS 中间件函数
    auto cors_middleware = [this](const common::http::HttpRequest& request, common::http::HttpResponse& response) {
        // 处理 OPTIONS 预检请求
        if (request.getMethod() == common::http::HttpMethod::OPTIONS) {
            response.setHeader("Access-Control-Allow-Origin", "*");
            response.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH");
            response.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Accept, Origin");
            response.setHeader("Access-Control-Max-Age", "86400");
            response.setStatus(200);
            response.setBody("");
            return false; // 停止处理
        }

        // 为其他请求设置 CORS 头
        std::string origin = request.getHeader("origin");
        if (!origin.empty()) {
            response.setHeader("Access-Control-Allow-Origin", origin);
        } else {
            response.setHeader("Access-Control-Allow-Origin", "*");
        }
        response.setHeader("Access-Control-Allow-Credentials", "true");

        return true; // 继续处理
    };

    http_server_->use(cors_middleware, "CorsMiddleware");
    LOG_DEBUG("CORS 中间件设置完成");
}

} // namespace user_service
} // namespace core_services




