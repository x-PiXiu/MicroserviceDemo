/**
 * @file game_server_base.cpp
 * @brief 游戏服务器基类实现
 *
 * 架构说明：
 * 1. 统一的WebSocket架构：所有实时游戏操作通过WebSocket进行
 * 2. HTTP API仅用于查询类操作：如获取房间列表、服务器状态等
 * 3. 共享EventLoop：HTTP和WebSocket共享同一个事件循环，提高性能
 * 4. 一次认证持续会话：WebSocket连接时认证一次，后续操作无需重复验证
 *
 * 业务流程：
 * 1. 客户端通过Auth Service获取游戏会话令牌
 * 2. 客户端连接到游戏服务器WebSocket并认证
 * 3. 所有房间操作（创建、加入、离开、开始游戏）通过WebSocket消息进行
 * 4. HTTP API仅用于查询房间列表等不需要认证的操作
 */

#include "game_server_base.h"
#include "common/http/http_client.h"
#include "common/network/event_loop.h"
#include <random>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>  // for std::isnan, std::isinf

namespace game_services {
    namespace game_base
    {
        GameServerBase::GameServerBase(const BaseConfig& config) 
            : config_(config), start_time_(std::chrono::system_clock::now()) {
            LOG_INFO("Initializing GameServerBase with game type: " + std::to_string(static_cast<int>(config_.game_type)));
            
            // 🔧 优化：初始化性能监控
            try {
                if (performance_monitoring_enabled_) {
                    // 使用现有的性能监控器单例
                    auto& perf_monitor = common::monitoring::PerformanceMonitorSingleton::getInstance();
                    (void)perf_monitor;  // 用于确保单例初始化，避免未使用变量警告
                    LOG_INFO("Performance monitoring initialized successfully");
                }
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to initialize performance monitoring: " + std::string(e.what()));
                performance_monitoring_enabled_ = false;
            }

            // 🔧 架构简化：从ConfigManager读取安全和告警配置
            try {
                auto& config_manager = common::config::ConfigManager::getInstance();
                
                // 从ConfigManager读取安全配置
                SecurityConfig security_config;
                security_config.max_messages_per_minute = config_manager.get<int>("security.max_messages_per_minute", 60);
                security_config.max_connections_per_ip = config_manager.get<int>("security.max_connections_per_ip", 10);
                security_config.max_message_size = config_manager.get<size_t>("security.max_message_size", 65536);
                
                security_manager_ = std::make_unique<SecurityManager>(security_config);
                
                // 从ConfigManager读取告警配置
                AlertConfig alert_config;
                alert_config.enable_alerts = config_manager.get<bool>("alerts.enable", true);
                alert_config.check_interval_seconds = config_manager.get<int>("alerts.check_interval", 60);
                alert_config.enable_notifications = config_manager.get<bool>("alerts.enable_notifications", false);
                
                if (performance_monitoring_enabled_) {
                    auto& perf_monitor = common::monitoring::PerformanceMonitorSingleton::getInstance();
                    alert_manager_ = std::make_unique<AlertManager>(alert_config, perf_monitor);
                }
                
                LOG_INFO("✅ Enterprise components initialized successfully (ConfigManager模式)");
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize enterprise components: " + std::string(e.what()));
                // 继续运行，但功能会受限
            }
        }

        // 🔧 架构简化：移除DynamicConfigManager构造函数，统一使用ConfigManager

        GameServerBase::~GameServerBase() {
            try {
                if (running_.load()) {
                    stop();
                }

                // 确保所有异步任务完成
                if (thread_pool_) {
                    thread_pool_->shutdown();
                    thread_pool_.reset();
                }

                LOG_INFO("GameServerBase destroyed");
            } catch (const std::exception& e) {
                // 析构函数中不抛出异常
                LOG_ERROR("Error in destructor: " + std::string(e.what()));
            } catch (...) {
                // 捕获所有异常
            }
        }

        bool GameServerBase::initialize() {
            LOG_INFO("Initializing game server...");

            // 1. 验证配置 - 确保所有必需的配置项都正确设置
            if (!config_.validate()) {
                LOG_ERROR("Invalid server configuration");
                return false;
            }

            // 2. 初始化数据库连接池 - 为游戏数据存储做准备
            // 包括MySQL（持久化数据）和Redis（缓存和会话）
            if (!initializeDatabasePools()) {
                LOG_ERROR("Failed to initialize database pools");
                return false;
            }

            // 3. 初始化线程池 - 处理并发请求和后台任务
            // 使用配置文件中的线程池参数
            if (!initializeThreadPool()) {
                LOG_ERROR("Failed to initialize thread pool");
                return false;
            }

            // 4. 初始化HTTP服务器 - 处理REST API请求
            // 主要用于查询类操作和服务发现
            if (!initializeHttpServer()) {
                LOG_ERROR("Failed to initialize HTTP server");
                return false;
            }

            // 5. 初始化游戏特定功能 - 由子类实现具体游戏逻辑
            // 🔧 关键修复：移到HTTP服务器初始化之后，确保http_server_可用
            if (!initializeGameSpecific()) {
                LOG_ERROR("Failed to initialize game specific features");
                return false;
            }

            // 5.5. 🔧 修复：移除EventLoop检查，WebSocket将在start()阶段初始化
            // EventLoop在HTTP服务器start()时才创建，初始化阶段不可用是正常的
            LOG_INFO("HTTP服务器已创建，WebSocket将在服务器启动时初始化");

            // 6. 跳过WebSocket初始化 - 将在start()阶段EventLoop启动后再初始化
            // 原因：WebSocket需要在EventLoop线程启动后才能安全初始化
            LOG_INFO("WebSocket initialization deferred until EventLoop starts");

            // 7. 🔧 修复：延迟API网关注册至start()阶段，确保HTTP服务器完全启动
            // 原因：API Gateway会立即尝试访问API发现端点，但此时HTTP服务器还在初始化
            LOG_INFO("API Gateway registration deferred until HTTP server is fully started");

            LOG_INFO("Game server initialized successfully");
            return true;
        }

