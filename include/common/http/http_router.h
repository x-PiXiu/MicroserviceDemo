 /**
 * @file http_router.h
 * @brief HTTP路由管理器 - 高效的URL路由匹配和处理器分发
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - 基于前缀树（Trie）的高效路由匹配算法
 * - 支持静态路由、参数路由、通配符路由
 * - 路径参数提取（如 /users/:id）
 * - 通配符路由支持（如 /static/wildcard）
 * - 路由优先级管理
 * - 中间件支持
 * - 路由分组和嵌套
 *
 * 路由类型:
 * - 静态路由: /api/users
 * - 参数路由: /api/users/:id
 * - 通配符路由: /static/wildcard
 * - 正则路由: /api/users/\d+
 *
 * 与现有模块集成:
 * - 使用Logger进行路由匹配日志
 * - 支持与HttpHandler的集成
 * - 集成中间件系统
 *
 * 使用示例:
 * @code
 * HttpRouter router;
 * 
 * // 添加静态路由
 * router.get("/api/users", userListHandler);
 * 
 * // 添加参数路由
 * router.get("/api/users/:id", userDetailHandler);
 * 
 * // 添加通配符路由
 * router.get("/static/ *", staticFileHandler);
 * 
 * // 路由匹配
 * auto match = router.match("GET", "/api/users/123");
 * if (match.found) {
 *     std::string user_id = match.params["id"];
 *     match.handler->handle(request, response);
 * }
 * @endcode
 */

#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <unordered_map>
#include <regex>
#include <shared_mutex>
#include <functional>

#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/http/http_handler.h"
#include "common/logger/logger.h"
#include "nlohmann/json.hpp"

namespace common {
    namespace http {

        /**
         * @brief 路由参数类型
         */
        using RouteParams = std::unordered_map<std::string, std::string>;

        /**
         * @brief 路由节点类型
         */
        enum class RouteNodeType {
            STATIC,     ///< 静态节点
            PARAM,      ///< 参数节点（:param）
            WILDCARD    ///< 通配符节点（*）
        };

        /**
         * @brief 路由树节点
         * @details 路由匹配树的节点结构
         */
        struct RouteNode {
            RouteNodeType type = RouteNodeType::STATIC;         ///< 节点类型
            std::string segment;                                ///< 路径段
            std::string param_name;                             ///< 参数名（仅参数节点）
            std::string full_pattern;                           ///< 完整路径模式
            int priority = 0;                                   ///< 优先级
            
            std::shared_ptr<HttpHandler> handler;               ///< 处理器
            std::unordered_map<std::string, std::shared_ptr<RouteNode>> children; ///< 子节点
            std::shared_ptr<RouteNode> param_child;             ///< 参数子节点
            std::shared_ptr<RouteNode> wildcard_child;          ///< 通配符子节点

            /**
             * @brief 构造函数
             */
            RouteNode(RouteNodeType t = RouteNodeType::STATIC, const std::string& seg = "")
                : type(t), segment(seg) {}
        };

        /**
         * @brief 路由匹配结果
         */
        struct RouteMatch {
            bool found = false;                                 ///< 是否找到匹配
            std::shared_ptr<HttpHandler> handler;               ///< 匹配的处理器
            RouteParams params;                                 ///< 路径参数
            std::string matched_pattern;                        ///< 匹配的模式
            int priority = 0;                                   ///< 优先级

            /**
             * @brief 重置匹配结果
             */
            void reset() {
                found = false;
                handler.reset();
                params.clear();
                matched_pattern.clear();
                priority = 0;
            }
        };

        /**
         * @brief HTTP路由管理器
         * @details 负责管理和匹配HTTP路由
         * 
         * 核心职责:
         * - 路由注册和管理
         * - 高效的路由匹配算法
         * - 路径参数提取
         * - 中间件集成
         * - 路由统计和监控
         */
        class HttpRouter {
        public:
            /**
             * @brief 构造函数
             */
            HttpRouter();

            /**
             * @brief 析构函数
             */
            ~HttpRouter() = default;

            // 禁用拷贝构造和赋值
            HttpRouter(const HttpRouter&) = delete;
            HttpRouter& operator=(const HttpRouter&) = delete;

            // ==================== 路由注册方法 ====================

            /**
             * @brief 添加路由
             * @param method HTTP方法
             * @param pattern 路径模式
             * @param handler 处理器
             * @param priority 优先级（数值越大优先级越高）
             */
            void addRoute(const std::string& method, const std::string& pattern, 
                         std::shared_ptr<HttpHandler> handler, int priority = 0);

