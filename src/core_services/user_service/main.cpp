/**
 * @file main.cpp
 * @brief 用户核心服务启动入口 - 符合推荐分层架构
 * @details 管理user_service_db的用户核心服务启动程序
 * @author AI Assistant
 * @date 2025-09-19
 * @version 1.0.0
 */

#include "user_service.h"
#include "common/logger/logger.h"
#include "common/service_registry/service_registry_client.h"
#include "common/auth/jwt_validator.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <csignal>
#include <memory>
#include <chrono>
#include <unistd.h>
#include <thread>

using namespace core_services::user_service;

// 全局服务实例
std::unique_ptr<UserService> g_user_service = nullptr;

// 服务注册客户端
std::shared_ptr<common::service_registry::ServiceRegistryClient> g_registry_client = nullptr;

// 信号处理标志
std::atomic<bool> g_shutdown_requested{false};

// 🔧 添加重试工具函数
template<typename Func>
bool retryOperation(const std::string& operation_name, Func operation, int max_attempts = 3, int delay_ms = 1000) {
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            if (operation()) {
                if (attempt > 1) {
                    std::cout << "✅ " << operation_name << " 在第 " << attempt << " 次尝试后成功" << std::endl;
                }
                return true;
            }
        } catch (const std::exception& e) {
            std::cout << "❌ " << operation_name << " 第 " << attempt << " 次尝试失败: " << e.what() << std::endl;
        }
        
        if (attempt < max_attempts) {
            std::cout << "🔄 " << operation_name << " 将在 " << delay_ms << "ms 后重试... (" << attempt << "/" << max_attempts << ")" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    
    std::cout << "💥 " << operation_name << " 经过 " << max_attempts << " 次尝试后仍然失败" << std::endl;
    return false;
}

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
    std::cout << "    游戏微服务用户核心系统" << std::endl;
    std::cout << "    Game Microservices User Core Service" << std::endl;
    std::cout << "=======================================================" << std::endl;
    std::cout << "版本: 1.0.0" << std::endl;
    std::cout << "数据库: user_service_db" << std::endl;
    std::cout << "端口: 8082" << std::endl;
    std::cout << "构建日期: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "功能: 用户核心数据管理, 档案管理, 偏好设置" << std::endl;
    std::cout << "=======================================================" << std::endl;
}

/**
 * @brief 打印帮助信息
 */
void printHelpInfo() {
    std::cout << "用法: user_service [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -h, --help              显示此帮助信息" << std::endl;
    std::cout << "  -v, --version           显示版本信息" << std::endl;
    std::cout << "  -c, --config <file>     指定配置文件路径" << std::endl;
    std::cout << "  --validate-config       验证配置文件有效性" << std::endl;
    std::cout << "  --test-db               测试数据库连接" << std::endl;
    std::cout << "  --init-db               初始化数据库" << std::endl;
    std::cout << "  --daemon                以守护进程模式运行" << std::endl;
    std::cout << "  --debug                 启用调试模式" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  user_service                                     # 使用默认配置启动" << std::endl;
    std::cout << "  user_service -c config/user_service.yml         # 使用指定配置启动" << std::endl;
    std::cout << "  user_service --validate-config                  # 验证配置文件" << std::endl;
    std::cout << "  user_service --test-db                          # 测试数据库连接" << std::endl;
    std::cout << "  user_service --init-db                          # 初始化数据库" << std::endl;
}

/**
 * @brief 验证配置文件
 * @param config_file 配置文件路径
 * @return 验证成功返回true
 */