        bool GameServerBase::start() {
            // 防止重复启动
            if (running_.load()) {
                LOG_WARNING("Server is already running");
                return true;
            }

            LOG_INFO("Starting game server...");

            // 🔧 关键修复：HTTP服务器的start()会阻塞在event_loop_->loop()
            // 导致WebSocket初始化代码永远不会执行！
            // 修复方案：先调用initialize()创建EventLoop，初始化WebSocket，再运行loop

            // 1. 初始化HTTP服务器（创建EventLoop但不运行）
            LOG_INFO("Initializing HTTP server (creating EventLoop)...");
            if (http_server_ && !http_server_->initialize()) {
                LOG_ERROR("Failed to initialize HTTP server");
                return false;
            }

            // 2. 获取EventLoop并初始化WebSocket
            auto event_loop = http_server_ ? http_server_->getEventLoop() : nullptr;
            if (!event_loop) {
                LOG_ERROR("HTTP server EventLoop not available");
                return false;
            }
            LOG_INFO("HTTP server EventLoop created successfully");

            // 3. 初始化WebSocket（需要EventLoop已存在）
            if (config_.enable_websocket) {
                LOG_INFO("Initializing WebSocket server on port " + std::to_string(config_.websocket_port));
                if (!initializeWebSocket()) {
                    LOG_ERROR("Failed to initialize WebSocket server");
                    return false;
                }

                // 等待WebSocket异步初始化完成
                if (websocket_handler_) {
                    if (!websocket_handler_->waitForInitialization(5000)) {
                        LOG_ERROR("WebSocket initialization timed out or failed");
                        return false;
                    }

                    websocket_handler_->start();
                    LOG_INFO("WebSocket handler started on port " + std::to_string(config_.websocket_port));
                }
            } else {
                LOG_INFO("WebSocket disabled in configuration");
            }

            // 4. 启动后台定时任务
            startTimerBasedTasks(event_loop);

            // 5. 启动企业级组件
            if (alert_manager_) {
                alert_manager_->start();
                LOG_INFO("Alert manager started");
            }

            // 6. 标记服务为运行状态
            running_.store(true);
            start_time_ = std::chrono::system_clock::now();

            // [REMOVED] API Gateway 注册代码 - 现在使用 Nginx + Service Registry

            LOG_INFO("Game server components initialized, starting event loop...");

            // 8. 🔧 关键：现在运行HTTP服务器的事件循环（这会阻塞）
            // 使用HttpServer的runEventLoop()方法（需要在HttpServer中添加）
            // 临时方案：直接获取EventLoop并运行
            if (http_server_) {
                http_server_->startEventLoop();
            }

            LOG_INFO("Game server started successfully on port " + std::to_string(config_.port));
            return true;
        }

        // 🔧 优化：启动服务器（基于EventLoop定时器）
        bool GameServerBase::start(std::shared_ptr<common::network::EventLoop> event_loop) {
            // 防止重复启动
            if (running_.load()) {
                LOG_WARNING("Server is already running");
                return true;
            }

            LOG_INFO("Starting game server (timer mode)...");

            // 1. 启动WebSocket服务器（在HTTP服务器之前）
            // 原因：WebSocket与HTTP共享EventLoop，需要先注册WebSocket监听器
            if (websocket_handler_) {
                websocket_handler_->start();
                LOG_INFO("WebSocket handler started on port " + std::to_string(config_.websocket_port));
            }

            // 2. 🔧 优化：启动基于EventLoop的定时任务
            if (event_loop) {
                LOG_INFO("Starting timer-based tasks...");
                startTimerBasedTasks(event_loop);
            } else {
                // 🔧 修复：移除回退到线程池模式，强制使用EventLoop
                LOG_WARNING("EventLoop参数为空，尝试使用HTTP服务器的EventLoop");
                if (http_server_ && http_server_->getEventLoop()) {
                    startTimerBasedTasks(http_server_->getEventLoop());
                } else {
                    LOG_ERROR("无可用的EventLoop，定时任务启动失败！");
                }
            }

            // 3. 🔧 新增：启动企业级组件
            if (alert_manager_) {
                alert_manager_->start();
                LOG_INFO("Alert manager started (timer mode)");
            }

            // 4. 标记服务为运行状态
            running_.store(true);
            start_time_ = std::chrono::system_clock::now();

            // [REMOVED] API Gateway 注册代码 - 现在使用 Nginx + Service Registry

            // 5. 启动HTTP服务器（这会阻塞当前线程运行EventLoop）
            // EventLoop会处理HTTP和WebSocket的所有网络事件
            LOG_INFO("Starting HTTP server (this will block)...");
            if (http_server_ && !http_server_->start()) {
                LOG_ERROR("Failed to start HTTP server");
                return false;
            }

            LOG_INFO("Game server started successfully on port " + std::to_string(config_.port) + " (timer mode)");
            return true;
        }

