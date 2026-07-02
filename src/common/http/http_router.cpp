/**
 * @file http_router.cpp
 * @brief HTTP路由管理器的实现
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 * 
 * 实现说明:
 * - 基于前缀树（Trie）的高效路由匹配
 * - 支持静态路由、参数路由、通配符路由
 * - 集成中间件系统
 * - 提供完整的路由管理功能
 */

#include "common/http/http_router.h"
#include <algorithm>
#include <sstream>
#include <list>  // ⭐⭐⭐ 内存泄漏修复：添加list支持LRU

namespace common {
    namespace http {

        HttpRouter::HttpRouter() {
            // 初始化HTTP路由器
            // 创建空的路由树结构，准备接收路由注册
            // method_trees_映射将在首次使用时延迟创建

            // 从配置读取路由缓存大小
            try {
                auto& config_manager = common::config::ConfigManager::getInstance();
                max_cache_size_ = config_manager.get<size_t>("http.server.route_cache_size", 1024);
            } catch (const std::exception& e) {
                LOG_WARNING("无法从配置读取route_cache_size，使用默认值1024: " + std::string(e.what()));
                max_cache_size_ = 1024;
            }
        }

        void HttpRouter::addRoute(const std::string& method, const std::string& pattern,
                                 std::shared_ptr<HttpHandler> handler, int priority) {
            // 添加路由到路由树（处理器版本）
            // 使用写锁保证线程安全，支持多线程并发注册路由
            std::unique_lock<std::shared_mutex> lock(router_mutex_);

            // 验证处理器有效性，防止空指针异常
            if (!handler) {
                LOG_ERROR("Cannot add route with null handler: " + method + " " + pattern);
                return;
            }

            // 标准化路径格式，确保路径的一致性
            // 例如: "/api//users/" -> "/api/users", "api/users" -> "/api/users"
            std::string normalized_pattern = normalizePath(pattern);

            // 解析路径模式为路径段数组
            // 例如: "/api/users/:id" -> ["api", "users", ":id"]
            auto segments = parsePattern(normalized_pattern);

            // 获取或创建指定HTTP方法的路由树根节点
            // 每个HTTP方法（GET、POST等）都有独立的路由树
            auto root = getOrCreateMethodTree(method);

            // 将路由插入到路由树中，构建前缀树结构
            // 支持静态路由、参数路由(:param)、通配符路由(*)
            insertRoute(root, segments, handler, priority, normalized_pattern);
        }

        void HttpRouter::addRoute(const std::string& method, const std::string& pattern,
                                 RequestHandler handler, int priority) {
            // 添加路由到路由树（函数版本）
            // 将函数式处理器包装为HttpHandler对象，提供更便捷的路由注册方式

            // 创建函数处理器包装器，将lambda函数或函数指针转换为HttpHandler
            // 生成唯一的处理器名称，便于调试和日志记录
            auto function_handler = std::make_shared<FunctionHandler>(handler,
                "FunctionHandler_" + method + "_" + pattern);

            // 委托给处理器版本的addRoute方法，复用核心逻辑
            addRoute(method, pattern, function_handler, priority);
        }

        // ==================== HTTP方法便捷注册方法 ====================
        // 这些方法提供了更简洁的路由注册方式，避免手动指定HTTP方法字符串

        void HttpRouter::get(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority) {
            // 注册GET路由（处理器版本）
            // 用于处理HTTP GET请求，通常用于数据查询和资源获取
            addRoute("GET", pattern, handler, priority);
        }

        void HttpRouter::get(const std::string& pattern, RequestHandler handler, int priority) {
            // 注册GET路由（函数版本）
            // 支持lambda表达式和函数指针，使用更便捷
            addRoute("GET", pattern, handler, priority);
        }

        void HttpRouter::post(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority) {
            // 注册POST路由（处理器版本）
            // 用于处理HTTP POST请求，通常用于数据创建和提交
            addRoute("POST", pattern, handler, priority);
        }

        void HttpRouter::post(const std::string& pattern, RequestHandler handler, int priority) {
            // 注册POST路由（函数版本）
            // 常用于表单提交、文件上传、API数据创建等
            addRoute("POST", pattern, handler, priority);
        }

        void HttpRouter::put(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority) {
            // 注册PUT路由（处理器版本）
            // 用于处理HTTP PUT请求，通常用于数据更新和替换
            addRoute("PUT", pattern, handler, priority);
        }

        void HttpRouter::put(const std::string& pattern, RequestHandler handler, int priority) {
            // 注册PUT路由（函数版本）
            // 常用于RESTful API的资源更新操作
            addRoute("PUT", pattern, handler, priority);
        }

        void HttpRouter::del(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority) {
            // 注册DELETE路由（处理器版本）
            // 用于处理HTTP DELETE请求，通常用于数据删除
            addRoute("DELETE", pattern, handler, priority);
        }

        void HttpRouter::del(const std::string& pattern, RequestHandler handler, int priority) {
            // 注册DELETE路由（函数版本）
            // 常用于RESTful API的资源删除操作
            addRoute("DELETE", pattern, handler, priority);
        }

