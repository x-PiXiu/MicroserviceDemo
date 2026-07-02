//
// Created by Microservice Team
// 游戏数据服务入口点 - 简洁稳定架构
//

#include "include/game_service.h"
#include <common/logger/logger.h>
#include <common/service_registry/service_registry_client.h>
#include <common/auth/jwt_validator.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <csignal>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <thread>

using namespace core_services::game_service;

// 全局服务实例指针（用于信号处理）
std::shared_ptr<GameService> g_service = nullptr;

// 服务注册客户端
std::shared_ptr<common::service_registry::ServiceRegistryClient> g_registry_client = nullptr;

// 信号处理标志
std::atomic<bool> g_shutdown_requested{false};

/**
 * 信号处理器 - 仅设置标志位，由主循环执行实际关闭
 * 注意：信号处理函数中只能调用 async-signal-safe 的函数，
 * 绝不能调用 mutex 操作、线程 join、内存分配等。
 * @param sig 信号编号
 */
void signalHandler(int sig) {
    // 只设置原子标志位（async-signal-safe）
    g_shutdown_requested = true;

    // write() 是 async-signal-safe 的，可以安全使用
    // 使用固定长度避免调用 strlen（不是 async-signal-safe）
    static const char msg[] = "\n[Signal] Shutdown requested\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)sig;
}

/**
 * 设置信号处理器
 */
void setupSignalHandlers() {
    // 设置优雅关闭的信号处理器
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGQUIT, signalHandler);

    // 忽略 SIGPIPE（避免网络连接断开时程序崩溃）
    std::signal(SIGPIPE, SIG_IGN);
}

/**
 * 等待服务停止 - 适用于 Docker 容器环境
 * 使用循环等待而非 std::cin.get()，因为容器中 stdin 通常不可用
 */
void waitForService(GameService* service) {
    LOG_INFO("游戏数据服务启动完成，等待请求...");
    std::cout << "🚀 游戏数据服务启动完成，等待请求..." << std::endl;
    std::cout << "💡 按 Ctrl+C 或发送 SIGTERM 优雅停止服务" << std::endl;

    // 定期输出状态信息
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::minutes(5);  // 每5分钟输出一次统计

    while (!g_shutdown_requested && service->isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 定期输出心跳日志
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            LOG_INFO("游戏数据服务运行中...");
            last_stats_time = now;
        }
    }

    LOG_INFO("收到关闭信号，开始优雅退出...");
    std::cout << "\n收到关闭信号，开始优雅退出..." << std::endl;
}

/**
 * 全局异常处理器
 */
