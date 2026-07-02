/**
 * @file registry_handler.h
 * @brief HTTP 处理器接口
 * @details 处理服务注册相关的 HTTP 请求
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "registry_manager.h"
#include "health_monitor.h"
#include "query_engine.h"
#include "common/http/http_server.h"
#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/http/http_handler.h"
#include <memory>

namespace core_services {
namespace service_registry {

/**
 * @brief HTTP 处理器
 * @details 提供 RESTful API 端点
 */
class RegistryHandler {
public:
    /**
     * @brief 构造函数
     * @param registry_manager 注册管理器
     * @param health_monitor 健康监控器
     * @param query_engine 查询引擎
     */
    RegistryHandler(
        std::shared_ptr<RegistryManager> registry_manager,
        std::shared_ptr<HealthMonitor> health_monitor,
        std::shared_ptr<QueryEngine> query_engine
    );

    /**
     * @brief 析构函数
     */
    ~RegistryHandler() = default;

    /**
     * @brief 注册 HTTP 路由到服务器
     * @param http_server HTTP 服务器
     */
    void registerRoutes(common::http::HttpServer* http_server);

private:
    std::shared_ptr<RegistryManager> registry_manager_;
    std::shared_ptr<HealthMonitor> health_monitor_;
    std::shared_ptr<QueryEngine> query_engine_;

    // ==================== 服务注册相关 ====================

    /**
     * @brief 处理服务注册请求
     * @brief POST /api/v1/services/register
     */
    void handleRegister(const common::http::HttpRequest& req,
                       common::http::HttpResponse& res);

    /**
     * @brief 处理批量服务注册请求
     * @brief POST /api/v1/services/batch-register
     */
    void handleBatchRegister(const common::http::HttpRequest& req,
                            common::http::HttpResponse& res);

    /**
     * @brief 处理服务注销请求
     * @brief DELETE /api/v1/services/deregister
     */
    void handleDeregister(const common::http::HttpRequest& req,
                         common::http::HttpResponse& res);

    /**
     * @brief 处理心跳请求
     * @brief POST /api/v1/services/heartbeat
     */
    void handleHeartbeat(const common::http::HttpRequest& req,
                        common::http::HttpResponse& res);

    // ==================== 查询相关 ====================

    /**
     * @brief 处理服务列表查询
     * @brief GET /api/v1/services （兼容旧路径）
     * @brief GET /api/v1/services/lists （推荐，避免 Nginx location 匹配问题）
     */
    void handleListServices(const common::http::HttpRequest& req,
                           common::http::HttpResponse& res);

    /**
     * @brief 处理特定服务查询
     * @brief GET /api/v1/services/{name}
     */
    void handleGetService(const common::http::HttpRequest& req,
                         common::http::HttpResponse& res);

    /**
     * @brief 处理服务实例查询
     * @brief GET /api/v1/services/{name}/{host}:{port}
     */
    void handleGetInstance(const common::http::HttpRequest& req,
                          common::http::HttpResponse& res);

    /**
     * @brief 处理服务健康状态查询
     * @brief GET /api/v1/services/{name}/health
     */
    void handleServiceHealth(const common::http::HttpRequest& req,
                            common::http::HttpResponse& res);

    /**
     * @brief 处理统计信息查询
     * @brief GET /api/v1/services/stats
     */
    void handleGetStats(const common::http::HttpRequest& req,
                       common::http::HttpResponse& res);

    /**
     * @brief 处理推荐请求
     * @brief GET /api/v1/services/recommend
     */
    void handleRecommend(const common::http::HttpRequest& req,
                        common::http::HttpResponse& res);

    // ==================== 管理接口 ====================

    /**
     * @brief 处理管理清理请求
     * @brief POST /api/v1/services/admin/cleanup
     */
    void handleAdminCleanup(const common::http::HttpRequest& req,
                           common::http::HttpResponse& res);

    /**
     * @brief 处理管理重建索引请求
     * @brief POST /api/v1/services/admin/rebuild-index
     */
    void handleAdminRebuildIndex(const common::http::HttpRequest& req,
                                common::http::HttpResponse& res);

    // ==================== 健康检查 ====================

    /**
     * @brief 处理健康检查
     * @brief GET /api/v1/services/health
     */
    void handleHealth(const common::http::HttpRequest& req,
                     common::http::HttpResponse& res);

    // ==================== 对局统计 ====================

    /**
     * @brief 处理对局完成报告
     * @brief POST /api/v1/services/matches/report
     */
    void handleReportMatch(const common::http::HttpRequest& req,
                          common::http::HttpResponse& res);

    // ==================== 辅助方法 ====================

    /**
     * @brief 构建成功响应
     */
    nlohmann::json buildSuccessResponse(const std::string& message,
                                        const nlohmann::json& data = nlohmann::json::object());

    /**
     * @brief 构建错误响应
     */
    nlohmann::json buildErrorResponse(const std::string& error_code,
                                      const std::string& error_message);

    /**
     * @brief 设置 JSON 响应头
     */
    void setJsonHeaders(common::http::HttpResponse& res);

    /**
     * @brief 解析 JSON 请求体
     */
    std::optional<nlohmann::json> parseJsonBody(const common::http::HttpRequest& req);

    /**
     * @brief 从 JSON 构建 ServiceInfo
     */
    ServiceInfo buildServiceInfoFromJson(const nlohmann::json& json);

    /**
     * @brief 从查询参数构建过滤器
     */
    ServiceFilter buildFilterFromQuery(const std::unordered_map<std::string, std::string>& params);

    /**
     * @brief 获取当前时间戳（毫秒）
     */
    int64_t getCurrentTimestampMs();
};

} // namespace service_registry
} // namespace core_services
