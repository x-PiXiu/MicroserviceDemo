/**
 * @file http_middleware.cpp
 * @brief HTTP中间件框架的实现
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 */

#include "common/http/http_middleware.h"
#include "common/auth/jwt_validator.h"
#include <algorithm>
#include <sstream>

namespace common {
    namespace http {

        // ==================== CorsMiddleware 实现 ====================

        MiddlewareResult CorsMiddleware::execute(HttpRequest& request, HttpResponse& response,
                                               MiddlewareContext& /* context */, NextFunction next) {
            try {
                // 检查是否为预检请求
                if (request.getMethod() == HttpMethod::OPTIONS) {
                    handlePreflightRequest(request, response);
                    return MiddlewareResult::STOP; // 预检请求直接返回，不继续处理
                }

                // 设置CORS头部
                setCorsHeaders(request, response);

                // 继续执行下一个中间件
                if (next()) {
                    return MiddlewareResult::CONTINUE;
                }
                return MiddlewareResult::STOP;

            } catch (const std::exception& e) {
                LOG_ERROR("CORS middleware error: " + std::string(e.what()));
                return MiddlewareResult::ERROR;
            }
        }

        void CorsMiddleware::handlePreflightRequest(const HttpRequest& request, HttpResponse& response) {
            // 设置预检请求的响应头
            response.setHeader("Access-Control-Allow-Origin", config_.allow_origin);
            
            // 设置允许的方法
            std::string methods;
            for (size_t i = 0; i < config_.allow_methods.size(); ++i) {
                if (i > 0) methods += ", ";
                methods += config_.allow_methods[i];
            }
            response.setHeader("Access-Control-Allow-Methods", methods);
            
            // 设置允许的头部
            std::string headers;
            for (size_t i = 0; i < config_.allow_headers.size(); ++i) {
                if (i > 0) headers += ", ";
                headers += config_.allow_headers[i];
            }
            response.setHeader("Access-Control-Allow-Headers", headers);
            
            // 设置缓存时间
            response.setHeader("Access-Control-Max-Age", std::to_string(config_.max_age));
            
            // 设置是否允许凭据
            if (config_.allow_credentials) {
                response.setHeader("Access-Control-Allow-Credentials", "true");
            }
            
            // 返回200状态码
            response.setStatus(200);
            response.setBody("");
            
            // LOG_DEBUG("Handled CORS preflight request for: " + request.getPath());
        }

        void CorsMiddleware::setCorsHeaders(const HttpRequest& request, HttpResponse& response) {
            // 设置允许的源
            std::string origin = request.getHeader("origin");
            if (!origin.empty() && config_.allow_origin == "*") {
                response.setHeader("Access-Control-Allow-Origin", origin);
            } else {
                response.setHeader("Access-Control-Allow-Origin", config_.allow_origin);
            }
            
            // 设置暴露的头部
            if (!config_.expose_headers.empty()) {
                std::string expose_headers;
                for (size_t i = 0; i < config_.expose_headers.size(); ++i) {
                    if (i > 0) expose_headers += ", ";
                    expose_headers += config_.expose_headers[i];
                }
                response.setHeader("Access-Control-Expose-Headers", expose_headers);
            }
            
            // 设置是否允许凭据
            if (config_.allow_credentials) {
                response.setHeader("Access-Control-Allow-Credentials", "true");
            }
        }

        // ==================== AuthMiddleware 实现 ====================

        bool AuthMiddleware::shouldExecute(const HttpRequest& request) const {
            if (!HttpMiddleware::shouldExecute(request)) {
                return false;
            }
            
            // 检查路径是否被排除
            return !isPathExcluded(request.getPath());
        }

