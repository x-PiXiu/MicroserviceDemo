/**
 * @file http_handler.h
 * @brief HTTP请求处理器框架 - 提供可扩展的请求处理接口
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - 抽象的请求处理接口
 * - 支持同步和异步处理
 * - 内置错误处理机制
 * - 支持处理器链模式
 * - 集成日志和监控
 * - 与现有模块深度集成
 *
 * 设计模式:
 * - 策略模式：不同的处理器实现不同的处理策略
 * - 模板方法模式：定义处理流程框架
 * - 责任链模式：支持处理器链式调用
 *
 * 与现有模块集成:
 * - 使用Logger进行日志记录
 * - 支持ThreadPool异步处理
 * - 集成ConfigManager配置管理
 *
 * 使用示例:
 * @code
 * class UserHandler : public HttpHandler {
 * public:
 *     void handle(const HttpRequest& req, HttpResponse& res, HttpContext& ctx) override {
 *         if (req.getPath() == "/api/users") {
 *             handleGetUsers(req, res, ctx);
 *         }
 *     }
 * };
 * 
 * auto handler = std::make_shared<UserHandler>();
 * server.route("GET", "/api/users", handler);
 * @endcode
 */

#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <map>

#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"

namespace common {
    namespace http {

        // 前置声明
        class HttpContext;

        /**
         * @brief HTTP处理器上下文
         * @details 在请求处理过程中传递的上下文信息
         */
        class HttpContext {
        public:
            /**
             * @brief 构造函数
             */
            HttpContext() : start_time_(std::chrono::steady_clock::now()) {}

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
             * @brief 获取处理耗时（毫秒）
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
         * @brief HTTP请求处理器基类
         * @details 所有HTTP请求处理器的基类，定义了处理器的基本接口
         * 
         * 核心职责:
         * - 定义请求处理接口
         * - 提供错误处理机制
         * - 支持处理器生命周期管理
         * - 集成日志和监控功能
         * - 支持异步处理
         */
        class HttpHandler {
        public:
            /**
             * @brief 构造函数
             * @param name 处理器名称
             */
            explicit HttpHandler(const std::string& name = "HttpHandler") 
                : name_(name), enabled_(true) {}

            /**
             * @brief 虚析构函数
             */
            virtual ~HttpHandler() = default;

            /**
             * @brief 处理HTTP请求（纯虚函数）
             * @param request HTTP请求
             * @param response HTTP响应
             * @param context 处理上下文
             */
            virtual void handle(const HttpRequest& request, HttpResponse& response, 
                              HttpContext& context) = 0;

            /**
             * @brief 处理HTTP请求（简化版本）
             * @param request HTTP请求
             * @param response HTTP响应
             */
            virtual void handle(const HttpRequest& request, HttpResponse& response) {
                HttpContext context;
                handle(request, response, context);
            }

            /**
             * @brief 检查是否应该处理此请求
             * @param request HTTP请求
             * @return 应该处理返回true
             */
            virtual bool shouldHandle(const HttpRequest& /* request */) const {
                return enabled_.load();
            }

            /**
             * @brief 获取处理器名称
             */
            const std::string& getName() const { return name_; }

            /**
             * @brief 启用/禁用处理器
             */
            void setEnabled(bool enabled) { enabled_.store(enabled); }

            /**
             * @brief 检查是否启用
             */
            bool isEnabled() const { return enabled_.load(); }

            /**
             * @brief 初始化处理器
             * @return 初始化成功返回true
             */
            virtual bool initialize() {
                LOG_INFO("Initializing handler: " + name_);
                return true;
            }

            /**
             * @brief 清理处理器资源
             */
            virtual void cleanup() {
                LOG_INFO("Cleaning up handler: " + name_);
            }

        protected:
            /**
             * @brief 处理异常
             * @param e 异常对象
             * @param request HTTP请求
             * @param response HTTP响应
             */
            virtual void handleException(const std::exception& e, const HttpRequest& /* request */,
                                       HttpResponse& response) {
                LOG_ERROR("Handler exception in " + name_ + ": " + std::string(e.what()));
                response.internalServerError("Internal server error");
            }

            std::string name_;              ///< 处理器名称
            std::atomic<bool> enabled_;     ///< 是否启用
        };

        /**
         * @brief 函数式HTTP处理器
         * @details 将函数包装为HTTP处理器
         */
        class FunctionHandler : public HttpHandler {
        public:
            using HandlerFunction = std::function<void(const HttpRequest&, HttpResponse&)>;
            using HandlerFunctionWithContext = std::function<void(const HttpRequest&, HttpResponse&, HttpContext&)>;

            /**
             * @brief 构造函数（简单函数版本）
             * @param func 处理函数
             * @param name 处理器名称
             */
            FunctionHandler(HandlerFunction func, const std::string& name = "FunctionHandler")
                : HttpHandler(name), simple_func_(func) {}

            /**
             * @brief 构造函数（带上下文函数版本）
             * @param func 处理函数
             * @param name 处理器名称
             */
            FunctionHandler(HandlerFunctionWithContext func, const std::string& name = "FunctionHandler")
                : HttpHandler(name), context_func_(func) {}

            void handle(const HttpRequest& request, HttpResponse& response, HttpContext& context) override {
                try {
                    if (context_func_) {
                        context_func_(request, response, context);
                    } else if (simple_func_) {
                        simple_func_(request, response);
                    } else {
                        LOG_ERROR("No handler function set for FunctionHandler: " + name_);
                        response.internalServerError("Handler not configured");
                    }
                } catch (const std::exception& e) {
                    handleException(e, request, response);
                }
            }

