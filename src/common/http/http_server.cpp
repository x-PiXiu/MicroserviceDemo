/**
 * @file http_server.cpp
 * @brief HTTP服务器框架的实现
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 * 
 * 实现说明:
 * - 基于事件驱动的异步HTTP服务器
 * - 集成现有的网络、日志、配置、线程池模块
 * - 支持Keep-Alive、中间件、路由等高级功能
 * - 提供完整的错误处理和监控能力
 */

#include "common/http/http_server.h"
#include "common/config/config_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <future>
#include <fcntl.h>
#include <nlohmann/json.hpp>

namespace common {
    namespace http {
        HttpServerConfig HttpServerConfig::fromConfigManager()
        {
            auto& config_manager = common::config::ConfigManager::getInstance();
            HttpServerConfig config_;
            // 加载基础配置
            config_.network_config = common::config::NetworkConfig::fromConfigManager();
            // 加载HTTP协议配置
            config_.max_request_size = config_manager.get<size_t>("http.server.max_request_size",
                                                                  config_.max_request_size);
            config_.max_header_size = config_manager.get<
                size_t>("http.server.max_header_size", config_.max_header_size);
            config_.request_timeout_ms = config_manager.get<int>("http.server.request_timeout_ms",
                                                                 config_.request_timeout_ms);
            config_.keep_alive_timeout_ms = config_manager.get<int>("http.server.keep_alive_timeout_ms",
                                                                    config_.keep_alive_timeout_ms);

            // 加载线程池配置
            config_.worker_threads = config_manager.get<int>("http.server.worker_threads", config_.worker_threads);
            config_.enable_thread_pool = config_manager.get<bool>("http.server.enable_thread_pool",
                                                                  config_.enable_thread_pool);

            // 加载性能配置
            config_.enable_tcp_nodelay = config_manager.get<bool>("http.server.enable_tcp_nodelay",
                                                                  config_.enable_tcp_nodelay);
            config_.send_buffer_size = config_manager.get<
                int>("http.server.send_buffer_size", config_.send_buffer_size);
            config_.recv_buffer_size = config_manager.get<
                int>("http.server.recv_buffer_size", config_.recv_buffer_size);

            // 加载监控配置
            config_.enable_performance_monitoring = config_manager.get<bool>(
                "http.server.enable_performance_monitoring", config_.enable_performance_monitoring);
            config_.stats_interval_ms = config_manager.get<int>("http.server.stats_interval_ms",
                                                                config_.stats_interval_ms);
            
            // ⭐⭐⭐ 阶段2性能优化配置（从配置文件读取）
            config_.session_shard_count = config_manager.get<size_t>("http.server.session_shard_count", 
                                                                    config_.session_shard_count);
            config_.route_cache_size = config_manager.get<size_t>("http.server.route_cache_size", 
                                                                  config_.route_cache_size);
            
            // 读取轻量级请求路径列表（使用get<std::vector<std::string>>读取逗号分隔的字符串）
            try {
                config_.lightweight_paths = config_manager.get<std::vector<std::string>>(
                    "http.server.lightweight_paths", std::vector<std::string>{});
                if (!config_.lightweight_paths.empty()) {
                    LOG_INFO("✅ 加载轻量级请求路径: " + std::to_string(config_.lightweight_paths.size()) + "条");
                } else {
                    // 如果配置为空，使用默认值
                    config_.lightweight_paths = {
                        "/api/v1/health", "/api/v1/ping", "/api/v1/status",
                        "/health", "/ping", "/ready", "/alive", "/metrics"
                    };
                    LOG_INFO("⚠️  配置lightweight_paths为空，使用默认值: " + 
                             std::to_string(config_.lightweight_paths.size()) + "条");
                }
            } catch (const std::exception& e) {
                // 如果配置文件没有该配置或读取失败，使用默认值
                config_.lightweight_paths = {
                    "/api/v1/health", "/api/v1/ping", "/api/v1/status",
                    "/health", "/ping", "/ready", "/alive", "/metrics"
                };
                LOG_WARNING("⚠️  读取lightweight_paths配置失败：" + std::string(e.what()) + 
                           ", 使用默认值: " + std::to_string(config_.lightweight_paths.size()) + "条");
            }

            // LOG_DEBUG("Configuration loaded from ConfigManager");
            return config_;
        }

        HttpServer::HttpServer(const HttpServerConfig& config)
            : config_(config)
            , running_(false)
            , stopping_(false) {
            
            if (!config_.validate()) {
                throw std::invalid_argument("Invalid HttpServer configuration");
            }
            
            // 初始化组件
            router_ = std::make_unique<HttpRouter>();
            middleware_manager_ = std::make_unique<MiddlewareManager>();
            
            // 创建线程池
            if (config_.enable_thread_pool) {
                auto pool_config = common::config::ThreadPoolConfig::fromConfigManager();
                pool_config.core_pool_size = (config_.worker_threads > 0) ? 
                    config_.worker_threads : std::thread::hardware_concurrency();

                pool_config.maximum_pool_size = pool_config.core_pool_size * 2;
                
                thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(pool_config);
            }
            
            setupDefaultHandlers();

            LOG_INFO("HttpServer created with config:");
            LOG_INFO("  - Host: " + config_.network_config.bind_address);
            LOG_INFO("  - Port: " + std::to_string(config_.network_config.listen_port));
            LOG_INFO("  - Worker threads: " + std::to_string(config_.worker_threads));
            LOG_INFO("  - Reuse addr: " + std::string(config_.network_config.so_reuseaddr ? "true" : "false"));
            LOG_INFO("  - Reuse port: " + std::string(config_.network_config.so_reuseport ? "true" : "false"));
        }

        HttpServer::HttpServer(const HttpServerConfig& config, std::shared_ptr<common::thread_pool::ThreadPool> thread_pool)
            : config_(config)
            , running_(false)
            , stopping_(false)
            , thread_pool_(thread_pool) {

            if (!config_.validate()) {
                throw std::invalid_argument("Invalid HttpServer configuration");
            }

            // 初始化组件
            router_ = std::make_unique<HttpRouter>();
            middleware_manager_ = std::make_unique<MiddlewareManager>();

            // 使用外部提供的线程池，不创建新的
            if (!thread_pool_) {
                throw std::invalid_argument("External thread pool cannot be null");
            }

            setupDefaultHandlers();
        }

        HttpServer::HttpServer(const std::string& host, int port)
            : HttpServer(HttpServerConfig{}) {
            config_.network_config.bind_address = host;
            config_.network_config.listen_port = port;
        }

        HttpServer::~HttpServer() {
            try {
                if (running_.load()) {
                    stop();
                }
                // 避免在析构时使用日志系统，使用标准输出
                std::cout << "[HttpServer] HttpServer destroyed" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[HttpServer] Exception in destructor: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[HttpServer] Unknown exception in destructor" << std::endl;
            }
        }

        bool HttpServer::initialize() {
            try {
                // 检查是否已经初始化
                if (event_loop_) {
                    return true;
                }

                // 创建监听地址
                listen_address_ = std::make_unique<common::network::InetAddress>(config_.network_config.bind_address, config_.network_config.listen_port);

                // 创建监听套接字
                listen_socket_ = std::make_unique<common::network::Socket>();

                // 设置套接字选项
                if (config_.network_config.so_reuseaddr || config_.enable_so_reuseaddr) {
                    listen_socket_->setReuseAddr(true);
                }
                if (config_.network_config.so_reuseport) {
                    listen_socket_->setReusePort(true);
                }
                if (config_.enable_tcp_nodelay) {
                    listen_socket_->setTcpNoDelay(true);
                }
                if (config_.enable_keep_alive) {
                    listen_socket_->setKeepAlive(true);
                }

                // 设置SO_LINGER以支持立即重启
                if (config_.enable_so_linger) {
                    // 通过Socket类的applySocketOptions方法设置
                    // 这里我们需要直接设置SO_LINGER
                    struct linger linger_opt;
                    linger_opt.l_onoff = 1;  // 启用linger
                    linger_opt.l_linger = config_.linger_timeout;  // 超时时间

                    if (::setsockopt(listen_socket_->fd(), SOL_SOCKET, SO_LINGER,
                                   &linger_opt, sizeof(linger_opt)) < 0) {
                        LOG_WARNING("Failed to set SO_LINGER: " + std::string(strerror(errno)));
                    }
                }

                // 绑定地址并开始监听
                listen_socket_->bindAddress(*listen_address_);
                listen_socket_->listen();

                // 设置监听socket为非阻塞模式，避免accept阻塞EventLoop
                int flags = fcntl(listen_socket_->fd(), F_GETFL, 0);
                if (flags == -1) {
                    LOG_ERROR("Failed to get socket flags: " + std::string(strerror(errno)));
                    throw std::runtime_error("Failed to get socket flags");
                }
                if (fcntl(listen_socket_->fd(), F_SETFL, flags | O_NONBLOCK) == -1) {
                    LOG_ERROR("Failed to set socket non-blocking: " + std::string(strerror(errno)));
                    throw std::runtime_error("Failed to set socket non-blocking");
                }

                auto network_config = common::network::networkConfig::fromConfigManager();

                if (thread_pool_) {
                    // 使用配置和线程池构造EventLoop（改为shared_ptr支持共享）
                    event_loop_ = std::make_shared<common::network::EventLoop>(network_config, thread_pool_);
                } else {
                    // 使用配置构造EventLoop（改为shared_ptr支持共享）
                    event_loop_ = std::make_shared<common::network::EventLoop>(network_config);
                }

                int listen_fd = listen_socket_->fd();
                listen_channel_ = std::make_unique<common::network::Channel>(event_loop_.get(), listen_fd);
                listen_channel_->setReadCallback([this] { handleNewConnection(); });
                listen_channel_->enableReading();

                // 初始化中间件管理器
                if (middleware_manager_ && !middleware_manager_->initialize()) {
                    LOG_ERROR("Failed to initialize middleware manager");
                    return false;
                }
                
                // ⭐⭐⭐ 阶段2优化2: 设置路由缓存大小（从配置加载）
                if (router_ && config_.route_cache_size > 0) {
                    router_->setMaxCacheSize(config_.route_cache_size);
                    LOG_INFO("✅ 路由缓存大小已设置为: " + std::to_string(config_.route_cache_size));
                }

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize HttpServer: " + std::string(e.what()));
                return false;
            }
        }


        bool HttpServer::start() {
            if (running_.load()) {
                LOG_WARNING("HttpServer is already running");
                return false;
            }

            if (!initialize()) {
                LOG_ERROR("Failed to initialize HttpServer");
                return false;
            }

            running_.store(true);
            stopping_.store(false);

            // 确保EventLoop已启动
            if (!event_loop_) {
                LOG_ERROR("EventLoop not initialized");
                return false;
            }

            // 添加一个小的延迟，确保服务器完全准备好
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // 启动定时器替代统计线程
            if (config_.enable_performance_monitoring) {
                startTimerBasedMonitoring();
            }
            event_loop_->loop();

            return true;
        }

        /**
         * @brief 仅运行事件循环（用于分离初始化和运行）
         * @details 配合initialize()使用，允许在事件循环运行前执行其他初始化
         *          主要用于WebSocket等需要在EventLoop创建后、运行前初始化的组件
         */
        void HttpServer::startEventLoop() {
            if (!event_loop_) {
                LOG_ERROR("EventLoop not initialized, cannot start");
                return;
            }

            if (running_.load()) {
                LOG_WARNING("HttpServer is already running");
                return;
            }

            running_.store(true);
            stopping_.store(false);

            LOG_INFO("Starting HTTP server event loop...");

            // 启动定时器替代统计线程
            if (config_.enable_performance_monitoring) {
                startTimerBasedMonitoring();
            }

            // 运行事件循环（阻塞）
            event_loop_->loop();

            LOG_INFO("HTTP server event loop exited");
        }

        void HttpServer::stop() {
            if (!running_.load()) {
                return;
            }

            stopping_.store(true);
            running_.store(false);

            LOG_INFO("正在停止HTTP服务器...");

            // 停止定时器监控
            stopTimerBasedMonitoring();

            // 1. 首先请求 EventLoop 退出（这是线程安全的）
            // quit() 会设置 quit_ = true 并 wakeup() EventLoop
            if (event_loop_) {
                event_loop_->quit();
            }

            // 2. 关闭监听 socket（可以在任何线程执行，会中断 accept()）
            if (listen_socket_) {
                try {
                    listen_socket_->forceClose();
                } catch (const std::exception& e) {
                    LOG_ERROR("关闭监听socket异常: " + std::string(e.what()));
                }
                listen_socket_.reset();
            }

            // 3. 尝试在 EventLoop 线程中安全关闭 listen_channel
            // 即使 EventLoop 已经退出，我们也需要清理 channel
            if (event_loop_ && listen_channel_) {
                try {
                    // 使用 queueInLoop 而不是 runInLoop，不等待执行完成
                    // 因为 quit() 已经调用，EventLoop 会在退出前执行所有 pending functors
                    event_loop_->queueInLoop([this]() {
                        if (listen_channel_) {
                            try {
                                listen_channel_->disableAll();
                                listen_channel_->remove();
                            } catch (const std::exception& e) {
                                LOG_ERROR("清理listen_channel异常: " + std::string(e.what()));
                            }
                            listen_channel_.reset();
                        }
                    });
                } catch (const std::exception& e) {
                    // 如果 queueInLoop 失败（如 EventLoop 已销毁），直接 reset
                    LOG_WARNING("queueInLoop失败，直接reset listen_channel: " + std::string(e.what()));
                    listen_channel_.reset();
                }
            }

            // 4. 关闭所有会话
            {
                std::vector<std::shared_ptr<HttpSession>> sessions_to_close;
                {
                    for (auto& shard : session_shards_) {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        for (auto& [id, session] : shard.sessions) {
                            if (session) {
                                sessions_to_close.push_back(session);
                            }
                        }
                        shard.sessions.clear();
                    }
                }

                LOG_INFO("正在关闭 " + std::to_string(sessions_to_close.size()) + " 个HTTP会话...");
                for (auto& session : sessions_to_close) {
                    try {
                        session->close();
                    } catch (const std::exception& e) {
                        LOG_ERROR("关闭会话异常: " + std::string(e.what()));
                    }
                }
            }

            // 5. 等待 EventLoop 退出（最多等待 2 秒）
            if (event_loop_) {
                int wait_count = 0;
                while (event_loop_->isLooping() && wait_count < 20) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    wait_count++;
                }
                if (event_loop_->isLooping()) {
                    LOG_WARNING("EventLoop未能在2秒内停止");
                } else {
                    LOG_INFO("EventLoop已停止");
                }
            }