        void GameServerBase::stop() {
            if (!running_.exchange(false)) {
                return;
            }

            LOG_INFO("Stopping game server...");

            try {
            // 🔧 优化：首先停止基于EventLoop的定时器
            stopTimerBasedTasks();

            // 🔧 新增：停止企业级组件
            if (alert_manager_) {
                alert_manager_->stop();
                LOG_INFO("Alert manager stopped");
            }

            // 1. 首先停止WebSocket服务器（因为它使用HTTP服务器的EventLoop）
            if (websocket_handler_) {
                LOG_INFO("Stopping WebSocket server first...");
                websocket_handler_->stop();
                LOG_INFO("WebSocket server stopped");

                // 给WebSocket处理器充足时间完成所有Channel清理
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

                // 2. 然后停止HTTP服务器（这会停止共享的EventLoop）
                if (http_server_) {
                    LOG_INFO("Stopping HTTP server...");
                    http_server_->stop();
                    LOG_INFO("HTTP server stopped");
                }

                // 3. 停止线程池（对照API Gateway）
                if (thread_pool_) {
                    thread_pool_->shutdown();
                    LOG_INFO("Thread pool stopped");
                }

                // 4. 通知所有房间游戏即将停止
                {
                    std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
                    for (const auto& [room_id, room] : rooms_) {
                        if (room) {
                            room->forceStop("Server shutdown");
                        }
                    }
                }

                // 5. 清理所有房间
                {
                    std::unique_lock<std::shared_mutex> lock(rooms_mutex_);
                    for (auto& [room_id, room] : rooms_) {
                        if (room) {
                            room->cleanup();
                        }
                    }
                    rooms_.clear();
                    LOG_INFO("All rooms cleaned up");
                }

                // 6. 清理所有玩家会话
                {
                    std::unique_lock<std::shared_mutex> lock(players_mutex_);
                    players_.clear();
                    LOG_INFO("All player sessions cleaned up");
                }

                // 7. 关闭数据库连接池
                if (mysql_pool_) {
                    mysql_pool_->stop();
                }
                if (redis_pool_) {
                    redis_pool_->stop();
                }

                LOG_INFO("Game server stopped successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Error during server shutdown: " + std::string(e.what()));
            }
        }

        GameStats GameServerBase::getGameStats() const {
            std::shared_lock<std::shared_mutex> lock(stats_mutex_);
            return current_stats_;
        }

        std::shared_ptr<common::network::EventLoop> GameServerBase::getHttpServerEventLoop() const {
            if (http_server_) {
                // 🔧 修复：EventLoop只有在HTTP服务器启动后才可用
                // 初始化阶段返回nullptr是正常的，不应该导致启动失败
                try {
                    return http_server_->getEventLoop();
                } catch (const std::exception& e) {
                    // HTTP服务器可能没有getEventLoop方法，或EventLoop还未创建
                    LOG_DEBUG("HTTP服务器EventLoop暂不可用: " + std::string(e.what()));
                    return nullptr;
                }
            }
            return nullptr;
        }

        void GameServerBase::handleCreateRoom(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            // ⚠️ 此HTTP API已废弃 - 游戏操作应通过WebSocket进行
            //
            // 架构变更说明：
            // 1. 旧架构：HTTP API + WebSocket混合模式，每次HTTP请求都需要验证令牌
            // 2. 新架构：纯WebSocket模式，一次认证后持续会话
            // 3. 优势：更低延迟、更好的实时性、统一的消息处理
            LOG_WARNING("Deprecated HTTP API called: POST /rooms - Client should use WebSocket API");

            // 返回详细的迁移指导信息，帮助客户端迁移到WebSocket API
            nlohmann::json migration_guide = {
                {"error", "API_DEPRECATED"},
                {"message", "This HTTP API is deprecated. Please use WebSocket API for game operations."},
                {"migration_guide", {
                        {"step1", "Connect to WebSocket: ws://" + config_.host + ":" + std::to_string(config_.websocket_port)},
                        {"step2", "Send authentication message with your game session token"},
                        {"step3", "Send create_room message through WebSocket"},
                        {"websocket_message_format", {
                            {"type", "create_room"},
                            {"room_config", {
                                {"max_players", 4},
                                {"game_mode", "classic"},
                                {"private", false}
                            }}
                        }}
                }},
                {"websocket_url", "ws://" + config_.host + ":" + std::to_string(config_.websocket_port)},
                {"documentation", "/api/docs/websocket"}
            };

            res.setStatus(410); // 410 Gone - 资源已不再可用
            res.setJsonBody(migration_guide);
            return;

            // 注意：原HTTP实现已移除，所有房间操作现在通过WebSocket进行
        }

        void GameServerBase::handleJoinRoom(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            // ⚠️ 此HTTP API已废弃 - 游戏操作应通过WebSocket进行
            //
            // 业务逻辑说明：
            // 加入房间需要实时的状态同步，HTTP API无法提供最佳体验
            // WebSocket API能够立即通知房间内其他玩家，并实时更新房间状态
            LOG_WARNING("Deprecated HTTP API called: POST /rooms/{room_id}/join - Client should use WebSocket API");

            std::string room_id = req.getParam("room_id");

            // 返回针对加入房间操作的迁移指导信息
            nlohmann::json migration_guide = {
                {"error", "API_DEPRECATED"},
                {"message", "This HTTP API is deprecated. Please use WebSocket API for game operations."},
                {"migration_guide", {
                    {"step1", "Connect to WebSocket: ws://" + config_.host + ":" + std::to_string(config_.websocket_port)},
                    {"step2", "Send authentication message with your game session token"},
                    {"step3", "Send join_room message through WebSocket"},
                    {"websocket_message_format", {
                        {"type", "join_room"},
                        {"room_id", room_id.empty() ? "ROOM_ID" : room_id},
                        {"player_name", "YOUR_PLAYER_NAME"}
                    }}
                }},
                {"websocket_url", "ws://" + config_.host + ":" + std::to_string(config_.websocket_port)},
                {"documentation", "/api/docs/websocket"}
            };

            res.setStatus(410); // 410 Gone
            res.setJsonBody(migration_guide);
            return;

            // 注意：原HTTP实现已移除，所有房间操作现在通过WebSocket进行
        }

        void GameServerBase::handleGetRoomList(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            try {
                // 获取房间列表 - 这是查询类操作，保留HTTP API
                // 原因：房间列表查询不需要实时性，HTTP API更适合
                nlohmann::json room_list = nlohmann::json::array();

                {
                    std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
                    // 遍历所有房间，只返回未满的房间（可加入的房间）
                    for (const auto& [room_id, room] : rooms_) {
                        if (room && !room->isFull()) {
                            nlohmann::json room_info = {
                                {"room_id", room_id},
                                {"player_count", room->getPlayerCount()},
                                {"max_players", room->getMaxPlayers()},
                                {"state", static_cast<int>(room->getState())},
                                {"game_type", getGameTypeName()}
                            };
                            room_list.push_back(room_info);
                        }
                    }
                }

                // 构建响应，包含房间列表和服务器信息
                nlohmann::json response = {
                    {"rooms", room_list},
                    {"total_rooms", room_list.size()},
                    {"server_info", {
                        {"game_type", getGameTypeName()},
                        {"qt6_support", config_.qt6_optimizations},
                        {"max_rooms", config_.max_rooms}
                    }}
                };

                res.ok(response);

            } catch (const std::exception& e) {
                LOG_ERROR("Get room list error: " + std::string(e.what()));
                res.internalServerError("Failed to get room list");
            }
        }

        void GameServerBase::handleHealthCheck(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            // 🔧 优化：记录HTTP请求性能
            auto start_time = std::chrono::high_resolution_clock::now();
            
            LOG_DEBUG("Starting handleHealthCheck()");
            
            // 🔧 安全的健康检查JSON构建
            try {
                auto stats = getGameStats();
                nlohmann::json health = {
                    {"status", "healthy"},
                    {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now() - start_time_).count()},
                    {"game_type", getGameTypeName()}
                };
                
                // 🔧 安全添加统计信息
                try {
                    health["stats"] = stats.toJson();
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Health check stats JSON serialization failed: " + std::string(e.what()));
                    health["stats"] = {{"error", "serialization_failed"}};
                }
                
                res.ok(health);
                LOG_DEBUG("✅ Health check response sent successfully");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Health check failed: " + std::string(e.what()));
                res.internalServerError("Health check failed");
            }
            
            // 🔧 安全记录性能指标，防止JSON序列化错误
            if (performance_monitoring_enabled_) {
                try {
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    
                    // 🔧 验证duration有效性，防止NaN/Inf导致JSON错误
                    auto duration_count = duration.count();
                    if (std::isnan(duration_count) || std::isinf(duration_count) || duration_count < 0) {
                        LOG_WARNING("⚠️ Invalid duration in health check: " + std::to_string(duration_count) + "ms, using 0");
                        duration = std::chrono::milliseconds(0);
                    }
                    
                    LOG_DEBUG("📊 Health check duration: " + std::to_string(duration.count()) + "ms");
                    PERF_HTTP_REQUEST("GET", "/health", 200, duration);
                    LOG_DEBUG("✅ Health check performance recorded successfully");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Failed to record health check performance: " + std::string(e.what()));
                }
            }
        }

