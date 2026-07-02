/**
 * @file http_middleware.h
 * @brief HTTP中间件框架 - 提供可插拔的请求处理中间件系统
 * @author 29108
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - 链式中间件执行模式
 * - 支持请求前置和后置处理
 * - 内置常用中间件（CORS、认证、日志等）
 * - 支持异步中间件处理
 * - 中间件优先级管理
 * - 条件中间件执行
 *
 * 设计模式:
 * - 责任链模式：中间件按顺序执行
 * - 装饰器模式：为请求处理添加额外功能
 * - 策略模式：不同的中间件实现不同的处理策略
 *
 * 使用示例:
 * @code
 * // 创建中间件管理器
 * MiddlewareManager manager;
 * 
 * // 添加内置中间件
 * manager.use(std::make_shared<CorsMiddleware>());
 * manager.use(std::make_shared<AuthMiddleware>("secret_key"));
 * manager.use(std::make_shared<LoggingMiddleware>());
 * 
 * // 添加自定义中间件
 * manager.use([](HttpRequest& req, HttpResponse& res, NextFunction next) {
 *     res.setHeader("X-Custom-Header", "MyValue");
 *     return next(); // 继续执行下一个中间件
 * });
 * 
 * // 执行中间件链
 * manager.execute(request, response, []() {
 *     // 最终的请求处理逻辑
 *     res.ok("Hello World");
 * });
 * @endcode
 */

#ifndef HTTP_MIDDLEWARE_H
#define HTTP_MIDDLEWARE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <regex>
#include <chrono>
#include <atomic>
#include <cstdint>

#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"
#include "common/thread_pool/thread_pool.h"

namespace common {
    namespace http {

        // 前置声明
        class MiddlewareContext;

        /**
         * @brief 下一个中间件执行函数类型
         */
        using NextFunction = std::function<bool()>;

        /**
         * @brief 中间件处理函数类型
         * @param request HTTP请求对象
         * @param response HTTP响应对象
         * @param next 下一个中间件执行函数
         * @return 是否继续执行后续中间件
         */
        using MiddlewareFunction = std::function<bool(HttpRequest&, HttpResponse&, NextFunction)>;

        /**
         * @brief 中间件执行结果
         */
        enum class MiddlewareResult {
            CONTINUE,    ///< 继续执行下一个中间件
            STOP,        ///< 停止执行，但不是错误
            ERROR        ///< 发生错误，停止执行
        };

        /**
         * @brief 中间件上下文信息
         * @details 在中间件执行过程中传递的上下文数据
         */
        class MiddlewareContext {
        public:
            /**
             * @brief 构造函数
             */
            MiddlewareContext() : start_time_(std::chrono::steady_clock::now()) {}

            /**
             * @brief 设置上下文属性
             * @param key 属性键
             * @param value 属性值
             */
            void set(const std::string& key, const std::string& value) {
                attributes_[key] = value;
            }

            /**
             * @brief 获取上下文属性
             * @param key 属性键
             * @param default_value 默认值
             * @return 属性值
             */
            std::string get(const std::string& key, const std::string& default_value = "") const {
                auto it = attributes_.find(key);
                return (it != attributes_.end()) ? it->second : default_value;
            }

            /**
             * @brief 检查是否存在属性
             * @param key 属性键
             * @return 存在返回true
             */
            bool has(const std::string& key) const {
                return attributes_.find(key) != attributes_.end();
            }

            /**
             * @brief 获取执行耗时（毫秒）
             * @return 耗时毫秒数
             */
            long getElapsedMs() const {
                auto now = std::chrono::steady_clock::now();
                return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
            }

            /**
             * @brief 重置上下文
             */
            void reset() {
                attributes_.clear();
                start_time_ = std::chrono::steady_clock::now();
            }

        private:
            std::map<std::string, std::string> attributes_;     ///< 上下文属性
            std::chrono::steady_clock::time_point start_time_; ///< 开始时间
        };

        /**
         * @brief HTTP中间件基类
         * @details 所有中间件的基类，定义了中间件的基本接口
         */
        class HttpMiddleware {
        public:
            /**
             * @brief 构造函数
             * @param name 中间件名称
             * @param priority 优先级（数值越大优先级越高）
             */
            explicit HttpMiddleware(const std::string& name = "HttpMiddleware", int priority = 0)
                : name_(name), priority_(priority), enabled_(true) {}

            /**
             * @brief 虚析构函数
             */
            virtual ~HttpMiddleware() = default;

