#include "gomoku_server.h"
#include "common/logger/logger.h"
#include "common/service_registry/service_registry_client.h"
#include "common/auth/jwt_validator.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <fstream>
#include <thread>
#include <chrono>
#include <unistd.h>  // for write() and STDOUT_FILENO

using namespace game_services::gomoku;

// 全局服务器实例
std::unique_ptr<GomokuServer> g_server;
std::atomic<bool> g_running{true};

// 服务注册客户端
std::shared_ptr<common::service_registry::ServiceRegistryClient> g_registry_client = nullptr;

/**
 * @brief 信号处理器
 * @note 信号处理器中只设置标志位，不执行复杂的关闭逻辑
 *       实际的关闭操作由主循环检测标志位后执行
 */
void signalHandler(int sig) {
    // 只设置标志位，让主循环处理关闭
    // 在信号处理器中调用 stop() 是不安全的，可能导致死锁
    g_running = false;

    // 使用 write() 而不是 LOG_INFO，因为 write() 是异步信号安全的
    // 使用 sizeof 避免调用 strlen（不是 async-signal-safe）
    static const char msg[] = "\n[Signal] Shutdown requested\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)sig;
}

/**
 * @brief 打印使用帮助
 */
void printUsage(const char* program_name) {
    std::cout << "五子棋游戏服务器 v1.0.0\n";
    std::cout << "用法: " << program_name << " [选项]\n\n";
    std::cout << "选项:\n";
    std::cout << "  -c, --config <file>    指定配置文件路径 (默认: config/gomoku_server_config.json)\n";
    std::cout << "  -p, --port <port>      指定HTTP服务端口 (默认: 8084)\n";
    std::cout << "  -w, --websocket <port> 指定WebSocket端口 (默认: 8085)\n";
    std::cout << "  -h, --help             显示此帮助信息\n";
    std::cout << "  --version              显示版本信息\n\n";
    std::cout << "示例:\n";
    std::cout << "  " << program_name << " --config ./my_config.json\n";
    std::cout << "  " << program_name << " --port 9000 --websocket 9001\n";
}

/**
 * @brief 打印版本信息
 */
void printVersion() {
    std::cout << "五子棋游戏服务器 v1.0.0\n";
    std::cout << "基于 GameBase 框架构建\n";
    std::cout << "支持的游戏模式: 自由模式, 连珠模式, Swap2\n";
    std::cout << "Copyright (c) 2024 GameServices\n";
}

/**
 * @brief 注册服务到服务注册中心
 */