        std::string GameServerBase::generateRoomId() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(100000, 999999);
            
            std::stringstream ss;
            ss << getGameTypeName() << "_" << dis(gen) << "_" << 
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            
            return ss.str();
        }

        bool GameServerBase::validateUserToken(const common::http::HttpRequest& req, std::string& user_id) {
            // ⚠️ 此方法已废弃 - 仅用于向后兼容
            //
            // 废弃原因：
            // 1. HTTP令牌验证增加延迟，每次请求都需要验证
            // 2. 与WebSocket长连接架构不一致
            // 3. 无法提供实时游戏所需的低延迟通信
            //
            // 新的认证流程：
            // 1. 客户端通过Auth Service获取游戏会话令牌
            // 2. WebSocket连接时进行一次性认证
            // 3. 认证成功后，整个会话期间无需重复验证
            LOG_WARNING("validateUserToken() is deprecated - use WebSocket authentication instead");

            try {
                std::string auth_header = req.getHeader("Authorization");
                if (auth_header.empty() || auth_header.find("Bearer ") != 0) {
                    return false;
                }

                std::string token = auth_header.substr(7);

                // 调用Auth Service验证token（保留实现以支持遗留的HTTP API）
                common::http::HttpClient client;
                nlohmann::json verify_request = {{"token", token}};

                // 使用配置中的 auth_service_url
                std::string verify_url = config_.auth_service_url + "/api/v1/auth/verify";
                auto response = client.post(verify_url, verify_request.dump());

                if (response.success && response.status_code == 200) {
                    auto verify_response = nlohmann::json::parse(response.body);
                    if (verify_response["valid"].get<bool>()) {
                        user_id = verify_response["user_id"].get<std::string>();
                        return true;
                    }
                }

                return false;

            } catch (const std::exception& e) {
                LOG_ERROR("Token validation error: " + std::string(e.what()));
                return false;
            }
        }

        void GameServerBase::setPlayerAuthenticated(const std::string& player_id, bool authenticated) {
            // WebSocket认证状态管理
            // 与HTTP令牌验证不同，WebSocket认证是一次性的，认证成功后维持整个会话
            std::unique_lock<std::shared_mutex> lock(auth_mutex_);
            if (authenticated) {
                authenticated_players_[player_id] = true;
                LOG_DEBUG("Player " + player_id + " authenticated via WebSocket");
            } else {
                authenticated_players_.erase(player_id);
                LOG_DEBUG("Player " + player_id + " authentication removed");
            }
        }

        bool GameServerBase::isPlayerAuthenticated(const std::string& player_id) const {
            // 检查玩家是否已通过WebSocket认证
            // 用于房间操作前的权限验证
            std::shared_lock<std::shared_mutex> lock(auth_mutex_);
            auto it = authenticated_players_.find(player_id);
            return it != authenticated_players_.end() && it->second;
        }

        void GameServerBase::removePlayerAuthentication(const std::string& player_id) {
            // 清理玩家认证状态，通常在连接断开时调用
            std::unique_lock<std::shared_mutex> lock(auth_mutex_);
            authenticated_players_.erase(player_id);
            LOG_DEBUG("Removed authentication for player " + player_id);
        }

        bool GameServerBase::initializeDatabasePools() {
            try {
                // 初始化MySQL连接池
                common::config::DatabaseConfig mysql_config;
                mysql_config.mysql_host = config_.mysql_host;
                mysql_config.mysql_port = config_.mysql_port;
                mysql_config.mysql_database = config_.mysql_database;
                mysql_config.mysql_user = config_.mysql_username;
                mysql_config.mysql_password = config_.mysql_password;
                mysql_config.mysql_pool_size = config_.mysql_max_connections;
                mysql_config.mysql_connect_timeout_ms = config_.mysql_connection_timeout / 1000; // 转换为秒

                mysql_pool_ = std::make_shared<common::database::MySQLPool>(mysql_config);
                mysql_pool_->start(); // 使用start方法而不是initialize

                // 初始化Redis连接池
                common::config::RedisConfig redis_config;
                redis_config.redis_host = config_.redis_host;
                redis_config.redis_port = config_.redis_port;
                redis_config.redis_database = config_.redis_database;
                redis_config.redis_password = config_.redis_password;
                redis_config.redis_pool_size = config_.redis_max_connections;
                redis_config.redis_connect_timeout_ms = config_.redis_connection_timeout ; // 转换毫秒为秒

                redis_pool_ = std::make_shared<common::database::RedisPool>(redis_config);
                redis_pool_->start(); // 使用start方法而不是initialize

                LOG_INFO("Database pools initialized successfully");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Database pools initialization error: " + std::string(e.what()));
                return false;
            }
        }

        bool GameServerBase::initializeHttpServer() {
            try {
                // 创建HTTP服务器配置 - 完全对照API Gateway的默认配置
                common::http::HttpServerConfig server_config;
                server_config.network_config.bind_address = config_.host;
                server_config.network_config.listen_port = config_.port;
                server_config.network_config.worker_threads = 8;
                server_config.worker_threads = 8;  // 与API Gateway一致
                server_config.enable_thread_pool = true;

                // 基础网络配置 - 与API Gateway保持一致
                server_config.max_request_size = 1048576;  // 1MB，与API Gateway一致
                server_config.max_header_size = 8192;      // 8KB，与API Gateway一致
                server_config.request_timeout_ms = 30000;  // 30秒，与API Gateway一致
                server_config.keep_alive_timeout_ms = 60000; // 60秒，与API Gateway一致

                // 关键修复：网络配置优化，完全对照API Gateway
                server_config.enable_so_reuseaddr = true;   // 允许地址重用（用于快速重启）
                server_config.enable_tcp_nodelay = true;   // 禁用Nagle算法，降低延迟
                server_config.enable_keep_alive = true;    // 启用TCP Keep-Alive
                server_config.enable_so_linger = true;     // 启用SO_LINGER控制
                server_config.linger_timeout = 0;          // 恢复为0，与API Gateway默认值一致

                http_server_ = std::make_unique<common::http::HttpServer>(server_config);

                // 注意：系统级路由 (health, stats, endpoints) 由子类在各服务的统一前缀下注册
                // 例如 GomokuServer 注册 /api/v1/gomoku/health 等
                // 这样保持所有服务的 API 路径格式一致: /api/v1/{serviceName}/{resource}

                // 🔧 修复：移除HTTP服务器初始化调用
                // HTTP服务器的初始化将在start()方法中自动进行
                // 这里只负责配置和路由注册

                LOG_INFO("HTTP server initialized on " + config_.host + ":" + std::to_string(config_.port));
                
                // 🔧 修复：注册API发现端点，支持服务发现和文档生成
                try {
                    std::string service_name = "gomoku_service"; // 可以从配置中获取
                    std::string service_version = "1.0.0"; // 可以从配置中获取
                    std::string base_url = ""; // 使用默认值
                    
                    http_server_->registerApiDiscoveryEndpoints(service_name, service_version, base_url);
                    LOG_INFO("API发现端点注册成功");
                } catch (const std::exception& e) {
                    LOG_WARNING("API发现端点注册失败: " + std::string(e.what()));
                    // 这不是致命错误，继续运行
                }
                
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("HTTP server initialization error: " + std::string(e.what()));
                return false;
            }
        }