void globalExceptionHandler() {
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::system_error& e) {
        // 🔧 全局异常处理器中不使用LOG，避免过早初始化Logger
        std::cerr << "❌ 系统错误: " << e.what() << " (错误码: " << e.code().value() << ")" << std::endl;
        // 只有在GameService已创建时才使用LOG
        if (g_service) {
            LOG_ERROR("❌ 系统错误异常: " + std::string(e.what()) + " (错误码: " + std::to_string(e.code().value()) + ")");
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "❌ 运行时错误: " << e.what() << std::endl;
        if (g_service) {
            LOG_ERROR("❌ 运行时异常: " + std::string(e.what()));
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ 标准异常: " << e.what() << std::endl;
        if (g_service) {
            LOG_ERROR("❌ 标准异常: " + std::string(e.what()));
        }
    } catch (...) {
        std::cerr << "❌ 未知异常" << std::endl;
        if (g_service) {
            LOG_ERROR("❌ 未知异常");
        }
    }
    
    // 清理资源
    if (g_service) {
        try {
            g_service->shutdown();
        } catch (...) {
            // 忽略关闭时的异常
        }
        g_service.reset();
    }
    
    std::cerr << "❌ 程序异常终止" << std::endl;
    std::abort(); // 使用 abort 确保程序终止
}

/**
 * 初始化日志系统
 * @param config_file 配置文件路径
 * @return 是否成功
 */
bool initializeLogger(const std::string& /* config_file */) {
    try {
        // 使用现有的Logger初始化方法
        common::logger::Logger::getInstance().initializeFromConfig();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return false;
    }
}

/**
 * 验证服务配置
 * @param config 服务配置
 * @return 验证结果
 */
bool validateServiceConfig(const GameServiceConfig& config) {
        LOG_INFO("开始验证游戏数据服务配置...");
        
    if (!config.validate()) {
        LOG_ERROR("配置验证失败");
            return false;
        }
        
        LOG_INFO("配置验证通过");
        
        // 打印关键配置信息
    std::cout << "✅ 配置验证通过" << std::endl;
        std::cout << "📋 关键配置信息:" << std::endl;
        std::cout << "  • 服务名称: " << config.service_name << std::endl;
        std::cout << "  • 服务版本: " << config.service_version << std::endl;
    std::cout << "  • 监听地址: " << config.host << ":" << config.port << std::endl;
    std::cout << "  • 工作线程: " << config.worker_threads << std::endl;
    std::cout << "  • 数据库: " << config.database_config.mysql_database << std::endl;
    std::cout << "  • Redis数据库: " << config.redis_config.redis_database << std::endl;
        
        return true;
}

/**
 * 测试数据库连接
 * @param config 服务配置  
 * @return 测试结果
 */
bool testDatabaseConnections(const GameServiceConfig& config) {
    std::cout << "🔍 测试游戏数据服务数据库连接..." << std::endl;
    LOG_INFO("测试游戏数据服务数据库连接...");

    try {
        // 创建临时的数据库连接池进行测试（🔧 禁用异步操作）
        common::config::DatabaseConfig mysql_config;
        mysql_config.mysql_host = config.database_config.mysql_host;
        mysql_config.mysql_port = config.database_config.mysql_port;
        mysql_config.mysql_user = config.database_config.mysql_user;
        mysql_config.mysql_password = config.database_config.mysql_password;
        mysql_config.mysql_database = config.database_config.mysql_database;
        mysql_config.mysql_min_pool_size = 1;
        mysql_config.mysql_pool_size = 1;
        mysql_config.mysql_max_pool_size = 1;
        mysql_config.mysql_connect_timeout_ms = 10;
        mysql_config.enable_async_operations = false;
        
        auto mysql_pool = std::make_shared<common::database::MySQLPool>(mysql_config);
        try {
        mysql_pool->start();
            LOG_INFO("MySQL连接池启动成功");
        } catch (const std::exception& e) {
            LOG_ERROR("MySQL连接测试失败: " + std::string(e.what()));
            return false;
        }
        
        common::config::RedisConfig redis_config;
        redis_config.redis_host = config.redis_config.redis_host;
        redis_config.redis_port = config.redis_config.redis_port;
        redis_config.redis_password = config.redis_config.redis_password;
        redis_config.redis_database = config.redis_config.redis_database;
        redis_config.redis_pool_size = 1;
        redis_config.redis_max_pool_size = 1;
        redis_config.redis_min_pool_size = 1;
        redis_config.enable_async_operations = false;
        // 关键修复：设置极长的健康检查间隔，避免健康检查线程在临时测试期间运行
        // 防止线程 detach 后仍持有 robust mutex 导致崩溃
        redis_config.redis_pool_validation_interval_ms = 3600000;  // 1小时

        auto redis_pool = std::make_shared<common::database::RedisPool>(redis_config);
        try {
        redis_pool->start();
            LOG_INFO("Redis连接池启动成功");
        } catch (const std::exception& e) {
            LOG_ERROR("Redis连接测试失败: " + std::string(e.what()));
            mysql_pool->stop();
            return false;
        }

        // 关键修复：确保健康检查线程完全退出后再销毁连接池
        // 给予足够的等待时间（5秒），避免线程 detach 导致的 robust mutex 问题
        mysql_pool->stop();

        // 先等待 MySQL 完全停止
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 停止 Redis，此时健康检查线程仍在 sleep(1小时)，会很快响应 running_=false
        redis_pool->stop();

        // 额外等待确保线程完全退出
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        LOG_INFO("✅ 数据库连接测试通过");
        // std::cout << "✅ 数据库连接测试通过" << std::endl;
        // std::cout << "  • MySQL: " << config.database_host << ":" << config.database_port << "/" << config.database_name << std::endl;
        // std::cout << "  • Redis: " << config.redis_host << ":" << config.redis_port << "/" << config.redis_database << std::endl;
        //
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库连接测试异常: " + std::string(e.what()));
        std::cerr << "❌ 数据库连接测试失败: " << e.what() << std::endl;
        return false;
    }
}

/**
 * 打印服务横幅
 */
void printServiceBanner() {
    std::cout << "\n=======================================================" << std::endl;
    std::cout << "    游戏微服务数据管理系统" << std::endl;
    std::cout << "    Game Microservices Data Management Service" << std::endl;
    std::cout << "=======================================================" << std::endl;
    std::cout << "版本: 1.0.0" << std::endl;
    std::cout << "数据库: game_service_db" << std::endl;
    std::cout << "端口: 8084" << std::endl;
    std::cout << "构建日期: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "功能: 游戏档案, 成就系统, 库存管理, 货币系统, 排行榜" << std::endl;
    std::cout << "=======================================================" << std::endl;
}

/**
 * 注册服务到服务注册中心
 */
bool registerToServiceRegistry() {
    try {
        auto& cm = common::config::ConfigManager::getInstance();

        bool registry_enabled = cm.get<bool>("service_registry.enabled", false);
        if (!registry_enabled) {
            LOG_INFO("服务注册已禁用，跳过注册");
            return true;
        }

        common::service_registry::ServiceRegistryClientConfig registry_config;
        registry_config.registry_host = cm.get<std::string>("service_registry.host", "127.0.0.1");
        registry_config.registry_port = cm.get<int>("service_registry.port", 8090);
        registry_config.heartbeat_interval_s = cm.get<int>("service_registry.heartbeat_interval_s", 30);
        registry_config.auto_heartbeat = cm.get<bool>("service_registry.auto_heartbeat", true);

        g_registry_client = std::make_shared<common::service_registry::ServiceRegistryClient>(registry_config);

        common::service_registry::ServiceInstance instance;
        instance.service_name = "game_data_service";
        instance.service_version = "1.0.0";
        instance.host = cm.get<std::string>("network.host", "0.0.0.0");
        instance.port = cm.get<int>("network.port", 8084);
        instance.health_check_endpoint = "/api/v1/gamedata/health";

        // 设置 API 端点列表
        instance.endpoints = {
            "/api/v1/gamedata/profile",
            "/api/v1/gamedata/achievements",
            "/api/v1/gamedata/inventory",
            "/api/v1/gamedata/currency",
            "/api/v1/gamedata/leaderboard",
            "/api/v1/gamedata/stats",
            "/api/v1/gamedata/health"
        };

        instance.metadata["service_type"] = cm.get<std::string>("service_registry.metadata.service_type", "core");
        instance.metadata["region"] = cm.get<std::string>("service_registry.metadata.region", "cn-east");

        if (!g_registry_client->registerService(instance)) {
            LOG_WARNING("服务注册失败，但服务将继续运行");
            return false;
        }

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
 * 从服务注册中心注销
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
 * 主函数
 */
int main(int argc, char* argv[]) {
    // 打印服务横幅
    printServiceBanner();

    // 设置全局异常处理器
    std::set_terminate(globalExceptionHandler);

    // 设置信号处理器
    setupSignalHandlers();

    try {
        // 确定配置文件路径
        std::string config_file;

        // 解析 -c/--config 选项（最高优先级）
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
                config_file = argv[++i];
            } else if (arg.find(".yml") != std::string::npos ||
                       arg.find(".yaml") != std::string::npos) {
                config_file = arg;
            }
        }

        // 命令行未指定时，智能查找
        if (config_file.empty()) {
            config_file = common::config::ConfigManager::findConfigFile(
                "game_data_service.yml", "GAME_DATA_SERVICE_CONFIG");
        }

        // 这样Logger就不会在早期创建独立线程池
        std::cout << "[Main] 游戏数据服务启动" << std::endl;
        std::cout << "[Main] 配置文件: " << config_file << std::endl;

        // Logger将在第一次LOG调用时自动初始化，配置从ConfigManager读取
        std::cout << "[Main] Logger将在ConfigManager加载后自动初始化" << std::endl;

        // 加载配置文件到ConfigManager
        auto& config_manager = common::config::ConfigManager::getInstance();
        if (!config_manager.loadFromFile(config_file)) {
            // 🔧 配置加载失败时不使用LOG，因为Logger依赖ConfigManager配置
            std::cerr << "❌ 无法加载配置文件: " << config_file << std::endl;
            return 1;
        }
        
        // 从ConfigManager加载服务配置
        auto config = GameServiceConfig::fromConfigManager();

        // 2. 初始化日志系统
        common::logger::Logger::getInstance().initializeFromConfig();

        // 3. 初始化 JWT 验证器
        {
            common::auth::JwtValidatorConfig jwt_config = common::auth::JwtValidatorConfig::fromConfig();
            if (!common::auth::JwtValidator::getInstance().initialize(jwt_config)) {
                LOG_ERROR("JwtValidator 初始化失败");
                std::cerr << "❌ JwtValidator 初始化失败" << std::endl;
                return 1;
            }
            LOG_INFO("JwtValidator 初始化成功");
        }

        // 执行启动前检查
        // std::cout << "🔍 执行启动前检查..." << std::endl;
        LOG_INFO("🔍 执行启动前检查...");

        // 验证配置（使用内建的validate方法）
        try {
            config.validate();
            // std::cout << "[Main] ✅ 配置验证通过" << std::endl;
            LOG_INFO("[Main] ✅ 配置验证通过");
        } catch (const std::exception& e) {
            // std::cerr << "❌ 配置验证失败: " << e.what() << std::endl;
            LOG_INFO("❌ 配置验证失败: " + std::string(e.what()) );
            return 1;
        }
        
        g_service = std::make_shared<GameService>(config);

        LOG_INFO("=== 游戏数据服务实例创建成功 ===");
        LOG_INFO("✅ 配置文件加载成功: " + config_file);
        LOG_INFO("📡 实际监听端口: " + std::to_string(config.port));

        // 测试数据库连接（现在Logger已正确配置）
        if (!testDatabaseConnections(config)) {
            // std::cerr << "❌ 数据库连接测试失败" << std::endl;
            LOG_INFO("❌ 数据库连接测试失败");
            return 1;
        }
        
        std::cout << "✅ 启动前检查全部通过" << std::endl;

        // 初始化服务
        if (!g_service->initialize()) {
            LOG_ERROR("服务初始化失败");
            // std::cerr << "❌ 服务初始化失败" << std::endl;
            return 1;
        }

        LOG_INFO("=== 游戏数据服务创建成功 ===");
        // std::cout << "[INFO] === 游戏数据服务创建成功 ===" << std::endl;
        
        // 启动服务
        if (!g_service->start()) {
            LOG_ERROR("服务启动失败");
            // std::cerr << "❌ 服务启动失败" << std::endl;
            return 1;
        }

        // 注册到服务注册中心
        registerToServiceRegistry();

        // 等待服务停止（适用于 Docker 容器环境）
        waitForService(g_service.get());

        // 从服务注册中心注销
        deregisterFromServiceRegistry();

        // 7. 停止网关
        g_service->stop();

        // 清理资源
        LOG_INFO("开始清理用户服务资源...");
        g_service.reset();

        LOG_INFO("用户服务已退出");
        std::cout << "👋 用户服务已安全退出" << std::endl;

        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("程序异常: " + std::string(e.what()));
        std::cerr << "❌ 程序异常: " << e.what() << std::endl;
        
        if (g_service) {
            g_service->shutdown();
            g_service.reset();
        }
        
        return 1;
    } catch (...) {
        LOG_ERROR("未知异常");
        std::cerr << "❌ 未知异常" << std::endl;
        
        if (g_service) {
            g_service->shutdown();
            g_service.reset();
        }
        
        return 1;
    }

    return 0;
}