        private:
            HandlerFunction simple_func_;           ///< 简单处理函数
            HandlerFunctionWithContext context_func_; ///< 带上下文处理函数
        };

        /**
         * @brief 异步HTTP处理器
         * @details 支持异步处理的HTTP处理器基类
         */
        class AsyncHttpHandler : public HttpHandler {
        public:
            /**
             * @brief 构造函数
             * @param thread_pool 线程池
             * @param name 处理器名称
             */
            AsyncHttpHandler(std::shared_ptr<common::thread_pool::ThreadPool> thread_pool,
                           const std::string& name = "AsyncHttpHandler")
                : HttpHandler(name), thread_pool_(thread_pool) {}

            void handle(const HttpRequest& request, HttpResponse& response, HttpContext& context) override {
                if (!thread_pool_) {
                    LOG_ERROR("No thread pool available for async handler: " + name_);
                    response.internalServerError("Async processing not available");
                    return;
                }

                try {
                    // 异步处理
                    auto task = [this, request, &response, &context]() {
                        handleAsync(request, response, context);
                    };

                    // 提交任务到线程池
                    auto future = thread_pool_->submit(task);
                    
                    // 等待任务完成（可以设置超时）
                    future.wait();

                } catch (const std::exception& e) {
                    handleException(e, request, response);
                }
            }

            /**
             * @brief 异步处理HTTP请求（纯虚函数）
             * @param request HTTP请求
             * @param response HTTP响应
             * @param context 处理上下文
             */
            virtual void handleAsync(const HttpRequest& request, HttpResponse& response, 
                                   HttpContext& context) = 0;

        protected:
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_; ///< 线程池
        };

        /**
         * @brief 路由处理器
         * @details 基于路径模式的处理器
         */
        class RouteHandler : public HttpHandler {
        public:
            /**
             * @brief 构造函数
             * @param pattern 路径模式
             * @param handler 实际处理器
             */
            RouteHandler(const std::string& pattern, std::shared_ptr<HttpHandler> handler)
                : HttpHandler("RouteHandler_" + pattern)
                , pattern_(pattern)
                , handler_(handler) {}

            bool shouldHandle(const HttpRequest& request) const override {
                if (!HttpHandler::shouldHandle(request)) {
                    return false;
                }
                
                // 简单的路径匹配（可以扩展为更复杂的模式匹配）
                std::string path = request.getPath();
                return path.find(pattern_) == 0;
            }

            void handle(const HttpRequest& request, HttpResponse& response, HttpContext& context) override {
                if (handler_) {
                    handler_->handle(request, response, context);
                } else {
                    LOG_ERROR("No handler set for route pattern: " + pattern_);
                    response.internalServerError("Route handler not configured");
                }
            }

        private:
            std::string pattern_;                   ///< 路径模式
            std::shared_ptr<HttpHandler> handler_;  ///< 实际处理器
        };

        /**
         * @brief 处理器链
         * @details 支持多个处理器链式调用
         */
        class HandlerChain : public HttpHandler {
        public:
            /**
             * @brief 构造函数
             * @param name 处理器链名称
             */
            explicit HandlerChain(const std::string& name = "HandlerChain")
                : HttpHandler(name) {}

            /**
             * @brief 添加处理器
             * @param handler 处理器
             */
            void addHandler(std::shared_ptr<HttpHandler> handler) {
                if (handler) {
                    handlers_.push_back(handler);
                    LOG_INFO("Added handler to chain: " + handler->getName());
                }
            }

            /**
             * @brief 移除处理器
             * @param handler_name 处理器名称
             */
            void removeHandler(const std::string& handler_name) {
                handlers_.erase(
                    std::remove_if(handlers_.begin(), handlers_.end(),
                                 [&handler_name](const std::shared_ptr<HttpHandler>& h) {
                                     return h->getName() == handler_name;
                                 }),
                    handlers_.end());
                LOG_INFO("Removed handler from chain: " + handler_name);
            }

            /**
             * @brief 清空处理器链
             */
            void clear() {
                handlers_.clear();
                LOG_INFO("Cleared handler chain: " + name_);
            }

            void handle(const HttpRequest& request, HttpResponse& response, HttpContext& context) override {
                for (auto& handler : handlers_) {
                    if (handler && handler->shouldHandle(request)) {
                        try {
                            handler->handle(request, response, context);
                            
                            // 如果响应已经设置，停止处理
                            if (response.getStatus() != 200 || !response.getBody().empty()) {
                                break;
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("Handler chain exception: " + std::string(e.what()));
                            response.internalServerError("Handler chain error");
                            break;
                        }
                    }
                }
            }

            /**
             * @brief 获取处理器数量
             */
            size_t size() const { return handlers_.size(); }

        private:
            std::vector<std::shared_ptr<HttpHandler>> handlers_; ///< 处理器列表
        };

        // ==================== 类型别名 ====================

        /**
         * @brief 请求处理函数类型
         */
        using RequestHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

        /**
         * @brief 带上下文的请求处理函数类型
         */
        using RequestHandlerWithContext = std::function<void(const HttpRequest&, HttpResponse&, HttpContext&)>;

        /**
         * @brief 中间件处理函数类型
         */
        using MiddlewareHandler = std::function<bool(const HttpRequest&, HttpResponse&)>;

        /**
         * @brief 错误处理函数类型
         */
        using ErrorHandler = std::function<void(const std::exception&, const HttpRequest&, HttpResponse&)>;

    } // namespace http
} // namespace common

#endif // HTTP_HANDLER_H