        bool GameServerBase::initializeWebSocket() {
            // WebSocket初始化将在子类中实现
            return true;
        }

        bool GameServerBase::initializeThreadPool() {
            try {
                LOG_INFO("Initializing thread pool with BaseConfig settings...");

                // 从配置文件加载线程池配置，支持运行时调优
                // 这些配置影响服务器的并发处理能力和资源使用
                common::config::ThreadPoolConfig thread_config;

                // 核心线程数：始终保持活跃的线程数量
                thread_config.core_pool_size = config_.thread_pool_core_size;

                // 最大线程数：高负载时可创建的最大线程数
                thread_config.maximum_pool_size = config_.thread_pool_max_size;

                // 线程保活时间：空闲线程的最大存活时间
                thread_config.keep_alive_time_ms = config_.thread_pool_keep_alive_ms;

                // 任务队列容量：待处理任务的最大数量
                thread_config.queue_capacity = config_.thread_pool_queue_capacity;

                // 其他配置项
                thread_config.allow_core_thread_timeout = config_.thread_pool_allow_core_timeout;
                thread_config.rejection_policy = config_.thread_pool_rejection_policy;
                thread_config.prestartAllCoreThreads = config_.thread_pool_prestart_core_threads;
                thread_config.thread_name_prefix = config_.thread_pool_name_prefix;
                thread_config.enable_monitoring = config_.thread_pool_enable_monitoring;

                // 验证配置的合理性（如核心线程数不能大于最大线程数）
                thread_config.validate();

                // 创建线程池实例
                thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(thread_config);

                LOG_INFO("Thread pool initialized successfully:");
                LOG_INFO("  - Core pool size: " + std::to_string(thread_config.core_pool_size));
                LOG_INFO("  - Maximum pool size: " + std::to_string(thread_config.maximum_pool_size));
                LOG_INFO("  - Queue capacity: " + std::to_string(thread_config.queue_capacity));
                LOG_INFO("  - Keep alive time: " + std::to_string(thread_config.keep_alive_time_ms) + "ms");
                LOG_INFO("  - Thread name prefix: " + thread_config.thread_name_prefix);
                LOG_INFO("  - Monitoring enabled: " + std::string((thread_config.enable_monitoring ? "true" : "false")));

                // 🔧 修复：线程池创建后，为PerformanceMonitor设置线程池
                if (performance_monitoring_enabled_) {
                    try {
                        // 重新初始化PerformanceMonitor，传入线程池
                        common::monitoring::PerformanceMonitor::Config perf_config;
                        perf_config.enable_monitoring = true;
                        perf_config.collection_interval_seconds = 5;  // 5秒收集间隔
                        
                        common::monitoring::PerformanceMonitorSingleton::initialize(perf_config, thread_pool_);
                        LOG_INFO("✅ PerformanceMonitor 已重新初始化并设置线程池");
                    } catch (const std::exception& e) {
                        LOG_WARNING("重新初始化PerformanceMonitor失败: " + std::string(e.what()));
                    }
                }

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize thread pool: " + std::string(e.what()));
                return false;
            }
        }