            /**
             * @brief 添加路由（函数版本）
             * @param method HTTP方法
             * @param pattern 路径模式
             * @param handler 处理函数
             * @param priority 优先级
             */
            void addRoute(const std::string& method, const std::string& pattern, 
                         RequestHandler handler, int priority = 0);

            /**
             * @brief GET路由
             */
            void get(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0);
            void get(const std::string& pattern, RequestHandler handler, int priority = 0);

            /**
             * @brief POST路由
             */
            void post(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0);
            void post(const std::string& pattern, RequestHandler handler, int priority = 0);

            /**
             * @brief PUT路由
             */
            void put(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0);
            void put(const std::string& pattern, RequestHandler handler, int priority = 0);

            /**
             * @brief DELETE路由
             */
            void del(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0);
            void del(const std::string& pattern, RequestHandler handler, int priority = 0);

            // ==================== 路由匹配方法 ====================

            /**
             * @brief 匹配路由
             * @param method HTTP方法
             * @param path 请求路径
             * @return 匹配结果
             */
            RouteMatch match(const std::string& method, const std::string& path) const;

            /**
             * @brief 匹配路由（请求对象版本）
             * @param request HTTP请求
             * @return 匹配结果
             */
            RouteMatch match(const HttpRequest& request) const;

            // ==================== 中间件管理 ====================

            /**
             * @brief 添加全局中间件
             * @param middleware 中间件函数
             */
            void addGlobalMiddleware(MiddlewareHandler middleware);

            /**
             * @brief 添加路径特定中间件
             * @param pattern 路径模式
             * @param middleware 中间件函数
             */
            void addMiddleware(const std::string& pattern, MiddlewareHandler middleware);

            /**
             * @brief 执行中间件
             * @param request HTTP请求
             * @param response HTTP响应
             * @param path 请求路径
             * @return 是否继续处理
             */
            bool executeMiddlewares(const HttpRequest& request, HttpResponse& response, 
                                  const std::string& path) const;

            // ==================== 路由管理方法 ====================

            /**
             * @brief 移除路由
             * @param method HTTP方法
             * @param pattern 路径模式
             * @return 移除成功返回true
             */
            bool removeRoute(const std::string& method, const std::string& pattern);

            /**
             * @brief 清空所有路由
             */
            void clear();

            /**
             * @brief 获取路由数量
             * @param method HTTP方法（空表示所有方法）
             * @return 路由数量
             */
            size_t getRouteCount(const std::string& method = "") const;

            /**
             * @brief 路由统计信息结构
             */
            struct RouteStatistics {
                size_t total_routes = 0;        ///< 总路由数
                size_t static_routes = 0;       ///< 静态路由数
                size_t param_routes = 0;        ///< 参数路由数
                size_t wildcard_routes = 0;     ///< 通配符路由数
                std::map<std::string, size_t> method_counts; ///< 各HTTP方法的路由数

                /**
                 * @brief 转换为JSON格式
                 */
                nlohmann::json toJson() const {
                    nlohmann::json result;
                    result["total_routes"] = total_routes;
                    result["static_routes"] = static_routes;
                    result["param_routes"] = param_routes;
                    result["wildcard_routes"] = wildcard_routes;
                    result["method_counts"] = method_counts;
                    return result;
                }

                /**
                 * @brief 转换为字符串格式
                 */
                std::string toString() const {
                    std::ostringstream oss;
                    oss << "Route Statistics:\n";
                    oss << "  Total Routes: " << total_routes << "\n";
                    oss << "  Static Routes: " << static_routes << "\n";
                    oss << "  Parameter Routes: " << param_routes << "\n";
                    oss << "  Wildcard Routes: " << wildcard_routes << "\n";
                    oss << "  By HTTP Method:\n";
                    for (const auto& [method, count] : method_counts) {
                        oss << "    " << method << ": " << count << "\n";
                    }
                    return oss.str();
                }
            };

            /**
             * @brief 获取详细的路由统计信息
             * @return 路由统计信息
             */
            RouteStatistics getRouteStatistics() const;

            /**
             * @brief 获取所有路由列表
             * @return 路由列表
             */
            std::vector<std::string> getAllRoutes() const;

            /**
             * @brief 获取指定HTTP方法的路由列表
             * @param method HTTP方法
             * @return 该方法的路由列表
             */
            std::vector<std::string> getRoutesByMethod(const std::string& method) const;