        MiddlewareResult AuthMiddleware::execute(HttpRequest& request, HttpResponse& response,
                                               MiddlewareContext& context, NextFunction next) {
            try {
                // 获取认证头部
                std::string auth_header = request.getHeader(config_.token_header);

                if (auth_header.empty()) {
                    response.unauthorized(R"({"success":false,"error":"MISSING_AUTH_HEADER","message":"缺少认证头部"})");
                    return MiddlewareResult::STOP;
                }

                // 检查Token前缀
                if (auth_header.find(config_.token_prefix) != 0) {
                    response.unauthorized(R"({"success":false,"error":"INVALID_AUTH_FORMAT","message":"认证格式无效，需要 Bearer Token"})");
                    return MiddlewareResult::STOP;
                }

                // 提取Token
                std::string token = auth_header.substr(config_.token_prefix.length());

                // 使用 JwtValidator 验证 Token
                auto& validator = common::auth::JwtValidator::getInstance();

                if (!validator.isInitialized()) {
                    LOG_ERROR("JwtValidator 未初始化，请检查配置");
                    response.internalServerError(R"({"success":false,"error":"AUTH_NOT_INITIALIZED","message":"认证服务未初始化"})");
                    return MiddlewareResult::ERROR;
                }

                auto result = validator.validate(token);

                if (!result.valid) {
                    std::string error_json = R"({"success":false,"error":")" + result.error_code +
                                           R"(","message":")" + result.error_message + R"("})";
                    response.unauthorized(error_json);
                    return MiddlewareResult::STOP;
                }

                // 将用户信息存储到上下文中
                context.set("authenticated", "true");
                context.set("user_id", result.user_id);
                context.set("username", result.username);
                context.set("email", result.email);
                context.set("session_id", result.session_id);
                context.set("token", token);

                // 存储角色信息（逗号分隔）
                if (!result.roles.empty()) {
                    std::string roles_str;
                    for (size_t i = 0; i < result.roles.size(); ++i) {
                        if (i > 0) roles_str += ",";
                        roles_str += result.roles[i];
                    }
                    context.set("roles", roles_str);
                }

                // LOG_DEBUG("认证成功: user_id=" + result.user_id +
                //          ", username=" + result.username +
                //          ", token=" + token.substr(0, std::min(size_t(10), token.length())) + "...");