        void GameServerBase::cleanupExpiredRooms() {
            LOG_DEBUG("🧹 Starting expired rooms cleanup");
            std::vector<std::string> expired_rooms;
            
            try {
                std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
                LOG_DEBUG("🔍 Checking " + std::to_string(rooms_.size()) + " rooms for expiration");
                
                for (const auto& [room_id, room] : rooms_) {
                    try {
                        if (room && room->isExpired()) {
                            expired_rooms.push_back(room_id);
                            LOG_DEBUG("🗑️ Room marked for cleanup: " + room_id);
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("❌ Error checking room expiration for " + room_id + ": " + std::string(e.what()));
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Error during expired rooms check: " + std::string(e.what()));
                return;
            }

            if (!expired_rooms.empty()) {
                LOG_INFO("🧹 Cleaning up " + std::to_string(expired_rooms.size()) + " expired rooms");
                
                try {
                    std::unique_lock<std::shared_mutex> lock(rooms_mutex_);
                    for (const auto& room_id : expired_rooms) {
                        try {
                            rooms_.erase(room_id);
                            LOG_INFO("✅ Cleaned up expired room: " + room_id);
                            
                            // 🔧 安全执行回调，防止JSON序列化错误
                            if (room_destroyed_callback_) {
                                try {
                                    room_destroyed_callback_(room_id);
                                    LOG_DEBUG("✅ Room destroyed callback executed for: " + room_id);
                                } catch (const nlohmann::json::exception& json_e) {
                                    LOG_ERROR("❌ JSON error in room_destroyed_callback for " + room_id + ": " + std::string(json_e.what()));
                                } catch (const std::exception& callback_e) {
                                    LOG_ERROR("❌ Error in room_destroyed_callback for " + room_id + ": " + std::string(callback_e.what()));
                                }
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("❌ Error cleaning up room " + room_id + ": " + std::string(e.what()));
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Error during expired rooms cleanup: " + std::string(e.what()));
                }
            } else {
                LOG_DEBUG("✅ No expired rooms found");
            }
        }

        void GameServerBase::updateGameStats() {
            LOG_DEBUG("Starting updateGameStats()");
            
            GameStats stats;
            
            // 🔧 安全收集房间统计信息
            try {
                std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
                stats.total_rooms = static_cast<int>(rooms_.size());
                for (const auto& [room_id, room] : rooms_) {
                    if (room && room->getState() == RoomState::PLAYING) {
                        stats.active_rooms++;
                    }
                }
                LOG_DEBUG("✅ Room stats collected: total=" + std::to_string(stats.total_rooms) + 
                         ", active=" + std::to_string(stats.active_rooms));
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to collect room stats: " + std::string(e.what()));
                stats.total_rooms = 0;
                stats.active_rooms = 0;
            }

            // 🔧 安全收集玩家统计信息
            try {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                stats.total_players = static_cast<int>(players_.size());
                for (const auto& [player_id, player_info] : players_) {
                    if (player_info.state == PlayerState::PLAYING) {
                        stats.active_players++;
                    }
                }
                LOG_DEBUG("✅ Player stats collected: total=" + std::to_string(stats.total_players) + 
                         ", active=" + std::to_string(stats.active_players));
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to collect player stats: " + std::string(e.what()));
                stats.total_players = 0;
                stats.active_players = 0;
            }

            // 🔧 安全更新统计信息
            try {
                std::unique_lock<std::shared_mutex> lock(stats_mutex_);
                current_stats_ = stats;
                LOG_DEBUG("✅ Game stats updated successfully");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to update game stats: " + std::string(e.what()));
            }
            
            LOG_DEBUG("updateGameStats() completed");
        }

        // ⚠️ 已废弃：此方法已被基于EventLoop时间轮的startTimerBasedTasks()替代
        // 废弃原因：
        // 1. 使用独立线程池循环，资源开销大
        // 2. 与EventLoop时间轮架构不统一
        // 3. 无法利用统一的事件调度机制
        // 替代方案：使用startTimerBasedTasks()中的cleanup_timer_id_
        void GameServerBase::startCleanupTimer() {
            LOG_WARNING("startCleanupTimer() is deprecated - use startTimerBasedTasks() instead");
            // ❌ 原实现已移除，请使用EventLoop时间轮版本
            // thread_pool_->submit([this]() {
            //     while (running_.load()) {
            //         std::this_thread::sleep_for(std::chrono::minutes(5));
            //         if (running_.load()) {
            //             cleanupExpiredRooms();
            //             cleanupOfflinePlayers();
            //         }
            //     }
            // });
        }

        // ⚠️ 已废弃：此方法已被基于EventLoop时间轮的startTimerBasedTasks()替代
        // 废弃原因：
        // 1. 使用独立线程池循环，资源开销大
        // 2. 与EventLoop时间轮架构不统一
        // 3. 无法利用统一的事件调度机制
        // 替代方案：使用startTimerBasedTasks()中的stats_update_timer_id_
        void GameServerBase::startStatsUpdateTimer() {
            LOG_WARNING("startStatsUpdateTimer() is deprecated - use startTimerBasedTasks() instead");
            // ❌ 原实现已移除，请使用EventLoop时间轮版本
            // thread_pool_->submit([this]() {
            //     while (running_.load()) {
            //         std::this_thread::sleep_for(std::chrono::seconds(30));
            //         if (running_.load()) {
            //             updateGameStats();
            //         }
            //     }
            // });
        }

        // [REMOVED] registerToApiGateway() - 现在使用 Nginx + Service Registry (端口 8090)
        // [REMOVED] sendHeartbeatToApiGateway() - 心跳通过 Service Registry 处理

        void GameServerBase::cleanupOfflinePlayers() {
            LOG_DEBUG("🧹 Starting offline players cleanup");
            std::vector<std::string> offline_players;

            try {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                LOG_DEBUG("🔍 Checking " + std::to_string(players_.size()) + " players for offline status");
                
                auto now = std::chrono::system_clock::now();
                
                for (const auto& [player_id, player_info] : players_) {
                    try {
                        // 🔧 安全时间计算，防止无效时间导致NaN/Inf
                        if (player_info.last_activity > now) {
                            LOG_WARNING("⚠️ Player " + player_id + " has future last_activity time, skipping");
                            continue;
                        }
                        
                        auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - player_info.last_activity);
                        auto minutes_inactive = duration.count();
                        
                        // 🔧 验证计算结果有效性
                        if (std::isnan(minutes_inactive) || std::isinf(minutes_inactive) || minutes_inactive < 0) {
                            LOG_ERROR("❌ Invalid duration calculated for player " + player_id + ": " + std::to_string(minutes_inactive));
                            continue;
                        }
                        
                        if (minutes_inactive > 10) { // 10分钟无活动
                            offline_players.push_back(player_id);
                            LOG_DEBUG("🗑️ Player marked for cleanup: " + player_id + " (inactive for " + std::to_string(minutes_inactive) + " minutes)");
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("❌ Error checking player activity for " + player_id + ": " + std::string(e.what()));
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Error during offline players check: " + std::string(e.what()));
                return;
            }

            if (!offline_players.empty()) {
                LOG_INFO("🧹 Cleaning up " + std::to_string(offline_players.size()) + " offline players");
                
                try {
                    std::unique_lock<std::shared_mutex> lock(players_mutex_);
                    for (const auto& player_id : offline_players) {
                        try {
                            players_.erase(player_id);
                            LOG_INFO("✅ Cleaned up offline player: " + player_id);

                            // 🔧 安全执行回调，防止JSON序列化错误
                            if (player_left_callback_) {
                                try {
                                    player_left_callback_("", player_id); // 空房间ID表示全局离线
                                    LOG_DEBUG("✅ Player left callback executed for: " + player_id);
                                } catch (const nlohmann::json::exception& json_e) {
                                    LOG_ERROR("❌ JSON error in player_left_callback for " + player_id + ": " + std::string(json_e.what()));
                                } catch (const std::exception& callback_e) {
                                    LOG_ERROR("❌ Error in player_left_callback for " + player_id + ": " + std::string(callback_e.what()));
                                }
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("❌ Error cleaning up player " + player_id + ": " + std::string(e.what()));
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Error during offline players cleanup: " + std::string(e.what()));
                }
            } else {
                LOG_DEBUG("✅ No offline players found");
            }
        }

        void GameServerBase::handleGetRoomInfo(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            try {
                std::string room_id = req.getParam("room_id");
                if (room_id.empty()) {
                    res.badRequest("Missing room_id parameter");
                    return;
                }

                std::shared_ptr<GameRoomBase> room;
                {
                    std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
                    auto it = rooms_.find(room_id);
                    if (it == rooms_.end()) {
                        res.notFound("Room not found");
                        return;
                    }
                    room = it->second;
                }

                res.ok(room->getRoomInfo());

            } catch (const std::exception& e) {
                LOG_ERROR("Get room info error: " + std::string(e.what()));
                res.internalServerError("Failed to get room info");
            }
        }

        void GameServerBase::handleLeaveRoom(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            // ⚠️ 此HTTP API已废弃 - 游戏操作应通过WebSocket进行
            LOG_WARNING("Deprecated HTTP API called: POST /rooms/{room_id}/leave - Client should use WebSocket API");

            std::string room_id = req.getParam("room_id");

            // 返回迁移指导信息
            nlohmann::json migration_guide = {
                {"error", "API_DEPRECATED"},
                {"message", "This HTTP API is deprecated. Please use WebSocket API for game operations."},
                {"migration_guide", {
                    {"step1", "Connect to WebSocket: ws://" + config_.host + ":" + std::to_string(config_.websocket_port)},
                    {"step2", "Send authentication message with your game session token"},
                    {"step3", "Send leave_room message through WebSocket"},
                    {"websocket_message_format", {
                        {"type", "leave_room"},
                        {"room_id", room_id.empty() ? "ROOM_ID" : room_id}
                    }}
                }},
                {"websocket_url", "ws://" + config_.host + ":" + std::to_string(config_.websocket_port)},
                {"documentation", "/api/docs/websocket"}
            };

            res.setStatus(410); // 410 Gone
            res.setJsonBody(migration_guide);
            return;

            // 注意：原HTTP实现已移除，所有房间操作现在通过WebSocket进行
        }

        void GameServerBase::handleGetServerStatus(const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            // 🔧 优化：记录HTTP请求性能
            auto start_time = std::chrono::high_resolution_clock::now();
            
            LOG_DEBUG("Starting handleGetServerStatus()");
            
            // 🔧 安全的服务器状态JSON构建
            try {
                auto stats = getGameStats();
                auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now() - start_time_).count();

                nlohmann::json status = {
                    {"server_name", getGameTypeName() + "_server"},
                    {"version", "1.0.0"},
                    {"uptime_seconds", uptime},
                    {"running", running_.load()}
                };
                
                // 🔧 安全添加配置信息
                try {
                    status["config"] = config_.toJson();
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Server status config JSON serialization failed: " + std::string(e.what()));
                    status["config"] = {{"error", "serialization_failed"}};
                }
                
                // 🔧 安全添加统计信息
                try {
                    status["stats"] = stats.toJson();
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Server status stats JSON serialization failed: " + std::string(e.what()));
                    status["stats"] = {{"error", "serialization_failed"}};
                }
                
                // 🔧 安全添加特性信息
                try {
                    status["features"] = {
                        {"qt6_support", config_.qt6_optimizations},
                        {"websocket", config_.enable_websocket},
                        {"leaderboard", config_.enable_leaderboard}
                    };
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Server status features JSON creation failed: " + std::string(e.what()));
                    status["features"] = {{"error", "creation_failed"}};
                }

                res.ok(status);
                LOG_DEBUG("✅ Server status response sent successfully");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Server status failed: " + std::string(e.what()));
                res.internalServerError("Server status failed");
            }
            
            // 🔧 安全记录性能指标，防止JSON序列化错误
            if (performance_monitoring_enabled_) {
                try {
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    
                    // 🔧 验证duration有效性，防止NaN/Inf导致JSON错误
                    auto duration_count = duration.count();
                    if (std::isnan(duration_count) || std::isinf(duration_count) || duration_count < 0) {
                        LOG_WARNING("⚠️ Invalid duration in server status: " + std::to_string(duration_count) + "ms, using 0");
                        duration = std::chrono::milliseconds(0);
                    }
                    
                    LOG_DEBUG("📊 Server status duration: " + std::to_string(duration.count()) + "ms");
                    PERF_HTTP_REQUEST("GET", "/api/v1/status", 200, duration);
                    LOG_DEBUG("✅ Server status performance recorded successfully");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ Failed to record server status performance: " + std::string(e.what()));
                }
            }
        }

        // 🔧 优化：启动基于EventLoop定时器的任务
        void GameServerBase::startTimerBasedTasks(std::shared_ptr<common::network::EventLoop> event_loop) {
            if (!event_loop) {
                LOG_ERROR("GameServerBase: EventLoop cannot be null for timer-based tasks");
                return;
            }

            event_loop_ = event_loop;
            
            LOG_INFO("Starting timer-based tasks for GameServerBase (replacing thread pool loops)");

            // 🔧 修复：使用runInLoop确保在EventLoop线程中执行定时器设置
            event_loop_->runInLoop([this]() {
                try {
                    // 1. 清理定时器（每5分钟执行一次）
                    cleanup_timer_id_ = event_loop_->runEvery(
                300000, // 5分钟
                [this]() {
                    if (!running_.load()) return;
                    LOG_DEBUG("🧹 Starting GameServerBase cleanup (5-minute timer)");
                    try {
                        LOG_DEBUG("🧹 Cleaning up expired rooms...");
                        cleanupExpiredRooms();
                        LOG_DEBUG("✅ Expired rooms cleanup completed");
                        
                        LOG_DEBUG("🧹 Cleaning up offline players...");
                        cleanupOfflinePlayers();
                        LOG_DEBUG("✅ Offline players cleanup completed");
                        
                        LOG_DEBUG("✅ GameServerBase timer-based cleanup completed successfully");
                    } catch (const nlohmann::json::exception& json_e) {
                        LOG_ERROR("❌ JSON error in GameServerBase cleanup: " + std::string(json_e.what()));
                        LOG_ERROR("❌ This indicates JSON serialization with invalid double values");
                    } catch (const std::exception& e) {
                        LOG_ERROR("❌ GameServerBase timer-based cleanup error: " + std::string(e.what()));
                    } catch (...) {
                        LOG_ERROR("❌ GameServerBase timer-based cleanup unknown error");
                    }
                }
            );

            // 2. 统计信息更新定时器（每30秒执行一次）
            stats_update_timer_id_ = event_loop_->runEvery(
                30000, // 30秒
                [this]() {
                    if (!running_.load()) return;
                    LOG_DEBUG("📊 Starting GameServerBase stats update (30-second timer)");
                    try {
                        updateGameStats();
                        LOG_DEBUG("✅ GameServerBase timer-based stats update completed");
                    } catch (const nlohmann::json::exception& json_e) {
                        LOG_ERROR("❌ JSON error in GameServerBase stats update: " + std::string(json_e.what()));
                        LOG_ERROR("❌ This indicates JSON serialization with invalid double values in stats");
                    } catch (const std::exception& e) {
                        LOG_ERROR("❌ GameServerBase timer-based stats update error: " + std::string(e.what()));
                    } catch (...) {
                        LOG_ERROR("❌ GameServerBase timer-based stats update unknown error");
                    }
                }
            );

            // [REMOVED] API Gateway 心跳定时器 - 现在使用 Nginx + Service Registry

            // 检查所有定时器是否启动成功
                    if (cleanup_timer_id_ != 0 && stats_update_timer_id_ != 0) {
                        LOG_INFO("GameServerBase timer-based tasks started successfully:");
                        LOG_INFO("  - Cleanup timer ID: " + std::to_string(cleanup_timer_id_) + " (interval: 5min)");
                        LOG_INFO("  - Stats update timer ID: " + std::to_string(stats_update_timer_id_) + " (interval: 30s)");
                    } else {
                        LOG_ERROR("Failed to start GameServerBase timer-based tasks");
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in GameServerBase timer setup: " + std::string(e.what()));
                } catch (...) {
                    LOG_ERROR("Unknown exception in GameServerBase timer setup");
                }
            });
        }

        // 🔧 优化：停止基于EventLoop定时器的任务
        void GameServerBase::stopTimerBasedTasks() {
            if (!event_loop_) {
                return;
            }

            LOG_INFO("Stopping GameServerBase timer-based tasks");

            if (cleanup_timer_id_ != 0) {
                event_loop_->cancelTimer(cleanup_timer_id_);
                LOG_DEBUG("GameServerBase cleanup timer cancelled: " + std::to_string(cleanup_timer_id_));
                cleanup_timer_id_ = 0;
            }

            if (stats_update_timer_id_ != 0) {
                event_loop_->cancelTimer(stats_update_timer_id_);
                LOG_DEBUG("GameServerBase stats update timer cancelled: " + std::to_string(stats_update_timer_id_));
                stats_update_timer_id_ = 0;
            }

            // [REMOVED] heartbeat_timer_id_ 取消 - API Gateway心跳已移除

            event_loop_.reset();
            LOG_INFO("GameServerBase timer-based tasks stopped");
        }

        // ==================== 新增组件管理方法实现 ====================

        void GameServerBase::updateSecurityConfig(const SecurityConfig& security_config) {
            if (security_manager_) {
                security_manager_->updateConfig(security_config);
                LOG_INFO("🔧 Security configuration updated");
            } else {
                LOG_WARNING("Security manager not available");
            }
        }

        void GameServerBase::updateAlertConfig(const AlertConfig& alert_config) {
            if (alert_manager_) {
                alert_manager_->updateConfig(alert_config);
                LOG_INFO("🔧 Alert configuration updated");
            } else {
                LOG_WARNING("Alert manager not available");
            }
        }

        nlohmann::json GameServerBase::getEnhancedServerStatus() const {
            LOG_DEBUG("Starting getEnhancedServerStatus()");
            
            // 🔧 安全获取游戏统计信息
            GameStats stats;
            try {
                stats = getGameStats();
                LOG_DEBUG("✅ GameStats retrieved successfully");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to get GameStats: " + std::string(e.what()));
                // 使用默认的空统计信息
            }
            
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now() - start_time_).count();

            nlohmann::json status = {
                {"server_name", getGameTypeName() + "_server"},
                {"version", "1.0.0"},
                {"uptime_seconds", uptime},
                {"running", running_.load()}
            };
            
            // 🔧 安全序列化配置信息
            try {
                status["config"] = config_.toJson();
                LOG_DEBUG("✅ Config JSON serialization successful");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Config JSON serialization failed: " + std::string(e.what()));
                status["config"] = {{"error", "serialization_failed"}};
            }
            
            // 🔧 安全序列化统计信息  
            try {
                status["stats"] = stats.toJson();
                LOG_DEBUG("✅ Stats JSON serialization successful");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Stats JSON serialization failed: " + std::string(e.what()));
                status["stats"] = {{"error", "serialization_failed"}};
            }
            
            // 🔧 安全设置特性信息
            try {
                status["features"] = {
                    {"qt6_support", config_.qt6_optimizations},
                    {"websocket", config_.enable_websocket},
                    {"leaderboard", config_.enable_leaderboard},
                    {"performance_monitoring", performance_monitoring_enabled_},
                    {"config_manager", true},  // 🔧 使用统一的ConfigManager
                    {"security_manager", security_manager_ != nullptr},
                    {"alert_manager", alert_manager_ != nullptr}
                };
                LOG_DEBUG("✅ Features JSON creation successful");
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Features JSON creation failed: " + std::string(e.what()));
                status["features"] = {{"error", "creation_failed"}};
            }

            // 🔧 安全处理配置管理器状态（使用统一的ConfigManager）
            try {
                LOG_DEBUG("Getting ConfigManager status...");
                auto& config_manager = common::config::ConfigManager::getInstance();
                auto all_configs = config_manager.getAllConfigs();
                
                // 🔧 防止JSON类型错误：安全获取配置数量
                int config_count = 0;
                try {
                    config_count = static_cast<int>(all_configs.size());
                    LOG_DEBUG("✅ ConfigManager config count: " + std::to_string(config_count));
                } catch (const std::exception& size_error) {
                    LOG_WARNING("Failed to get config count: " + std::string(size_error.what()));
                    config_count = -1;  // 使用-1表示无法获取
                }
                
                status["config_manager_info"] = {
                    {"type", "ConfigManager"},
                    {"available", true},
                    {"loaded_configs_count", config_count}
                };
                LOG_DEBUG("✅ ConfigManager status JSON creation successful");
                
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to get ConfigManager status: " + std::string(e.what()));
                status["config_manager_info"] = {
                    {"type", "ConfigManager"},
                    {"available", false},
                    {"error", e.what()}
                };
            }

            // 🔧 安全处理安全管理器状态
            if (security_manager_) {
                try {
                    LOG_DEBUG("Getting SecurityManager stats...");
                    status["security"] = security_manager_->getSecurityStats();
                    LOG_DEBUG("✅ SecurityManager stats JSON creation successful");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ SecurityManager stats JSON creation failed: " + std::string(e.what()));
                    status["security"] = {{"error", e.what()}};
                }
            }

            // 🔧 安全处理告警管理器状态
            if (alert_manager_) {
                try {
                    LOG_DEBUG("Getting AlertManager stats...");
                    status["alerts"] = alert_manager_->getAlertStats();
                    LOG_DEBUG("✅ AlertManager stats JSON creation successful");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ AlertManager stats JSON creation failed: " + std::string(e.what()));
                    status["alerts"] = {{"error", e.what()}};
                }
            }

            // 🔧 安全处理性能监控数据
            if (performance_monitoring_enabled_) {
                try {
                    LOG_DEBUG("Getting PerformanceMonitor metrics...");
                    auto& perf_monitor = common::monitoring::PerformanceMonitorSingleton::getInstance();
                    status["performance_metrics"] = perf_monitor.getAllMetrics();
                    LOG_DEBUG("✅ PerformanceMonitor metrics JSON creation successful");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ PerformanceMonitor metrics JSON creation failed: " + std::string(e.what()));
                    status["performance_metrics"] = {{"error", e.what()}};
                }
            }

            LOG_DEBUG("getEnhancedServerStatus() completed successfully");
            return status;
        }

        // [REMOVED] sendHeartbeatToApiGateway() - API Gateway 心跳功能已移除
        // 现在使用 Service Registry (端口 8090) 进行服务发现和健康检查


    } // namespace game_base
} // namespace game_services
