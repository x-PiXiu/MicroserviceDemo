/**
 * @file http_server.h
 * @brief HTTP服务器框架主类 - 基于事件驱动的高性能HTTP服务器
 * @author USER
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - 基于EventLoop的事件驱动架构
 * - 支持Keep-Alive长连接
 * - 线程池异步请求处理
 * - 路由管理和中间件支持
 * - 配置热更新
 * - 完善的错误处理和日志记录
 * - 性能监控和统计
 * - SSL/TLS支持（可选）
 *
 * 架构设计:
 * - 主线程：事件循环，接受连接
 * - 工作线程：处理HTTP请求
 * - IO线程：网络数据传输
 * - 统计线程：性能监控和统计
 *
 * 与现有模块集成:
 * - 使用网络模块的EventLoop、Socket、Channel
 * - 集成ThreadPool进行并发处理
 * - 使用ConfigManager进行配置管理
 * - 使用Logger进行日志记录
 * - 支持与TaskScheduler的任务调度集成
 *
 * 使用示例:
 * @code
 * HttpServerConfig config;
 * config.host = "0.0.0.0";
 * config.port = 8080;
 * config.worker_threads = 4;
 * 
 * HttpServer server(config);
 * 
 * // 添加中间件
 * server.use(std::make_shared<CorsMiddleware>());
 * server.use(std::make_shared<LoggingMiddleware>());
 * 
 * // 添加路由
 * server.get("/api/users", userListHandler);
 * server.post("/api/users", createUserHandler);
 * 
 * // 启动服务器
 * server.start();
 * @endcode
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <vector>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdint>

#include <climits>

#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/http/http_handler.h"
#include "common/http/http_router.h"
#include "common/http/http_session.h"
#include "common/http/http_middleware.h"
#include "common/network/HierarchicalTimingWheel.h"
#include "common/network/event_loop.h"
#include "common/network/socket.h"
#include "common/network/channel.h"
#include "common/network/inet_address.h"
#include "common/thread_pool/thread_pool.h"
#include "common/logger/logger.h"
#include "common/utils/ip_blacklist_manager.h"  // ⭐ 新增：黑名单管理
#include <nlohmann/json.hpp>

namespace common {
    namespace http {

        /**
         * @brief HTTP服务器配置
         */
        struct HttpServerConfig {
            common::config::NetworkConfig network_config;

            // HTTP协议配置
            size_t max_request_size = 1024 * 1024;     ///< 最大请求大小（1MB）
            size_t max_header_size = 8192;             ///< 最大头部大小（8KB）
            int request_timeout_ms = 30000;            ///< 请求超时时间（30秒）
            int keep_alive_timeout_ms = 60000;         ///< Keep-Alive超时时间（60秒）
            int max_keep_alive_requests = 10000;         ///< 单连接最大请求数

            // 线程池配置
            int worker_threads = 0;                    ///< 工作线程数（0=自动检测）
            bool enable_thread_pool = true;            ///< 启用线程池

            // 性能优化配置
            bool enable_tcp_nodelay = true;            ///< 禁用Nagle算法
            bool enable_so_reuseaddr = true;           ///< SO_REUSEADDR选项
            bool enable_so_linger = true;              ///< 启用SO_LINGER控制
            int linger_timeout = 0;                    ///< SO_LINGER超时时间（0=立即关闭）
            bool enable_keep_alive = true;             ///< 启用TCP Keep-Alive
            int send_buffer_size = 65536;              ///< 发送缓冲区大小（64KB）
            int recv_buffer_size = 65536;              ///< 接收缓冲区大小（64KB）

            // 和监控配置
            bool enable_performance_monitoring = true; ///< 启用性能监控
            int stats_interval_ms = 30000;              ///< 统计间隔（5秒）

            // 静态文件服务配置
            bool enable_directory_listing = false;     ///< 启用目录浏览
            size_t max_static_file_size = 100 * 1024 * 1024; ///< 最大静态文件大小（100MB）
            int static_file_cache_time = 3600;         ///< 静态文件缓存时间（秒）
            
            // ⭐⭐⭐ 阶段2性能优化配置（从配置文件读取）
            size_t session_shard_count = 64;           ///< 会话分段锁数量（必须是2的幂次方）
            size_t route_cache_size = 1024;            ///< 路由缓存最大条目数
            std::vector<std::string> lightweight_paths; ///< 轻量级请求路径列表（快速通道）


            /**
             * @brief 从配置管理器加载http配置
             * @return http配置实例
             */
            static HttpServerConfig fromConfigManager();

            /**
             * @brief 验证配置
             * @return 配置有效返回true
             */
            bool validate() const {
                try
                {
                    network_config.validate();
                    return true;
                } catch (const std::invalid_argument& e)
                {
                    LOG_ERROR("httpServerConfig validate error: " + std::string(e.what()));
                    return false;
                }
            }
        };

        /**
         * @brief HTTP服务器统计信息
         */
        struct HttpServerStats {
            std::atomic<uint64_t> total_connections{0};     ///< 总连接数
            std::atomic<uint64_t> active_connections{0};    ///< 活跃连接数
            std::atomic<uint64_t> total_requests{0};        ///< 总请求数
            std::atomic<uint64_t> total_responses{0};       ///< 总响应数
            std::atomic<uint64_t> error_count{0};           ///< 错误次数
            std::atomic<uint64_t> timeout_count{0};         ///< 超时次数
            std::atomic<uint64_t> reject_count{0};         ///< 超时次数
            std::chrono::steady_clock::time_point start_time; ///< 启动时间

            /**
             * @brief 构造函数
             */
            HttpServerStats() : start_time(std::chrono::steady_clock::now()) {}

            /**
             * @brief 获取运行时间（秒）
             */
            long getUptimeSeconds() const {
                auto now = std::chrono::steady_clock::now();
                return std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            }

            /**
             * @brief 转换为JSON格式
             */
            nlohmann::json toJson() const {
                return {
                    {"total_connections", total_connections.load()},
                    {"active_connections", active_connections.load()},
                    {"total_requests", total_requests.load()},
                    {"total_responses", total_responses.load()},
                    {"error_count", error_count.load()},
                    {"timeout_count", timeout_count.load()},
                    {"uptime_seconds", getUptimeSeconds()}
                };
            }
        };

        /**
         * @brief 可拷贝的统计数据快照
         */
        struct ServerStatsSnapshot {
            uint64_t total_connections;
            uint64_t active_connections;
            uint64_t total_requests;
            uint64_t total_responses;
            uint64_t error_count;
            uint64_t timeout_count;
            long uptime_seconds;

            /**
             * @brief 从HttpServerStats创建快照
             */
            static ServerStatsSnapshot fromStats(const HttpServerStats& stats) {
                return {
                    stats.total_connections.load(),
                    stats.active_connections.load(),
                    stats.total_requests.load(),
                    stats.total_responses.load(),
                    stats.error_count.load(),
                    stats.timeout_count.load(),
                    stats.getUptimeSeconds()
                };
            }

            /**
             * @brief 转换为JSON格式
             */
            nlohmann::json toJson() const {
                return {
                    {"total_connections", total_connections},
                    {"active_connections", active_connections},
                    {"total_requests", total_requests},
                    {"total_responses", total_responses},
                    {"error_count", error_count},
                    {"timeout_count", timeout_count},
                    {"uptime_seconds", uptime_seconds}
                };
            }
        };

        /**
         * @brief HTTP服务器主类
         * @details 基于事件驱动的高性能HTTP服务器实现
         * 
         * 核心职责:
         * - 网络连接管理：监听、接受、管理客户端连接
         * - 请求处理：HTTP请求解析、路由分发、响应生成
         * - 中间件管理：中间件注册、执行、生命周期管理
         * - 性能优化：线程池、连接池、缓存等性能优化
         * - 监控统计：连接统计、性能监控、健康检查
         * - 配置管理：配置加载、热更新、验证
         */
        class HttpServer {
        public:
            /**
             * @brief 构造函数
             * @param config 服务器配置
             */
            explicit HttpServer(const HttpServerConfig& config = HttpServerConfig{});

            /**
             * @brief 构造函数（使用外部线程池）
             * @param config 服务器配置
             * @param thread_pool 外部线程池
             */
            HttpServer(const HttpServerConfig& config, std::shared_ptr<common::thread_pool::ThreadPool> thread_pool);

            /**
             * @brief 构造函数（简化版本）
             * @param host 监听地址
             * @param port 监听端口
             */
            HttpServer(const std::string& host, int port);

            /**
             * @brief 析构函数
             */
            ~HttpServer();

            // 禁用拷贝构造和赋值
            HttpServer(const HttpServer&) = delete;
            HttpServer& operator=(const HttpServer&) = delete;

            // ==================== 服务器控制方法 ====================

            /**
             * @brief 启动服务器
             * @return 启动成功返回true
             */
            bool start();

            /**
             * @brief 仅运行事件循环（用于分离初始化和运行）
             * @details 配合initialize()使用，允许在事件循环运行前执行其他初始化
             *          主要用于WebSocket等需要在EventLoop创建后、运行前初始化的组件
             */
            void startEventLoop();

            /**
             * @brief 停止服务器
             */
            void stop();

            /**
             * @brief 等待服务器停止
             */
            void waitForStop();

            std::string getHost() {
                return listen_address_->toIp();
            }

            std::vector<std::string> getIps() {
                return listen_address_->getLocalIPs();
            }

            uint16_t getPort() {
                return listen_address_->port();
            }

            std::string getAddress() {
                return listen_address_->toIpPort();
            }
            /**
             * @brief 检查服务器是否正在运行
             */
            bool isRunning() const { return running_.load(); }

            // ==================== 路由管理方法 ====================

            /**
             * @brief 添加路由
             * @param method HTTP方法
             * @param path 路径
             * @param handler 处理器
             */
            void route(const std::string& method, const std::string& path, RequestHandler handler);

            /**
             * @brief GET路由
             */
            void get(const std::string& path, RequestHandler handler);

            /**
             * @brief POST路由
             */
            void post(const std::string& path, RequestHandler handler);

            /**
             * @brief PUT路由
             */
            void put(const std::string& path, RequestHandler handler);

            /**
             * @brief DELETE路由
             */
            void del(const std::string& path, RequestHandler handler);

            /**
             * @brief PATCH路由
             */
            void patch(const std::string& path, RequestHandler handler);

            /**
             * @brief OPTIONS路由
             */
            void options(const std::string& path, RequestHandler handler);

            // ==================== 中间件管理方法 ====================

            /**
             * @brief 添加全局中间件
             * @param middleware 中间件
             */
            void use(MiddlewareHandler middleware);
            void use(MiddlewareHandler middleware, std::string middleName);

            /**
             * @brief 添加路径特定中间件
             * @param path 路径
             * @param middleware 中间件
             */
            void use(const std::string& path, MiddlewareHandler middleware);

            // ==================== 静态文件服务 ====================

            /**
             * @brief 设置静态文件服务
             * @param path URL路径
             * @param directory 本地目录
             */
            void serveStatic(const std::string& path, const std::string& directory);

            // ==================== 错误处理 ====================

            /**
             * @brief 设置错误处理器
             * @param handler 错误处理器
             */
            void setErrorHandler(ErrorHandler handler);

            /**
             * @brief 设置404处理器
             * @param handler 404处理器
             */
            void setNotFoundHandler(RequestHandler handler);

            // ==================== 配置管理 ====================

            /**
             * @brief 更新配置
             * @param config 新配置
             */
            void updateConfig(const HttpServerConfig& config);

            /**
             * @brief 从配置管理器加载配置
             */
            void loadConfigFromManager();

            /**
             * @brief 获取当前配置
             */
            const HttpServerConfig& getConfig() const { return config_; }

            /**
             * @brief 获取EventLoop
             * @return EventLoop指针，用于与其他组件共享事件循环
             * @details 此方法主要用于WebSocket Handler等组件共享HTTP服务器的EventLoop，
             *          实现统一的事件处理和资源优化
             */
            std::shared_ptr<common::network::EventLoop> getEventLoop() const {
                return event_loop_;
            }

            // ==================== 统计和监控 ====================

            /**
             * @brief 获取服务器统计信息
             */
            const HttpServerStats& getStats() const { return stats_; }

            /**
             * @brief 获取可拷贝的统计数据快照
             */
            ServerStatsSnapshot getStatsSnapshot() const { 
                return ServerStatsSnapshot::fromStats(stats_); 
            }

            /**
             * @brief 获取活跃会话数
             * @details ⭐⭐⭐ 阶段2优化1: 使用原子计数器，无需锁
             */
            size_t getActiveSessionCount() const {
                size_t total = 0;
                for (const auto& shard : session_shards_) {
                    total += shard.session_count.load();  // ✅ 原子操作，无锁
                }
                return total;
            }

            std::vector<std::string> getAllRoutes()
            {
                return router_->getAllRoutes();
            }

            std::vector<std::string> getAllRoutes(const std::string& method)
            {
                return router_->getRoutesByMethod(method);
            }

            bool hasRoute(const std::string& method, const std::string& path) const
            {
                return router_->hasRoute(method, path);
            }

            // ==================== API发现和文档生成 ====================

            /**
             * @brief 获取所有API端点信息
             * @return API端点信息列表
             */
            std::vector<HttpRouter::ApiEndpoint> getAllApiEndpoints() const {
                return router_->getAllApiEndpoints();
            }

            std::vector<HttpRouter::ApiEndpoint> getApiEndpointsByMethod(const std::string& method) const {
                return router_->getApiEndpointsByMethod(method);
            }

            /**
             * @brief 打印整棵路由树
             */
            void printRoutes(const std::string& method = "") const {
                router_->printRoutes(method);
            }

            /**
             * @brief 设置API端点元数据
             * @param method HTTP方法
             * @param path 路径模式
             * @param metadata 元数据
             */
            void setApiMetadata(const std::string& method, const std::string& path, const HttpRouter::ApiEndpoint& metadata) {
                router_->setApiMetadata(method, path, metadata);
            }

            /**
             * @brief 获取OpenAPI规范文档
             * @param service_info 服务信息
             * @return OpenAPI格式的JSON
             */
            nlohmann::json getOpenApiSpec(const nlohmann::json& service_info = {}) const {
                return router_->getOpenApiSpec(service_info);
            }

            /**
             * @brief 注册标准的API发现端点
             * @param service_name 服务名称
             * @param service_version 服务版本
             * @param base_url 服务基础URL
             */
            void registerApiDiscoveryEndpoints(const std::string& service_name = "microservice", 
                                              const std::string& service_version = "1.0.0",
                                              const std::string& base_url = "");

            /**
             * @brief 初始化服务器
             * @return 初始化成功返回true
             */
            bool initialize();

        private:
            // ==================== 静态文件服务数据结构 ====================

            /**
             * @brief 文件信息结构
             */
            struct FileInfo {
                bool valid = false;                 ///< 信息是否有效
                size_t size = 0;                    ///< 文件大小
                time_t last_modified = 0;           ///< 最后修改时间
                std::string etag;                   ///< ETag值
                bool is_directory = false;          ///< 是否为目录
            };

            /**
             * @brief HTTP范围请求信息
             */
            struct RangeInfo {
                bool valid = false;                 ///< 范围是否有效
                size_t start = 0;                   ///< 起始位置
                size_t end = 0;                     ///< 结束位置
            };

            /**
             * @brief 目录项信息
             */
            struct DirectoryEntry {
                std::string name;                   ///< 文件/目录名
                bool is_directory = false;          ///< 是否为目录
                size_t size = 0;                    ///< 文件大小
                time_t last_modified = 0;           ///< 最后修改时间
            };

            // ==================== 私有成员变量 ====================
            HttpServerConfig config_;                           ///< 服务器配置
            std::atomic<bool> running_;                         ///< 是否正在运行
            std::atomic<bool> stopping_;                       ///< 是否正在停止

            // 网络组件
            std::shared_ptr<common::network::EventLoop> event_loop_;        ///< 事件循环（使用shared_ptr支持共享）
            std::unique_ptr<common::network::Socket> listen_socket_;        ///< 监听套接字
            std::unique_ptr<common::network::Channel> listen_channel_;      ///< 监听通道
            std::unique_ptr<common::network::InetAddress> listen_address_;  ///< 监听地址

            // 处理组件
            std::unique_ptr<HttpRouter> router_;                ///< 路由器
            std::unique_ptr<MiddlewareManager> middleware_manager_; ///< 中间件管理器
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_; ///< 线程池

            // ⭐⭐⭐ 阶段2优化1: 分段锁 - 降低锁竞争64倍
            static constexpr size_t SHARD_COUNT = 64;  ///< 分段数量（2的幂次方）
            
            /**
             * @brief 会话分段结构
             */
            struct SessionShard {
                std::unordered_map<int, std::shared_ptr<HttpSession>> sessions; ///< 该分段的会话
                mutable std::mutex mutex;                                       ///< 分段锁
                std::atomic<size_t> session_count{0};                          ///< 会话计数（原子操作）
            };
            
            std::array<SessionShard, SHARD_COUNT> session_shards_;  ///< 会话分段数组
            
            /**
             * @brief 计算会话ID对应的分段索引
             * @param session_id 会话ID
             * @return 分段索引（0-63）
             */
            size_t getShardIndex(int session_id) const {
                return std::hash<int>{}(session_id) % SHARD_COUNT;
            }

            // 处理器
            ErrorHandler error_handler_;                        ///< 错误处理器
            RequestHandler not_found_handler_;                  ///< 404处理器

            // 统计信息
            HttpServerStats stats_;                             ///< 统计信息
            std::thread stats_thread_;                          ///< 统计线程（兼容模式）
            std::atomic<bool> is_use_stats_thread_;             ///< 是否使用统计线程


            
            // 🔧 优化：定时器管理（替代独立线程）
            uint64_t stats_timer_id_ = 0;                       ///< 统计信息输出定时器ID
            uint64_t timeout_check_timer_id_ = 0;               ///< 超时检查定时器ID

            // ==================== 私有方法 ====================

            /**
             * @brief 处理新连接
             */
            void handleNewConnection();

            /**
             * @brief 处理HTTP请求
             * @param session 会话
             * @param request 请求
             * @param response 响应
             */
            void handleHttpRequest(HttpRequest& request, HttpResponse& response);

            /**
             * @brief 处理请求
             * @param request 请求
             * @param response 响应
             */
            void processRequest(HttpRequest& request, HttpResponse& response);

            /**
             * @brief 清理会话
             * @param session_id 会话ID
             */
            void cleanupSession(int session_id);

            /**
             * @brief 统计线程函数（保留兼容）
             */
            void statsThreadFunc();
            
            /**
             * 🔧 优化：启动基于定时器的监控（替代独立线程）
             */
            void startTimerBasedMonitoring();
            
            /**
             * 🔧 优化：停止基于定时器的监控
             */
            void stopTimerBasedMonitoring();
            
            /**
             * 🔧 性能统计方法
             */
            void logPerformanceStats();
            
            /**
             * 🔧 连接超时检查方法
             */
            void checkConnectionTimeouts();
            
            /**
             * 🔧 优化：检查和清理超时会话（定时器回调）
             */
            // void checkAndCleanupTimeoutSessions();  // 🔧 删除无用函数
            
            /**
             * 监控系统实现方法
             */
            void startTimerWheelTicker();                        ///< 启动时间轮ticker
            void startThreadBasedMonitoring();                   ///< 启动线程监控（兜底方案）
            void stopTimerWheelMonitoring();                     ///< 停止时间轮监控
            void stopThreadBasedMonitoring();                    ///< 停止线程监控

            /**
             * @brief 设置默认处理器
             */
            void setupDefaultHandlers();

            // ==================== 静态文件服务私有方法 ====================

            /**
             * @brief 处理静态文件请求（核心方法）
             * @param request HTTP请求
             * @param response HTTP响应
             * @param base_directory 基础目录
             * @param url_path URL路径前缀
             * @param head_only 是否只返回头部（HEAD请求）
             */
            void handleStaticFileRequest(const HttpRequest& request, HttpResponse& response,
                                        const std::string& base_directory, const std::string& url_path, bool head_only = false);

            /**
             * @brief 从请求路径提取文件路径
             * @param request_path 请求路径
             * @param url_path URL前缀
             * @return 文件路径
             */
            std::string extractFilePath(const std::string& request_path, const std::string& url_path);

            /**
             * @brief 检查路径安全性（防止目录遍历攻击）
             * @param full_path 完整路径
             * @param base_directory 基础目录
             * @return 路径是否安全
             */
            bool isPathSafe(const std::string& full_path, const std::string& base_directory);

            /**
             * @brief 标准化目录路径
             * @param path 目录路径
             * @return 标准化后的路径
             */
            std::string normalizeDirectoryPath(const std::string& path);

            /**
             * @brief 标准化文件路径
             * @param path 文件路径
             * @return 标准化后的路径
             */
            std::string normalizeFilePath(const std::string& path);

            /**
             * @brief 获取规范化路径
             * @param path 路径
             * @return 规范化路径
             */
            std::string getCanonicalPath(const std::string& path);

            /**
             * @brief 检查文件是否存在
             * @param file_path 文件路径
             * @return 文件是否存在
             */
            bool fileExists(const std::string& file_path);

            /**
             * @brief 检查目录是否存在
             * @param dir_path 目录路径
             * @return 目录是否存在
             */
            bool directoryExists(const std::string& dir_path);

            /**
             * @brief 检查文件读取权限
             * @param file_path 文件路径
             * @return 是否有读取权限
             */
            bool hasFileReadPermission(const std::string& file_path);

            /**
             * @brief 检查目录读取权限
             * @param dir_path 目录路径
             * @return 是否有读取权限
             */
            bool hasDirectoryReadPermission(const std::string& dir_path);

            // ==================== 文件信息和内容处理方法 ====================

            /**
             * @brief 获取文件信息
             * @param file_path 文件路径
             * @return 文件信息
             */
            FileInfo getFileInfo(const std::string& file_path);

            /**
             * @brief 检查路径是否为目录
             * @param path 路径
             * @return 是否为目录
             */
            bool isDirectory(const std::string& path);

            /**
             * @brief 查找索引文件
             * @param dir_path 目录路径
             * @return 索引文件路径（如果找到）
             */
            std::string findIndexFile(const std::string& dir_path);

            /**
             * @brief 设置静态文件响应头
             * @param response HTTP响应
             * @param file_info 文件信息
             * @param file_path 文件路径
             */
            void setStaticFileHeaders(HttpResponse& response, const FileInfo& file_info, const std::string& file_path);

            /**
             * @brief 获取MIME类型
             * @param file_path 文件路径
             * @return MIME类型
             */
            std::string getMimeType(const std::string& file_path);

            /**
             * @brief 获取文件扩展名
             * @param file_path 文件路径
             * @return 文件扩展名
             */
            std::string getFileExtension(const std::string& file_path);

            /**
             * @brief 判断文件是否可缓存
             * @param file_path 文件路径
             * @return 是否可缓存
             */
            bool isCacheableFile(const std::string& file_path);

            /**
             * @brief 判断文件是否为可执行文件
             * @param file_path 文件路径
             * @return 是否为可执行文件
             */
            bool isExecutableFile(const std::string& file_path);

            // ==================== 条件请求和缓存处理方法 ====================

            /**
             * @brief 处理条件请求
             * @param request HTTP请求
             * @param response HTTP响应
             * @param file_info 文件信息
             * @return 是否返回304 Not Modified
             */
            bool handleConditionalRequest(const HttpRequest& request, HttpResponse& response, const FileInfo& file_info);

            /**
             * @brief 格式化HTTP日期
             * @param timestamp 时间戳
             * @return HTTP日期字符串
             */
            std::string formatHttpDate(time_t timestamp);

            /**
             * @brief 解析HTTP日期
             * @param date_str 日期字符串
             * @return 时间戳
             */
            time_t parseHttpDate(const std::string& date_str);

            // ==================== 文件内容读取方法 ====================

            /**
             * @brief 处理普通文件请求
             * @param response HTTP响应
             * @param file_info 文件信息
             * @param file_path 文件路径
             * @param head_only 是否只返回头部
             */
            void handleNormalFileRequest(HttpResponse& response, const FileInfo& file_info,
                                       const std::string& file_path, bool head_only = false);

            /**
             * @brief 读取文件内容
             * @param file_path 文件路径
             * @return 文件内容
             */
            std::string readFileContent(const std::string& file_path);

            /**
             * @brief 读取文件范围
             * @param file_path 文件路径
             * @param start 起始位置
             * @param length 读取长度
             * @return 文件内容
             */
            std::string readFileRange(const std::string& file_path, size_t start, size_t length);

            // ==================== 范围请求处理方法 ====================

            /**
             * @brief 处理范围请求
             * @param request HTTP请求
             * @param response HTTP响应
             * @param file_info 文件信息
             * @param file_path 文件路径
             * @param head_only 是否只返回头部
             */
            void handleRangeRequest(const HttpRequest& request, HttpResponse& response,
                                  const FileInfo& file_info, const std::string& file_path, bool head_only = false);

            /**
             * @brief 解析Range头
             * @param range_header Range头内容
             * @param file_size 文件大小
             * @return 范围信息
             */
            RangeInfo parseRangeHeader(const std::string& range_header, size_t file_size);

            // ==================== 目录处理方法 ====================

            /**
             * @brief 处理目录请求
             * @param request HTTP请求
             * @param response HTTP响应
             * @param dir_path 目录路径
             * @param url_path URL路径
             */
            void handleDirectoryRequest(const HttpRequest& request, HttpResponse& response,
                                      const std::string& dir_path, const std::string& url_path);

            /**
             * @brief 检查目录是否为空
             * @param dir_path 目录路径
             * @return 目录是否为空
             */
            bool isDirectoryEmpty(const std::string& dir_path);

            /**
             * @brief 生成空目录列表页面
             * @param response HTTP响应
             * @param dir_path 目录路径
             * @param url_path URL路径
             */
            void generateEmptyDirectoryListing(HttpResponse& response, const std::string& dir_path, const std::string& url_path);

            /**
             * @brief 生成403禁止访问页面
             * @param url_path URL路径
             * @return HTML页面内容
             */
            std::string generateForbiddenPage(const std::string& url_path);

            /**
             * @brief 获取父级路径
             * @param url_path URL路径
             * @return 父级路径
             */
            std::string getParentPath(const std::string& url_path);

            /**
             * @brief 获取当前时间字符串
             * @return 当前时间的字符串表示
             */
            std::string getCurrentTimeString();

            /**
             * @brief 生成目录列表
             * @param response HTTP响应
             * @param dir_path 目录路径
             * @param url_path URL路径
             */
            void generateDirectoryListing(HttpResponse& response, const std::string& dir_path, const std::string& url_path);

            /**
             * @brief 生成目录统计信息
             * @param html HTML流
             * @param dir_path 目录路径
             */
            void generateDirectoryStats(std::ostringstream& html, const std::string& dir_path);

            /**
             * @brief 列出目录内容
             * @param html HTML流
             * @param dir_path 目录路径
             * @param url_path URL路径
             */
            void listDirectoryContents(std::ostringstream& html, const std::string& dir_path, const std::string& url_path);

            /**
             * @brief 格式化文件大小
             * @param size 文件大小
             * @return 格式化后的大小字符串
             */
            std::string formatFileSize(size_t size);

            /**
             * @brief 获取文件图标
             * @param filename 文件名
             * @return 文件图标emoji
             */
            std::string getFileIcon(const std::string& filename);

            /**
             * @brief 获取文件类型描述
             * @param filename 文件名
             * @return 文件类型描述
             */
            std::string getFileTypeDescription(const std::string& filename);
        };

    } // namespace http
} // namespace common

#endif // HTTP_SERVER_H