                // 继续执行下一个中间件
                if (next()) {
                    return MiddlewareResult::CONTINUE;
                }
                return MiddlewareResult::STOP;

            } catch (const std::exception& e) {
                LOG_ERROR("Auth middleware error: " + std::string(e.what()));
                response.internalServerError(R"({"success":false,"error":"AUTH_ERROR","message":"认证过程发生异常"})");
                return MiddlewareResult::ERROR;
            }
        }

        bool AuthMiddleware::validateToken(const std::string& token) const {
            // 使用 JwtValidator 进行真正的 JWT 验证
            auto& validator = common::auth::JwtValidator::getInstance();

            if (!validator.isInitialized()) {
                LOG_ERROR("JwtValidator 未初始化");
                return false;
            }

            auto result = validator.validate(token);
            return result.valid;
        }

        bool AuthMiddleware::isPathExcluded(const std::string& path) const {
            for (const auto& excluded_path : config_.exclude_paths) {
                if (path.find(excluded_path) == 0) {
                    return true;
                }
            }
            return false;
        }

        // ==================== LoggingMiddleware 实现 ====================

        bool LoggingMiddleware::shouldExecute(const HttpRequest& request) const {
            if (!HttpMiddleware::shouldExecute(request)) {
                return false;
            }
            
            // 检查路径是否被排除
            std::string path = request.getPath();
            for (const auto& excluded_path : config_.exclude_paths) {
                if (path.find(excluded_path) == 0) {
                    return false;
                }
            }
            
            return true;
        }

        MiddlewareResult LoggingMiddleware::execute(HttpRequest& request, HttpResponse& response,
                                                  MiddlewareContext& context, NextFunction next) {
            try {
                // 记录请求开始
                logRequest(request, context);
                
                // 执行下一个中间件
                bool continue_execution = next();
                
                // 记录响应信息
                logResponse(request, response, context);
                
                return continue_execution ? MiddlewareResult::CONTINUE : MiddlewareResult::STOP;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Logging middleware error: " + std::string(e.what()));
                return MiddlewareResult::ERROR;
            }
        }

        void LoggingMiddleware::logRequest(const HttpRequest& request, MiddlewareContext& /* context */) {
            std::ostringstream log_stream;
            
            // 基本请求信息
            log_stream << "HTTP " << HttpRequest::methodToString(request.getMethod())
                      << " " << request.getPath();
            
            if (!request.getQuery().empty()) {
                log_stream << "?" << request.getQuery();
            }
            
            // 记录请求头
            if (config_.log_request_headers) {
                log_stream << " Headers: {";
                const auto& headers = request.getHeaders();
                bool first = true;
                for (const auto& [key, value] : headers) {
                    if (!first) log_stream << ", ";
                    log_stream << key << ": " << value;
                    first = false;
                }
                log_stream << "}";
            }
            
            // 记录请求体
            if (config_.log_request_body && !request.getBody().empty()) {
                std::string body = request.getBody();
                if (body.length() > config_.max_body_log_size) {
                    body = body.substr(0, config_.max_body_log_size) + "...";
                }
                log_stream << " Body: " << body;
            }
            
            LOG_INFO(log_stream.str());
        }

        void LoggingMiddleware::logResponse(const HttpRequest& request, const HttpResponse& response,
                                          MiddlewareContext& context) {
            std::ostringstream log_stream;
            
            // 基本响应信息
            log_stream << "HTTP " << HttpRequest::methodToString(request.getMethod())
                      << " " << request.getPath()
                      << " -> " << response.getStatus()
                      << " (" << context.getElapsedMs() << "ms)";
            
            // 记录响应头
            if (config_.log_response_headers) {
                log_stream << " Headers: {";
                const auto& headers = response.getHeaders();
                bool first = true;
                for (const auto& [key, value] : headers) {
                    if (!first) log_stream << ", ";
                    log_stream << key << ": " << value;
                    first = false;
                }
                log_stream << "}";
            }
            
            // 记录响应体
            if (config_.log_response_body && !response.getBody().empty()) {
                std::string body = response.getBody();
                if (body.length() > config_.max_body_log_size) {
                    body = body.substr(0, config_.max_body_log_size) + "...";
                }
                log_stream << " Body: " << body;
            }
            
            LOG_INFO(log_stream.str());
        }

        // ==================== RateLimitMiddleware 实现 ====================

        MiddlewareResult RateLimitMiddleware::execute(HttpRequest& request, HttpResponse& response,
                                                     MiddlewareContext& /* context */, NextFunction next) {
            try {
                // 生成限流键
                std::string key = generateKey(request);
                
                // 检查是否超过限流
                if (isRateLimited(key)) {
                    response.setStatus(429); // Too Many Requests
                    response.setHeader("Retry-After", "60");
                    response.setJsonBody({
                        {"error", true},
                        {"message", "Rate limit exceeded"},
                        {"limit", config_.requests_per_minute},
                        {"window", "1 minute"}
                    });
                    
                    LOG_WARNING("Rate limit exceeded for key: " + key);
                    return MiddlewareResult::STOP;
                }
                
                // 继续执行下一个中间件
                if (next()) {
                    return MiddlewareResult::CONTINUE;
                }
                return MiddlewareResult::STOP;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Rate limit middleware error: " + std::string(e.what()));
                return MiddlewareResult::ERROR;
            }
        }

        std::string RateLimitMiddleware::generateKey(const HttpRequest& request) const {
            if (config_.key_generator == "ip") {
                // 在实际实现中，应该从连接信息中获取客户端IP
                return request.getClientIP(); // 模拟IP
            } else if (config_.key_generator == "user") {
                // 从认证信息中获取用户ID
                return "user_" + request.getHeader("authorization");
            } else {
                // 自定义键生成策略
                return "custom_" + request.getPath();
            }
        }

        bool RateLimitMiddleware::isRateLimited(const std::string& key) {
            std::lock_guard<std::mutex> lock(rate_limit_mutex_);
            
            auto now = std::chrono::steady_clock::now();
            auto it = rate_limits_.find(key);
            
            if (it == rate_limits_.end()) {
                // 第一次请求
                rate_limits_[key] = {1, now};
                return false;
            }
            
            auto& [count, last_time] = it->second;
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_time).count();
            
            if (elapsed >= 1) {
                // 重置计数器
                count = 1;
                last_time = now;
                return false;
            }
            
            // 检查是否超过限制
            if (count >= config_.requests_per_minute) {
                return true;
            }
            
            // 增加计数
            count++;
            return false;
        }

        // ==================== MiddlewareManager 实现 ====================

        MiddlewareManager::MiddlewareManager() : initialized_(false) {
            LOG_INFO("MiddlewareManager created");
        }

        MiddlewareManager::~MiddlewareManager() {
            try {
                cleanup();
                // 避免在析构时使用日志系统，使用标准输出
                std::cout << "[MiddlewareManager] MiddlewareManager destroyed" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[MiddlewareManager] Exception in destructor: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[MiddlewareManager] Unknown exception in destructor" << std::endl;
            }
        }

        void MiddlewareManager::use(std::shared_ptr<HttpMiddleware> middleware) {
            if (!middleware) {
                LOG_ERROR("Cannot add null middleware");
                return;
            }

            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            // 检查是否已存在同名中间件
            for (const auto& existing : middlewares_) {
                if (existing->getName() == middleware->getName()) {
                    LOG_WARNING("Middleware with name '" + middleware->getName() + "' already exists, replacing");
                    // 移除旧的中间件
                    middlewares_.erase(
                        std::remove_if(middlewares_.begin(), middlewares_.end(),
                                     [&middleware](const std::shared_ptr<HttpMiddleware>& m) {
                                         return m->getName() == middleware->getName();
                                     }),
                        middlewares_.end());
                    break;
                }
            }

            middlewares_.push_back(middleware);
            sortMiddlewares();

            LOG_INFO("Added middleware: " + middleware->getName() +
                    " (priority: " + std::to_string(middleware->getPriority()) + ")");
        }

        void MiddlewareManager::use(MiddlewareFunction func, const std::string& name, int priority) {
            auto middleware = std::make_shared<FunctionMiddleware>(func, name, priority);
            use(middleware);
        }

        void MiddlewareManager::use(const std::string& path_pattern, std::shared_ptr<HttpMiddleware> middleware) {
            auto path_middleware = std::make_shared<PathMiddleware>(path_pattern, middleware);
            use(path_middleware);
        }

        bool MiddlewareManager::execute(HttpRequest& request, HttpResponse& response,
                                       std::function<void()> final_handler) {
            if (!initialized_) {
                LOG_ERROR("MiddlewareManager not initialized");
                return false;
            }

            MiddlewareContext context;

            try {
                return executeMiddlewares(request, response, context, 0, final_handler);
            } catch (const std::exception& e) {
                LOG_ERROR("Middleware execution failed: " + std::string(e.what()));
                return false;
            }
        }

        bool MiddlewareManager::remove(const std::string& name) {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            auto it = std::remove_if(middlewares_.begin(), middlewares_.end(),
                                   [&name](const std::shared_ptr<HttpMiddleware>& middleware) {
                                       return middleware->getName() == name;
                                   });

            if (it != middlewares_.end()) {
                middlewares_.erase(it, middlewares_.end());
                LOG_INFO("Removed middleware: " + name);
                return true;
            }

            LOG_WARNING("Middleware not found: " + name);
            return false;
        }

        void MiddlewareManager::clear() {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            // 清理所有中间件
            for (auto& middleware : middlewares_) {
                middleware->cleanup();
            }

            middlewares_.clear();
            execution_stats_.clear();
            initialized_ = false;

            LOG_INFO("All middlewares cleared");
        }

        std::vector<std::string> MiddlewareManager::getMiddlewareNames() const {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            std::vector<std::string> names;
            names.reserve(middlewares_.size());

            for (const auto& middleware : middlewares_) {
                names.push_back(middleware->getName());
            }

            return names;
        }

        bool MiddlewareManager::initialize() {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            if (initialized_) {
                LOG_WARNING("MiddlewareManager already initialized");
                return true;
            }

            // 初始化所有中间件
            for (auto& middleware : middlewares_) {
                if (!middleware->initialize()) {
                    LOG_ERROR("Failed to initialize middleware: " + middleware->getName());
                    return false;
                }
            }

            initialized_ = true;

            return true;
        }

        void MiddlewareManager::cleanup() {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            if (!initialized_) {
                return;
            }

            try {
                // 清理所有中间件
                for (auto& middleware : middlewares_) {
                    if (middleware) {
                        middleware->cleanup();
                    }
                }

                middlewares_.clear();
                execution_stats_.clear();
                initialized_ = false;

                // 避免在析构时使用日志系统，使用标准输出
                std::cout << "[MiddlewareManager] MiddlewareManager cleaned up" << std::endl;
            } catch (const std::exception& e) {
                // 避免在析构时使用日志系统，使用标准错误输出
                std::cerr << "[MiddlewareManager] Exception during cleanup: " << e.what() << std::endl;
                initialized_ = false; // 确保状态被重置
            }
        }

        void MiddlewareManager::setMiddlewareEnabled(const std::string& name, bool enabled) {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);

            for (auto& middleware : middlewares_) {
                if (middleware->getName() == name) {
                    middleware->setEnabled(enabled);
                    LOG_INFO("Middleware '" + name + "' " + (enabled ? "enabled" : "disabled"));
                    return;
                }
            }

            LOG_WARNING("Middleware not found: " + name);
        }

        void MiddlewareManager::loadConfigFromManager() {
            try {
                auto& config = common::config::ConfigManager::getInstance();

                // 加载中间件配置
                bool cors_enabled = config.get<bool>("middleware.cors.enable", true);
                bool auth_enabled = config.get<bool>("middleware.auth.enable", false);
                bool logging_enabled = config.get<bool>("middleware.logging.enable", true);
                bool rate_limit_enabled = config.get<bool>("middleware.rate_limit.enable", false);

                // 根据配置启用/禁用中间件
                setMiddlewareEnabled("CorsMiddleware", cors_enabled);
                setMiddlewareEnabled("AuthMiddleware", auth_enabled);
                setMiddlewareEnabled("LoggingMiddleware", logging_enabled);
                setMiddlewareEnabled("RateLimitMiddleware", rate_limit_enabled);

                LOG_INFO("Middleware configuration loaded from ConfigManager");

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to load middleware configuration: " + std::string(e.what()));
            }
        }

        std::map<std::string, uint64_t> MiddlewareManager::getExecutionStats() const {
            std::lock_guard<std::mutex> lock(middlewares_mutex_);
            return execution_stats_;
        }

        void MiddlewareManager::sortMiddlewares() {
            std::sort(middlewares_.begin(), middlewares_.end(),
                     [](const std::shared_ptr<HttpMiddleware>& a, const std::shared_ptr<HttpMiddleware>& b) {
                         return a->getPriority() > b->getPriority(); // 优先级高的在前
                     });
        }

        bool MiddlewareManager::executeMiddlewares(HttpRequest& request, HttpResponse& response,
                                                 MiddlewareContext& context, size_t index,
                                                 std::function<void()> final_handler) {
            // 如果所有中间件都执行完毕，执行最终处理函数
            if (index >= middlewares_.size()) {
                if (final_handler) {
                    try {
                        final_handler();
                    } catch (const std::exception& e) {
                        LOG_ERROR("Final handler execution failed: " + std::string(e.what()));
                        return false;
                    }
                }
                return true;
            }

            auto& middleware = middlewares_[index];

            // 检查是否应该执行此中间件
            if (!middleware->shouldExecute(request)) {
                return executeMiddlewares(request, response, context, index + 1, final_handler);
            }

            // 更新执行统计
            updateExecutionStats(middleware->getName());

            // 创建下一个中间件的执行函数
            NextFunction next = [this, &request, &response, &context, index, final_handler]() -> bool {
                return executeMiddlewares(request, response, context, index + 1, final_handler);
            };

            // 执行当前中间件
            auto start_time = std::chrono::steady_clock::now();
            MiddlewareResult result = middleware->execute(request, response, context, next);
            auto end_time = std::chrono::steady_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
            // LOG_DEBUG("Middleware '" + middleware->getName() + "' executed in " + std::to_string(duration) + " μs");

            switch (result) {
                case MiddlewareResult::CONTINUE:
                    return true;
                case MiddlewareResult::STOP:
                    LOG_INFO("Middleware chain stopped by: " + middleware->getName());
                    return true; // 正常停止，不是错误
                case MiddlewareResult::ERROR:
                    LOG_ERROR("Middleware execution failed: " + middleware->getName());
                    return false;
                default:
                    LOG_ERROR("Unknown middleware result from: " + middleware->getName());
                    return false;
            }
        }

        void MiddlewareManager::updateExecutionStats(const std::string& middleware_name) {
            execution_stats_[middleware_name]++;
        }

    } // namespace http
} // namespace common