            LOG_INFO("HTTP服务器已停止");
            // 不关闭线程池，由创建者负责管理
        }

        void HttpServer::waitForStop() {
            while (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        void HttpServer::route(const std::string& method, const std::string& path, RequestHandler handler) {
            if (!router_) {
                LOG_ERROR("Router not initialized");
                return;
            }
            
            router_->addRoute(method, path, handler);
        }

        void HttpServer::get(const std::string& path, RequestHandler handler) {
            route("GET", path, handler);
        }

        void HttpServer::post(const std::string& path, RequestHandler handler) {
            route("POST", path, handler);
        }

        void HttpServer::put(const std::string& path, RequestHandler handler) {
            route("PUT", path, handler);
        }

        void HttpServer::del(const std::string& path, RequestHandler handler) {
            route("DELETE", path, handler);
        }

        void HttpServer::patch(const std::string& path, RequestHandler handler) {
            route("PATCH", path, handler);
        }

        void HttpServer::options(const std::string& path, RequestHandler handler) {
            route("OPTIONS", path, handler);
        }

        void HttpServer::use(MiddlewareHandler middleware) {
            if (!middleware_manager_) {
                LOG_ERROR("Middleware manager not initialized");
                return;
            }

            // 将MiddlewareHandler转换为MiddlewareFunction
            // MiddlewareHandler: bool(const HttpRequest&, HttpResponse&)
            // MiddlewareFunction: bool(const HttpRequest&, HttpResponse&, NextFunction)
            //
            // NextFunction 是一个闭包，由中间件管理器创建，包含了执行下一个中间件的所有上下文
            // 格式: NextFunction = std::function<bool()>
            // 实际内容: [manager, &request, &response, &context, next_index, final_handler]() -> bool
            auto middleware_func = [middleware](const HttpRequest& request, HttpResponse& response, NextFunction next) -> bool {
                // 执行原始的MiddlewareHandler
                bool result = middleware(request, response);

                // 如果中间件返回true，调用next()继续执行下一个中间件
                // next() 是一个无参数函数，内部自动处理所有复杂的中间件链逻辑
                if (result && next) {
                    return next(); // 这里的next已经包含了所有必要的上下文信息
                }

                return result;
            };

            middleware_manager_->use(middleware_func, "CustomMiddleware");
            LOG_INFO("Added global middleware");
        }

        void HttpServer::use(MiddlewareHandler middleware, std::string middleName)
        {
            if (!middleware_manager_) {
                LOG_ERROR("Middleware manager not initialized");
                return;
            }

            // 将MiddlewareHandler转换为MiddlewareFunction
            // MiddlewareHandler: bool(const HttpRequest&, HttpResponse&)
            // MiddlewareFunction: bool(const HttpRequest&, HttpResponse&, NextFunction)
            //
            // NextFunction 是一个闭包，由中间件管理器创建，包含了执行下一个中间件的所有上下文
            // 格式: NextFunction = std::function<bool()>
            // 实际内容: [manager, &request, &response, &context, next_index, final_handler]() -> bool
            auto middleware_func = [middleware](const HttpRequest& request, HttpResponse& response, NextFunction next) -> bool {
                // 执行原始的MiddlewareHandler
                bool result = middleware(request, response);

                // 如果中间件返回true，调用next()继续执行下一个中间件
                // next() 是一个无参数函数，内部自动处理所有复杂的中间件链逻辑
                if (result && next) {
                    return next(); // 这里的next已经包含了所有必要的上下文信息
                }

                return result;
            };

            middleware_manager_->use(middleware_func, middleName);
            LOG_INFO("Added middleware " + middleName);
        }

        void HttpServer::use(const std::string& path, MiddlewareHandler middleware) {
            if (!router_) {
                LOG_ERROR("Router not initialized");
                return;
            }

            // 将MiddlewareHandler转换为路由器期望的MiddlewareHandler类型
            // 注意：HttpRouter::addMiddleware期望的是相同的MiddlewareHandler类型
            // 所以这里可以直接传递
            router_->addMiddleware(path, middleware);
            LOG_INFO("Added middleware for path: " + path);
        }

        void HttpServer::serveStatic(const std::string& path, const std::string& directory) {
            // 完整的静态文件服务实现
            // 支持文件类型检测、缓存控制、安全检查、范围请求、目录浏览等功能

            if (!router_) {
                LOG_ERROR("Router not initialized for static file service");
                return;
            }

            // 标准化目录路径
            std::string normalized_dir = normalizeDirectoryPath(directory);

            // 验证目录是否存在
            if (!directoryExists(normalized_dir)) {
                LOG_ERROR("Static directory does not exist: " + normalized_dir);
                return;
            }

            // 验证目录读取权限
            if (!hasDirectoryReadPermission(normalized_dir)) {
                LOG_ERROR("No read permission for static directory: " + normalized_dir);
                return;
            }

            // 注册GET请求的静态文件路由处理器
            router_->addRoute("GET", path + "/*", [this, normalized_dir, path](const HttpRequest& req, HttpResponse& res) {
                try {
                    handleStaticFileRequest(req, res, normalized_dir, path, false);
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in static file handler: " + std::string(e.what()));
                    res.internalServerError("Internal server error");
                }
            });

            // 注册HEAD请求的静态文件路由处理器（用于检查文件是否存在，不返回内容）
            router_->addRoute("HEAD", path + "/*", [this, normalized_dir, path](const HttpRequest& req, HttpResponse& res) {
                try {
                    handleStaticFileRequest(req, res, normalized_dir, path, true); // HEAD请求
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in static file HEAD handler: " + std::string(e.what()));
                    res.internalServerError("Internal server error");
                }
            });

            // LOG_DEBUG("Added static file service: " + path + " -> " + normalized_dir);
            // LOG_DEBUG("Static file service features: MIME detection, caching, range requests, directory listing");
        }

        void HttpServer::setErrorHandler(ErrorHandler handler) {
            error_handler_ = handler;
            // LOG_DEBUG("Error handler set");
        }

        void HttpServer::setNotFoundHandler(RequestHandler handler) {
            not_found_handler_ = handler;
            LOG_INFO("404 handler set");
        }

        void HttpServer::updateConfig(const HttpServerConfig& config) {
            if (!config.validate()) {
                LOG_ERROR("Invalid configuration provided");
                return;
            }
            
            config_ = config;
            LOG_INFO("Configuration updated");
        }

        void HttpServer::loadConfigFromManager() {
            try {
                auto& config_manager = common::config::ConfigManager::getInstance();
                
                // 加载基础配置
                config_.network_config = common::config::NetworkConfig::fromConfigManager();
                // 加载HTTP协议配置
                config_.max_request_size = config_manager.get<size_t>("http.server.max_request_size", config_.max_request_size);
                config_.max_header_size = config_manager.get<size_t>("http.server.max_header_size", config_.max_header_size);
                config_.request_timeout_ms = config_manager.get<int>("http.server.request_timeout_ms", config_.request_timeout_ms);
                config_.keep_alive_timeout_ms = config_manager.get<int>("http.server.keep_alive_timeout_ms", config_.keep_alive_timeout_ms);
                
                // 加载线程池配置
                config_.worker_threads = config_manager.get<int>("http.server.worker_threads", config_.worker_threads);
                config_.enable_thread_pool = config_manager.get<bool>("http.server.enable_thread_pool", config_.enable_thread_pool);
                
                // 加载性能配置
                config_.enable_tcp_nodelay = config_manager.get<bool>("http.server.enable_tcp_nodelay", config_.enable_tcp_nodelay);
                config_.send_buffer_size = config_manager.get<int>("http.server.send_buffer_size", config_.send_buffer_size);
                config_.recv_buffer_size = config_manager.get<int>("http.server.recv_buffer_size", config_.recv_buffer_size);
                
                // 加载监控配置
                config_.enable_performance_monitoring = config_manager.get<bool>("http.server.enable_performance_monitoring", config_.enable_performance_monitoring);
                config_.stats_interval_ms = config_manager.get<int>("http.server.stats_interval_ms", config_.stats_interval_ms);
                
                // LOG_DEBUG("Configuration loaded from ConfigManager");
                
                // 同时加载中间件配置
                if (middleware_manager_) {
                    middleware_manager_->loadConfigFromManager();
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to load configuration from ConfigManager: " + std::string(e.what()));
            }
        }



        void HttpServer::handleNewConnection() {
            try {
                // 循环处理所有等待的连接，避免重复触发回调
                int accept_count = 0;
                const int max_accept_per_loop = 100;  // 每次最多处理100个连接，避免饿死其他事件
                
                while (accept_count < max_accept_per_loop) {
                    common::network::InetAddress client_addr;
                    int client_fd = listen_socket_->accept(&client_addr);

                    if (client_fd < 0) {
                        // 没有更多连接等待处理（包括EAGAIN和EMFILE情况）
                        break;
                    }
                    
                    accept_count++;

                    // LOG_DEBUG("=== 接受新连接 ===");
                    // LOG_DEBUG("客户端地址: " + client_addr.toIpPort() + " -> 客户端文件描述符: " + std::to_string(client_fd));
                    
                    // ⭐⭐⭐ 新增：黑名单检查
                    std::string client_ip = client_addr.toIp();
                    if (common::utils::IPBlacklistManager::getInstance().isBlacklisted(client_ip)) {
                        LOG_WARNING("Blocked connection from blacklisted IP: " + client_ip);
                        ::close(client_fd);  // 直接关闭连接
                        stats_.reject_count++;  // 统计被拒绝的连接
                        continue;  // 继续处理下一个连接
                    }
                    
                    stats_.total_connections++;
                    stats_.active_connections++;
                    // LOG_DEBUG("当前连接统计 - 总连接数: " + std::to_string(stats_.total_connections) +
                    //         ", 活跃连接数: " + std::to_string(stats_.active_connections));

                    // 设置非阻塞
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                    // 创建会话 ⭐ 修复：传递缓冲区配置参数
                    auto session = std::make_shared<HttpSession>(
                        client_fd, 
                        client_addr, 
                        event_loop_.get(),
                        config_.recv_buffer_size,    // 接收缓冲区大小
                        config_.send_buffer_size,    // 发送缓冲区大小
                        config_.max_request_size     // 最大请求大小
                    );

                    // LOG_DEBUG("[handleNewConnection 1] Session Used " + std::to_string(client_fd) +  " num " + std::to_string(session.use_count()));

                    // 🔧 修复：根据配置决定是否启用Keep-Alive
                    session->setKeepAliveEnabled(config_.enable_keep_alive);

                    // LOG_WARNING("[handleNewConnection 4] Session Used " + std::to_string(client_fd) +  " num " + std::to_string(session.use_count()));

                    // 设置请求处理器
                    session->setRequestHandler([this](HttpRequest& req, HttpResponse& res) {
                        handleHttpRequest(req, res);
                    });

                    // LOG_DEBUG("[handleNewConnection 2] Session Used " + std::to_string(client_fd) +  " num " + std::to_string(session.use_count()));

                    // 设置关闭回调
                    session->setCloseCallback([this](std::shared_ptr<HttpSession> s) {
                        cleanupSession(s->getId());
                    });

                    // 配置会话
                    session->setReadTimeout(config_.request_timeout_ms);
                    session->setKeepAliveTimeout(config_.keep_alive_timeout_ms);
                    session->setMaxKeepAliveRequests(config_.max_keep_alive_requests);

                    // ⭐⭐⭐ 阶段2优化1: 使用分段锁保存会话
                    {
                        auto& shard = session_shards_[getShardIndex(client_fd)];
                        std::lock_guard<std::mutex> lock(shard.mutex);  // ✅ 只锁1/64的锁
                        shard.sessions[client_fd] = session;
                        shard.session_count.fetch_add(1);  // 原子递增
                    }

                    // 启动会话
                    event_loop_->runInLoop([session]{
                        if (session) {
                            session->start();
                        }
                    });
                    // LOG_DEBUG("[handleNewConnection 3] Session Used " + std::to_string(client_fd) +  " num " + std::to_string(session.use_count()));

                } // 结束while循环

            } catch (const std::exception& e) {
                LOG_ERROR("Exception in handleNewConnection: " + std::string(e.what()));
            }
        }

        void HttpServer::handleHttpRequest(HttpRequest& request, HttpResponse& response) {
            try {
                // LOG_DEBUG("=== 处理HTTP请求 ===");
                stats_.total_requests++;

                // ⭐⭐⭐ 新增：HTTP层黑名单检查（获取真实客户端IP）
                std::string client_ip = request.getClientIP();
                if (common::utils::IPBlacklistManager::getInstance().isBlacklisted(client_ip)) {
                    LOG_WARNING("HTTP层黑名单拦截: " + client_ip + " -> " + request.getPath());
                    response.setStatus(403);
                    response.setHeader("Content-Type", "application/json");
                    nlohmann::json error_response;
                    error_response["success"] = false;
                    error_response["message"] = "Access denied: IP blocked";
                    error_response["client_ip"] = client_ip;
                    response.setBody(error_response.dump());
                    stats_.reject_count++;  // 统计被拒绝的请求
                    return;  // 直接返回，不继续处理
                }

                // 执行中间件
                if (middleware_manager_) {
                    bool continue_processing = middleware_manager_->execute(request, response, [this, &request, &response]() {
                        // 中间件执行完成后的处理逻辑
                        processRequest(request, response);
                    });
                    // LOG_INFO("中间件执行完成后的处理逻辑: " + request.getPath());

                    if (!continue_processing) {
                        LOG_WARNING("handleHttpRequest() -> 中间件已处理响应或发生错误!");
                        return; // 中间件已处理响应或发生错误
                    }
                } else {
                    // 没有中间件，直接处理请求
                    processRequest(request, response);
                }

                stats_.total_responses++;

                // 记录访问日志（包含查询参数）
                std::string log_url = request.getPath();
                const std::string& query_string = request.getQuery();
                if (!query_string.empty()) {
                    log_url += "?" + query_string;
                }
                LOG_INFO("HTTP " + request.getMethodString() + " " +
                    log_url + " -> " + std::to_string(response.getStatus()));

            } catch (const std::exception& e) {
                stats_.error_count++;

                if (error_handler_) {
                    error_handler_(e, request, response);
                } else {
                    LOG_ERROR("Unhandled exception in request processing: " + std::string(e.what()));
                    response.internalServerError("Internal server error");
                }
            }
        }

        void HttpServer::processRequest(HttpRequest& request, HttpResponse& response) {
            // 路由匹配
            auto match = router_->match(request.getMethodString(), request.getPath());
            if (match.found && match.handler) {
                // LOG_DEBUG("找到匹配的路由处理器: " + match.matched_pattern);

                if (!match.params.empty()) {
                    request.setRouteParams(match.params);
                }
                
                // 执行处理器
                match.handler->handle(request, response);
                // LOG_DEBUG("路由处理器执行完成，响应状态码: " + std::to_string(response.getStatus()));
            } else {
                LOG_WARNING("未找到匹配的路由: " + request.getMethodString() + " " + request.getPath());
                // 404处理
                if (not_found_handler_) {
                    not_found_handler_(request, response);
                } else {
                    response.notFound("Resource not found");
                }
            }
        }

        void HttpServer::cleanupSession(int session_id) {
            // LOG_DEBUG("cleanupSession() 被调用: session_id=" + std::to_string(session_id));

            // ⭐⭐⭐ 阶段2优化1: 使用分段锁清理会话
            auto& shard = session_shards_[getShardIndex(session_id)];
            std::lock_guard<std::mutex> lock(shard.mutex);  // ✅ 只锁1/64的锁

            auto it = shard.sessions.find(session_id);
            if (it != shard.sessions.end()) {
                // 获取会话的详细信息用于日志
                auto session = it->second;

                shard.sessions.erase(it);
                shard.session_count.fetch_sub(1);  // 原子递减
                stats_.active_connections--;
                // LOG_DEBUG("Session Used " + std::to_string(session_id) +  " num " + std::to_string(session.use_count()));

            } else {
                LOG_WARNING("尝试清理不存在的会话: session_id=" + std::to_string(session_id) +
                           ", 当前活跃会话数: " + std::to_string(getActiveSessionCount()));
            }
        }

        void HttpServer::statsThreadFunc() {
            while (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.stats_interval_ms));

                if (!running_.load()) break;

                // 只在有活动时输出统计信息
                if (stats_.active_connections > 0 || stats_.total_requests > 0) {
                    auto stats_json = stats_.toJson();
                    LOG_INFO("Server stats: " + stats_json.dump());
                }

                // 检查超时会话
                std::vector<int> timeout_sessions;
                {
                    // ⭐⭐⭐ 阶段2优化1: 遍历所有分段收集超时会话
                    for (auto& shard : session_shards_) {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        for (const auto& [id, session] : shard.sessions) {
                            if (session && session->isTimeout()) {
                                timeout_sessions.push_back(id);
                            }
                        }
                    }
                }

                // 清理超时会话（避免在EventLoop任务中持有锁）
                for (int id : timeout_sessions) {
                    // ⭐⭐⭐ 阶段2优化1: 从对应的分段获取会话
                    if (event_loop_) {
                        event_loop_->runInLoop([this, id]() {
                            // ⭐⭐⭐ 阶段2优化1: 使用分段锁获取会话
                            std::shared_ptr<HttpSession> session_to_close;
                            {
                                auto& shard = session_shards_[getShardIndex(id)];
                                std::lock_guard<std::mutex> lock(shard.mutex);
                                auto it = shard.sessions.find(id);
                                if (it != shard.sessions.end() && it->second) {
                                    session_to_close = it->second;
                                }
                            }

                            // 在锁外关闭会话，避免死锁
                            if (session_to_close) {
                                session_to_close->close();
                                stats_.timeout_count++;
                                // LOG_DEBUG("Timeout session closed: " + std::to_string(id));
                            }
                        });
                    }
                }
            }
        }


        //  架构优化：集成到EventLoop的时间轮（推荐方案）
        void HttpServer::startTimerBasedMonitoring() {
            
            try {
                if (config_.stats_interval_ms > 0 && config_.enable_performance_monitoring) {
                    
                    // 🎯 方案1：集成到EventLoop时间轮（架构最优）
                    if (event_loop_) {
                        
                        try {
                            // ✅ 使用EventLoop的内置时间轮（通过runInLoop投递）
                            event_loop_->runInLoop([this]() {
                                
                                stats_timer_id_ = event_loop_->runEvery(
                                    config_.stats_interval_ms,
                                    [this]() {
                                        try {
                                            logPerformanceStats();
                                        } catch (const std::exception& e) {
                                            LOG_WARNING("⚠️ 性能统计异常: " + std::string(e.what()));
                                        }
                                    }
                                );
                                
                                // 添加连接超时检查定时器
                                if (config_.request_timeout_ms > 0) {
                                    timeout_check_timer_id_ = event_loop_->runEvery(
                                        config_.request_timeout_ms / 2,
                                        [this]() {
                                            try {
                                                checkConnectionTimeouts();
                                            } catch (const std::exception& e) {
                                                LOG_WARNING("⚠️ 连接检查异常: " + std::string(e.what()));
                                            }
                                        }
                                    );
                                }

                            });

                            is_use_stats_thread_.store(false);

                            LOG_INFO("✅ HTTP监控系统已集成到EventLoop");
                            return;
                            
                        } catch (const std::exception& e) {
                            LOG_WARNING("⚠️ EventLoop时间轮集成失败: " + std::string(e.what()) + "，尝试独立时间轮");
                        }
                    }
                    
                    // 🎯 方案2：线程模式（兜底方案）
                    startThreadBasedMonitoring();
                    
                } else {
                    LOG_INFO("📊 性能监控已禁用（间隔=" + std::to_string(config_.stats_interval_ms) + 
                            "ms，启用标志=" + (config_.enable_performance_monitoring ? "true" : "false") + "）");
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("❌ HTTP服务器监控启动失败: " + std::string(e.what()));
                stats_timer_id_ = 0;
                timeout_check_timer_id_ = 0;
            }
        }

        // 🔧 性能统计方法 - 使用真实统计数据
        void HttpServer::logPerformanceStats() {
            try {
                auto active_conn = stats_.active_connections.load();
                auto total_conn = stats_.total_connections.load();
                auto total_req = stats_.total_requests.load();
                auto total_resp = stats_.total_responses.load();
                auto error_count = stats_.error_count.load();
                auto timeout_count = stats_.timeout_count.load();
                auto reject_count = stats_.reject_count.load();
                auto uptime = stats_.getUptimeSeconds();
                
                // 计算平均QPS
                double avg_qps = uptime > 0 ? static_cast<double>(total_req) / uptime : 0.0;
                
                // 计算成功率
                double success_rate = total_req > 0 ? 
                    (1.0 - static_cast<double>(error_count) / total_req) * 100.0 : 100.0;
                
                // 计算当前连接使用率（假设最大1000连接）
                double conn_usage = static_cast<double>(active_conn) / 1000.0 * 100.0;
                
                std::string stats_info = "📊 HTTP统计: 活跃连接=" + std::to_string(active_conn) + 
                                       " (" + std::to_string(static_cast<int>(conn_usage)) + "%)" +
                                       ", 总连接=" + std::to_string(total_conn) +
                                       ", 总请求=" + std::to_string(total_req) +
                                       ", 总响应=" + std::to_string(total_resp) +
                                       ", 错误数=" + std::to_string(error_count) +
                                       ", 超时数=" + std::to_string(timeout_count) +
                                       ", 拒绝数=" + std::to_string(reject_count) +
                                       ", 成功率=" + std::to_string(static_cast<int>(success_rate)) + "%" +
                                       ", 平均QPS=" + std::to_string(static_cast<int>(avg_qps)) +
                                       ", 运行时间=" + std::to_string(uptime) + "秒";
                LOG_INFO(stats_info);
                
                // 如果有异常情况，记录警告
                if (error_count > total_req * 0.05) { // 错误率超过5%
                    LOG_WARNING("⚠️ 错误率较高: " + std::to_string(static_cast<int>((static_cast<double>(error_count) / total_req) * 100)) + "%");
                }
                if (active_conn > 8000) { // 连接数超过80%
                    LOG_WARNING("⚠️ 连接数接近上限: " + std::to_string(active_conn) + "/" + std::to_string(config_.max_keep_alive_requests));
                }
                
            } catch (const std::exception& e) {
                LOG_WARNING("⚠️ 性能统计异常: " + std::string(e.what()));
            }
        }
        
        // 🔧 连接超时检查方法 - 改进实现
        void HttpServer::checkConnectionTimeouts() {
            try {
                std::vector<std::pair<int, std::shared_ptr<HttpSession>>> expired_sessions;
                
                {
                    // ⭐⭐⭐ 阶段2优化1: 使用分段锁遍历所有会话
                    for (auto& shard : session_shards_) {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        for (auto& pair : shard.sessions) {
                            auto& session = pair.second;
                            if (session) {
                                // ⭐⭐⭐ 修复：使用 isTimeout() 方法判断，基于最后IO时间
                                // 而不是从创建时间计算，避免误杀活跃的 Keep-Alive 连接
                                if (session->isTimeout()) {
                                    // ⚠️ 修复：保存session的shared_ptr，避免过早释放
                                    expired_sessions.emplace_back(pair.first, session);
                                }
                            }
                        }
                    }
                }
                
                // ⚠️ 修复：通过EventLoop异步清理，避免直接操作可能正在使用的session
                for (auto& [session_id, session] : expired_sessions) {
                    if (event_loop_) {
                        event_loop_->runInLoop([this, session_id, session]() {
                            // ⭐⭐⭐ 阶段2优化1: 使用分段锁获取会话
                            std::shared_ptr<HttpSession> current_session;
                            {
                                auto& shard = session_shards_[getShardIndex(session_id)];
                                std::lock_guard<std::mutex> lock(shard.mutex);
                                auto it = shard.sessions.find(session_id);
                                if (it != shard.sessions.end() && it->second == session) {
                                    current_session = it->second;
                                }
                            }
                            
                            // 在锁外安全关闭session
                            if (current_session) {
                                try {
                                    current_session->close();
                                    stats_.timeout_count.fetch_add(1);
                                    // LOG_DEBUG("Timeout session closed: " + std::to_string(session_id));
                                } catch (const std::exception& e) {
                                    LOG_ERROR("关闭超时会话异常: " + std::string(e.what()) + 
                                             ", session_id=" + std::to_string(session_id));
                                }
                            }
                        });
                    }
                }
                
                if (!expired_sessions.empty()) {
                    LOG_INFO("🧹 清理超时连接: " + std::to_string(expired_sessions.size()) + "个");
                }
                
            } catch (const std::exception& e) {
                LOG_WARNING("⚠️ 连接检查异常: " + std::string(e.what()));
            }
        }

        // 🔧 优化：停止监控系统（统一入口）
        void HttpServer::stopTimerBasedMonitoring() {
            LOG_INFO("🔧 停止HTTP服务器监控系统");
            
            try {
                if (is_use_stats_thread_.exchange(true))
                    stopThreadBasedMonitoring();
                
                // 清理状态
                stats_timer_id_ = 0;
                timeout_check_timer_id_ = 0;
                
                LOG_INFO("🛑 HTTP服务器监控系统已停止");
                
            } catch (const std::exception& e) {
                LOG_ERROR("❌ 停止监控系统异常: " + std::string(e.what()));
                stats_timer_id_ = 0;
                timeout_check_timer_id_ = 0;
            }
        }
        
        // 🔧 线程监控（兜底方案）
        void HttpServer::startThreadBasedMonitoring() {
            LOG_INFO("📊 启动线程监控（兜底模式），间隔: " + std::to_string(config_.stats_interval_ms / 1000) + "秒");
            
            stats_thread_ = std::thread([this]() {
                LOG_INFO("🚀 监控线程已启动（兜底模式）");
                
                while (running_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config_.stats_interval_ms));
                    
                    if (!running_.load()) {
                        LOG_INFO("💤 监控线程收到停止信号，准备退出");
                        break;
                    }
                    
                    try {
                        logPerformanceStats();
                        
                        if (config_.request_timeout_ms > 0) {
                            checkConnectionTimeouts();
                        }
                    } catch (const std::exception& e) {
                        LOG_WARNING("⚠️ 监控任务执行异常: " + std::string(e.what()));
                    }
                }
                
                LOG_INFO("🛑 监控线程已安全退出（兜底模式）");
            });
            
            LOG_INFO("✅ HTTP监控系统已启动（线程模式）");
        }
        