            /**
             * @brief 检查路由是否存在
             * @param method HTTP方法
             * @param path 路径
             * @return 路由是否存在
             */
            bool hasRoute(const std::string& method, const std::string& path) const;

            /**
             * @brief 获取路由匹配的详细信息（用于调试）
             * @param method HTTP方法
             * @param path 路径
             * @return 匹配详情字符串
             */
            std::string getMatchDetails(const std::string& method, const std::string& path) const;

            /**
             * @brief 打印路由信息
             * @param method HTTP方法（空表示所有方法）
             */
            void printRoutes(const std::string& method = "") const;

            /**
             * @brief 验证路由配置
             * @return 验证成功返回true
             */
            bool validate() const;

            // ==================== API端点发现接口 ====================

            /**
             * @brief API端点元数据结构
             */
            struct ApiEndpoint {
                std::string method;                                         ///< HTTP方法
                std::string path;                                          ///< 路径模式
                std::string description;                                   ///< 端点描述
                std::vector<std::string> tags;                             ///< 标签
                bool requires_auth = false;                                ///< 是否需要认证
                std::string permission_level = "public";                   ///< 权限级别
                nlohmann::json parameters;                                 ///< 参数信息
                nlohmann::json responses;                                  ///< 响应信息
                std::string version = "v1";                                ///< API版本
                
                /**
                 * @brief 转换为JSON格式
                 */
                nlohmann::json toJson() const {
                    return nlohmann::json{
                        {"method", method},
                        {"path", path},
                        {"description", description},
                        {"tags", tags},
                        {"requires_auth", requires_auth},
                        {"permission_level", permission_level},
                        {"parameters", parameters},
                        {"responses", responses},
                        {"version", version}
                    };
                }
            };

            /**
             * @brief 获取所有API端点信息
             * @return API端点信息列表
             */
            std::vector<ApiEndpoint> getAllApiEndpoints() const;

            /**
             * @brief 获取指定方法的API端点信息
             * @param method HTTP方法
             * @return API端点信息列表
             */
            std::vector<ApiEndpoint> getApiEndpointsByMethod(const std::string& method) const;

            /**
             * @brief 设置API端点元数据
             * @param method HTTP方法
             * @param path 路径模式
             * @param metadata 元数据
             */
            void setApiMetadata(const std::string& method, const std::string& path, const ApiEndpoint& metadata);

            /**
             * @brief 获取OpenAPI规范格式的API文档
             * @param service_info 服务信息
             * @return OpenAPI格式的JSON
             */
            nlohmann::json getOpenApiSpec(const nlohmann::json& service_info = {}) const;

            /**
                 * @brief 设置路由缓存最大大小
                 * @param size 最大缓存大小
                 * @details 调用此方法可以动态调整路由缓存大小，适应不同的服务规模
                 */
            void setMaxCacheSize(size_t size);

        private:
            // ⭐⭐⭐ 阶段2优化2: 路由缓存（从配置加载）
            // ⭐⭐⭐ 内存泄漏修复（2025-12-31）：使用真正的LRU实现
            struct CacheEntry {
                std::string key;
                RouteMatch value;
                std::chrono::steady_clock::time_point timestamp;  // 插入时间戳
            };

            mutable std::list<CacheEntry> route_cache_list_;  // 维护访问时间顺序（新->旧）
            mutable std::unordered_map<std::string, std::list<CacheEntry>::iterator> route_cache_map_;  // 快速查找
            mutable std::mutex cache_mutex_;                  ///< 缓存锁
            size_t max_cache_size_ = 1024;                    ///< 最大缓存大小（可配置）

            /**
             * @brief 获取路由缓存最大大小
             * @return 最大缓存大小
             */
            size_t getMaxCacheSize() const { return max_cache_size_; }

            // ==================== 私有成员变量 ====================
            std::unordered_map<std::string, std::shared_ptr<RouteNode>> method_trees_; ///< 方法路由树
            std::vector<MiddlewareHandler> global_middlewares_;                         ///< 全局中间件
            std::unordered_map<std::string, std::vector<MiddlewareHandler>> path_middlewares_; ///< 路径中间件
            mutable std::shared_mutex router_mutex_;                                    ///< 路由器锁
            
            // API元数据存储
            std::unordered_map<std::string, ApiEndpoint> api_metadata_;                 ///< API端点元数据（key: method_path）

            // ==================== 私有数据结构 ====================

