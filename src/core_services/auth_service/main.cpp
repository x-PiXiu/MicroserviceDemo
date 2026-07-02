/**
 * @file main.cpp
 * @brief 认证服务启动入口 - 重构版本
 * @details 重构后的认证服务启动程序，支持用户服务客户端架构
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

#include "auth_service.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"
#include "common/service_registry/service_registry_client.h"
#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <thread>
#include <exception>
#include <unistd.h>

using namespace core_services::auth_service;

// 全局服务实例
std::unique_ptr<AuthService> g_auth_service = nullptr;

// 服务注册客户端
std::shared_ptr<common::service_registry::ServiceRegistryClient> g_registry_client = nullptr;

// 信号处理标志
std::atomic<bool> g_shutdown_requested{false};

/**
 * @brief 信号处理函数 - 仅设置标志位，由主循环执行实际关闭
 * 注意：信号处理函数中只能调用 async-signal-safe 的函数，
 * 绝不能调用 mutex 操作、线程 join、内存分配、std::cout 等。
 * @param sig 信号编号
 */
void signalHandler(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            g_shutdown_requested = true;
            break;
        case SIGUSR1:
            // 配置重载标志（如果需要）
            break;
        default:
            break;
    }
    static const char msg[] = "\n[Signal] Shutdown requested\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

/**
 * @brief 设置信号处理器
 */
void setupSignalHandlers() {
    signal(SIGINT, signalHandler);   // Ctrl+C
    signal(SIGTERM, signalHandler);  // 终止信号
    signal(SIGUSR1, signalHandler);  // 用户自定义信号1 (重载配置)
    signal(SIGPIPE, SIG_IGN);        // 忽略管道信号
}

/**
 * @brief 打印版本信息
 */
void printVersionInfo() {
    std::cout << "=======================================================" << std::endl;
    std::cout << "    游戏微服务认证系统 - 重构版本" << std::endl;
    std::cout << "    Game Microservices Authentication Service" << std::endl;
    std::cout << "=======================================================" << std::endl;
    std::cout << "版本: 2.0.0 (Refactored)" << std::endl;
    std::cout << "架构: 用户服务客户端架构" << std::endl;
    std::cout << "构建日期: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "特性: JWT认证, 会话管理, 用户服务集成" << std::endl;
    std::cout << "=======================================================" << std::endl;
}

/**
 * @brief 打印帮助信息
 */
void printHelpInfo() {
    std::cout << "用法: auth_service [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -h, --help              显示此帮助信息" << std::endl;
    std::cout << "  -v, --version           显示版本信息" << std::endl;
    std::cout << "  -c, --config <file>     指定配置文件路径" << std::endl;
    std::cout << "  --validate-config       验证配置文件有效性" << std::endl;
    std::cout << "  --test-db               测试数据库连接" << std::endl;
    std::cout << "  --test-user-service     测试用户服务连接" << std::endl;
    std::cout << "  --daemon                以守护进程模式运行" << std::endl;
    std::cout << "  --debug                 启用调试模式" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  auth_service                                     # 使用默认配置启动" << std::endl;
    std::cout << "  auth_service -c config/auth_service_config.yml  # 使用指定配置启动" << std::endl;
    std::cout << "  auth_service --validate-config                  # 验证配置文件" << std::endl;
    std::cout << "  auth_service --test-db                          # 测试数据库连接" << std::endl;
}

/**
 * @brief 验证配置文件
 * @param config_file 配置文件路径
 * @return 验证成功返回true
 */