bool registerToServiceRegistry(int http_port, int websocket_port) {
    LOG_INFO("[REGISTRY] 开始服务注册流程...");

    try {
        auto& cm = common::config::ConfigManager::getInstance();

        bool registry_enabled = cm.get<bool>("service_registry.enabled", false);
        LOG_INFO("[REGISTRY] 服务注册配置: enabled=" + std::string(registry_enabled ? "true" : "false"));

        if (!registry_enabled) {
            LOG_WARNING("[REGISTRY] 服务注册已禁用，跳过注册（在配置文件中设置 service_registry.enabled: true 启用）");
            return true;
        }

        std::string registry_host = cm.get<std::string>("service_registry.host", "127.0.0.1");
        int registry_port = cm.get<int>("service_registry.port", 8090);
        LOG_INFO("[REGISTRY] 注册中心地址: " + registry_host + ":" + std::to_string(registry_port));

        common::service_registry::ServiceRegistryClientConfig registry_config;
        registry_config.registry_host = registry_host;
        registry_config.registry_port = registry_port;
        registry_config.heartbeat_interval_s = cm.get<int>("service_registry.heartbeat_interval_s", 30);
        registry_config.auto_heartbeat = cm.get<bool>("service_registry.auto_heartbeat", true);

        g_registry_client = std::make_shared<common::service_registry::ServiceRegistryClient>(registry_config);

        common::service_registry::ServiceInstance instance;
        instance.service_name = "gomoku_server";
        instance.service_version = "1.0.0";
        instance.host = cm.get<std::string>("network.host", "0.0.0.0");
        instance.port = http_port;
        instance.health_check_endpoint = "/api/v1/gomoku/health";

        // 设置 API 端点列表
        instance.endpoints = {
            "/api/v1/gomoku/rooms",
            "/api/v1/gomoku/rooms/create",
            "/api/v1/gomoku/rooms/join",
            "/api/v1/gomoku/rooms/leave",
            "/api/v1/gomoku/game/move",
            "/api/v1/gomoku/game/surrender",
            "/api/v1/gomoku/health",
            "/ws/gomoku/game"
        };

        // 设置元数据 - 游戏服务
        instance.metadata["service_type"] = "game";  // 游戏服务
        instance.metadata["game_type"] = "gomoku";   // 游戏类型
        instance.metadata["region"] = cm.get<std::string>("service_registry.metadata.region", "cn-east");
        instance.metadata["websocket_port"] = std::to_string(websocket_port);

        LOG_INFO("[REGISTRY] 正在注册服务: " + instance.service_name + " at " + instance.instanceId());

        if (!g_registry_client->registerService(instance)) {
            LOG_WARNING("❌ 服务注册失败，但服务将继续运行");
            return false;
        }

        if (registry_config.auto_heartbeat) {
            g_registry_client->startHeartbeat(instance);
            LOG_INFO("[REGISTRY] 自动心跳已启动，间隔: " + std::to_string(registry_config.heartbeat_interval_s) + "秒");

            // 设置心跳回调 - 动态上报游戏服务器统计数据
            g_registry_client->setHeartbeatCallback([]() {
                std::unordered_map<std::string, std::string> dynamic_metadata;

                if (g_server) {
                    try {
                        auto stats = g_server->getGameStatistics();

                        // 提取统计数据
                        if (stats.contains("gomokuStats") && stats["gomokuStats"].is_object()) {
                            auto& gomoku_stats = stats["gomokuStats"];

                            // 在线人数
                            if (gomoku_stats.contains("activePlayers") && gomoku_stats["activePlayers"].is_number()) {
                                dynamic_metadata["online_players"] = std::to_string(gomoku_stats["activePlayers"].get<int>());
                            }

                            // 活跃房间数
                            if (gomoku_stats.contains("activeRooms") && gomoku_stats["activeRooms"].is_number()) {
                                dynamic_metadata["active_rooms"] = std::to_string(gomoku_stats["activeRooms"].get<int>());
                            }

                            // 今日完成对局数
                            if (gomoku_stats.contains("totalGamesFinished") && gomoku_stats["totalGamesFinished"].is_number()) {
                                dynamic_metadata["today_matches"] = std::to_string(gomoku_stats["totalGamesFinished"].get<int>());
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG_WARNING("心跳回调获取统计数据失败: " + std::string(e.what()));
                    }
                }

                return dynamic_metadata;
            });
            LOG_INFO("[REGISTRY] 心跳回调已设置，将动态上报统计数据");
        }

        LOG_INFO("✅ 服务已成功注册到服务注册中心: " + instance.service_name + " at " + instance.instanceId());

        // 设置服务注册客户端到服务器（用于报告对局完成）
        if (g_server) {
            g_server->setRegistryClient(g_registry_client);
            LOG_INFO("✅ 服务注册客户端已设置到服务器");
        }

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 服务注册异常: " + std::string(e.what()));
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
 * @brief 创建默认配置文件
 */
bool createDefaultConfig(const std::string& config_path) {
    try {
        auto config = GomokuServer::getDefaultConfig();
        nlohmann::json config_json = config.toJson();
        
        std::ofstream file(config_path);
        if (!file.is_open()) {
            LOG_ERROR("无法创建配置文件: " + config_path);
            return false;
        }
        
        file << std::setw(4) << config_json << std::endl;
        LOG_INFO("创建默认配置文件: " + config_path);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建默认配置文件失败: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 解析命令行参数
 */
bool parseArguments(int argc, char* argv[], std::string& config_file, 
                   int& port, int& websocket_port) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--version") {
            printVersion();
            return false;
        }
        else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        }
        else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                std::cerr << "错误: 无效的端口号 " << port << std::endl;
                return false;
            }
        }
        else if ((arg == "-w" || arg == "--websocket") && i + 1 < argc) {
            websocket_port = std::atoi(argv[++i]);
            if (websocket_port <= 0 || websocket_port > 65535) {
                std::cerr << "错误: 无效的WebSocket端口号 " << websocket_port << std::endl;
                return false;
            }
        }
        else {
            std::cerr << "错误: 未知参数 " << arg << std::endl;
            printUsage(argv[0]);
            return false;
        }
    }
    
    return true;
}

/**
 * @brief 服务器状态监控定时任务（使用线程池，无独立线程）
 */