        // 🔧 停止线程监控
        void HttpServer::stopThreadBasedMonitoring() {
            LOG_INFO("⏹️ 停止线程监控系统");
            
            try {
                if (stats_thread_.joinable()) {
                    LOG_INFO("⏳ 等待监控线程安全退出...");
                    
                    auto timeout = std::chrono::milliseconds(config_.stats_interval_ms * 2 + 1000);
                    auto start_time = std::chrono::steady_clock::now();
                    
                    bool thread_finished = false;
                    std::thread wait_thread([&]() {
                        stats_thread_.join();
                        thread_finished = true;
                    });
                    
                    while (!thread_finished && 
                           (std::chrono::steady_clock::now() - start_time) < timeout) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    
                    if (thread_finished) {
                        wait_thread.join();
                        LOG_INFO("✅ 监控线程已正常停止");
                    } else {
                        wait_thread.detach();
                        LOG_WARNING("⚠️ 监控线程超时，已分离线程");
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARNING("⚠️ 停止线程监控异常: " + std::string(e.what()));
            }
        }


        void HttpServer::setupDefaultHandlers() {
            // 设置默认404处理器
            not_found_handler_ = [](const HttpRequest& /* req */, HttpResponse& res) {
                res.notFound("The requested resource was not found on this server");
            };

            // 设置默认错误处理器
            error_handler_ = [](const std::exception& e, const HttpRequest& /* req */, HttpResponse& res) {
                LOG_ERROR("Request processing error: " + std::string(e.what()));
                res.internalServerError("An internal error occurred while processing your request");
            };
        }


        // ==================== 路径标准化和验证方法 ====================

        std::string HttpServer::normalizeDirectoryPath(const std::string& path) {
            // 标准化目录路径
            // 处理相对路径、多余斜杠、路径分隔符等

            if (path.empty()) {
                return "."; // 空路径默认为当前目录
            }

            std::string normalized = path;

            // 替换反斜杠为正斜杠（Windows兼容性）
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            // 移除末尾的斜杠
            while (!normalized.empty() && normalized.back() == '/') {
                normalized.pop_back();
            }

            // 如果路径为空（原来只有斜杠），使用当前目录
            if (normalized.empty()) {
                normalized = ".";
            }

            // 处理相对路径组件
            std::vector<std::string> components;
            std::stringstream ss(normalized);
            std::string component;

            while (std::getline(ss, component, '/')) {
                if (component.empty() || component == ".") {
                    continue; // 跳过空组件和当前目录
                } else if (component == "..") {
                    // 处理上级目录
                    if (!components.empty() && components.back() != "..") {
                        components.pop_back();
                    } else if (!normalized.empty() && normalized[0] != '/') {
                        // 相对路径可以有..
                        components.push_back(component);
                    }
                } else {
                    components.push_back(component);
                }
            }

            // 重建路径
            std::string result;
            if (!normalized.empty() && normalized[0] == '/') {
                result = "/"; // 保持绝对路径
            }

            for (size_t i = 0; i < components.size(); ++i) {
                if (i > 0 || (!result.empty() && result != "/")) {
                    result += "/";
                }
                result += components[i];
            }

            // 如果结果为空，返回当前目录
            if (result.empty()) {
                result = ".";
            }

            // LOG_DEBUG("Normalized directory path: '" + path + "' -> '" + result + "'");
            return result;
        }

        bool HttpServer::directoryExists(const std::string& dir_path) {
            // 检查目录是否存在
            try {
                struct stat buffer;
                if (stat(dir_path.c_str(), &buffer) == 0) {
                    bool is_dir = S_ISDIR(buffer.st_mode);
                    // LOG_DEBUG("Directory check: '" + dir_path + "' exists=" + (is_dir ? "true" : "false (not a directory)"));
                    return is_dir;
                }

                // LOG_DEBUG("Directory check: '" + dir_path + "' does not exist");
                return false;

            } catch (const std::exception& e) {
                LOG_WARNING("Exception checking directory existence: " + std::string(e.what()));
                return false;
            }
        }

        bool HttpServer::hasDirectoryReadPermission(const std::string& dir_path) {
            // 检查目录读取权限
            try {
                // 方法1: 使用access()系统调用检查权限
                if (access(dir_path.c_str(), R_OK) == 0) {
                    // LOG_DEBUG("Directory permission check (access): '" + dir_path + "' readable=true");
                    return true;
                }

                // 方法2: 尝试打开目录进行验证
                DIR* dir = opendir(dir_path.c_str());
                if (dir) {
                    closedir(dir);
                    // LOG_DEBUG("Directory permission check (opendir): '" + dir_path + "' readable=true");
                    return true;
                }

                // 记录权限检查失败的原因
                std::string error_msg = "Permission denied";
                if (errno == EACCES) {
                    error_msg = "Access denied";
                } else if (errno == ENOENT) {
                    error_msg = "Directory not found";
                } else if (errno == ENOTDIR) {
                    error_msg = "Not a directory";
                }

                // LOG_DEBUG("Directory permission check: '" + dir_path + "' readable=false (" + error_msg + ")");
                return false;

            } catch (const std::exception& e) {
                LOG_WARNING("Exception checking directory permission: " + std::string(e.what()));
                return false;
            }
        }

        // ==================== 静态文件请求处理核心方法 ====================

        void HttpServer::handleStaticFileRequest(const HttpRequest& request, HttpResponse& response,
                                                const std::string& base_directory, const std::string& url_path, bool head_only) {
            // 处理静态文件请求的核心方法
            // 支持文件服务、目录浏览、缓存控制、范围请求等完整功能

            try {
                // 1. 提取文件路径（移除URL前缀）
                std::string request_path = request.getPath();
                std::string file_path = extractFilePath(request_path, url_path);

                // LOG_DEBUG("Static file request: '" + request_path + "' -> file_path: '" + file_path + "'");

                // 2. 构建完整的文件系统路径
                std::string full_path = base_directory;
                if (!file_path.empty()) {
                    if (full_path.back() != '/' && file_path.front() != '/') {
                        full_path += "/";
                    }
                    full_path += file_path;
                }

                // 3. 安全检查：防止目录遍历攻击
                if (!isPathSafe(full_path, base_directory)) {
                    LOG_WARNING("Potential directory traversal attack blocked: " + request_path + " -> " + full_path);
                    response.forbidden("Access denied");
                    return;
                }

                // 4. 标准化文件路径
                full_path = normalizeFilePath(full_path);
                // LOG_DEBUG("Normalized file path: '" + full_path + "'");

                // 5. 检查文件/目录是否存在
                if (!fileExists(full_path)) {
                    // 尝试查找默认索引文件
                    if (isDirectory(full_path)) {
                        std::string index_file = findIndexFile(full_path);
                        if (!index_file.empty()) {
                            full_path = index_file;
                            // LOG_DEBUG("Found index file: '" + full_path + "'");
                        } else {
                            // 处理目录请求
                            handleDirectoryRequest(request, response, full_path, request_path);
                            return;
                        }
                    } else {
                        // LOG_DEBUG("File not found: '" + full_path + "'");
                        response.notFound("File not found");
                        return;
                    }
                }

                // 6. 检查是否为目录
                if (isDirectory(full_path)) {
                    handleDirectoryRequest(request, response, full_path, request_path);
                    return;
                }

                // 7. 检查文件读取权限
                if (!hasFileReadPermission(full_path)) {
                    LOG_WARNING("No read permission for file: " + full_path);
                    response.forbidden("Access denied");
                    return;
                }

                // 8. 获取文件信息
                FileInfo file_info = getFileInfo(full_path);
                if (!file_info.valid) {
                    LOG_ERROR("Failed to get file info: " + full_path);
                    response.internalServerError("Failed to read file information");
                    return;
                }

                // 9. 检查文件大小限制
                if (file_info.size > config_.max_static_file_size) {
                    LOG_WARNING("File too large: " + full_path + " (" + std::to_string(file_info.size) + " bytes)");
                    response.requestEntityTooLarge("File too large");
                    return;
                }

                // 10. 检查条件请求（If-Modified-Since, If-None-Match）
                if (handleConditionalRequest(request, response, file_info)) {
                    // LOG_DEBUG("Conditional request returned 304 Not Modified for: " + full_path);
                    return; // 304 Not Modified
                }

                // 11. 设置响应头
                setStaticFileHeaders(response, file_info, full_path);

                // 12. 处理范围请求（Range header）
                if (request.hasHeader("Range") && !head_only) {
                    // LOG_DEBUG("Processing range request for: " + full_path);
                    handleRangeRequest(request, response, file_info, full_path, head_only);
                } else {
                    // 13. 普通文件请求
                    // LOG_DEBUG("Processing normal file request for: " + full_path);
                    handleNormalFileRequest(response, file_info, full_path, head_only);
                }

                // 14. 记录成功日志
                std::string method = head_only ? "HEAD" : "GET";
                // LOG_DEBUG("Served static file [" + method + "]: " + full_path +
                //         " (" + std::to_string(file_info.size) + " bytes) to " + request.getClientIP());

            } catch (const std::exception& e) {
                LOG_ERROR("Exception in handleStaticFileRequest for '" + request.getPath() + "': " + std::string(e.what()));
                response.internalServerError("Internal server error");
            }
        }

        // ==================== 路径处理方法 ====================

        std::string HttpServer::extractFilePath(const std::string& request_path, const std::string& url_path) {
            // 从请求路径中提取文件路径
            // 例如: request_path="/static/css/style.css", url_path="/static" -> "css/style.css"

            if (request_path.length() <= url_path.length()) {
                return ""; // 根路径或无效路径
            }

            // 检查请求路径是否以URL路径开头
            if (request_path.substr(0, url_path.length()) != url_path) {
                LOG_WARNING("Request path doesn't match URL path: '" + request_path + "' vs '" + url_path + "'");
                return "";
            }

            std::string file_path = request_path.substr(url_path.length());

            // 移除前导斜杠
            while (!file_path.empty() && file_path[0] == '/') {
                file_path = file_path.substr(1);
            }

            // LOG_DEBUG("Extracted file path: '" + request_path + "' -> '" + file_path + "'");
            return file_path;
        }

        bool HttpServer::isPathSafe(const std::string& full_path, const std::string& base_directory) {
            // 安全检查：防止目录遍历攻击
            // 确保解析后的路径仍在基础目录内

            try {
                // 获取规范化的绝对路径
                std::string canonical_path = getCanonicalPath(full_path);
                std::string canonical_base = getCanonicalPath(base_directory);

                // 确保路径以斜杠结尾进行比较
                if (!canonical_base.empty() && canonical_base.back() != '/') {
                    canonical_base += "/";
                }

                // 检查文件路径是否以基础目录开头
                bool is_safe = (canonical_path.find(canonical_base) == 0) ||
                              (canonical_path == canonical_base.substr(0, canonical_base.length() - 1));

                // LOG_DEBUG("Path safety check: '" + full_path + "' -> canonical: '" + canonical_path +
                //          "', base: '" + canonical_base + "', safe: " + (is_safe ? "true" : "false"));

                return is_safe;

            } catch (const std::exception& e) {
                LOG_WARNING("Path safety check failed for '" + full_path + "': " + std::string(e.what()));
                return false; // 安全起见，拒绝访问
            }
        }

        std::string HttpServer::normalizeFilePath(const std::string& path) {
            // 标准化文件路径，处理相对路径和多余的斜杠
            if (path.empty()) {
                return "";
            }

            std::string normalized = path;

            // 替换反斜杠为正斜杠（Windows兼容性）
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            // 处理路径组件
            std::vector<std::string> components;
            std::stringstream ss(normalized);
            std::string component;

            while (std::getline(ss, component, '/')) {
                if (component.empty() || component == ".") {
                    continue; // 跳过空组件和当前目录
                } else if (component == "..") {
                    // 处理上级目录
                    if (!components.empty()) {
                        components.pop_back();
                    }
                } else {
                    components.push_back(component);
                }
            }

            // 重建路径
            std::string result;
            if (!normalized.empty() && normalized[0] == '/') {
                result = "/"; // 保持绝对路径
            }

            for (size_t i = 0; i < components.size(); ++i) {
                if (i > 0 || (!result.empty() && result != "/")) {
                    result += "/";
                }
                result += components[i];
            }

            return result.empty() ? "/" : result;
        }

        std::string HttpServer::getCanonicalPath(const std::string& path) {
            // 获取规范化的绝对路径
            try {
                // 尝试使用realpath获取真实路径
                char* canonical = realpath(path.c_str(), nullptr);
                if (canonical) {
                    std::string result(canonical);
                    free(canonical);
                    // LOG_DEBUG("Canonical path: '" + path + "' -> '" + result + "'");
                    return result;
                }

                // 如果realpath失败，手动构建绝对路径
                std::string abs_path = path;
                if (!abs_path.empty() && abs_path[0] != '/') {
                    // 相对路径，添加当前工作目录
                    char cwd[PATH_MAX];
                    if (getcwd(cwd, sizeof(cwd))) {
                        abs_path = std::string(cwd) + "/" + abs_path;
                    }
                }

                // 标准化路径
                std::string normalized = normalizeFilePath(abs_path);
                // LOG_DEBUG("Manual canonical path: '" + path + "' -> '" + normalized + "'");
                return normalized;

            } catch (const std::exception& e) {
                LOG_WARNING("Failed to get canonical path for '" + path + "': " + std::string(e.what()));
                return normalizeFilePath(path);
            }
        }

        // ==================== 文件系统检查方法 ====================

        bool HttpServer::fileExists(const std::string& file_path) {
            // 检查文件是否存在
            try {
                struct stat buffer;
                bool exists = (stat(file_path.c_str(), &buffer) == 0);
                // LOG_DEBUG("File existence check: '" + file_path + "' exists=" + (exists ? "true" : "false"));
                return exists;

            } catch (const std::exception& e) {
                LOG_WARNING("Exception checking file existence: " + std::string(e.what()));
                return false;
            }
        }

        bool HttpServer::hasFileReadPermission(const std::string& file_path) {
            // 检查文件读取权限
            try {
                // 方法1: 使用access()系统调用检查权限
                if (access(file_path.c_str(), R_OK) == 0) {
                    // LOG_DEBUG("File permission check (access): '" + file_path + "' readable=true");
                    return true;
                }

                // 方法2: 尝试打开文件进行验证
                std::ifstream file(file_path, std::ios::binary);
                bool readable = file.is_open();
                if (readable) {
                    file.close();
                    // LOG_DEBUG("File permission check (ifstream): '" + file_path + "' readable=true");
                } else {
                    // LOG_DEBUG("File permission check: '" + file_path + "' readable=false");
                }

                return readable;

            } catch (const std::exception& e) {
                LOG_WARNING("Exception checking file permission: " + std::string(e.what()));
                return false;
            }
        }

        HttpServer::FileInfo HttpServer::getFileInfo(const std::string& file_path) {
            // 获取文件信息（大小、修改时间、ETag等）
            FileInfo info;

            try {
                struct stat file_stat;
                if (stat(file_path.c_str(), &file_stat) == 0) {
                    info.valid = true;
                    info.size = file_stat.st_size;
                    info.last_modified = file_stat.st_mtime;
                    info.is_directory = S_ISDIR(file_stat.st_mode);

                    // 生成ETag（基于文件大小和修改时间）
                    std::ostringstream etag_stream;
                    etag_stream << "\"" << std::hex << file_stat.st_size << "-" << file_stat.st_mtime << "\"";
                    info.etag = etag_stream.str();

                    // LOG_DEBUG("File info for '" + file_path + "': size=" + std::to_string(info.size) +
                    //          ", modified=" + std::to_string(info.last_modified) + ", etag=" + info.etag);
                } else {
                    info.valid = false;
                    LOG_WARNING("Failed to get file info for: " + file_path + " (errno: " + std::to_string(errno) + ")");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Exception getting file info for '" + file_path + "': " + std::string(e.what()));
                info.valid = false;
            }

            return info;
        }

        bool HttpServer::isDirectory(const std::string& path) {
            // 检查路径是否为目录
            try {
                struct stat buffer;
                if (stat(path.c_str(), &buffer) == 0) {
                    bool is_dir = S_ISDIR(buffer.st_mode);
                    // LOG_DEBUG("Directory check: '" + path + "' is_directory=" + (is_dir ? "true" : "false"));
                    return is_dir;
                }
                // LOG_DEBUG("Directory check: '" + path + "' does not exist");
                return false;
            } catch (const std::exception& e) {
                LOG_WARNING("Exception checking if path is directory: " + std::string(e.what()));
                return false;
            }
        }

        std::string HttpServer::findIndexFile(const std::string& dir_path) {
            // 查找目录中的默认索引文件
            std::vector<std::string> index_files = {
                "index.html", "index.htm", "default.html", "default.htm", "index.php"
            };

            for (const auto& index_file : index_files) {
                std::string full_path = dir_path;
                if (!full_path.empty() && full_path.back() != '/') {
                    full_path += "/";
                }
                full_path += index_file;

                if (fileExists(full_path) && !isDirectory(full_path)) {
                    // LOG_DEBUG("Found index file: '" + full_path + "'");
                    return full_path;
                }
            }

            // LOG_DEBUG("No index file found in directory: '" + dir_path + "'");
            return ""; // 没有找到索引文件
        }

        // ==================== HTTP响应头设置方法 ====================

        void HttpServer::setStaticFileHeaders(HttpResponse& response, const FileInfo& file_info, const std::string& file_path) {
            // 设置静态文件的HTTP响应头

            try {
                // 设置Content-Type
                std::string content_type = getMimeType(file_path);
                response.setHeader("Content-Type", content_type);

                // 设置Content-Length
                response.setHeader("Content-Length", std::to_string(file_info.size));

                // 设置Last-Modified
                response.setHeader("Last-Modified", formatHttpDate(file_info.last_modified));

                // 设置ETag
                response.setHeader("ETag", file_info.etag);

                // 设置缓存控制头
                if (isCacheableFile(file_path)) {
                    response.setHeader("Cache-Control", "public, max-age=" + std::to_string(config_.static_file_cache_time));
                } else {
                    response.setHeader("Cache-Control", "no-cache, no-store, must-revalidate");
                    response.setHeader("Pragma", "no-cache");
                    response.setHeader("Expires", "0");
                }

                // 设置Accept-Ranges（支持范围请求）
                response.setHeader("Accept-Ranges", "bytes");

                // 安全头
                if (isExecutableFile(file_path)) {
                    response.setHeader("X-Content-Type-Options", "nosniff");
                    response.setHeader("X-Download-Options", "noopen");
                }

                // 设置服务器标识
                response.setHeader("Server", "HttpServer/1.0");

                // LOG_DEBUG("Set static file headers for: " + file_path + " (Content-Type: " + content_type + ")");

            } catch (const std::exception& e) {
                LOG_ERROR("Exception setting static file headers: " + std::string(e.what()));
            }
        }

        std::string HttpServer::getMimeType(const std::string& file_path) {
            // 根据文件扩展名确定MIME类型
            std::string extension = getFileExtension(file_path);

            // 转换为小写
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            // MIME类型映射表
            static const std::unordered_map<std::string, std::string> mime_types = {
                // 文本类型
                {".html", "text/html; charset=utf-8"},
                {".htm", "text/html; charset=utf-8"},
                {".css", "text/css; charset=utf-8"},
                {".js", "application/javascript; charset=utf-8"},
                {".json", "application/json; charset=utf-8"},
                {".xml", "application/xml; charset=utf-8"},
                {".txt", "text/plain; charset=utf-8"},
                {".md", "text/markdown; charset=utf-8"},

                // 图片类型
                {".png", "image/png"},
                {".jpg", "image/jpeg"},
                {".jpeg", "image/jpeg"},
                {".gif", "image/gif"},
                {".bmp", "image/bmp"},
                {".ico", "image/x-icon"},
                {".svg", "image/svg+xml"},
                {".webp", "image/webp"},
                {".tiff", "image/tiff"},

                // 音频类型
                {".mp3", "audio/mpeg"},
                {".wav", "audio/wav"},
                {".ogg", "audio/ogg"},
                {".m4a", "audio/mp4"},
                {".flac", "audio/flac"},
                {".aac", "audio/aac"},

                // 视频类型
                {".mp4", "video/mp4"},
                {".avi", "video/x-msvideo"},
                {".mov", "video/quicktime"},
                {".wmv", "video/x-ms-wmv"},
                {".webm", "video/webm"},
                {".mkv", "video/x-matroska"},
                {".flv", "video/x-flv"},

                // 文档类型
                {".pdf", "application/pdf"},
                {".doc", "application/msword"},
                {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
                {".xls", "application/vnd.ms-excel"},
                {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
                {".ppt", "application/vnd.ms-powerpoint"},
                {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},

                // 压缩文件
                {".zip", "application/zip"},
                {".rar", "application/x-rar-compressed"},
                {".tar", "application/x-tar"},
                {".gz", "application/gzip"},
                {".7z", "application/x-7z-compressed"},

                // 字体文件
                {".woff", "font/woff"},
                {".woff2", "font/woff2"},
                {".ttf", "font/ttf"},
                {".otf", "font/otf"},
                {".eot", "application/vnd.ms-fontobject"},

                // 应用程序类型
                {".exe", "application/octet-stream"},
                {".dmg", "application/x-apple-diskimage"},
                {".deb", "application/x-debian-package"},
                {".rpm", "application/x-rpm"}
            };

            auto it = mime_types.find(extension);
            if (it != mime_types.end()) {
                return it->second;
            }

            // 默认MIME类型
            return "application/octet-stream";
        }

        std::string HttpServer::getFileExtension(const std::string& file_path) {
            // 获取文件扩展名
            size_t dot_pos = file_path.find_last_of('.');
            if (dot_pos != std::string::npos && dot_pos < file_path.length() - 1) {
                return file_path.substr(dot_pos);
            }
            return "";
        }

        // ==================== 文件类型判断方法 ====================

        bool HttpServer::isCacheableFile(const std::string& file_path) {
            // 判断文件是否可以缓存
            std::string extension = getFileExtension(file_path);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            // 可缓存的文件类型
            static const std::set<std::string> cacheable_extensions = {
                ".css", ".js", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico",
                ".svg", ".webp", ".woff", ".woff2", ".ttf", ".otf", ".eot",
                ".mp3", ".wav", ".ogg", ".mp4", ".avi", ".mov", ".pdf"
            };

            return cacheable_extensions.find(extension) != cacheable_extensions.end();
        }

        bool HttpServer::isExecutableFile(const std::string& file_path) {
            // 判断文件是否为可执行文件（需要额外安全措施）
            std::string extension = getFileExtension(file_path);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            static const std::set<std::string> executable_extensions = {
                ".exe", ".bat", ".cmd", ".com", ".scr", ".pif", ".sh", ".bin", ".run", ".app", ".dmg"
            };

            return executable_extensions.find(extension) != executable_extensions.end();
        }

        // ==================== 条件请求处理方法 ====================

        bool HttpServer::handleConditionalRequest(const HttpRequest& request, HttpResponse& response, const FileInfo& file_info) {
            // 处理条件请求（304 Not Modified）

            try {
                // 检查If-None-Match (ETag)
                if (request.hasHeader("If-None-Match")) {
                    std::string if_none_match = request.getHeader("If-None-Match");
                    if (if_none_match == file_info.etag || if_none_match == "*") {
                        response.setStatus(304); // Not Modified
                        response.setHeader("ETag", file_info.etag);
                        response.setHeader("Last-Modified", formatHttpDate(file_info.last_modified));
                        response.setHeader("Cache-Control", "public, max-age=" + std::to_string(config_.static_file_cache_time));

                        // LOG_DEBUG("Conditional request: 304 Not Modified (ETag match)");
                        return true;
                    }
                }

                // 检查If-Modified-Since
                if (request.hasHeader("If-Modified-Since")) {
                    std::string if_modified_since = request.getHeader("If-Modified-Since");
                    time_t client_time = parseHttpDate(if_modified_since);

                    if (client_time != -1 && client_time >= file_info.last_modified) {
                        response.setStatus(304); // Not Modified
                        response.setHeader("ETag", file_info.etag);
                        response.setHeader("Last-Modified", formatHttpDate(file_info.last_modified));
                        response.setHeader("Cache-Control", "public, max-age=" + std::to_string(config_.static_file_cache_time));

                        // LOG_DEBUG("Conditional request: 304 Not Modified (Last-Modified)");
                        return true;
                    }
                }

                // 检查If-Match (用于PUT/POST等修改操作)
                if (request.hasHeader("If-Match")) {
                    std::string if_match = request.getHeader("If-Match");
                    if (if_match != "*" && if_match != file_info.etag) {
                        response.setStatus(412); // Precondition Failed
                        response.setHeader("ETag", file_info.etag);

                        // LOG_DEBUG("Conditional request: 412 Precondition Failed (If-Match)");
                        return true;
                    }
                }

                // 检查If-Unmodified-Since
                if (request.hasHeader("If-Unmodified-Since")) {
                    std::string if_unmodified_since = request.getHeader("If-Unmodified-Since");
                    time_t client_time = parseHttpDate(if_unmodified_since);

                    if (client_time != -1 && client_time < file_info.last_modified) {
                        response.setStatus(412); // Precondition Failed
                        response.setHeader("Last-Modified", formatHttpDate(file_info.last_modified));

                        // LOG_DEBUG("Conditional request: 412 Precondition Failed (If-Unmodified-Since)");
                        return true;
                    }
                }

                return false; // 需要发送完整响应

            } catch (const std::exception& e) {
                LOG_ERROR("Exception in conditional request handling: " + std::string(e.what()));
                return false; // 发生错误时发送完整响应
            }
        }

        // ==================== HTTP日期处理方法 ====================

        std::string HttpServer::formatHttpDate(time_t timestamp) {
            // 格式化HTTP日期（RFC 7231格式）
            try {
                struct tm* gmt = gmtime(&timestamp);
                if (!gmt) {
                    LOG_WARNING("Failed to convert timestamp to GMT: " + std::to_string(timestamp));
                    return "";
                }

                char buffer[64];
                size_t result = strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", gmt);
                if (result == 0) {
                    LOG_WARNING("Failed to format HTTP date for timestamp: " + std::to_string(timestamp));
                    return "";
                }

                return std::string(buffer);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception formatting HTTP date: " + std::string(e.what()));
                return "";
            }
        }

        time_t HttpServer::parseHttpDate(const std::string& date_str) {
            // 解析HTTP日期字符串
            try {
                struct tm tm_time = {};

                // 尝试不同的日期格式
                const char* formats[] = {
                    "%a, %d %b %Y %H:%M:%S GMT",     // RFC 7231: "Mon, 01 Jan 2024 12:00:00 GMT"
                    "%A, %d-%b-%y %H:%M:%S GMT",     // RFC 850: "Monday, 01-Jan-24 12:00:00 GMT"
                    "%a %b %d %H:%M:%S %Y"           // asctime(): "Mon Jan 1 12:00:00 2024"
                };

                for (const char* format : formats) {
                    if (strptime(date_str.c_str(), format, &tm_time)) {
                        time_t result = timegm(&tm_time);
                        if (result != -1) {
                            // LOG_DEBUG("Parsed HTTP date: '" + date_str + "' -> " + std::to_string(result));
                            return result;
                        }
                    }
                }

                LOG_WARNING("Failed to parse HTTP date: '" + date_str + "'");
                return -1; // 解析失败

            } catch (const std::exception& e) {
                LOG_ERROR("Exception parsing HTTP date '" + date_str + "': " + std::string(e.what()));
                return -1;
            }
        }

        // ==================== 文件内容读取和响应方法 ====================

        void HttpServer::handleNormalFileRequest(HttpResponse& response, const FileInfo& file_info,
                                               const std::string& file_path, bool head_only) {
            // 处理普通文件请求（非范围请求）

            try {
                response.setStatus(200); // OK

                if (!head_only) {
                    // 读取并发送文件内容
                    std::string file_content = readFileContent(file_path);
                    if (!file_content.empty() || file_info.size == 0) {
                        response.setBody(file_content);
                        // LOG_DEBUG("Normal file request completed: " + file_path + " (" + std::to_string(file_content.size()) + " bytes)");
                    } else {
                        LOG_ERROR("Failed to read file content: " + file_path);
                        response.internalServerError("Failed to read file");
                        return;
                    }
                } else {
                    // LOG_DEBUG("HEAD request completed: " + file_path);
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Exception in normal file request for '" + file_path + "': " + std::string(e.what()));
                response.internalServerError("Internal server error");
            }
        }

        std::string HttpServer::readFileContent(const std::string& file_path) {
            // 读取完整文件内容
            try {
                std::ifstream file(file_path, std::ios::binary);
                if (!file.is_open()) {
                    LOG_ERROR("Failed to open file: " + file_path);
                    return "";
                }

                // 获取文件大小
                file.seekg(0, std::ios::end);
                size_t file_size = file.tellg();
                file.seekg(0, std::ios::beg);

                // 检查文件大小限制
                if (file_size > config_.max_static_file_size) {
                    LOG_WARNING("File too large: " + file_path + " (" + std::to_string(file_size) + " bytes)");
                    return "";
                }

                // 读取文件内容
                std::string content(file_size, '\0');
                file.read(&content[0], file_size);

                if (file.gcount() != static_cast<std::streamsize>(file_size)) {
                    LOG_ERROR("Failed to read complete file: " + file_path +
                             " (expected: " + std::to_string(file_size) + ", read: " + std::to_string(file.gcount()) + ")");
                    return "";
                }

                // LOG_DEBUG("Read file content: " + file_path + " (" + std::to_string(file_size) + " bytes)");
                return content;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception reading file '" + file_path + "': " + std::string(e.what()));
                return "";
            }
        }

        std::string HttpServer::readFileRange(const std::string& file_path, size_t start, size_t length) {
            // 读取文件的指定范围
            try {
                std::ifstream file(file_path, std::ios::binary);
                if (!file.is_open()) {
                    LOG_ERROR("Failed to open file for range read: " + file_path);
                    return "";
                }

                // 定位到起始位置
                file.seekg(start, std::ios::beg);
                if (file.fail()) {
                    LOG_ERROR("Failed to seek to position " + std::to_string(start) + " in file: " + file_path);
                    return "";
                }

                // 读取指定长度的内容
                std::string content(length, '\0');
                file.read(&content[0], length);

                size_t bytes_read = file.gcount();
                if (bytes_read != length) {
                    LOG_WARNING("Read " + std::to_string(bytes_read) + " bytes instead of " +
                               std::to_string(length) + " from file: " + file_path);
                    content.resize(bytes_read);
                }

                // LOG_DEBUG("Read file range: " + file_path + " [" + std::to_string(start) +
                //          "-" + std::to_string(start + bytes_read - 1) + "] (" + std::to_string(bytes_read) + " bytes)");
                return content;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception reading file range '" + file_path + "': " + std::string(e.what()));
                return "";
            }
        }

        // ==================== 范围请求处理方法 ====================

        void HttpServer::handleRangeRequest(const HttpRequest& request, HttpResponse& response,
                                           const FileInfo& file_info, const std::string& file_path, bool head_only) {
            // 处理HTTP范围请求（用于断点续传、视频流等）

            try {
                std::string range_header = request.getHeader("Range");

                // 解析Range头（格式：bytes=start-end）
                RangeInfo range = parseRangeHeader(range_header, file_info.size);

                if (!range.valid) {
                    // 无效的Range请求，返回416 Range Not Satisfiable
                    response.setStatus(416);
                    response.setHeader("Content-Range", "bytes */" + std::to_string(file_info.size));
                    response.setHeader("Content-Length", "0");
                    LOG_WARNING("Invalid range request: " + range_header + " for file: " + file_path);
                    return;
                }

                // 设置206 Partial Content状态
                response.setStatus(206);

                // 设置Content-Range头
                std::ostringstream content_range;
                content_range << "bytes " << range.start << "-" << range.end << "/" << file_info.size;
                response.setHeader("Content-Range", content_range.str());

                // 设置Content-Length为范围大小
                size_t range_size = range.end - range.start + 1;
                response.setHeader("Content-Length", std::to_string(range_size));

                if (!head_only) {
                    // 读取指定范围的文件内容
                    std::string range_content = readFileRange(file_path, range.start, range_size);
                    if (!range_content.empty() || range_size == 0) {
                        response.setBody(range_content);
                        // LOG_DEBUG("Range request completed: " + file_path + " bytes " + std::to_string(range.start) +
                        //          "-" + std::to_string(range.end) + " (" + std::to_string(range_content.size()) + " bytes)");
                    } else {
                        LOG_ERROR("Failed to read file range: " + file_path);
                        response.internalServerError("Failed to read file range");
                        return;
                    }
                } else {
                    // LOG_DEBUG("HEAD range request completed: " + file_path + " bytes " + std::to_string(range.start) +
                    //          "-" + std::to_string(range.end));
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Exception in range request for '" + file_path + "': " + std::string(e.what()));
                response.internalServerError("Internal server error");
            }
        }

        HttpServer::RangeInfo HttpServer::parseRangeHeader(const std::string& range_header, size_t file_size) {
            // 解析HTTP Range头
            // 支持格式：bytes=start-end, bytes=start-, bytes=-suffix

            RangeInfo range;
            range.valid = false;

            try {
                // 检查是否以"bytes="开头
                if (range_header.substr(0, 6) != "bytes=") {
                    LOG_WARNING("Invalid range header format: " + range_header);
                    return range;
                }

                std::string range_spec = range_header.substr(6);
                size_t dash_pos = range_spec.find('-');

                if (dash_pos == std::string::npos) {
                    LOG_WARNING("Invalid range specification: " + range_spec);
                    return range; // 无效格式
                }

                std::string start_str = range_spec.substr(0, dash_pos);
                std::string end_str = range_spec.substr(dash_pos + 1);

                if (start_str.empty() && end_str.empty()) {
                    LOG_WARNING("Empty range specification: " + range_spec);
                    return range; // 无效格式
                }

                if (start_str.empty()) {
                    // 后缀范围：bytes=-500 (最后500字节)
                    size_t suffix_length = std::stoull(end_str);
                    if (suffix_length >= file_size) {
                        range.start = 0;
                        range.end = file_size - 1;
                    } else {
                        range.start = file_size - suffix_length;
                        range.end = file_size - 1;
                    }
                    // LOG_DEBUG("Suffix range: last " + std::to_string(suffix_length) + " bytes");
                } else if (end_str.empty()) {
                    // 开始范围：bytes=500- (从500字节到文件末尾)
                    range.start = std::stoull(start_str);
                    range.end = file_size - 1;
                    // LOG_DEBUG("Start range: from " + std::to_string(range.start) + " to end");
                } else {
                    // 完整范围：bytes=500-999
                    range.start = std::stoull(start_str);
                    range.end = std::stoull(end_str);
                    // LOG_DEBUG("Full range: " + std::to_string(range.start) + "-" + std::to_string(range.end));
                }

                // 验证范围有效性
                if (range.start < file_size && range.end < file_size && range.start <= range.end) {
                    range.valid = true;
                    // LOG_DEBUG("Valid range: " + std::to_string(range.start) + "-" + std::to_string(range.end) +
                    //          " (size: " + std::to_string(range.end - range.start + 1) + ")");
                } else {
                    LOG_WARNING("Invalid range values: start=" + std::to_string(range.start) +
                               ", end=" + std::to_string(range.end) + ", file_size=" + std::to_string(file_size));
                }

            } catch (const std::exception& e) {
                LOG_WARNING("Exception parsing range header '" + range_header + "': " + std::string(e.what()));
            }

            return range;
        }

        // ==================== 目录请求处理方法 ====================

        void HttpServer::handleDirectoryRequest(const HttpRequest& request, HttpResponse& response,
                                              const std::string& dir_path, const std::string& url_path) {
            // 处理目录请求
            // 完善的目录处理逻辑，包括索引文件查找、URL重定向、权限检查等

            try {
                // 1. 验证目录路径
                if (!directoryExists(dir_path)) {
                    LOG_WARNING("Directory does not exist: " + dir_path);
                    response.notFound("Directory not found");
                    return;
                }

                // 2. 检查目录访问权限
                if (!hasDirectoryReadPermission(dir_path)) {
                    LOG_WARNING("No read permission for directory: " + dir_path);
                    response.forbidden("Access denied");
                    return;
                }

                // 3. 标准化URL路径（确保以斜杠结尾）
                std::string normalized_url_path = url_path;
                if (!normalized_url_path.empty() && normalized_url_path.back() != '/') {
                    // 如果URL路径不以斜杠结尾，重定向到正确的URL
                    std::string redirect_url = normalized_url_path + "/";

                    // 保留查询参数
                    std::string query_string = request.getQueryString();
                    if (!query_string.empty()) {
                        redirect_url += "?" + query_string;
                    }

                    response.setStatus(301); // Moved Permanently
                    response.setHeader("Location", redirect_url);
                    response.setHeader("Content-Type", "text/html; charset=utf-8");
                    response.setBody("<html><body><h1>301 Moved Permanently</h1><p>The document has moved <a href=\"" +
                                   redirect_url + "\">here</a>.</p></body></html>");

                    // LOG_DEBUG("Redirecting directory request: " + url_path + " -> " + redirect_url);
                    return;
                }

                // 4. 查找默认索引文件
                std::string index_file = findIndexFile(dir_path);
                if (!index_file.empty()) {
                    // LOG_DEBUG("Found index file: " + index_file);

                    // 获取索引文件信息
                    FileInfo file_info = getFileInfo(index_file);
                    if (file_info.valid) {
                        // 检查索引文件是否可读
                        if (!hasFileReadPermission(index_file)) {
                            LOG_WARNING("No read permission for index file: " + index_file);
                            response.forbidden("Access denied");
                            return;
                        }

                        // 检查条件请求
                        if (handleConditionalRequest(request, response, file_info)) {
                            return; // 304 Not Modified
                        }

                        // 设置响应头
                        setStaticFileHeaders(response, file_info, index_file);
                        response.setStatus(200);

                        // 处理HEAD请求
                        if (request.getMethod() == HttpMethod::HEAD) {
                            return;
                        }

                        // 读取并发送索引文件内容
                        std::string file_content = readFileContent(index_file);
                        if (!file_content.empty() || file_info.size == 0) {
                            response.setBody(file_content);
                            // LOG_DEBUG("Served index file: " + index_file + " (" + std::to_string(file_info.size) + " bytes)");
                        } else {
                            LOG_ERROR("Failed to read index file: " + index_file);
                            response.internalServerError("Failed to read index file");
                        }
                        return;
                    } else {
                        LOG_WARNING("Invalid index file info: " + index_file);
                    }
                }

                // 5. 处理目录浏览请求
                if (config_.enable_directory_listing) {
                    // LOG_DEBUG("Generating directory listing for: " + dir_path);

                    // 检查目录是否为空
                    if (isDirectoryEmpty(dir_path)) {
                        generateEmptyDirectoryListing(response, dir_path, normalized_url_path);
                    } else {
                        generateDirectoryListing(response, dir_path, normalized_url_path);
                    }
                } else {
                    // 目录浏览被禁用
                    // LOG_DEBUG("Directory listing disabled for: " + dir_path);
                    response.setStatus(403);
                    response.setHeader("Content-Type", "text/html; charset=utf-8");
                    response.setBody(generateForbiddenPage(normalized_url_path));
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Error handling directory request for " + dir_path + ": " + std::string(e.what()));
                response.internalServerError("Internal server error");
            }
        }

        // ==================== 目录辅助方法 ====================

        bool HttpServer::isDirectoryEmpty(const std::string& dir_path) {
            // 检查目录是否为空（忽略隐藏文件）
            try {
                DIR* dir = opendir(dir_path.c_str());
                if (!dir) {
                    LOG_WARNING("Failed to open directory for empty check: " + dir_path);
                    return true; // 无法打开，视为空
                }

                struct dirent* entry;
                int count = 0;

                while ((entry = readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;

                    // 跳过当前目录和上级目录
                    if (name == "." || name == "..") {
                        continue;
                    }

                    // 跳过隐藏文件
                    if (name[0] == '.') {
                        continue;
                    }

                    count++;
                    if (count > 0) {
                        break; // 找到至少一个文件，不是空目录
                    }
                }

                closedir(dir);
                bool is_empty = (count == 0);
                // LOG_DEBUG("Directory empty check: '" + dir_path + "' is_empty=" + (is_empty ? "true" : "false"));
                return is_empty;

            } catch (const std::exception& e) {
                LOG_WARNING("Exception checking if directory is empty: " + std::string(e.what()));
                return true;
            }
        }

        void HttpServer::generateEmptyDirectoryListing(HttpResponse& response, const std::string& dir_path, const std::string& url_path) {
            // 生成空目录的HTML页面
            try {
                std::ostringstream html;

                // HTML头部
                html << "<!DOCTYPE html>\n";
                html << "<html>\n<head>\n";
                html << "<meta charset=\"utf-8\">\n";
                html << "<title>Directory listing for " << url_path << "</title>\n";
                html << "<style>\n";
                html << "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f9f9f9; }\n";
                html << "h1 { color: #333; border-bottom: 2px solid #ddd; padding-bottom: 10px; }\n";
                html << ".empty-message { color: #666; font-style: italic; margin: 20px 0; }\n";
                html << ".info-box { background-color: #e7f3ff; border: 1px solid #b3d9ff; border-radius: 4px; padding: 15px; margin: 20px 0; }\n";
                html << "a { color: #0066cc; text-decoration: none; }\n";
                html << "a:hover { text-decoration: underline; }\n";
                html << ".footer { margin-top: 40px; padding-top: 20px; border-top: 1px solid #ddd; color: #666; font-size: 0.9em; }\n";
                html << "</style>\n";
                html << "</head>\n<body>\n";

                html << "<h1>📁 Directory listing for " << url_path << "</h1>\n";

                // 空目录消息
                html << "<div class=\"empty-message\">\n";
                html << "📂 This directory is empty.\n";
                html << "</div>\n";

                // 信息框
                html << "<div class=\"info-box\">\n";
                html << "<strong>ℹ️ Information:</strong><br>\n";
                html << "• No files or subdirectories found in this location<br>\n";
                html << "• Hidden files (starting with '.') are not displayed<br>\n";
                html << "• You can upload files to this directory if you have write permissions\n";
                html << "</div>\n";

                // 导航链接
                if (url_path != "/") {
                    std::string parent_path = getParentPath(url_path);
                    html << "<p><a href=\"" << parent_path << "\">⬆️ Go to parent directory</a></p>\n";
                }

                // 页脚
                html << "<div class=\"footer\">\n";
                html << "<em>Generated by HttpServer at " << getCurrentTimeString() << "</em>\n";
                html << "</div>\n";

                html << "</body>\n</html>\n";

                response.setStatus(200);
                response.setHeader("Content-Type", "text/html; charset=utf-8");
                response.setHeader("Cache-Control", "no-cache, no-store, must-revalidate");
                response.setBody(html.str());

                // LOG_DEBUG("Generated empty directory listing for: " + dir_path);

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to generate empty directory listing: " + std::string(e.what()));
                response.internalServerError("Failed to generate directory listing");
            }
        }

        // ==================== 页面生成和工具方法 ====================

        std::string HttpServer::generateForbiddenPage(const std::string& url_path) {
            // 生成403禁止访问页面
            std::ostringstream html;

            html << "<!DOCTYPE html>\n";
            html << "<html>\n<head>\n";
            html << "<meta charset=\"utf-8\">\n";
            html << "<title>403 Forbidden</title>\n";
            html << "<style>\n";
            html << "body { font-family: Arial, sans-serif; margin: 40px; text-align: center; background-color: #f9f9f9; }\n";
            html << "h1 { color: #d32f2f; font-size: 3em; margin-bottom: 20px; }\n";
            html << "h2 { color: #333; margin-bottom: 30px; }\n";
            html << "p { color: #666; line-height: 1.6; max-width: 600px; margin: 0 auto 20px; }\n";
            html << ".error-code { font-size: 6em; color: #ffcdd2; font-weight: bold; margin: 20px 0; }\n";
            html << ".info-box { background-color: #fff3e0; border: 1px solid #ffcc02; border-radius: 4px; padding: 20px; margin: 30px auto; max-width: 500px; }\n";
            html << "a { color: #0066cc; text-decoration: none; }\n";
            html << "a:hover { text-decoration: underline; }\n";
            html << "</style>\n";
            html << "</head>\n<body>\n";

            html << "<div class=\"error-code\">403</div>\n";
            html << "<h1>🚫 Forbidden</h1>\n";
            html << "<h2>Directory listing is disabled</h2>\n";

            html << "<p>You don't have permission to access the directory listing for <strong>" << url_path << "</strong> on this server.</p>\n";

            html << "<div class=\"info-box\">\n";
            html << "<strong>Possible reasons:</strong><br>\n";
            html << "• Directory browsing is disabled by the server administrator<br>\n";
            html << "• No default index file (index.html) was found<br>\n";
            html << "• Access to this directory is restricted\n";
            html << "</div>\n";

            html << "<p><a href=\"/\">🏠 Return to home page</a></p>\n";

            html << "</body>\n</html>\n";

            return html.str();
        }

        std::string HttpServer::getParentPath(const std::string& url_path) {
            // 获取父级路径
            if (url_path == "/" || url_path.empty()) {
                return "/";
            }

            std::string path = url_path;

            // 移除末尾的斜杠
            if (path.back() == '/') {
                path.pop_back();
            }

            // 查找最后一个斜杠
            size_t last_slash = path.find_last_of('/');
            if (last_slash == std::string::npos) {
                return "/";
            }

            if (last_slash == 0) {
                return "/";
            }

            return path.substr(0, last_slash + 1);
        }

        std::string HttpServer::getCurrentTimeString() {
            // 获取当前时间的字符串表示
            try {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);

                std::ostringstream oss;
                oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
                return oss.str();
            } catch (const std::exception& e) {
                LOG_WARNING("Exception getting current time string: " + std::string(e.what()));
                return "Unknown";
            }
        }

        // ==================== 目录列表生成方法 ====================

        void HttpServer::generateDirectoryListing(HttpResponse& response, const std::string& dir_path, const std::string& url_path) {
            // 生成目录列表HTML页面
            try {
                std::ostringstream html;

                // HTML头部
                html << "<!DOCTYPE html>\n";
                html << "<html>\n<head>\n";
                html << "<meta charset=\"utf-8\">\n";
                html << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
                html << "<title>📁 Directory listing for " << url_path << "</title>\n";

                // CSS样式
                html << "<style>\n";
                html << "* { box-sizing: border-box; }\n";
                html << "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }\n";
                html << ".container { max-width: 1200px; margin: 0 auto; background-color: white; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); overflow: hidden; }\n";
                html << ".header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 30px; }\n";
                html << ".header h1 { margin: 0; font-size: 2em; font-weight: 300; }\n";
                html << ".header .path { margin-top: 10px; opacity: 0.9; font-size: 1.1em; }\n";
                html << ".stats { background-color: #f8f9fa; padding: 15px 30px; border-bottom: 1px solid #e9ecef; color: #6c757d; }\n";
                html << ".table-container { overflow-x: auto; }\n";
                html << "table { width: 100%; border-collapse: collapse; }\n";
                html << "th { background-color: #f8f9fa; text-align: left; padding: 15px 20px; font-weight: 600; color: #495057; border-bottom: 2px solid #dee2e6; }\n";
                html << "td { padding: 12px 20px; border-bottom: 1px solid #f1f3f4; }\n";
                html << "tr:hover { background-color: #f8f9fa; }\n";
                html << ".file-name { display: flex; align-items: center; }\n";
                html << "a { text-decoration: none; color: #0066cc; font-weight: 500; }\n";
                html << "a:hover { text-decoration: underline; color: #0052a3; }\n";
                html << ".directory { color: #6f42c1; }\n";
                html << ".size { text-align: right; font-family: 'Monaco', 'Menlo', monospace; }\n";
                html << ".date { white-space: nowrap; color: #6c757d; }\n";
                html << ".parent-link { background-color: #e3f2fd; }\n";
                html << ".footer { padding: 20px 30px; background-color: #f8f9fa; color: #6c757d; text-align: center; font-size: 0.9em; }\n";
                html << "@media (max-width: 768px) { .container { margin: 10px; border-radius: 4px; } .header { padding: 20px; } th, td { padding: 10px; } }\n";
                html << "</style>\n";
                html << "</head>\n<body>\n";

                html << "<div class=\"container\">\n";

                // 头部区域
                html << "<div class=\"header\">\n";
                html << "<h1>📁 Directory Listing</h1>\n";
                html << "<div class=\"path\">" << url_path << "</div>\n";
                html << "</div>\n";

                // 统计信息
                generateDirectoryStats(html, dir_path);

                // 文件列表表格
                html << "<div class=\"table-container\">\n";
                html << "<table>\n";
                html << "<thead>\n";
                html << "<tr>\n";
                html << "<th>📄 Name</th>\n";
                html << "<th>📏 Size</th>\n";
                html << "<th>📅 Last Modified</th>\n";
                html << "<th>🏷️ Type</th>\n";
                html << "</tr>\n";
                html << "</thead>\n";
                html << "<tbody>\n";

                // 添加上级目录链接
                if (url_path != "/") {
                    std::string parent_path = getParentPath(url_path);
                    html << "<tr class=\"parent-link\">\n";
                    html << "<td class=\"file-name\"><a href=\"" << parent_path << "\">📁 ../</a></td>\n";
                    html << "<td class=\"size\">-</td>\n";
                    html << "<td class=\"date\">-</td>\n";
                    html << "<td>Directory</td>\n";
                    html << "</tr>\n";
                }

                // 列出目录内容
                listDirectoryContents(html, dir_path, url_path);

                html << "</tbody>\n";
                html << "</table>\n";
                html << "</div>\n";

                // 页脚
                html << "<div class=\"footer\">\n";
                html << "Generated by HttpServer • " << getCurrentTimeString() << "\n";
                html << "</div>\n";

                html << "</div>\n";
                html << "</body>\n</html>\n";

                response.setStatus(200);
                response.setHeader("Content-Type", "text/html; charset=utf-8");
                response.setHeader("Cache-Control", "no-cache, no-store, must-revalidate");
                response.setHeader("Pragma", "no-cache");
                response.setHeader("Expires", "0");
                response.setBody(html.str());

                // LOG_DEBUG("Generated directory listing for: " + dir_path);

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to generate directory listing: " + std::string(e.what()));
                response.internalServerError("Failed to generate directory listing");
            }
        }

        // ==================== 目录内容列表和统计方法 ====================

        void HttpServer::generateDirectoryStats(std::ostringstream& html, const std::string& dir_path) {
            // 生成目录统计信息
            try {
                DIR* dir = opendir(dir_path.c_str());
                if (!dir) {
                    LOG_WARNING("Failed to open directory for stats: " + dir_path);
                    return;
                }

                int file_count = 0;
                int dir_count = 0;
                size_t total_size = 0;

                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;

                    // 跳过隐藏文件和特殊目录
                    if (name[0] == '.') {
                        continue;
                    }

                    std::string full_path = dir_path + "/" + name;
                    FileInfo file_info = getFileInfo(full_path);

                    if (file_info.valid) {
                        if (file_info.is_directory) {
                            dir_count++;
                        } else {
                            file_count++;
                            total_size += file_info.size;
                        }
                    }
                }

                closedir(dir);

                html << "<div class=\"stats\">\n";
                html << "📊 " << (file_count + dir_count) << " items total • ";
                html << "📁 " << dir_count << " directories • ";
                html << "📄 " << file_count << " files";
                if (total_size > 0) {
                    html << " • 💾 " << formatFileSize(total_size) << " total size";
                }
                html << "</div>\n";

                // LOG_DEBUG("Generated directory stats: " + std::to_string(file_count) + " files, " +
                //          std::to_string(dir_count) + " directories, " + std::to_string(total_size) + " bytes");

            } catch (const std::exception& e) {
                LOG_WARNING("Failed to generate directory stats: " + std::string(e.what()));
            }
        }

        void HttpServer::listDirectoryContents(std::ostringstream& html, const std::string& dir_path, const std::string& url_path) {
            // 列出目录内容
            try {
                DIR* dir = opendir(dir_path.c_str());
                if (!dir) {
                    LOG_ERROR("Failed to open directory: " + dir_path);
                    html << "<tr><td colspan=\"4\" style=\"text-align: center; color: #dc3545; padding: 20px;\">❌ Failed to read directory contents</td></tr>\n";
                    return;
                }

                std::vector<DirectoryEntry> entries;
                struct dirent* entry;

                // 读取目录项
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;

                    // 跳过隐藏文件和当前/上级目录
                    if (name[0] == '.') {
                        continue;
                    }

                    std::string full_path = dir_path + "/" + name;
                    FileInfo file_info = getFileInfo(full_path);

                    if (file_info.valid) {
                        DirectoryEntry dir_entry;
                        dir_entry.name = name;
                        dir_entry.is_directory = file_info.is_directory;
                        dir_entry.size = file_info.size;
                        dir_entry.last_modified = file_info.last_modified;
                        entries.push_back(dir_entry);
                    }
                }

                closedir(dir);

                // 如果目录为空
                if (entries.empty()) {
                    html << "<tr><td colspan=\"4\" style=\"text-align: center; color: #6c757d; padding: 30px; font-style: italic;\">📂 This directory is empty</td></tr>\n";
                    return;
                }

                // 排序：目录在前，然后按名称排序
                std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
                    if (a.is_directory != b.is_directory) {
                        return a.is_directory; // 目录在前
                    }
                    // 不区分大小写的排序
                    std::string a_lower = a.name;
                    std::string b_lower = b.name;
                    std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(), ::tolower);
                    std::transform(b_lower.begin(), b_lower.end(), b_lower.begin(), ::tolower);
                    return a_lower < b_lower;
                });

                // 生成HTML表格行
                for (const auto& entry : entries) {
                    std::string href = url_path;
                    if (!href.empty() && href.back() != '/') {
                        href += "/";
                    }
                    href += entry.name;

                    html << "<tr>";

                    // 文件名列（带图标）
                    html << "<td class=\"file-name\">";
                    html << "<a href=\"" << href;
                    if (entry.is_directory) {
                        html << "/";
                    }
                    html << "\">";

                    // 添加文件类型图标
                    if (entry.is_directory) {
                        html << "📁 " << entry.name << "/";
                    } else {
                        html << getFileIcon(entry.name) << " " << entry.name;
                    }

                    html << "</a></td>";

                    // 文件大小
                    html << "<td class=\"size\">";
                    if (entry.is_directory) {
                        html << "-";
                    } else {
                        html << formatFileSize(entry.size);
                    }
                    html << "</td>";

                    // 修改时间
                    html << "<td class=\"date\">" << formatHttpDate(entry.last_modified) << "</td>";

                    // 文件类型
                    html << "<td>";
                    if (entry.is_directory) {
                        html << "<span class=\"directory\">Directory</span>";
                    } else {
                        html << getFileTypeDescription(entry.name);
                    }
                    html << "</td>";

                    html << "</tr>\n";
                }

                // LOG_DEBUG("Listed " + std::to_string(entries.size()) + " directory entries for: " + dir_path);

            } catch (const std::exception& e) {
                LOG_ERROR("Error listing directory contents: " + std::string(e.what()));
                html << "<tr><td colspan=\"4\" style=\"text-align: center; color: #dc3545; padding: 20px;\">❌ Error reading directory: " << e.what() << "</td></tr>\n";
            }
        }

        // ==================== 文件显示辅助方法 ====================

        std::string HttpServer::formatFileSize(size_t size) {
            // 格式化文件大小（人类可读格式）
            const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            int unit_index = 0;
            double size_double = static_cast<double>(size);

            while (size_double >= 1024.0 && unit_index < 4) {
                size_double /= 1024.0;
                unit_index++;
            }

            std::ostringstream oss;
            if (unit_index == 0) {
                oss << static_cast<size_t>(size_double) << " " << units[unit_index];
            } else {
                oss << std::fixed << std::setprecision(1) << size_double << " " << units[unit_index];
            }

            return oss.str();
        }

        std::string HttpServer::getFileIcon(const std::string& filename) {
            // 根据文件类型返回相应的图标
            std::string extension = getFileExtension(filename);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            // 图标映射
            static const std::unordered_map<std::string, std::string> icon_map = {
                // 文档类型
                {".html", "🌐"}, {".htm", "🌐"},
                {".css", "🎨"}, {".js", "⚡"},
                {".json", "📋"}, {".xml", "📄"},
                {".txt", "📝"}, {".md", "📖"},
                {".pdf", "📕"}, {".doc", "📘"}, {".docx", "📘"},

                // 图片类型
                {".png", "🖼️"}, {".jpg", "🖼️"}, {".jpeg", "🖼️"},
                {".gif", "🎞️"}, {".bmp", "🖼️"}, {".ico", "🔷"},
                {".svg", "🎨"}, {".webp", "🖼️"},

                // 音频类型
                {".mp3", "🎵"}, {".wav", "🎵"}, {".ogg", "🎵"},
                {".m4a", "🎵"}, {".flac", "🎵"},

                // 视频类型
                {".mp4", "🎬"}, {".avi", "🎬"}, {".mov", "🎬"},
                {".wmv", "🎬"}, {".webm", "🎬"}, {".mkv", "🎬"},

                // 压缩文件
                {".zip", "📦"}, {".rar", "📦"}, {".tar", "📦"},
                {".gz", "📦"}, {".7z", "📦"},

                // 代码文件
                {".cpp", "💻"}, {".c", "💻"}, {".h", "💻"},
                {".py", "🐍"}, {".java", "☕"}, {".php", "🐘"},
                {".rb", "💎"}, {".go", "🐹"}, {".rs", "🦀"},

                // 配置文件
                {".conf", "⚙️"}, {".cfg", "⚙️"}, {".ini", "⚙️"},
                {".yaml", "⚙️"}, {".yml", "⚙️"}, {".toml", "⚙️"}
            };

            auto it = icon_map.find(extension);
            return (it != icon_map.end()) ? it->second : "📄";
        }

        std::string HttpServer::getFileTypeDescription(const std::string& filename) {
            // 获取文件类型描述
            std::string extension = getFileExtension(filename);
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            static const std::unordered_map<std::string, std::string> type_map = {
                {".html", "HTML Document"}, {".htm", "HTML Document"},
                {".css", "Stylesheet"}, {".js", "JavaScript"},
                {".json", "JSON Data"}, {".xml", "XML Document"},
                {".txt", "Text File"}, {".md", "Markdown"},
                {".pdf", "PDF Document"}, {".doc", "Word Document"},
                {".png", "PNG Image"}, {".jpg", "JPEG Image"}, {".jpeg", "JPEG Image"},
                {".gif", "GIF Image"}, {".svg", "SVG Image"},
                {".mp3", "MP3 Audio"}, {".wav", "WAV Audio"},
                {".mp4", "MP4 Video"}, {".avi", "AVI Video"},
                {".zip", "ZIP Archive"}, {".rar", "RAR Archive"},
                {".cpp", "C++ Source"}, {".c", "C Source"}, {".h", "Header File"},
                {".py", "Python Script"}, {".java", "Java Source"}
            };

            auto it = type_map.find(extension);
            if (it != type_map.end()) {
                return it->second;
            }

            if (!extension.empty()) {
                return extension.substr(1) + " File"; // 移除点号
            }

            return "Unknown";
        }

        void HttpServer::registerApiDiscoveryEndpoints(const std::string& service_name, 
                                                      const std::string& service_version,
                                                      const std::string& base_url) {
            // ⭐⭐⭐ 新增：注册黑名单管理API
            
            // GET /api/v1/blacklist - 获取所有黑名单条目
            this->get("/api/v1/blacklist", [](const HttpRequest& req, HttpResponse& res) {
                try {
                    auto entries = common::utils::IPBlacklistManager::getInstance().getAllEntries();
                    nlohmann::json entries_array = nlohmann::json::array();
                    for (const auto& entry : entries) {
                        entries_array.push_back(entry.toJson());
                    }
                    
                    nlohmann::json result;
                    result["success"] = true;
                    result["message"] = "Blacklist entries retrieved successfully";
                    result["count"] = entries.size();
                    result["data"] = entries_array;
                    res.ok(result);
                } catch (const std::exception& e) {
                    nlohmann::json error_result;
                    error_result["success"] = false;
                    error_result["message"] = "Failed to retrieve blacklist entries";
                    error_result["error"] = e.what();
                    res.internalServerError(error_result.dump());
                }
            });
            
            // GET /api/v1/blacklist/stats - 获取黑名单统计
            this->get("/api/v1/blacklist/stats", [](const HttpRequest& req, HttpResponse& res) {
                try {
                    auto stats = common::utils::IPBlacklistManager::getInstance().getStatistics();
                    
                    nlohmann::json result;
                    result["success"] = true;
                    result["message"] = "Blacklist statistics retrieved successfully";
                    result["data"] = stats;
                    res.ok(result);
                } catch (const std::exception& e) {
                    nlohmann::json error_result;
                    error_result["success"] = false;
                    error_result["message"] = "Failed to retrieve blacklist statistics";
                    error_result["error"] = e.what();
                    res.internalServerError(error_result.dump());
                }
            });
            
            // POST /api/v1/blacklist - 添加IP到黑名单
            this->post("/api/v1/blacklist", [](const HttpRequest& req, HttpResponse& res) {
                try {
                    auto body = nlohmann::json::parse(req.getBody());
                    std::string ip_pattern = body.value("ip_pattern", "");
                    std::string reason = body.value("reason", "API block");
                    int duration = body.value("duration_seconds", 0);
                    
                    if (ip_pattern.empty()) {
                        nlohmann::json error_result;
                        error_result["success"] = false;
                        error_result["message"] = "Missing ip_pattern parameter";
                        res.badRequest(error_result.dump());
                        return;
                    }
                    
                    std::string entry_id = common::utils::IPBlacklistManager::getInstance()
                        .addToBlacklist(ip_pattern, reason, duration);
                    
                    if (entry_id.empty()) {
                        nlohmann::json error_result;
                        error_result["success"] = false;
                        error_result["message"] = "Failed to add to blacklist";
                        error_result["ip_pattern"] = ip_pattern;
                        res.internalServerError(error_result.dump());
                    } else {
                        nlohmann::json result;
                        result["success"] = true;
                        result["message"] = "IP added to blacklist successfully";
                        result["entry_id"] = entry_id;
                        result["ip_pattern"] = ip_pattern;
                        result["reason"] = reason;
                        result["is_permanent"] = (duration == 0);
                        if (duration > 0) {
                            result["duration_seconds"] = duration;
                        }
                        res.ok(result);
                    }
                } catch (const std::exception& e) {
                    nlohmann::json error_result;
                    error_result["success"] = false;
                    error_result["message"] = "Invalid request";
                    error_result["error"] = e.what();
                    res.badRequest(error_result.dump());
                }
            });
            
            // DELETE /api/v1/blacklist/:id - 从黑名单移除IP
            this->del("/api/v1/blacklist/:id", [](const HttpRequest& req, HttpResponse& res) {
                try {
                    std::string id = req.getParam("id");
                    if (id.empty()) {
                        nlohmann::json error_result;
                        error_result["success"] = false;
                        error_result["message"] = "Missing entry_id parameter";
                        res.badRequest(error_result.dump());
                        return;
                    }
                    
                    bool success = common::utils::IPBlacklistManager::getInstance()
                        .removeFromBlacklist(id);
                    
                    if (success) {
                        nlohmann::json result;
                        result["success"] = true;
                        result["message"] = "IP removed from blacklist successfully";
                        result["entry_id"] = id;
                        res.ok(result);
                    } else {
                        nlohmann::json error_result;
                        error_result["success"] = false;
                        error_result["message"] = "Blacklist entry not found";
                        error_result["entry_id"] = id;
                        res.notFound(error_result.dump());
                    }
                } catch (const std::exception& e) {
                    nlohmann::json error_result;
                    error_result["success"] = false;
                    error_result["message"] = "Failed to remove from blacklist";
                    error_result["error"] = e.what();
                    res.internalServerError(error_result.dump());
                }
            });
            
            // POST /api/v1/blacklist/check - 检查IP是否在黑名单中
            this->post("/api/v1/blacklist/check", [](const HttpRequest& req, HttpResponse& res) {
                try {
                    auto body = nlohmann::json::parse(req.getBody());
                    std::string ip = body.value("ip", "");
                    
                    if (ip.empty()) {
                        nlohmann::json error_result;
                        error_result["success"] = false;
                        error_result["message"] = "Missing ip parameter";
                        res.badRequest(error_result.dump());
                        return;
                    }
                    
                    bool is_blacklisted = common::utils::IPBlacklistManager::getInstance()
                        .isBlacklisted(ip);
                    
                    nlohmann::json result;
                    result["success"] = true;
                    result["message"] = "IP check completed successfully";
                    result["ip"] = ip;
                    result["is_blacklisted"] = is_blacklisted;
                    res.ok(result);
                } catch (const std::exception& e) {
                    nlohmann::json error_result;
                    error_result["success"] = false;
                    error_result["message"] = "Invalid request";
                    error_result["error"] = e.what();
                    res.badRequest(error_result.dump());
                }
            });
            
            // DELETE /api/v1/blacklist - 清空所有黑名单
            this->del("/api/v1/blacklist", [](const HttpRequest& req, HttpResponse& res) {
                common::utils::IPBlacklistManager::getInstance().clearAll();
                nlohmann::json result;
                result["success"] = true;
                result["message"] = "All blacklist entries cleared";
                res.ok(result);
            });
            
            LOG_INFO("✅ 黑名单管理API已注册");
            
            // ... existing code ...
            LOG_INFO("🚀 [API_DISCOVERY] 注册API发现端点...");
            
            // 准备服务信息
            nlohmann::json service_info = {
                {"title", service_name + " API"},
                {"description", "自动生成的API文档"},
                {"version", service_version},
                {"base_url", base_url.empty() ? ("http://" + config_.network_config.bind_address + ":" + std::to_string(config_.network_config.listen_port)) : base_url},
                {"server_description", service_name + " 服务"}
            };

            // 1. 注册API端点列表接口
            router_->get("/api/v1/service/endpoints", [this]([[maybe_unused]] const HttpRequest& req, HttpResponse& res) {
                try {
                    auto endpoints = this->getAllApiEndpoints();
                    
                    nlohmann::json response = {
                        {"success", true},
                        {"count", endpoints.size()},
                        {"endpoints", nlohmann::json::array()}
                    };
                    
                    for (const auto& endpoint : endpoints) {
                        response["endpoints"].push_back(endpoint.toJson());
                    }
                    
                    res.setStatus(200);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(response.dump(2));
                    
                    // LOG_DEBUG("📊 [API_DISCOVERY] 返回 " + std::to_string(endpoints.size()) + " 个API端点");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ [API_DISCOVERY] 获取API端点失败: " + std::string(e.what()));
                    res.setStatus(500);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(R"({"success": false, "error": "内部服务器错误"})");
                }
            });
            
            // 2. 注册OpenAPI文档接口
            router_->get("/api/v1/service/openapi", [this, service_info]([[maybe_unused]] const HttpRequest& req, HttpResponse& res) {
                try {
                    auto openapi_spec = this->getOpenApiSpec(service_info);
                    
                    res.setStatus(200);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(openapi_spec.dump(2));
                    
                    // LOG_DEBUG("📑 [API_DISCOVERY] 返回OpenAPI文档");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ [API_DISCOVERY] 生成OpenAPI文档失败: " + std::string(e.what()));
                    res.setStatus(500);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(R"({"success": false, "error": "内部服务器错误"})");
                }
            });
            
            // 3. 注册服务信息接口
            router_->get("/api/v1/service/info", [this, service_name, service_version, service_info]([[maybe_unused]] const HttpRequest& req, HttpResponse& res) {
                try {
                    auto stats = this->getStatsSnapshot();
                    auto endpoint_count = this->getAllApiEndpoints().size();
                    
                    nlohmann::json response = {
                        {"service_name", service_name},
                        {"service_version", service_version},
                        {"service_type", "http"},
                        {"base_url", service_info["base_url"]},
                        {"status", "healthy"},
                        {"uptime_seconds", stats.uptime_seconds},
                        {"api_count", endpoint_count},
                        {"total_requests", stats.total_requests},
                        {"active_connections", stats.active_connections},
                        {"features", {
                            "api_discovery",
                            "openapi_documentation", 
                            "route_management", 
                            "middleware_support"
                        }}
                    };
                    
                    res.setStatus(200);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(response.dump(2));
                    
                    // LOG_DEBUG("ℹ️ [API_DISCOVERY] 返回服务信息");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ [API_DISCOVERY] 获取服务信息失败: " + std::string(e.what()));
                    res.setStatus(500); 
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(R"({"success": false, "error": "内部服务器错误"})");
                }
            });
            
            // 4. 注册路由列表接口（简化版本）
            router_->get("/api/v1/service/routes", [this]([[maybe_unused]] const HttpRequest& req, HttpResponse& res) {
                try {
                    auto routes = this->getAllRoutes();
                    
                    nlohmann::json response = {
                        {"success", true},
                        {"count", routes.size()},
                        {"routes", routes}
                    };
                    
                    res.setStatus(200);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(response.dump(2));
                    
                    // LOG_DEBUG("🛤️ [API_DISCOVERY] 返回 " + std::to_string(routes.size()) + " 个路由");
                } catch (const std::exception& e) {
                    LOG_ERROR("❌ [API_DISCOVERY] 获取路由列表失败: " + std::string(e.what()));
                    res.setStatus(500);
                    res.setHeader("Content-Type", "application/json");
                    res.setBody(R"({"success": false, "error": "内部服务器错误"})");
                }
            });
            
            LOG_INFO("✅ [API_DISCOVERY] API发现端点注册完成:");
            LOG_INFO("  - GET /api/v1/service/endpoints   (获取所有API端点详细信息)");
            LOG_INFO("  - GET /api/v1/service/openapi     (获取OpenAPI文档)");
            LOG_INFO("  - GET /api/v1/service/info        (获取服务信息)");
            LOG_INFO("  - GET /api/v1/service/routes      (获取路由列表)");
        }


    } // namespace http
} // namespace common