bool validateConfig(const std::string& config_file = "") {
    try {
        LOG_INFO("开始验证认证服务配置...");
        
        // 加载配置文件
        if (!config_file.empty()) {
            auto& config_manager = common::config::ConfigManager::getInstance();
            if (!config_manager.loadFromFile(config_file)) {
                LOG_ERROR("配置文件加载失败: " + config_file);
                return false;
            }
        }
        
        // 验证配置
        auto config = AuthService::Config::fromConfigManager();
        if (!config.validate()) {
            LOG_ERROR("认证服务配置验证失败");
            std::cout << "❌ 认证服务配置验证失败，请检查配置文件" << std::endl;
            return false;
        }
        
        LOG_INFO("配置验证通过");
        std::cout << "✅ 配置验证通过" << std::endl;

        // 打印关键配置信息
        std::cout << "📋 关键配置信息:" << std::endl;
        std::cout << "  • 服务名称: " << config.service_name << std::endl;
        std::cout << "  • 服务版本: " << config.service_version << std::endl;
        std::cout << "  • 监听地址: " << config.network_config.bind_address << ":" << config.network_config.listen_port << std::endl;
        std::cout << "  • 工作线程: " << config.network_config.worker_threads << std::endl;
        std::cout << "  • 认证数据库: " << config.database_config.mysql_database << std::endl;
        std::cout << "  • 用户服务: " << config.user_service_base_url << std::endl;

        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("配置验证异常: " + std::string(e.what()));
        std::cout << "❌ 配置验证异常: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 测试数据库连接
 * @return 连接成功返回true
 */
bool testDatabaseConnection() {
    try {
        LOG_INFO("测试认证专用数据库连接...");
        std::cout << "🔍 测试认证专用数据库连接..." << std::endl;
        
        auto config = AuthService::Config::fromConfigManager();

        // 创建数据库连接池配置
        common::config::DatabaseConfig mysql_config;
        mysql_config.mysql_host = config.database_config.mysql_host;
        mysql_config.mysql_port = config.database_config.mysql_port;
        mysql_config.mysql_user = config.database_config.mysql_user;
        mysql_config.mysql_password = config.database_config.mysql_password;
        mysql_config.mysql_database = config.database_config.mysql_database;
        mysql_config.mysql_pool_size = 1;  // 测试时只需要一个连接
        mysql_config.mysql_max_pool_size = 1;
        mysql_config.mysql_min_pool_size = 1;
        
        // 创建连接池
        auto mysql_pool = std::make_shared<common::database::MySQLPool>(mysql_config);
        mysql_pool->start();
        if (!mysql_pool->isRunning()) {
            std::cout << "❌ MySQL连接池初始化失败" << std::endl;
            return false;
        }
        
        // 测试Redis连接
        common::config::RedisConfig redis_config;
        redis_config.redis_host = config.redis_config.redis_host;
        redis_config.redis_port = config.redis_config.redis_port;
        redis_config.redis_password = config.redis_config.redis_password;
        redis_config.redis_database = config.redis_config.redis_database;
        redis_config.redis_pool_size = 1;
        redis_config.redis_max_pool_size = 1;
        redis_config.redis_min_pool_size = 1;
        // 关键修复：设置极长的健康检查间隔，避免健康检查线程在临时测试期间运行
        redis_config.redis_pool_validation_interval_ms = 3600000;  // 1小时

        auto redis_pool = std::make_shared<common::database::RedisPool>(redis_config);
        redis_pool->start();
        if (!redis_pool->isRunning()) {
            std::cout << "❌ Redis连接池初始化失败" << std::endl;
            return false;
        }
        
        std::cout << "✅ 数据库连接测试通过" << std::endl;
        std::cout << "  • MySQL: " << config.database_config.mysql_host << ":" << config.database_config.mysql_port << "/" << config.database_config.mysql_database << std::endl;
        std::cout << "  • Redis: " << config.redis_config.redis_host << ":" << config.redis_config.redis_port << "/" << config.redis_config.redis_database << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库连接测试异常: " + std::string(e.what()));
        std::cout << "❌ 数据库连接测试异常: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 测试用户服务连接
 * @return 连接成功返回true
 */
bool testUserServiceConnection() {
    try {
        LOG_INFO("测试用户服务连接...");
        std::cout << "🔍 测试用户服务连接..." << std::endl;
        
        auto config = AuthService::Config::fromConfigManager();
        
        // 创建用户服务客户端
        UserServiceClientConfig client_config;
        client_config.base_url = config.user_service_base_url;
        client_config.timeout_seconds = config.user_service_timeout_seconds;
        client_config.retry_count = 1;  // 测试时只重试一次
        
        auto user_service_client = std::make_unique<UserServiceClient>(client_config);
        
        // 执行健康检查
        bool is_healthy = user_service_client->isHealthy();
        
        if (is_healthy) {
            std::cout << "✅ 用户服务连接测试通过" << std::endl;
            std::cout << "  • 服务地址: " << config.user_service_base_url << std::endl;
            std::cout << "  • 超时设置: " << config.user_service_timeout_seconds << "s" << std::endl;
        } else {
            std::cout << "❌ 用户服务连接失败" << std::endl;
            std::cout << "  • 请确保用户服务已启动: " << config.user_service_base_url << std::endl;
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("用户服务连接测试异常: " + std::string(e.what()));
        std::cout << "❌ 用户服务连接测试异常: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 执行启动前检查
 * @return 检查通过返回true
 */
bool performPreStartupChecks() {
    std::cout << "🔍 执行启动前检查..." << std::endl;
    
    bool all_checks_passed = true;
    
    // 1. 验证配置
    if (!validateConfig()) {
        all_checks_passed = false;
    }
    
    // 2. 测试数据库连接
    if (!testDatabaseConnection()) {
        all_checks_passed = false;
    }
    
    // 3. 测试用户服务连接
    if (!testUserServiceConnection()) {
        std::cout << "⚠️  用户服务连接失败，但服务将继续启动" << std::endl;
        // 用户服务连接失败不阻止启动，允许容错启动
    }
    
    if (all_checks_passed) {
        std::cout << "✅ 启动前检查全部通过" << std::endl;
    } else {
        std::cout << "❌ 启动前检查失败" << std::endl;
    }
    
    return all_checks_passed;
}

/**
 * @brief 创建认证服务实例
 * @return 服务实例
 */
std::unique_ptr<AuthService> createAuthService() {
    try {
        LOG_INFO("=== 创建认证服务实例 (重构版) ===");

        // 加载配置
        auto config = AuthService::Config::fromConfigManager();

        // 创建服务实例
        auto service = std::make_unique<AuthService>(config);

        // 初始化服务
        if (!service->initialize()) {
            LOG_ERROR("认证服务初始化失败");
            return nullptr;
        }

        LOG_INFO("=== 认证服务创建成功 (重构版) ===");
        return service;

    } catch (const std::exception& e) {
        LOG_ERROR("创建认证服务异常: " + std::string(e.what()));
        return nullptr;
    }
}

/**
 * @brief 注册服务到服务注册中心
 * @param config 服务配置
 * @return 注册成功返回true
 */
bool registerToServiceRegistry() {
    try {
        auto& cm = common::config::ConfigManager::getInstance();

        // 检查是否启用服务注册
        bool registry_enabled = cm.get<bool>("service_registry.enabled", false);
        if (!registry_enabled) {
            LOG_INFO("服务注册已禁用，跳过注册");
            return true;
        }

        // 创建服务注册客户端
        common::service_registry::ServiceRegistryClientConfig registry_config;
        registry_config.registry_host = cm.get<std::string>("service_registry.host", "127.0.0.1");
        registry_config.registry_port = cm.get<int>("service_registry.port", 8090);
        registry_config.heartbeat_interval_s = cm.get<int>("service_registry.heartbeat_interval_s", 30);
        registry_config.auto_heartbeat = cm.get<bool>("service_registry.auto_heartbeat", true);

        g_registry_client = std::make_shared<common::service_registry::ServiceRegistryClient>(registry_config);

        // 构建服务实例信息
        common::service_registry::ServiceInstance instance;
        instance.service_name = "auth_service";
        instance.service_version = "2.0.0";
        instance.host = cm.get<std::string>("network.host", "0.0.0.0");
        instance.port = cm.get<int>("network.port", 8083);
        instance.health_check_endpoint = "/api/v1/auth/health";

        // 设置 API 端点列表
        instance.endpoints = {
            "/api/v1/auth/login",
            "/api/v1/auth/logout",
            "/api/v1/auth/register",
            "/api/v1/auth/validate",
            "/api/v1/auth/refresh",
            "/api/v1/auth/health"
        };

        // 设置元数据
        instance.metadata["service_type"] = cm.get<std::string>("service_registry.metadata.service_type", "core");
        instance.metadata["region"] = cm.get<std::string>("service_registry.metadata.region", "cn-east");

        // 注册服务
        if (!g_registry_client->registerService(instance)) {
            LOG_WARNING("服务注册失败，但服务将继续运行");
            return false;
        }

        // 启动自动心跳
        if (registry_config.auto_heartbeat) {
            g_registry_client->startHeartbeat(instance);
        }

        LOG_INFO("服务已注册到服务注册中心: " + instance.service_name + " at " + instance.instanceId());
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("服务注册异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 从服务注册中心注销
 */
void deregisterFromServiceRegistry() {
    if (g_registry_client) {
        try {
            g_registry_client->stopHeartbeat();
            LOG_INFO("已停止心跳");
        } catch (const std::exception& e) {
            LOG_ERROR("停止心跳异常: " + std::string(e.what()));
        }
        g_registry_client.reset();
    }
}

/**
 * @brief 等待服务运行
 * @param service 服务实例
 */
void waitForService(AuthService* service) {
    LOG_INFO("认证服务启动完成，等待请求...");
    std::cout << "🚀 认证服务启动完成，等待请求..." << std::endl;
    std::cout << "💡 按 Ctrl+C 优雅停止服务" << std::endl;
    
    // 定期输出状态信息
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::minutes(5);  // 每5分钟输出一次统计
    
    while (!g_shutdown_requested && service->isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 定期输出统计信息
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            try {
                auto stats = service->getStatistics();
                LOG_INFO("服务统计: " + stats.dump());
                last_stats_time = now;
            } catch (const std::exception& e) {
                LOG_WARNING("获取服务统计失败: " + std::string(e.what()));
            }
        }
    }
    
    LOG_INFO("服务运行循环结束");
}

/**
 * @brief 主函数
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 退出码
 */
int main(int argc, char* argv[]) {
    try {
        // 默认配置文件路径
        std::string config_file;
        bool daemon_mode = false;
        (void)daemon_mode; // 避免未使用警告
        bool debug_mode = false;
        
        // 解析命令行参数
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            if (arg == "-h" || arg == "--help") {
                printHelpInfo();
                return 0;
            } else if (arg == "-v" || arg == "--version") {
                printVersionInfo();
                return 0;
            } else if (arg == "-c" || arg == "--config") {
                if (i + 1 < argc) {
                    config_file = argv[++i];
                } else {
                    std::cerr << "错误: --config 需要指定配置文件路径" << std::endl;
                    return 1;
                }
            } else if (arg == "--validate-config") {
                return validateConfig(config_file) ? 0 : 1;
            } else if (arg == "--test-db") {
                // 先加载配置
                auto& config_manager = common::config::ConfigManager::getInstance();
                config_manager.loadFromFile(config_file);
                return testDatabaseConnection() ? 0 : 1;
            } else if (arg == "--test-user-service") {
                // 先加载配置
                auto& config_manager = common::config::ConfigManager::getInstance();
                config_manager.loadFromFile(config_file);
                return testUserServiceConnection() ? 0 : 1;
            } else if (arg == "--daemon") {
                daemon_mode = true;
            } else if (arg == "--debug") {
                debug_mode = true;
            } else {
                std::cerr << "错误: 未知参数: " << arg << std::endl;
                printHelpInfo();
                return 1;
            }
        }
        
        // 打印启动信息
        printVersionInfo();

        // 智能查找配置文件
        if (config_file.empty()) {
            config_file = common::config::ConfigManager::findConfigFile(
                "auth_service_config.yml", "AUTH_SERVICE_CONFIG");
        }

        LOG_INFO("认证服务启动 (重构版)");
        LOG_INFO("配置文件: " + config_file);
        
        // 加载配置文件
        auto& config_manager = common::config::ConfigManager::getInstance();
        if (!config_manager.loadFromFile(config_file)) {
            LOG_ERROR("配置文件加载失败: " + config_file);
            std::cerr << "❌ 配置文件加载失败: " << config_file << std::endl;
            return 1;
        }

        // 2. 初始化日志系统
        common::logger::Logger::getInstance().initializeFromConfig();
        
        // 设置信号处理器
        setupSignalHandlers();
        
        // 执行启动前检查
        if (!performPreStartupChecks()) {
            LOG_ERROR("启动前检查失败");
            std::cerr << "❌ 启动前检查失败，请解决问题后重新启动" << std::endl;
            return 1;
        }
        
        // 创建认证服务
        g_auth_service = createAuthService();
        if (!g_auth_service) {
            LOG_ERROR("认证服务创建失败");
            std::cerr << "❌ 认证服务创建失败" << std::endl;
            return 1;
        }
        
        // 启动服务
        if (!g_auth_service->start()) {
            LOG_ERROR("认证服务启动失败");
            std::cerr << "❌ 认证服务启动失败" << std::endl;
            return 1;
        }

        // 注册到服务注册中心
        registerToServiceRegistry();

        // 等待服务运行
        waitForService(g_auth_service.get());

        // 从服务注册中心注销
        deregisterFromServiceRegistry();

        // 清理资源
        LOG_INFO("开始清理认证服务资源...");
        g_auth_service.reset();
        
        LOG_INFO("认证服务已退出");
        std::cout << "👋 认证服务已安全退出" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("认证服务主函数异常: " + std::string(e.what()));
        std::cerr << "❌ 认证服务异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        LOG_ERROR("认证服务主函数未知异常");
        std::cerr << "❌ 认证服务发生未知异常" << std::endl;
        return 1;
    }
}