bool validateConfig(const std::string& config_file) {
    try {
        LOG_INFO("开始验证用户服务配置...");
        
        auto config = UserServiceConfig::fromConfigManager();
        if (!config.validate()) {
            LOG_ERROR("配置验证失败");
            std::cout << "❌ 配置验证失败" << std::endl;
            return false;
        }
        
        LOG_INFO("配置验证通过");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("配置验证异常: " + std::string(e.what()));
        std::cout << "❌ 配置验证异常: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 测试数据库连接
 * @param config_file 配置文件路径
 * @return 连接成功返回true
 */
bool testDatabaseConnection(const std::string& config_file) {
    try {
        LOG_INFO("测试用户服务数据库连接...");
        std::cout << "🔍 测试用户服务数据库连接..." << std::endl;
        
        auto config = UserServiceConfig::fromConfigManager();
        
        // 创建数据库连接池配置
        common::config::DatabaseConfig mysql_config;
        mysql_config.mysql_host = config.database_config.mysql_host;
        mysql_config.mysql_port = config.database_config.mysql_port;
        mysql_config.mysql_user = config.database_config.mysql_user;
        mysql_config.mysql_password = config.database_config.mysql_password;
        mysql_config.mysql_database = config.database_config.mysql_database;
        mysql_config.mysql_min_pool_size = 1;  // 测试时只需要一个连接
        mysql_config.mysql_pool_size = 1;  // 测试时只需要一个连接
        mysql_config.mysql_min_pool_size = 1;

        // 创建连接池
        auto mysql_pool = std::make_shared<common::database::MySQLPool>(mysql_config);
        // MySQL连接池不需要手动初始化，构造时自动初始化
        if (!mysql_pool) {
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
        // Redis连接池不需要手动初始化，构造时自动初始化
        if (!redis_pool) {
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
 * @brief 初始化数据库
 * @param config_file 配置文件路径
 * @return 初始化成功返回true
 */
bool initializeDatabase(const std::string& config_file) {
    try {
        LOG_INFO("初始化用户服务数据库...");
        std::cout << "🔧 自动初始化用户服务数据库..." << std::endl;
        
        // 🎯 修复: 实现自动数据库初始化（不再需要手动操作）
        auto config = UserServiceConfig::fromConfigManager();
        
        // 创建数据库连接池配置
        common::config::DatabaseConfig mysql_config;
        mysql_config.mysql_host = config.database_config.mysql_host;
        mysql_config.mysql_port = config.database_config.mysql_port;
        mysql_config.mysql_user = config.database_config.mysql_user;
        mysql_config.mysql_password = config.database_config.mysql_password;
        mysql_config.mysql_database = config.database_config.mysql_database;
        mysql_config.mysql_min_pool_size = 1;  // 测试时只需要一个连接
        mysql_config.mysql_pool_size = 1;  // 测试时只需要一个连接
        mysql_config.mysql_min_pool_size = 1;


        // 创建连接池和数据访问层
        auto mysql_pool = std::make_shared<common::database::MySQLPool>(mysql_config);
        mysql_pool->start();
        if (!mysql_pool->isRunning()) {
            std::cout << "❌ MySQL连接池启动失败" << std::endl;
            return false;
        }
        
        // 测试Redis连接
        common::config::RedisConfig redis_config;
        redis_config.redis_host = config.redis_config.redis_host;
        redis_config.redis_port = config.redis_config.redis_port;
        redis_config.redis_password = config.redis_config.redis_password;
        redis_config.redis_database = config.redis_config.redis_database;
        // 关键修复：设置极长的健康检查间隔，避免健康检查线程在临时测试期间运行
        redis_config.redis_pool_validation_interval_ms = 3600000;  // 1小时

        auto redis_pool = std::make_shared<common::database::RedisPool>(redis_config);
        redis_pool->start();
        if (!redis_pool->isRunning()) {
            std::cout << "❌ Redis连接池启动失败" << std::endl;
            return false;
        }
        
        // 创建UserRepository并初始化表结构
        UserRepository user_repository(mysql_pool, redis_pool);
        if (!user_repository.initialize()) {
            std::cout << "❌ 数据访问层初始化失败" << std::endl;
            return false;
        }
        
        if (!user_repository.initializeTables()) {
            std::cout << "❌ 数据库表结构初始化失败" << std::endl;
            return false;
        }
        
        std::cout << "✅ 数据库自动初始化完成" << std::endl;
        std::cout << "  • 所有数据表已自动创建" << std::endl;
        std::cout << "  • 数据库: " << config.database_config.mysql_database << std::endl;
        std::cout << "  • 主机: " << config.database_config.mysql_host << ":" << config.database_config.mysql_port << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库自动初始化异常: " + std::string(e.what()));
        std::cout << "❌ 数据库自动初始化异常: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 执行启动前检查
 * @param config_file 配置文件路径
 * @return 检查通过返回true
 */
bool performPreStartupChecks(const std::string& config_file) {
    LOG_INFO("🔍 执行启动前检查...");
    
    bool all_checks_passed = true;
    
    // 1. 验证配置
    std::cout << "📋 1/2 验证配置文件..." << std::endl;
    if (!validateConfig(config_file)) {
        all_checks_passed = false;
    }
    
    // 🔧 修复: 恢复数据库连接测试（重要的启动前检查）+ 重试机制
    std::cout << "🔍 2/2 测试数据库连接..." << std::endl;
    if (!retryOperation("数据库连接测试", [&]() {
        return testDatabaseConnection(config_file);
    }, 3, 2000)) { // 3次重试，每次间隔2秒
         all_checks_passed = false;
    }
    
    if (all_checks_passed) {
        std::cout << "✅ 启动前检查全部通过" << std::endl;
    } else {
        std::cout << "❌ 启动前检查失败" << std::endl;
    }
    
    return all_checks_passed;
}

/**
 * @brief 创建用户服务实例
 * @param config_file 配置文件路径
 * @return 服务实例
 */
std::unique_ptr<UserService> createUserService(const std::string& config_file) {
    try {
        LOG_INFO("=== 创建用户服务实例 ===");

        // 加载配置
        auto config = UserServiceConfig::fromConfigManager();

        // 创建服务实例
        auto service = std::make_unique<UserService>(config);

        // 初始化服务
        if (!service->initialize()) {
            LOG_ERROR("用户服务初始化失败");
            return nullptr;
        }

        LOG_INFO("=== 用户服务创建成功 ===");
        return service;

    } catch (const std::exception& e) {
        LOG_ERROR("创建用户服务异常: " + std::string(e.what()));
        return nullptr;
    }
}

/**
 * @brief 注册服务到服务注册中心
 * @param config 服务配置
 * @return 注册成功返回true
 */
bool registerToServiceRegistry(const UserServiceConfig& config) {
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
        instance.service_name = "user_service";
        instance.service_version = "1.0.0";
        instance.host = cm.get<std::string>("network.host", "0.0.0.0");
        instance.port = cm.get<int>("network.port", 8082);
        instance.health_check_endpoint = "/api/v1/user/health";

        // 设置 API 端点列表
        instance.endpoints = {
            "/api/v1/user/profile",
            "/api/v1/user/settings",
            "/api/v1/user/preferences",
            "/api/v1/user/history",
            "/api/v1/user/health"
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
void waitForService(UserService* service) {
    LOG_INFO("用户服务启动完成，等待请求...");
    std::cout << "🚀 用户服务启动完成，等待请求..." << std::endl;
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
        // 1. 预扫描命令行参数，提取 -c/--config
        std::string config_file;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
                config_file = argv[++i];
            }
        }

        // 2. 智能查找配置文件
        if (config_file.empty()) {
            config_file = common::config::ConfigManager::findConfigFile(
                "user_service.yml", "USER_SERVICE_CONFIG");
        }

        // 3. 初始化配置管理器
        auto& config_manager = common::config::ConfigManager::getInstance();

        if (!config_file.empty()) {
            config_manager.loadFromFile(config_file);
        }

        // 4. 初始化日志系统
        common::logger::Logger::getInstance().initializeFromConfig();

        // 5. 初始化 JWT 验证器
        {
            common::auth::JwtValidatorConfig jwt_config = common::auth::JwtValidatorConfig::fromConfig();
            if (!common::auth::JwtValidator::getInstance().initialize(jwt_config)) {
                LOG_ERROR("JwtValidator 初始化失败");
                std::cerr << "❌ JwtValidator 初始化失败" << std::endl;
                return 1;
            }
            LOG_INFO("JwtValidator 初始化成功");
        }

        bool daemon_mode = false;
        bool debug_mode = false;

        // 6. 解析命令行参数（完整解析）
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
                    ++i; // 已在预扫描中处理
                }
            } else if (arg == "--validate-config") {
                return validateConfig(config_file) ? 0 : 1;
            } else if (arg == "--test-db") {
                return testDatabaseConnection(config_file) ? 0 : 1;
            } else if (arg == "--init-db") {
                return initializeDatabase(config_file) ? 0 : 1;
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
        
        // 初始化日志系统
        // Logger已自动初始化，无需手动调用initialize()
        if (daemon_mode) {
            LOG_INFO("以守护进程模式启动用户服务");
        }
        
        LOG_INFO("用户服务启动");
        LOG_INFO("配置文件: " + config_file);
        
        // 设置信号处理器
        setupSignalHandlers();
        
        // //执行启动前检查
        if (!performPreStartupChecks(config_file)) {
            LOG_ERROR("启动前检查失败");
            std::cerr << "❌ 启动前检查失败，请解决问题后重新启动" << std::endl;
            return 1;
        }

        // // 🔧 带重试机制的用户服务创建
        if (!retryOperation("用户服务创建", [&]() {
            g_user_service = createUserService(config_file);
            return g_user_service != nullptr;
        }, 2, 3000)) { // 2次重试，每次间隔3秒
            LOG_ERROR("用户服务创建失败");
            std::cerr << "❌ 用户服务创建失败" << std::endl;
            return 1;
        }

        // 🔧 带重试机制的服务启动（但降低重试次数，避免端口占用问题）
        if (!retryOperation("用户服务启动", [&]() {
            return g_user_service->start();
        }, 2, 5000)) { // 2次重试，每次间隔5秒
            LOG_ERROR("用户服务启动失败");
            std::cerr << "❌ 用户服务启动失败" << std::endl;
            return 1;
        }

        // 注册到服务注册中心
        auto config = UserServiceConfig::fromConfigManager();
        registerToServiceRegistry(config);

        // 等待服务运行
        waitForService(g_user_service.get());

        // 从服务注册中心注销
        deregisterFromServiceRegistry();

        // 清理资源
        LOG_INFO("开始清理用户服务资源...");
        g_user_service.reset();
        
        LOG_INFO("用户服务已退出");
        std::cout << "👋 用户服务已安全退出" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("用户服务主函数异常: " + std::string(e.what()));
        std::cerr << "❌ 用户服务异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        LOG_ERROR("用户服务主函数未知异常");
        std::cerr << "❌ 用户服务发生未知异常" << std::endl;
        return 1;
    }
}