        // ==================== 路由匹配方法 ====================

        RouteMatch HttpRouter::match(const std::string& method, const std::string& path) const {

            // 构建缓存键（方法:路径）
            std::string cache_key = method + ":" + path;

            // ⭐⭐⭐ 内存泄漏修复（2025-12-31）：使用cache_mutex_保护LRU缓存
            // 第一步：检查缓存
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                auto map_it = route_cache_map_.find(cache_key);
                if (map_it != route_cache_map_.end()) {
                    // 缓存命中，移动到链表头部（标记为最近使用）
                    CacheEntry entry = *(map_it->second);
                    route_cache_list_.erase(map_it->second);
                    route_cache_list_.push_front({cache_key, entry.value, std::chrono::steady_clock::now()});
                    route_cache_map_[cache_key] = route_cache_list_.begin();
                    return entry.value;
                }
            }

            // 第二步：缓存未命中，执行路由匹配
            RouteMatch result;
            auto method_it = method_trees_.find(method);
            if (method_it == method_trees_.end()) {
                // LOG_DEBUG("No routes found for method: " + method);
                return result;
            }

            // 标准化路径
            std::string normalized_path = normalizePath(path);
            auto segments = parsePattern(normalized_path);

            // 在路由树中搜索匹配
            result = searchRoute(method_it->second, segments, result.params);

            // 内存泄漏修复：使用真正的LRU缓存更新
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);

                // 插入到链表头部（最新位置）
                route_cache_list_.push_front({cache_key, result, std::chrono::steady_clock::now()});
                route_cache_map_[cache_key] = route_cache_list_.begin();

