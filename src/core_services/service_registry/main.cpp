/**
 * @file main.cpp
 * @brief 服务注册中心入口
 * @details 程序启动入口，使用 ConfigManager 统一管理配置
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "service_registry.h"
#include "registry_config.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"
#include <iostream>
#include <fstream>
#include <csignal>
#include <memory>
#include <unistd.h>

using namespace core_services::service_registry;

// 全局服务实例（用于信号处理）
std::unique_ptr<ServiceRegistry> g_service;

/**
 * @brief 信号处理函数 - 仅设置标志位，由主循环执行实际关闭
 * 注意：信号处理函数中只能调用 async-signal-safe 的函数
 * @param sig 信号编号
 */
void signalHandler(int sig) {
    // requestStop() 只做 atomic store，是 async-signal-safe 的
    if (g_service) {
        g_service->requestStop();
    }
    static const char msg[] = "\n[Signal] Shutdown requested\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)sig;
}

/**
 * @brief 打印使用说明
 */
void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -c, --config <file>   Configuration file path (default: config/service_registry.yml)\n"
              << "  -h, --help            Show this help message\n"
              << "  -v, --version         Show version information\n"
              << std::endl;
}

/**
 * @brief 打印版本信息
 */
void printVersion() {
    std::cout << "Service Registry v" << SERVICE_REGISTRY_VERSION << std::endl;
    std::cout << "A lightweight service registration and discovery center" << std::endl;
}

/**
 * @brief 从配置文件加载配置
 * @details 使用 ConfigManager 统一加载 YAML 配置
 */
std::optional<ServiceRegistryConfig> loadConfig(const std::string& config_path) {
    try {
        // 检查文件是否存在
        std::ifstream file(config_path);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: " + config_path);
            return std::nullopt;
        }
        file.close();

        // 使用 ConfigManager 加载 YAML 配置
        auto& config_manager = common::config::ConfigManager::getInstance();

        if (!config_manager.loadFromYaml(config_path)) {
            LOG_ERROR("Failed to load config from: " + config_path);
            return std::nullopt;
        }

        LOG_INFO("Config loaded successfully from: " + config_path);

        // 从 ConfigManager 构建配置对象
        return ServiceRegistryConfig::fromConfigManager();

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load config file: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 获取默认配置
 */
ServiceRegistryConfig getDefaultConfig() {
    ServiceRegistryConfig config;

    // 使用环境变量或默认值
    const char* redis_host = std::getenv("REDIS_HOST");
    const char* redis_port = std::getenv("REDIS_PORT");
    const char* http_port = std::getenv("HTTP_PORT");

    if (redis_host) config.redis.host = redis_host;
    if (redis_port) config.redis.port = std::stoi(redis_port);
    if (http_port) config.http.port = std::stoi(http_port);

    return config;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }

        if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        }

        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // 初始化日志
    common::logger::Logger::getInstance().initializeFromConfig();

    LOG_INFO("===========================================");
    LOG_INFO("  Service Registry v" + std::string(SERVICE_REGISTRY_VERSION));
    LOG_INFO("===========================================");

    // 加载配置
    if (config_path.empty()) {
        config_path = common::config::ConfigManager::findConfigFile(
            "service_registry.yml", "SERVICE_REGISTRY_CONFIG");
    }
    ServiceRegistryConfig config;
    auto loaded_config = loadConfig(config_path);

    if (loaded_config) {
        config = *loaded_config;
        LOG_INFO("Configuration loaded from: " + config_path);
    } else {
        config = getDefaultConfig();
        LOG_INFO("Using default configuration");
    }

    LOG_INFO("Configuration:");
    LOG_INFO("  Redis: " + config.redis.host + ":" + std::to_string(config.redis.port));
    LOG_INFO("  HTTP: " + config.http.host + ":" + std::to_string(config.http.port));
    LOG_INFO("  Service TTL: " + std::to_string(config.registry.service_ttl_s) + "s");
    LOG_INFO("  Heartbeat Timeout: " + std::to_string(config.registry.heartbeat_timeout_s) + "s");

    // 注册信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 创建并启动服务
    try {
        g_service = std::make_unique<ServiceRegistry>(config);

        if (!g_service->start()) {
            LOG_ERROR("Failed to start Service Registry");
            return 1;
        }

        // 等待服务停止
        g_service->wait();

    } catch (const std::exception& e) {
        LOG_ERROR("Service error: " + std::string(e.what()));
        return 1;
    }

    LOG_INFO("Service Registry shutdown complete");
    return 0;
}