void scheduleServerStatusMonitoring() {
    if (!g_server) {
        return;
    }
    
    // [TASK] 使用统一线程池执行定时任务，无独立监控线程
    static std::chrono::system_clock::time_point last_monitor_time = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    
    // 每5分钟执行一次监控
    if (std::chrono::duration_cast<std::chrono::minutes>(now - last_monitor_time).count() >= 5) {
        last_monitor_time = now;
        
        try {
            auto stats = g_server->getGameStatistics();
            
            // [FIX] 修复JSON访问路径：所有字段都在gomokuStats子对象中
            int active_rooms = 0, active_players = 0, total_games_finished = 0;
            
            // 安全提取JSON值，防止字段不存在导致崩溃
            if (stats.contains("gomokuStats") && stats["gomokuStats"].is_object()) {
                auto& gomoku_stats = stats["gomokuStats"];
                
                if (gomoku_stats.contains("activeRooms") && gomoku_stats["activeRooms"].is_number()) {
                    active_rooms = gomoku_stats["activeRooms"].get<int>();
                }
                
                if (gomoku_stats.contains("activePlayers") && gomoku_stats["activePlayers"].is_number()) {
                    active_players = gomoku_stats["activePlayers"].get<int>();
                }
                
                if (gomoku_stats.contains("totalGamesFinished") && gomoku_stats["totalGamesFinished"].is_number()) {
                    total_games_finished = gomoku_stats["totalGamesFinished"].get<int>();
                }
            }
            
            LOG_INFO("[STATUS] 服务器状态（定时任务） - 活跃房间: " + std::to_string(active_rooms) +
                    ", 在线玩家: " + std::to_string(active_players) +
                    ", 已完成游戏: " + std::to_string(total_games_finished));
                    
        } catch (const nlohmann::json::exception& json_e) {
            LOG_ERROR("❌ 定时监控获取服务器统计信息时发生JSON错误: " + std::string(json_e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("❌ 定时监控获取服务器统计信息时发生错误: " + std::string(e.what()));
        }
    }
}

/**
 * @brief 主函数
 */
int main(int argc, char* argv[]) {
    try {
        // 设置信号处理器
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
#ifdef SIGQUIT
        signal(SIGQUIT, signalHandler);
#endif

        std::cout << "五子棋游戏服务器启动中..." << std::endl;

        // ==================== 1. 查找并加载配置文件 ====================
        std::string config_file;
        int port = 0;           // 0表示使用配置文件中的值
        int websocket_port = 0; // 0表示使用配置文件中的值

        // 先解析命令行参数
        if (!parseArguments(argc, argv, config_file, port, websocket_port)) {
            return 0; // 显示帮助信息后正常退出
        }

        // 命令行未指定配置文件时，智能查找
        if (config_file.empty()) {
            config_file = common::config::ConfigManager::findConfigFile(
                "gomoku_server.yml", "GOMOKU_SERVICE_CONFIG");
        }

        // 检查配置文件是否存在
        std::ifstream config_check(config_file);
        if (!config_check.good()) {
            std::cout << "配置文件不存在，创建默认配置文件: " << config_file << std::endl;

            // 创建配置目录
            std::filesystem::path config_path(config_file);
            std::filesystem::create_directories(config_path.parent_path());

            if (!createDefaultConfig(config_file)) {
                std::cerr << "创建默认配置文件失败" << std::endl;
                return 1;
            }
        }
        config_check.close();

        // 加载配置到 ConfigManager
        auto& config_manager = common::config::ConfigManager::getInstance();
        config_manager.loadFromFile(config_file);

        // ==================== 2. 初始化日志系统 ====================
        try {
            common::logger::Logger::getInstance().initializeFromConfig();
            LOG_INFO("Logger系统初始化完成");
        } catch (const std::exception& e) {
            std::cerr << "日志系统初始化失败: " << e.what() << std::endl;
            return 1;
        }

        // ==================== 3. 初始化 JWT 验证器 ====================
        {
            common::auth::JwtValidatorConfig jwt_config = common::auth::JwtValidatorConfig::fromConfig();
            if (!common::auth::JwtValidator::getInstance().initialize(jwt_config)) {
                LOG_ERROR("JwtValidator 初始化失败");
                std::cerr << "❌ JwtValidator 初始化失败" << std::endl;
                return 1;
            }
            LOG_INFO("JwtValidator 初始化成功");
        }

        LOG_INFO("=== 五子棋游戏服务器启动 ===");

        // 从配置文件创建服务器
        g_server = GomokuServer::createFromConfig(config_file);
        if (!g_server) {
            LOG_ERROR("创建服务器失败");
            std::cerr << "服务器创建失败，请检查配置文件" << std::endl;
            return 1;
        }

        // 获取服务器配置信息
        auto config = g_server->getGomokuConfig();
        bool config_changed = false;  // 实现配置文件监控
        
        // 设置配置文件监控（简化版本）
        static std::chrono::system_clock::time_point last_config_check = std::chrono::system_clock::now();
        static auto check_config_file = [&config_file]() {
            try {
                // 检查配置文件的修改时间
                std::filesystem::file_time_type last_write_time = std::filesystem::last_write_time(config_file);
                static std::filesystem::file_time_type last_known_time = last_write_time;
                
                if (last_write_time != last_known_time) {
                    LOG_INFO("检测到配置文件变更: " + config_file);
                    last_known_time = last_write_time;
                    return true;
                }
            } catch (const std::exception& e) {
                LOG_WARNING("检查配置文件失败: " + std::string(e.what()));
            }
            return false;
        };
        
        // 定期检查配置文件变更（每30秒检查一次）
        auto now = std::chrono::system_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_config_check).count() >= 30) {
            config_changed = check_config_file();
            last_config_check = now;
            
            if (config_changed) {
                LOG_INFO("重新加载配置文件...");
                // 这里可以触发配置重载
                // g_server->reloadConfig(config_file);
            }
        }
        
        if (port > 0 && port != config.network_config.listen_port) {
            // 注意：这里需要重新创建服务器来应用端口更改
            // 简化实现中，建议在配置文件中设置端口
            LOG_WARNING("端口配置需要在配置文件中设置，命令行参数将被忽略");
        }

        // 初始化服务器
        if (!g_server->initialize()) {
            LOG_ERROR("服务器初始化失败");
            std::cerr << "服务器初始化失败" << std::endl;
            return 1;
        }

        // ⚠️ 注意：g_server->start() 会调用 event_loop_->loop() 并阻塞
        // 因此所有启动后需要执行的逻辑必须在 start() 之前完成

        // 先打印启动信息
        std::cout << "🎮 五子棋游戏服务器启动中..." << std::endl;
        std::cout << "📡 HTTP 服务端口: " << config.network_config.listen_port << std::endl;
        std::cout << "🔗 WebSocket 端口: " << config.websocket_port << std::endl;
        std::cout << "🎯 游戏类型: 五子棋 (Gomoku)" << std::endl;
        std::cout << "📊 最大并发游戏数: " << config.max_concurrent_games << std::endl;
        std::cout << "👥 是否允许观战: " << (config.allow_spectators ? "是" : "否") << std::endl;
        std::cout << "🏆 是否启用排名: " << (config.enable_ranking ? "是" : "否") << std::endl;
        std::cout << "💾 数据存储路径: " << config.game_data_path << std::endl;
        std::cout << "🔧 服务版本: " << config.service_version << std::endl;

        LOG_INFO("五子棋服务器启动中 - HTTP:" + std::to_string(config.network_config.listen_port) +
                ", WebSocket:" + std::to_string(config.websocket_port));
        LOG_INFO("[OK] 架构简化完成 - 无独立监控线程，使用定时任务");

        // [FIX] 关键：在 start() 之前注册到服务注册中心（因为 start() 会阻塞）
        registerToServiceRegistry(config.network_config.listen_port, config.websocket_port);

        // 启动服务器（此调用会阻塞，直到服务器停止）
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        std::cout << "🚀 正在启动服务器..." << std::endl;

        if (!g_server->start()) {
            LOG_ERROR("服务器启动失败");
            std::cerr << "服务器启动失败" << std::endl;
            return 1;
        }

        // 注意：下面的代码理论上不会执行，因为 start() 会阻塞
        // 但保留以防 EventLoop 实现变化
        std::cout << "🎮 五子棋游戏服务器启动成功!" << std::endl;
        LOG_INFO("五子棋服务器启动成功");

        // [TASK] 主循环 - 整合监控定时任务（无独立线程）
        while (g_running && g_server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // [TASK] 在主循环中执行定时监控任务（无独立线程）
            scheduleServerStatusMonitoring();
        }

        // 主循环退出后执行优雅关闭
        LOG_INFO("主循环退出，开始优雅关闭服务器...");
        std::cout << "\n🔄 正在关闭服务器..." << std::endl;

        // 从服务注册中心注销
        deregisterFromServiceRegistry();

        // 关闭服务器
        if (g_server) {
            LOG_INFO("调用服务器 stop() 方法...");
            g_server->stop();
            LOG_INFO("服务器 stop() 完成，正在重置...");
            g_server.reset();
        }

        std::cout << "✅ 服务器已安全关闭" << std::endl;
        LOG_INFO("=== 五子棋游戏服务器关闭完成 ===");

        return 0;

    } catch (const std::exception& e) {
        LOG_ERROR("服务器运行异常: " + std::string(e.what()));
        std::cerr << "服务器运行异常: " << e.what() << std::endl;
        
        if (g_server) {
            g_server->stop();
            g_server.reset();
        }
        
        return 1;
    }
}