            /**
             * @brief 路由类型计数结构
             */
            struct RouteTypeCount {
                size_t total = 0;           ///< 总路由数
                size_t static_count = 0;    ///< 静态路由数
                size_t param_count = 0;     ///< 参数路由数
                size_t wildcard_count = 0;  ///< 通配符路由数
            };

            // ==================== 私有方法 ====================

            /**
             * @brief 获取或创建方法树
             * @param method HTTP方法
             * @return 方法树根节点
             */
            std::shared_ptr<RouteNode> getOrCreateMethodTree(const std::string& method);

            /**
             * @brief 解析路径模式
             * @param pattern 路径模式
             * @return 路径段列表
             */
            std::vector<std::string> parsePattern(const std::string& pattern) const;

            /**
             * @brief 插入路由到树中
             * @param root 根节点
             * @param segments 路径段
             * @param handler 处理器
             * @param priority 优先级
             * @param full_pattern 完整模式
             */
            void insertRoute(std::shared_ptr<RouteNode> root, const std::vector<std::string>& segments,
                           std::shared_ptr<HttpHandler> handler, int priority, const std::string& full_pattern);

            /**
             * @brief 搜索路由
             * @param root 根节点
             * @param segments 路径段
             * @param params 路径参数（输出）
             * @return 匹配结果
             */
            RouteMatch searchRoute(std::shared_ptr<RouteNode> root, const std::vector<std::string>& segments,
                                 RouteParams& params) const;

            /**
             * @brief 检查是否为参数段
             * @param segment 路径段
             * @return 是参数段返回true
             */
            bool isParamSegment(const std::string& segment) const;

            /**
             * @brief 检查是否为通配符段
             * @param segment 路径段
             * @return 是通配符段返回true
             */
            bool isWildcardSegment(const std::string& segment) const;

            /**
             * @brief 提取参数名
             * @param segment 参数段
             * @return 参数名
             */
            std::string extractParamName(const std::string& segment) const;

            /**
             * @brief 标准化路径
             * @param path 原始路径
             * @return 标准化后的路径
             */
            std::string normalizePath(const std::string& path) const;

            /**
             * @brief 打印路由树
             * @param node 节点
             * @param prefix 前缀
             * @param depth 深度
             */
            void printRouteTree(std::shared_ptr<RouteNode> node, const std::string& prefix, int depth) const;

            /**
             * @brief 收集路由信息
             * @param node 节点
             * @param method HTTP方法
             * @param prefix 前缀
             * @param routes 路由列表（输出）
             */
            void collectRoutes(std::shared_ptr<RouteNode> node, const std::string& method,
                             const std::string& prefix, std::vector<std::string>& routes) const;

            /**
             * @brief 统计路由树中的路由数量
             * @param node 路由树节点
             * @return 该节点及其子树中有处理器的节点数量
             * @details 递归遍历路由树，精确统计有处理器的节点数量
             */
            size_t countRoutesInTree(std::shared_ptr<RouteNode> node) const;

            /**
             * @brief 按类型统计路由树中的路由数量
             * @param node 路由树节点
             * @return 按类型分类的路由统计信息
             * @details 递归遍历路由树，统计不同类型的路由数量
             */
            RouteTypeCount countRoutesByType(std::shared_ptr<RouteNode> node) const;

            /**
             * @brief 递归删除路由节点
             * @param node 当前节点
             * @param segments 路径段数组
             * @param index 当前处理的段索引
             * @param full_pattern 完整路径模式（用于日志）
             * @return 删除成功返回true
             * @details 递归查找目标节点并删除其处理器，同时清理空的子节点
             */
            bool removeRouteRecursive(std::shared_ptr<RouteNode> node,
                                     const std::vector<std::string>& segments,
                                     size_t index,
                                     const std::string& full_pattern);

            /**
             * @brief 检查节点是否为空（没有处理器且没有子节点）
             * @param node 要检查的节点
             * @return 节点为空返回true
             * @details 用于判断节点是否可以被安全删除
             */
            bool isNodeEmpty(std::shared_ptr<RouteNode> node) const;

            /**
             * @brief 检查方法树是否为空
             * @param root 方法树的根节点
             * @return 方法树为空返回true
             * @details 用于判断整个方法树是否可以被删除
             */
            bool isMethodTreeEmpty(std::shared_ptr<RouteNode> root) const;
        };

    } // namespace http
} // namespace common

#endif // HTTP_ROUTER_H