                // 检查是否超过大小限制
                if (route_cache_list_.size() > max_cache_size_) {
                    // 删除链表尾部（最久未使用）
                    route_cache_map_.erase(route_cache_list_.back().key);
                    route_cache_list_.pop_back();
                }
            }

            return result;
        }

        RouteMatch HttpRouter::match(const HttpRequest& request) const {
            // 匹配HTTP请求对象（便捷版本）
            // 从HttpRequest对象中提取方法和路径，委托给基础匹配方法
            return match(request.getMethodString(), request.getPath());
        }

        // ==================== 中间件管理方法 ====================

        void HttpRouter::addGlobalMiddleware(MiddlewareHandler middleware) {
            // 添加全局中间件
            // 全局中间件会在所有路由处理前执行，用于通用功能如认证、日志等
            std::unique_lock<std::shared_mutex> lock(router_mutex_);
            global_middlewares_.push_back(middleware);
        }

        void HttpRouter::addMiddleware(const std::string& pattern, MiddlewareHandler middleware) {
            // 添加路径特定中间件
            // 只有匹配指定路径模式的请求才会执行这些中间件
            std::unique_lock<std::shared_mutex> lock(router_mutex_);

            // 标准化路径模式，确保匹配的一致性
            std::string normalized_pattern = normalizePath(pattern);

            // 将中间件添加到指定路径模式的中间件列表中
            // 支持同一路径模式注册多个中间件，按注册顺序执行
            path_middlewares_[normalized_pattern].push_back(middleware);
        }

        bool HttpRouter::executeMiddlewares(const HttpRequest& request, HttpResponse& response,
                                          const std::string& path) const {
            // 执行中间件链
            // 按顺序执行全局中间件和路径特定中间件
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            // 第一阶段：执行全局中间件
            // 全局中间件对所有请求都生效，如CORS、认证、日志等
            for (const auto& middleware : global_middlewares_) {
                if (!middleware(request, response)) {
                    // 中间件返回false表示停止后续处理
                    // 通常用于认证失败、限流等场景
                    // LOG_DEBUG("Global middleware stopped request processing");
                    return false;
                }
            }

            // 第二阶段：执行路径特定中间件
            // 只有匹配路径模式的中间件才会执行
            std::string normalized_path = normalizePath(path);
            for (const auto& [pattern, middlewares] : path_middlewares_) {
                // 简单的前缀匹配（可以扩展为更复杂的模式匹配）
                // 例如: 路径"/api/users/123"匹配模式"/api"
                if (normalized_path.find(pattern) == 0) {
                    // 执行该模式下的所有中间件
                    for (const auto& middleware : middlewares) {
                        if (!middleware(request, response)) {
                            // 路径中间件停止处理，记录具体的模式信息
                            // LOG_DEBUG("Path middleware stopped request processing for pattern: " + pattern);
                            return false;
                        }
                    }
                }
            }

            // 所有中间件都执行成功，返回true继续处理请求
            return true;
        }

        // ==================== 路由管理方法 ====================

        /**
         * @brief 移除指定的路由
         * @param method HTTP方法，如"GET", "POST"等
         * @param pattern 路径模式，如"/api/users/\*", "/health"等
         * @return 删除成功返回true，路由不存在返回false
         *
         * @details 路由删除过程：
         * 1. 解析路径模式为路径段
         * 2. 在对应方法树中查找目标节点
         * 3. 移除节点的处理器
         * 4. 清理空的父节点（如果没有其他子节点）
         * 5. 更新路由统计信息
         *
         * 注意事项：
         * - 删除操作是线程安全的
         * - 只删除处理器，保留可能被其他路由使用的中间节点
         * - 支持通配符路由的删除
         * - 删除后会自动清理不再需要的空节点
         */
        bool HttpRouter::removeRoute(const std::string& method, const std::string& pattern) {
            // 参数验证
            if (method.empty() || pattern.empty()) {
                LOG_ERROR("Invalid parameters for route removal: method='" + method + "', pattern='" + pattern + "'");
                return false;
            }

            // 线程安全保护
            std::unique_lock<std::shared_mutex> lock(router_mutex_);

            // LOG_DEBUG("开始删除路由: " + method + " " + pattern);

            // 检查指定HTTP方法的路由树是否存在
            auto method_it = method_trees_.find(method);
            if (method_it == method_trees_.end()) {
                LOG_WARNING("方法树不存在，无法删除路由: " + method + " " + pattern);
                return false;
            }

            auto root = method_it->second;
            if (!root) {
                LOG_ERROR("方法树根节点为空: " + method);
                return false;
            }

            // 解析路径模式为路径段
            std::vector<std::string> segments = parsePattern(pattern);
            if (segments.empty()) {
                LOG_ERROR("路径解析失败: " + pattern);
                return false;
            }

            // 查找目标节点并删除路由
            bool removed = removeRouteRecursive(root, segments, 0, pattern);

            if (removed) {
                // 获取删除后的路由数量（动态计算）
                size_t remaining_routes = getRouteCount();
                // LOG_DEBUG("成功删除路由: " + method + " " + pattern + " (剩余路由数: " + std::to_string(remaining_routes) + ")");

                // 如果方法树变为空，考虑删除整个方法树
                if (isMethodTreeEmpty(root)) {
                    method_trees_.erase(method_it);
                    // LOG_DEBUG("方法树已空，删除方法树: " + method);
                }
            } else {
                LOG_WARNING("路由不存在，删除失败: " + method + " " + pattern);
            }

            return removed;
        }

        void HttpRouter::clear() {
            // 清空所有路由
            // 删除所有HTTP方法的路由树，重置路由器到初始状态
            std::unique_lock<std::shared_mutex> lock(router_mutex_);
            method_trees_.clear();
            // 同时清空中间件
            global_middlewares_.clear();
            path_middlewares_.clear();
            // LOG_DEBUG("All routes cleared");
        }

        // ==================== 路由统计和查询方法 ====================

        size_t HttpRouter::getRouteCount(const std::string& method) const {
            // 获取路由数量统计
            // 支持查询特定HTTP方法或所有方法的路由数量
            // 精确统计：遍历路由树，计算有处理器的节点数量
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            if (method.empty()) {
                // 统计所有HTTP方法的路由总数
                size_t total = 0;
                for (const auto& [m, tree] : method_trees_) {
                    // 递归遍历每个方法的路由树，统计有处理器的节点
                    size_t method_count = countRoutesInTree(tree);
                    total += method_count;

                    // 记录详细统计信息（调试模式）
                    // LOG_DEBUG("Method " + m + " has " + std::to_string(method_count) + " routes");
                }

                // LOG_DEBUG("Total routes across all methods: " + std::to_string(total));
                return total;
            } else {
                // 统计指定HTTP方法的路由数量
                auto it = method_trees_.find(method);
                if (it != method_trees_.end()) {
                    // 递归遍历指定方法的路由树，统计有处理器的节点
                    size_t count = countRoutesInTree(it->second);
                    // LOG_DEBUG("Method " + method + " has " + std::to_string(count) + " routes");
                    return count;
                } else {
                    // 指定的HTTP方法不存在，返回0
                    // LOG_DEBUG("Method " + method + " not found, returning 0 routes");
                    return 0;
                }
            }
        }

        HttpRouter::RouteStatistics HttpRouter::getRouteStatistics() const {
            // 获取详细的路由统计信息
            // 提供按类型和HTTP方法分类的完整统计数据
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            RouteStatistics stats;

            // 遍历所有HTTP方法的路由树
            for (const auto& [method, tree] : method_trees_) {
                // 统计该方法的路由信息
                RouteTypeCount type_count = countRoutesByType(tree);

                // 累加到总统计中
                stats.total_routes += type_count.total;
                stats.static_routes += type_count.static_count;
                stats.param_routes += type_count.param_count;
                stats.wildcard_routes += type_count.wildcard_count;

                // 记录各HTTP方法的路由数量
                stats.method_counts[method] = type_count.total;

                // LOG_DEBUG("Method " + method + " statistics: " +
                //          "Total=" + std::to_string(type_count.total) +
                //          ", Static=" + std::to_string(type_count.static_count) +
                //          ", Param=" + std::to_string(type_count.param_count) +
                //          ", Wildcard=" + std::to_string(type_count.wildcard_count));
            }

            // LOG_DEBUG("Route statistics collected: " + std::to_string(stats.total_routes) + " total routes");
            return stats;
        }

        std::vector<std::string> HttpRouter::getAllRoutes() const {
            // 获取所有注册的路由列表
            // 返回格式化的路由信息，便于调试和监控
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            std::vector<std::string> routes;
            // 遍历所有HTTP方法的路由树
            for (const auto& [method, tree] : method_trees_) {
                // 递归收集每个方法树中的所有路由
                // 生成格式: "GET /api/users", "POST /api/users/:id"
                collectRoutes(tree, method, "", routes);
            }

            // LOG_DEBUG("Collected " + std::to_string(routes.size()) + " routes");
            return routes;
        }

        std::vector<std::string> HttpRouter::getRoutesByMethod(const std::string& method) const {
            // 获取指定HTTP方法的路由列表
            // 只返回特定方法的路由，便于按方法查看路由配置
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            std::vector<std::string> routes;
            auto it = method_trees_.find(method);
            if (it != method_trees_.end()) {
                // 收集指定方法的所有路由
                collectRoutes(it->second, method, "", routes);
                // LOG_DEBUG("Collected " + std::to_string(routes.size()) + " routes for method " + method);
            } else {
                // LOG_DEBUG("No routes found for method: " + method);
            }

            return routes;
        }

        bool HttpRouter::hasRoute(const std::string& method, const std::string& path) const {
            // 检查指定的路由是否存在
            // 快速检查路由配置，不执行实际的处理器调用
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            auto it = method_trees_.find(method);
            if (it == method_trees_.end()) {
                return false;
            }

            std::string normalized_path = normalizePath(path);
            auto segments = parsePattern(normalized_path);

            RouteParams dummy_params;
            RouteMatch result = searchRoute(it->second, segments, dummy_params);

            bool exists = result.found;
            // LOG_DEBUG("Route " + method + " " + path + (exists ? " exists" : " does not exist"));

            return exists;
        }

        std::string HttpRouter::getMatchDetails(const std::string& method, const std::string& path) const {
            // 获取路由匹配的详细信息，用于调试和故障排除
            // 提供匹配过程的详细信息，包括匹配的模式、提取的参数等
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            std::ostringstream details;
            details << "Route Match Details for: " << method << " " << path << "\n";

            auto it = method_trees_.find(method);
            if (it == method_trees_.end()) {
                details << "  Result: No method tree found for " << method << "\n";
                details << "  Available methods: ";
                for (const auto& [m, tree] : method_trees_) {
                    details << m << " ";
                }
                details << "\n";
                return details.str();
            }

            std::string normalized_path = normalizePath(path);
            auto segments = parsePattern(normalized_path);

            details << "  Normalized path: " << normalized_path << "\n";
            details << "  Path segments: [";
            for (size_t i = 0; i < segments.size(); ++i) {
                if (i > 0) details << ", ";
                details << "\"" << segments[i] << "\"";
            }
            details << "]\n";

            RouteParams params;
            RouteMatch result = searchRoute(it->second, segments, params);

            if (result.found) {
                details << "  Result: MATCHED\n";
                details << "  Matched pattern: " << result.matched_pattern << "\n";
                details << "  Priority: " << result.priority << "\n";
                details << "  Extracted parameters:\n";
                for (const auto& [key, value] : params) {
                    details << "    " << key << " = \"" << value << "\"\n";
                }
            } else {
                details << "  Result: NOT MATCHED\n";
                details << "  Available routes for " << method << ":\n";
                std::vector<std::string> method_routes;
                collectRoutes(it->second, method, "", method_routes);
                for (const auto& route : method_routes) {
                    details << "    " << route << "\n";
                }
            }

            return details.str();
        }

        void HttpRouter::printRoutes(const std::string& method) const {
            // 打印路由信息到日志（调试级别）
            // 用于调试和监控，显示当前注册的所有路由结构
            std::shared_lock<std::shared_mutex> lock(router_mutex_);

            if (method.empty()) {
                // 打印所有HTTP方法的路由
                // LOG_DEBUG("=== All Routes ===");
                for (const auto& [m, tree] : method_trees_) {
                    // LOG_DEBUG("Method: " + m);
                    // 递归打印路由树结构，显示层次关系
                    printRouteTree(tree, "", 0);
                }
            } else {
                // 打印指定HTTP方法的路由
                auto it = method_trees_.find(method);
                if (it != method_trees_.end()) {
                    // LOG_DEBUG("=== Routes for " + method + " ===");
                    // 打印指定方法的路由树
                    printRouteTree(it->second, "", 0);
                }
            }
        }

        bool HttpRouter::validate() const {
            // 验证路由配置的有效性
            // 检查路由树结构、处理器有效性等
            std::shared_lock<std::shared_mutex> lock(router_mutex_);
            
            for (const auto& [method, tree] : method_trees_) {
                if (!tree) {
                    LOG_ERROR("Invalid null tree for method: " + method);
                    return false;
                }
            }

            return true;
        }

        // ==================== 私有方法实现 ====================

        std::shared_ptr<RouteNode> HttpRouter::getOrCreateMethodTree(const std::string& method) {
            // 获取或创建指定HTTP方法的路由树根节点
            // 每个HTTP方法都有独立的路由树，实现方法级别的路由隔离
            auto it = method_trees_.find(method);
            if (it == method_trees_.end()) {
                // 方法树不存在，创建新的根节点
                auto root = std::make_shared<RouteNode>();
                method_trees_[method] = root;
                // LOG_DEBUG("Created new route tree for method: " + method);
                return root;
            }
            // 返回已存在的根节点
            return it->second;
        }

        std::vector<std::string> HttpRouter::parsePattern(const std::string& pattern) const {
            // 解析路径模式为路径段数组
            // 将URL路径按'/'分割，过滤空段，便于在路由树中进行匹配
            // 例如: "/api/users/:id" -> ["api", "users", ":id"]
            std::vector<std::string> segments;
            std::stringstream ss(pattern);
            std::string segment;

            // 按'/'分割路径
            while (std::getline(ss, segment, '/')) {
                if (!segment.empty()) {
                    // 过滤空段，避免连续斜杠造成的问题
                    // 例如: "/api//users" -> ["api", "users"]
                    segments.push_back(segment);
                }
            }

            return segments;
        }

        void HttpRouter::insertRoute(std::shared_ptr<RouteNode> root, const std::vector<std::string>& segments,
                                   std::shared_ptr<HttpHandler> handler, int priority, const std::string& full_pattern) {
            // 将路由插入到路由树中
            // 构建前缀树（Trie）结构，支持高效的路由匹配
            // 支持三种节点类型：静态、参数(:param)、通配符(*)
            auto current = root;

            // 遍历路径段，逐级构建或查找树节点
            for (const auto& segment : segments) {
                if (isParamSegment(segment)) {
                    // 参数节点处理（如 :id, :name）
                    // 参数节点可以匹配任意非空字符串，并提取参数值
                    if (!current->param_child) {
                        // 创建新的参数节点
                        current->param_child = std::make_shared<RouteNode>(RouteNodeType::PARAM, segment);
                        // 提取参数名（去掉冒号前缀）
                        current->param_child->param_name = extractParamName(segment);
                    }
                    current = current->param_child;
                } else if (isWildcardSegment(segment)) {
                    // 通配符节点处理（如 *）
                    // 通配符节点可以匹配剩余的所有路径段
                    if (!current->wildcard_child) {
                        // 创建新的通配符节点
                        current->wildcard_child = std::make_shared<RouteNode>(RouteNodeType::WILDCARD, segment);
                    }
                    current = current->wildcard_child;
                } else {
                    // 静态节点处理（如 api, users, admin）
                    // 静态节点需要精确匹配路径段
                    auto it = current->children.find(segment);
                    if (it == current->children.end()) {
                        // 创建新的静态子节点
                        current->children[segment] = std::make_shared<RouteNode>(RouteNodeType::STATIC, segment);
                    }
                    current = current->children[segment];
                }
            }

            // 在叶子节点设置路由处理器和元信息
            current->handler = handler;           // 请求处理器
            current->priority = priority;         // 路由优先级
            current->full_pattern = full_pattern; // 完整路径模式（用于调试）
        }

        RouteMatch HttpRouter::searchRoute(std::shared_ptr<RouteNode> root, const std::vector<std::string>& segments,
                                         RouteParams& params) const {
            // 在路由树中搜索匹配的路由
            // 使用深度优先搜索算法，按优先级顺序尝试匹配
            // 匹配优先级：静态 > 参数 > 通配符
            RouteMatch result;

            // 定义递归搜索函数
            // node: 当前搜索的节点, index: 当前匹配的路径段索引
            std::function<bool(std::shared_ptr<RouteNode>, size_t)> search =
                [&](std::shared_ptr<RouteNode> node, size_t index) -> bool {

                // 检查是否已匹配完所有路径段
                if (index == segments.size()) {
                    // 路径段匹配完毕，检查当前节点是否有处理器
                    if (node->handler) {
                        // 找到匹配的路由，设置结果信息
                        result.found = true;
                        result.handler = node->handler;
                        result.matched_pattern = node->full_pattern;
                        result.priority = node->priority;
                        return true;
                    }
                    // 路径匹配但没有处理器，继续搜索
                    return false;
                }

                // 获取当前要匹配的路径段
                const std::string& segment = segments[index];

                // 第一优先级：尝试静态匹配
                // 静态匹配具有最高优先级，精确匹配路径段
                auto it = node->children.find(segment);
                if (it != node->children.end()) {
                    if (search(it->second, index + 1)) {
                        return true; // 静态匹配成功
                    }
                }

                // 第二优先级：尝试参数匹配
                // 参数节点可以匹配任意非空字符串，并提取参数值
                if (node->param_child) {
                    // 保存参数值到params映射中
                    params[node->param_child->param_name] = segment;
                    if (search(node->param_child, index + 1)) {
                        return true; // 参数匹配成功
                    }
                    // 匹配失败，回滚参数（回溯算法）
                    params.erase(node->param_child->param_name);
                }

                // 第三优先级：尝试通配符匹配
                // 通配符节点可以匹配剩余的所有路径段
                if (node->wildcard_child) {
                    // 将剩余的所有路径段组合为通配符值
                    // 例如: 路径"/static/css/style.css"匹配模式"/static/*"
                    // 通配符值为"css/style.css"
                    std::string wildcard_value;
                    for (size_t i = index; i < segments.size(); ++i) {
                        if (!wildcard_value.empty()) wildcard_value += "/";
                        wildcard_value += segments[i];
                    }
                    // 将通配符值保存到参数映射中，键名为"*"
                    params["*"] = wildcard_value;

                    // 检查通配符节点是否有处理器
                    if (node->wildcard_child->handler) {
                        // 通配符匹配成功
                        result.found = true;
                        result.handler = node->wildcard_child->handler;
                        result.matched_pattern = node->wildcard_child->full_pattern;
                        result.priority = node->wildcard_child->priority;
                        return true;
                    }
                }

                // 所有匹配尝试都失败
                return false;
            };

            // 从根节点开始搜索，初始路径段索引为0
            search(root, 0);
            
            // 🔧 关键修复：将提取的参数复制到返回对象中
            // 解决引用参数修改被返回对象覆盖的问题
            result.params = params;
            
            return result;
        }

        // ==================== 路径解析工具方法 ====================

        bool HttpRouter::isParamSegment(const std::string& segment) const {
            // 检查路径段是否为参数节点
            // 参数节点以冒号(:)开头，如 ":id", ":name", ":category"
            return !segment.empty() && segment[0] == ':';
        }

        bool HttpRouter::isWildcardSegment(const std::string& segment) const {
            // 检查路径段是否为通配符节点
            // 通配符节点为单个星号(*)，匹配剩余所有路径
            return segment == "*";
        }

        std::string HttpRouter::extractParamName(const std::string& segment) const {
            // 从参数段中提取参数名
            // 移除冒号前缀，例如: ":id" -> "id", ":user_name" -> "user_name"
            return segment.substr(1); // 移除前缀 ':'
        }

        std::string HttpRouter::normalizePath(const std::string& path) const {
            // 标准化URL路径格式
            // 确保路径的一致性，便于路由匹配和比较

            // 处理空路径和根路径的特殊情况
            if (path.empty() || path == "/") {
                return "/";
            }

            std::string normalized = path;

            // 移除末尾的多余斜杠
            // 例如: "/api/users/" -> "/api/users", "/api/users///" -> "/api/users"
            while (normalized.length() > 1 && normalized.back() == '/') {
                normalized.pop_back();
            }

            // 确保路径以斜杠开头
            // 例如: "api/users" -> "/api/users"
            if (normalized[0] != '/') {
                normalized = "/" + normalized;
            }

            return normalized;
        }

        // ==================== 调试和监控工具方法 ====================

        void HttpRouter::printRouteTree(std::shared_ptr<RouteNode> node, const std::string& prefix, int depth) const {
            // 递归打印路由树结构
            // 用于调试和可视化路由树的层次结构
            if (!node) return;

            // 生成缩进，显示树的层次结构
            std::string indent(depth * 2, ' ');
            std::string type_str;

            // 获取节点类型的字符串表示
            switch (node->type) {
                case RouteNodeType::STATIC: type_str = "STATIC"; break;      // 静态节点
                case RouteNodeType::PARAM: type_str = "PARAM"; break;        // 参数节点
                case RouteNodeType::WILDCARD: type_str = "WILDCARD"; break;  // 通配符节点
            }

            // 打印节点信息：路径段、类型、是否有处理器（调试级别）
            // LOG_DEBUG(indent + "Node: " + node->segment + " (" + type_str + ")" +
            //         (node->handler ? " [HANDLER]" : ""));

            // 递归打印所有静态子节点
            for (const auto& [segment, child] : node->children) {
                printRouteTree(child, prefix + "/" + segment, depth + 1);
            }

            // 递归打印参数子节点
            if (node->param_child) {
                printRouteTree(node->param_child, prefix + "/" + node->param_child->segment, depth + 1);
            }

            // 递归打印通配符子节点
            if (node->wildcard_child) {
                printRouteTree(node->wildcard_child, prefix + "/*", depth + 1);
            }
        }

        void HttpRouter::collectRoutes(std::shared_ptr<RouteNode> node, const std::string& method,
                                     const std::string& prefix, std::vector<std::string>& routes) const {
            // 递归收集路由树中的所有路由信息
            // 遍历整个路由树，收集有处理器的节点，生成路由列表
            if (!node) return;

            // 如果当前节点有处理器，说明这是一个有效的路由端点
            if (node->handler) {
                // 生成路由字符串，格式: "METHOD /path"
                // 例如: "GET /api/users", "POST /api/users/:id"
                routes.push_back(method + " " + (prefix.empty() ? "/" : prefix));
            }

            // 递归收集所有静态子节点的路由
            for (const auto& [segment, child] : node->children) {
                collectRoutes(child, method, prefix + "/" + segment, routes);
            }

            // 递归收集参数子节点的路由
            if (node->param_child) {
                collectRoutes(node->param_child, method, prefix + "/" + node->param_child->segment, routes);
            }

            // 递归收集通配符子节点的路由
            if (node->wildcard_child) {
                collectRoutes(node->wildcard_child, method, prefix + "/*", routes);
            }
        }

        size_t HttpRouter::countRoutesInTree(std::shared_ptr<RouteNode> node) const {
            // 递归统计路由树中有处理器的节点数量
            // 这是精确的路由计数实现，遍历整个树结构
            if (!node) {
                return 0;
            }

            // 当前节点的路由计数（如果有处理器则为1，否则为0）
            size_t count = node->handler ? 1 : 0;

            // 递归统计所有静态子节点的路由数量
            for (const auto& [segment, child] : node->children) {
                count += countRoutesInTree(child);
            }

            // 递归统计参数子节点的路由数量
            if (node->param_child) {
                count += countRoutesInTree(node->param_child);
            }

            // 递归统计通配符子节点的路由数量
            if (node->wildcard_child) {
                count += countRoutesInTree(node->wildcard_child);
            }

            return count;
        }

        HttpRouter::RouteTypeCount HttpRouter::countRoutesByType(std::shared_ptr<RouteNode> node) const {
            // 按类型递归统计路由树中的路由数量
            // 提供更详细的统计信息，区分静态、参数、通配符路由
            RouteTypeCount count;

            if (!node) {
                return count;
            }

            // 如果当前节点有处理器，根据节点类型进行分类统计
            if (node->handler) {
                count.total++;
                switch (node->type) {
                    case RouteNodeType::STATIC:
                        count.static_count++;
                        break;
                    case RouteNodeType::PARAM:
                        count.param_count++;
                        break;
                    case RouteNodeType::WILDCARD:
                        count.wildcard_count++;
                        break;
                }
            }

            // 递归统计所有静态子节点
            for (const auto& [segment, child] : node->children) {
                RouteTypeCount child_count = countRoutesByType(child);
                count.total += child_count.total;
                count.static_count += child_count.static_count;
                count.param_count += child_count.param_count;
                count.wildcard_count += child_count.wildcard_count;
            }

            // 递归统计参数子节点
            if (node->param_child) {
                RouteTypeCount param_count = countRoutesByType(node->param_child);
                count.total += param_count.total;
                count.static_count += param_count.static_count;
                count.param_count += param_count.param_count;
                count.wildcard_count += param_count.wildcard_count;
            }

            // 递归统计通配符子节点
            if (node->wildcard_child) {
                RouteTypeCount wildcard_count = countRoutesByType(node->wildcard_child);
                count.total += wildcard_count.total;
                count.static_count += wildcard_count.static_count;
                count.param_count += wildcard_count.param_count;
                count.wildcard_count += wildcard_count.wildcard_count;
            }

            return count;
        }

        /**
         * @brief 递归删除路由节点
         * @param node 当前节点
         * @param segments 路径段数组
         * @param index 当前处理的段索引
         * @param full_pattern 完整路径模式（用于日志）
         * @return 删除成功返回true
         */
        bool HttpRouter::removeRouteRecursive(std::shared_ptr<RouteNode> node,
                                             const std::vector<std::string>& segments,
                                             size_t index,
                                             const std::string& full_pattern) {
            if (!node) {
                // LOG_DEBUG("节点为空，删除失败: " + full_pattern);
                return false;
            }

            // 如果已经处理完所有路径段，检查当前节点是否有处理器
            if (index >= segments.size()) {
                if (node->handler) {
                    // 找到目标节点，删除处理器
                    // LOG_DEBUG("找到目标节点，删除处理器: " + full_pattern);
                    node->handler = nullptr;
                    node->full_pattern.clear();
                    node->priority = 0;
                    return true;
                } else {
                    // LOG_DEBUG("目标节点没有处理器: " + full_pattern);
                    return false;
                }
            }

            const std::string& segment = segments[index];
            bool removed = false;

            // 检查是否为通配符段
            if (isWildcardSegment(segment)) {
                // 处理通配符路由删除
                if (node->wildcard_child) {
                    // LOG_DEBUG("处理通配符路由删除: " + segment);
                    removed = removeRouteRecursive(node->wildcard_child, segments, index + 1, full_pattern);

                    // 如果通配符子节点变为空，删除它
                    if (removed && isNodeEmpty(node->wildcard_child)) {
                        // LOG_DEBUG("删除空的通配符子节点");
                        node->wildcard_child = nullptr;
                    }
                }
            } else {
                // 处理静态路由删除
                auto child_it = node->children.find(segment);
                if (child_it != node->children.end()) {
                    // LOG_DEBUG("处理静态路由删除: " + segment);
                    removed = removeRouteRecursive(child_it->second, segments, index + 1, full_pattern);

                    // 如果子节点变为空，删除它
                    if (removed && isNodeEmpty(child_it->second)) {
                        // LOG_DEBUG("删除空的静态子节点: " + segment);
                        node->children.erase(child_it);
                    }
                }
            }

            return removed;
        }

        /**
         * @brief 检查节点是否为空（没有处理器且没有子节点）
         * @param node 要检查的节点
         * @return 节点为空返回true
         */
        bool HttpRouter::isNodeEmpty(std::shared_ptr<RouteNode> node) const {
            if (!node) {
                return true;
            }

            // 检查是否有处理器
            if (node->handler) {
                return false;
            }

            // 检查是否有静态子节点
            if (!node->children.empty()) {
                return false;
            }

            // 检查是否有通配符子节点
            if (node->wildcard_child) {
                return false;
            }

            return true;
        }

        /**
         * @brief 检查方法树是否为空
         * @param root 方法树的根节点
         * @return 方法树为空返回true
         */
        bool HttpRouter::isMethodTreeEmpty(std::shared_ptr<RouteNode> root) const {
            return isNodeEmpty(root);
        }

        // ==================== API端点发现方法实现 ====================

        std::vector<HttpRouter::ApiEndpoint> HttpRouter::getAllApiEndpoints() const {
            // 获取所有API端点信息
            std::shared_lock<std::shared_mutex> lock(router_mutex_);
            
            std::vector<ApiEndpoint> endpoints;
            endpoints.reserve(api_metadata_.size());
            
            for (const auto& [key, metadata] : api_metadata_) {
                endpoints.push_back(metadata);
            }
            
            // LOG_DEBUG("返回 " + std::to_string(endpoints.size()) + " 个API端点");
            return endpoints;
        }

        std::vector<HttpRouter::ApiEndpoint> HttpRouter::getApiEndpointsByMethod(const std::string& method) const {
            // 获取指定方法的API端点信息
            std::shared_lock<std::shared_mutex> lock(router_mutex_);
            
            std::vector<ApiEndpoint> endpoints;
            
            for (const auto& [key, metadata] : api_metadata_) {
                if (metadata.method == method) {
                    endpoints.push_back(metadata);
                }
            }
            
            // LOG_DEBUG("返回 " + std::to_string(endpoints.size()) + " 个 " + method + " 方法的API端点");
            return endpoints;
        }

        void HttpRouter::setApiMetadata(const std::string& method, const std::string& path, const ApiEndpoint& metadata) {
            // 设置API端点元数据
            std::unique_lock<std::shared_mutex> lock(router_mutex_);
            
            std::string key = method + "_" + normalizePath(path);
            api_metadata_[key] = metadata;
            
            // LOG_DEBUG("设置API元数据: " + method + " " + path);
        }

        nlohmann::json HttpRouter::getOpenApiSpec(const nlohmann::json& service_info) const {
            // 生成OpenAPI 3.0规范格式的API文档
            std::shared_lock<std::shared_mutex> lock(router_mutex_);
            
            nlohmann::json spec = {
                {"openapi", "3.0.0"},
                {"info", {
                    {"title", service_info.value("title", "Microservice API")},
                    {"description", service_info.value("description", "自动生成的API文档")},
                    {"version", service_info.value("version", "1.0.0")}
                }},
                {"servers", {{
                    {"url", service_info.value("base_url", "http://localhost:8081")},
                    {"description", service_info.value("server_description", "主服务器")}
                }}},
                {"paths", nlohmann::json::object()},
                {"components", {
                    {"securitySchemes", {
                        {"bearerAuth", {
                            {"type", "http"},
                            {"scheme", "bearer"},
                            {"bearerFormat", "JWT"}
                        }}
                    }}
                }}
            };
            
            // 按路径分组API端点
            std::map<std::string, std::map<std::string, nlohmann::json>> paths;
            
            for (const auto& [key, endpoint] : api_metadata_) {
                std::string method_lower = endpoint.method;
                std::transform(method_lower.begin(), method_lower.end(), method_lower.begin(), ::tolower);
                
                nlohmann::json operation = {
                    {"summary", endpoint.description},
                    {"tags", endpoint.tags},
                    {"responses", endpoint.responses.empty() ? 
                        nlohmann::json{{"200", {{"description", "成功"}}}} : endpoint.responses}
                };
                
                if (endpoint.requires_auth) {
                    operation["security"] = {{{ "bearerAuth", {} }}};
                }
                
                if (!endpoint.parameters.empty()) {
                    operation["parameters"] = endpoint.parameters;
                }
                
                paths[endpoint.path][method_lower] = operation;
            }
            
            spec["paths"] = paths;
            
            // LOG_DEBUG("生成OpenAPI文档，包含 " + std::to_string(api_metadata_.size()) + " 个端点");
            return spec;
        }
        
        // ⭐⭐⭐ 阶段2优化2: 动态设置路由缓存大小
        // ⭐⭐⭐ 内存泄漏修复（2025-12-31）：使用真正的LRU清理逻辑
        void HttpRouter::setMaxCacheSize(size_t size) {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            max_cache_size_ = size;

            // 如果当前缓存超过新的大小限制，清理超出部分（从尾部删除最久未使用的）
            while (route_cache_list_.size() > max_cache_size_ && !route_cache_list_.empty()) {
                route_cache_map_.erase(route_cache_list_.back().key);
                route_cache_list_.pop_back();
            }
        }

    } // namespace http
} // namespace common