            /**
             * @brief 执行中间件逻辑（纯虚函数）
             * @param request HTTP请求
             * @param response HTTP响应
             * @param context 中间件上下文
             * @param next 下一个中间件执行函数
             * @return 中间件执行结果
             */
            virtual MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                           MiddlewareContext& context, NextFunction next) = 0;

            /**
             * @brief 检查是否应该执行此中间件
             * @param request HTTP请求
             * @return 应该执行返回true
             */
            virtual bool shouldExecute(const HttpRequest& /* request */) const {
                return enabled_;
            }

            /**
             * @brief 获取中间件名称
             */
            const std::string& getName() const { return name_; }

            /**
             * @brief 获取优先级
             */
            int getPriority() const { return priority_; }

            /**
             * @brief 启用/禁用中间件
             */
            void setEnabled(bool enabled) { enabled_ = enabled; }

            /**
             * @brief 检查是否启用
             */
            bool isEnabled() const { return enabled_; }

            /**
             * @brief 初始化中间件
             * @return 初始化成功返回true
             */
            virtual bool initialize() {
                LOG_INFO("Initializing middleware: " + name_);
                return true;
            }

            /**
             * @brief 清理中间件资源
             */
            virtual void cleanup() {
                // 避免在析构时使用日志系统，使用标准输出
                std::cout << "[HttpMiddleware] Cleaning up middleware: " << name_ << std::endl;
            }

        protected:
            std::string name_;      ///< 中间件名称
            int priority_;          ///< 优先级
            std::atomic<bool> enabled_; ///< 是否启用
        };

        /**
         * @brief 函数式中间件
         * @details 将函数包装为中间件
         */
        class FunctionMiddleware : public HttpMiddleware {
        public:
            /**
             * @brief 构造函数
             * @param func 中间件函数
             * @param name 中间件名称
             * @param priority 优先级
             */
            explicit FunctionMiddleware(MiddlewareFunction func, 
                                      const std::string& name = "FunctionMiddleware",
                                      int priority = 0)
                : HttpMiddleware(name, priority), func_(func) {}

            MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                   MiddlewareContext& /* context */, NextFunction next) override {
                try {
                    if (func_ && func_(request, response, next)) {
                        // 函数返回 true 表示继续执行
                        // 注意：HttpServer::use() 的包装器已经在内部调用了 next()
                        // 所以这里只需要返回 CONTINUE，不需要再次调用 next()
                        return MiddlewareResult::CONTINUE;
                    }
                    return MiddlewareResult::STOP;
                } catch (const std::exception& e) {
                    LOG_ERROR("Function middleware error: " + std::string(e.what()));
                    return MiddlewareResult::ERROR;
                }
            }

