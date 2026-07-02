/**
 * @file service_registry.cpp
 * @brief 服务注册中心主服务实现
 * @details 实现服务注册中心的核心协调逻辑
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "service_registry.h"
#include <csignal>
#include <iostream>

namespace core_services {
namespace service_registry {

ServiceRegistry::ServiceRegistry(const ServiceRegistryConfig& config)
    : config_(config) {
}

ServiceRegistry::~ServiceRegistry() {
    stop();
}

bool ServiceRegistry::start() {
    if (running_.load()) {
        LOG_WARNING("Service Registry is already running");
        return true;
    }

    LOG_INFO("Starting Service Registry...");

    // 1. 初始化组件
    if (!initializeComponents()) {
        LOG_ERROR("Failed to initialize components");
        return false;
    }

    // 2. 初始化定时任务
    initializeScheduledTasks();

    // 3. 启动 HTTP 服务器
    try {
        http_server_->start();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start HTTP server: " + std::string(e.what()));
        return false;
    }

    // 4. 启动任务调度器
    if (scheduler_) {
        scheduler_->start();
    }

    running_.store(true);

    LOG_INFO("Service Registry started on port " + std::to_string(config_.http.port));
    LOG_INFO("Service Registry version: " + config_.version);

    return true;
}

void ServiceRegistry::stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO("Stopping Service Registry...");

    running_.store(false);

    // 停止任务调度器
    if (scheduler_) {
        scheduler_->stop();
    }

    // 停止 HTTP 服务器
    if (http_server_) {
        http_server_->stop();
    }

    LOG_INFO("Service Registry stopped");
}

void ServiceRegistry::wait() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool ServiceRegistry::initializeComponents() {
    try {
        // 1. 初始化 Redis 连接池
        common::config::RedisConfig redis_config;
        redis_config.redis_host = config_.redis.host;
        redis_config.redis_port = config_.redis.port;
        redis_config.redis_password = config_.redis.password;  // 修复：传递 Redis 密码
        redis_config.redis_pool_size = config_.redis.pool_size;
        redis_config.redis_max_pool_size = config_.redis.pool_size * 4;
        redis_pool_ = std::make_shared<common::database::RedisPool>(redis_config);

        // 启动 Redis 连接池
        redis_pool_->start();
        if (!redis_pool_->isRunning()) {
            LOG_ERROR("Failed to start Redis pool at " +
                     config_.redis.host + ":" + std::to_string(config_.redis.port));
            return false;
        }

        // 测试连接 - 🔧 RAII修复：使用连接守护确保连接自动归还
        {
            common::database::RedisConnectionGuard conn_guard(*redis_pool_);
            auto conn = conn_guard.get();
            if (!conn) {
                LOG_ERROR("Failed to connect to Redis at " +
                         config_.redis.host + ":" + std::to_string(config_.redis.port));
                return false;
            }
        }

        LOG_INFO("Connected to Redis: " + config_.redis.host + ":" +
                std::to_string(config_.redis.port));

        // 2. 初始化存储层
        storage_ = std::make_shared<RedisStorage>(redis_pool_, config_.registry);

        // 3. 初始化索引管理器
        index_manager_ = std::make_shared<IndexManager>(redis_pool_);

        // 4. 初始化事件发布器
        if (config_.registry.enable_events) {
            event_publisher_ = std::make_shared<EventPublisher>(redis_pool_);
        }

        // 5. 初始化注册管理器
        registry_manager_ = std::make_shared<RegistryManager>(
            storage_,
            index_manager_,
            redis_pool_,
            config_.registry
        );

        if (event_publisher_) {
            registry_manager_->setEventPublisher(event_publisher_);
        }

        // 6. 初始化健康监控器
        health_monitor_ = std::make_shared<HealthMonitor>(config_.registry);

        // 7. 初始化查询引擎
        query_engine_ = std::make_shared<QueryEngine>(
            storage_,
            index_manager_,
            config_.registry
        );

        // 8. 初始化 HTTP 处理器
        handler_ = std::make_shared<RegistryHandler>(
            registry_manager_,
            health_monitor_,
            query_engine_
        );

        // 9. 初始化 HTTP 服务器 (using simplified constructor)
        http_server_ = std::make_shared<common::http::HttpServer>(
            config_.http.host,
            config_.http.port
        );

        // 注册路由
        handler_->registerRoutes(http_server_.get());

        // 设置 CORS 中间件
        setupCorsMiddleware();

        // 10. 初始化任务调度器
        scheduler_ = std::make_shared<common::scheduler::TaskScheduler>();

        LOG_INFO("All components initialized successfully");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize components: " + std::string(e.what()));
        return false;
    }
}

void ServiceRegistry::initializeScheduledTasks() {
    // 1. 定时清理过期服务
    scheduler_->scheduleEvery(
        std::chrono::seconds(config_.registry.cleanup_interval_s),
        [this]() {
            cleanupTask();
        }
    );

    // 2. 定时更新统计信息
    scheduler_->scheduleEvery(
        std::chrono::seconds(config_.registry.stats_update_interval_s),
        [this]() {
            statsUpdateTask();
        }
    );

    LOG_INFO("Scheduled tasks initialized");
}

void ServiceRegistry::cleanupTask() {
    try {
        int cleaned = registry_manager_->cleanupExpiredServices();
        if (cleaned > 0) {
            LOG_DEBUG("Cleaned up " + std::to_string(cleaned) + " expired services");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Cleanup task failed: " + std::string(e.what()));
    }
}

void ServiceRegistry::statsUpdateTask() {
    try {
        auto stats = registry_manager_->getStats();
        LOG_DEBUG("Stats updated: " + std::to_string(stats["total_instances"].get<size_t>()) +
                 " instances, " + std::to_string(stats["healthy_instances"].get<size_t>()) +
                 " healthy");
    } catch (const std::exception& e) {
        LOG_ERROR("Stats update task failed: " + std::string(e.what()));
    }
}

void ServiceRegistry::setupCorsMiddleware() {
    LOG_DEBUG("设置 CORS 中间件...");

    // 创建 CORS 中间件函数
    auto cors_middleware = [](const common::http::HttpRequest& request, common::http::HttpResponse& response) {
        // 处理 OPTIONS 预检请求
        if (request.getMethod() == common::http::HttpMethod::OPTIONS) {
            // 动态获取 Origin 并回显
            std::string origin = request.getHeader("origin");
            if (!origin.empty()) {
                response.setHeader("Access-Control-Allow-Origin", origin);
            } else {
                response.setHeader("Access-Control-Allow-Origin", "*");
            }
            response.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH");
            response.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Accept, Origin");
            response.setHeader("Access-Control-Max-Age", "86400");  // 24小时缓存
            response.setHeader("Access-Control-Allow-Credentials", "true");
            response.setStatus(200);
            response.setBody("");
            return false; // 中断处理，直接返回响应
        }

        // 为其他请求设置 CORS 头 - 动态 Origin 处理
        std::string origin = request.getHeader("origin");
        if (!origin.empty()) {
            response.setHeader("Access-Control-Allow-Origin", origin);
        } else {
            response.setHeader("Access-Control-Allow-Origin", "*");
        }
        response.setHeader("Access-Control-Allow-Credentials", "true");
        response.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH");
        response.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Accept, Origin");

        return true; // 继续处理
    };

    http_server_->use(cors_middleware, "CorsMiddleware");
    LOG_DEBUG("CORS 中间件设置完成");
}

} // namespace service_registry
} // namespace core_services