        private:
            MiddlewareFunction func_;
        };

        /**
         * @brief 路径匹配中间件
         * @details 只对特定路径执行的中间件
         */
        class PathMiddleware : public HttpMiddleware {
        public:
            /**
             * @brief 构造函数
             * @param path_pattern 路径模式（支持正则表达式）
             * @param middleware 实际的中间件
             */
            PathMiddleware(const std::string& path_pattern, std::shared_ptr<HttpMiddleware> middleware)
                : HttpMiddleware("PathMiddleware_" + path_pattern, middleware->getPriority())
                , path_pattern_(path_pattern)
                , middleware_(middleware) {
                try {
                    path_regex_ = std::regex(path_pattern);
                    use_regex_ = true;
                } catch (const std::exception&) {
                    // 如果不是有效的正则表达式，使用字符串匹配
                    use_regex_ = false;
                }
            }

            bool shouldExecute(const HttpRequest& request) const override {
                if (!HttpMiddleware::shouldExecute(request)) {
                    return false;
                }

                std::string path = request.getPath();
                if (use_regex_) {
                    return std::regex_match(path, path_regex_);
                } else {
                    return path.find(path_pattern_) == 0; // 前缀匹配
                }
            }

            MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                   MiddlewareContext& context, NextFunction next) override {
                return middleware_->execute(request, response, context, next);
            }

        private:
            std::string path_pattern_;              ///< 路径模式
            std::shared_ptr<HttpMiddleware> middleware_; ///< 实际中间件
            std::regex path_regex_;                 ///< 路径正则表达式
            bool use_regex_;                        ///< 是否使用正则表达式
        };

        /**
         * @brief CORS中间件
         * @details 处理跨域资源共享
         */
        class CorsMiddleware : public HttpMiddleware {
        public:
            /**
             * @brief CORS配置
             */
            struct Config {
                std::string allow_origin = "*";                    ///< 允许的源
                std::vector<std::string> allow_methods = {         ///< 允许的方法
                    "GET", "POST", "PUT", "DELETE", "OPTIONS", "HEAD"
                };
                std::vector<std::string> allow_headers = {         ///< 允许的头部
                    "Content-Type", "Authorization", "X-Requested-With"
                };
                std::vector<std::string> expose_headers;           ///< 暴露的头部
                bool allow_credentials = false;                   ///< 是否允许凭据
                int max_age = 86400;                              ///< 预检请求缓存时间（秒）
            };

            /**
             * @brief 默认构造函数
             */
            CorsMiddleware() : HttpMiddleware("CorsMiddleware", 1000), config_() {}

            /**
             * @brief 构造函数
             * @param config CORS配置
             */
            explicit CorsMiddleware(const Config& config)
                : HttpMiddleware("CorsMiddleware", 1000), config_(config) {}

            MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                   MiddlewareContext& context, NextFunction next) override;

        private:
            Config config_;

            /**
             * @brief 处理预检请求
             */
            void handlePreflightRequest(const HttpRequest& request, HttpResponse& response);

            /**
             * @brief 设置CORS头部
             */
            void setCorsHeaders(const HttpRequest& request, HttpResponse& response);
        };

        /**
         * @brief 认证中间件
         * @details 处理JWT Token认证
         */
        class AuthMiddleware : public HttpMiddleware {
        public:
            /**
             * @brief 认证配置
             */
            struct Config {
                std::string secret_key;                           ///< JWT密钥
                std::string token_header = "Authorization";       ///< Token头部名称
                std::string token_prefix = "Bearer ";             ///< Token前缀
                int token_expiry_hours = 24;                     ///< Token过期时间（小时）
                std::vector<std::string> exclude_paths;           ///< 排除的路径
            };

            /**
             * @brief 构造函数
             * @param config 认证配置
             */
            explicit AuthMiddleware(const Config& config)
                : HttpMiddleware("AuthMiddleware", 900), config_(config) {}

            bool shouldExecute(const HttpRequest& request) const override;

            MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                   MiddlewareContext& context, NextFunction next) override;

        private:
            Config config_;

            /**
             * @brief 验证JWT Token
             * @param token JWT Token
             * @return 验证成功返回true
             */
            bool validateToken(const std::string& token) const;

            /**
             * @brief 检查路径是否被排除
             * @param path 请求路径
             * @return 被排除返回true
             */
            bool isPathExcluded(const std::string& path) const;
        };

        /**
         * @brief 日志中间件
         * @details 记录HTTP请求日志
         */
        class LoggingMiddleware : public HttpMiddleware {
        public:
            /**
             * @brief 日志配置
             */
            struct Config {
                bool log_request_headers = false;                 ///< 是否记录请求头
                bool log_request_body = false;                    ///< 是否记录请求体
                bool log_response_headers = false;                ///< 是否记录响应头
                bool log_response_body = false;                   ///< 是否记录响应体
                std::vector<std::string> exclude_paths = {        ///< 排除的路径
                    "/health", "/metrics"
                };
                size_t max_body_log_size = 1024;                 ///< 最大记录的请求体大小
            };

            /**
             * @brief 默认构造函数
             */
            LoggingMiddleware() : HttpMiddleware("LoggingMiddleware", 100), config_() {}

            /**
             * @brief 构造函数
             * @param config 日志配置
             */
            explicit LoggingMiddleware(const Config& config)
                : HttpMiddleware("LoggingMiddleware", 100), config_(config) {}

            bool shouldExecute(const HttpRequest& request) const override;

            MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                   MiddlewareContext& context, NextFunction next) override;

        private:
            Config config_;

            /**
             * @brief 记录请求信息
             */
            void logRequest(const HttpRequest& request, MiddlewareContext& context);

            /**
             * @brief 记录响应信息
             */
            void logResponse(const HttpRequest& request, const HttpResponse& response, 
                           MiddlewareContext& context);
        };

        /**
         * @brief 限流中间件
         * @details 实现请求频率限制
         */
        class RateLimitMiddleware : public HttpMiddleware {
        public:
            /**
             * @brief 限流配置
             */
            struct Config {
                int requests_per_minute = 60;                     ///< 每分钟请求数限制
                int burst_size = 10;                             ///< 突发请求数
                std::string key_generator = "ip";                ///< 限流键生成策略（ip/user/custom）
                std::vector<std::string> exclude_paths;           ///< 排除的路径
            };

            /**
             * @brief 默认构造函数
             */
            RateLimitMiddleware() : HttpMiddleware("RateLimitMiddleware", 800), config_() {}

            /**
             * @brief 构造函数
             * @param config 限流配置
             */
            explicit RateLimitMiddleware(const Config& config)
                : HttpMiddleware("RateLimitMiddleware", 800), config_(config) {}

            MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                                   MiddlewareContext& context, NextFunction next) override;

        private:
            Config config_;
            std::map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> rate_limits_; ///< 限流记录
            std::mutex rate_limit_mutex_; ///< 限流锁

            /**
             * @brief 生成限流键
             * @param request HTTP请求
             * @return 限流键
             */
            std::string generateKey(const HttpRequest& request) const;

            /**
             * @brief 检查是否超过限流
             * @param key 限流键
             * @return 超过限流返回true
             */
            bool isRateLimited(const std::string& key);
        };

        /**
         * @brief 中间件管理器
         * @details 管理和执行中间件链，支持优先级排序和条件执行
         *
         * 核心功能：
         * - 中间件注册和管理
         * - 按优先级排序执行
         * - 支持路径匹配中间件
         * - 提供中间件生命周期管理
         * - 集成项目现有的日志和配置系统
         *
         * 与现有模块的集成：
         * - 使用Logger进行日志记录
         * - 支持ConfigManager的配置热更新
         * - 可与ThreadPool集成进行异步处理
         */
        class MiddlewareManager {
        public:
            /**
             * @brief 构造函数
             */
            MiddlewareManager();

            /**
             * @brief 析构函数
             */
            ~MiddlewareManager();

            // 禁用拷贝构造和赋值
            MiddlewareManager(const MiddlewareManager&) = delete;
            MiddlewareManager& operator=(const MiddlewareManager&) = delete;

            /**
             * @brief 添加中间件
             * @param middleware 中间件实例
             */
            void use(std::shared_ptr<HttpMiddleware> middleware);

            /**
             * @brief 添加函数式中间件
             * @param func 中间件函数
             * @param name 中间件名称
             * @param priority 优先级
             */
            void use(MiddlewareFunction func, const std::string& name = "CustomMiddleware", int priority = 0);

            /**
             * @brief 添加路径特定中间件
             * @param path_pattern 路径模式
             * @param middleware 中间件实例
             */
            void use(const std::string& path_pattern, std::shared_ptr<HttpMiddleware> middleware);

            /**
             * @brief 执行中间件链
             * @param request HTTP请求
             * @param response HTTP响应
             * @param final_handler 最终处理函数
             * @return 执行成功返回true
             */
            bool execute(HttpRequest& request, HttpResponse& response,
                        std::function<void()> final_handler = nullptr);

            /**
             * @brief 移除指定名称的中间件
             * @param name 中间件名称
             * @return 移除成功返回true
             */
            bool remove(const std::string& name);

            /**
             * @brief 清空所有中间件
             */
            void clear();

            /**
             * @brief 获取中间件数量
             */
            size_t size() const { return middlewares_.size(); }

            /**
             * @brief 获取中间件列表
             */
            std::vector<std::string> getMiddlewareNames() const;

            /**
             * @brief 初始化所有中间件
             * @return 初始化成功返回true
             */
            bool initialize();

            /**
             * @brief 清理所有中间件
             */
            void cleanup();

            /**
             * @brief 启用/禁用指定中间件
             * @param name 中间件名称
             * @param enabled 是否启用
             */
            void setMiddlewareEnabled(const std::string& name, bool enabled);

            /**
             * @brief 从配置管理器加载中间件配置
             */
            void loadConfigFromManager();

            /**
             * @brief 获取中间件执行统计信息
             */
            std::map<std::string, uint64_t> getExecutionStats() const;

        private:
            std::vector<std::shared_ptr<HttpMiddleware>> middlewares_; ///< 中间件列表
            mutable std::mutex middlewares_mutex_;                    ///< 中间件列表锁
            std::map<std::string, uint64_t> execution_stats_;         ///< 执行统计
            std::atomic<bool> initialized_;                           ///< 是否已初始化

            /**
             * @brief 按优先级排序中间件
             */
            void sortMiddlewares();

            /**
             * @brief 递归执行中间件
             * @param request HTTP请求
             * @param response HTTP响应
             * @param context 中间件上下文
             * @param index 当前中间件索引
             * @param final_handler 最终处理函数
             * @return 执行成功返回true
             */
            bool executeMiddlewares(HttpRequest& request, HttpResponse& response,
                                  MiddlewareContext& context, size_t index,
                                  std::function<void()> final_handler);

            /**
             * @brief 更新执行统计
             * @param middleware_name 中间件名称
             */
            void updateExecutionStats(const std::string& middleware_name);
        };

    } // namespace http
} // namespace common

#endif // HTTP_MIDDLEWARE_H
