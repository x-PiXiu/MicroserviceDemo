/**
 * @file registry_handler.cpp
 * @brief HTTP 处理器实现
 * @details 实现服务注册相关的 HTTP 请求处理
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "registry_handler.h"
#include <sstream>

namespace core_services {
namespace service_registry {

RegistryHandler::RegistryHandler(
    std::shared_ptr<RegistryManager> registry_manager,
    std::shared_ptr<HealthMonitor> health_monitor,
    std::shared_ptr<QueryEngine> query_engine
) : registry_manager_(registry_manager)
  , health_monitor_(health_monitor)
  , query_engine_(query_engine) {
}

void RegistryHandler::registerRoutes(common::http::HttpServer* http_server) {
    if (!http_server) return;

    // ==================== 服务注册相关 ====================

    // 单个服务注册
    http_server->post("/api/v1/services/register",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleRegister(req, res);
        });

    // 批量服务注册
    http_server->post("/api/v1/services/batch-register",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleBatchRegister(req, res);
        });

    // 服务注销
    http_server->del("/api/v1/services/deregister",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleDeregister(req, res);
        });

    // 心跳
    http_server->post("/api/v1/services/heartbeat",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleHeartbeat(req, res);
        });

    // ==================== 查询相关 ====================

    // 列出所有服务（保留兼容）
    http_server->get("/api/v1/services",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleListServices(req, res);
        });

    // 列出所有服务（推荐路径，避免 Nginx location 匹配问题）
    http_server->get("/api/v1/services/lists",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleListServices(req, res);
        });

    // 获取特定服务的所有实例
    http_server->get("/api/v1/services/:service_name",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleGetService(req, res);
        });

    // 获取特定服务实例 (host:port 格式)
    http_server->get("/api/v1/services/:service_name/:host::port",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleGetInstance(req, res);
        });

    // 服务健康检查
    http_server->get("/api/v1/services/:service_name/health",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleServiceHealth(req, res);
        });

    // 统计信息
    http_server->get("/api/v1/services/stats",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleGetStats(req, res);
        });

    // 服务推荐
    http_server->get("/api/v1/services/recommend",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleRecommend(req, res);
        });

    // ==================== 管理接口 ====================

    // 手动触发清理过期服务
    http_server->post("/api/v1/services/admin/cleanup",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleAdminCleanup(req, res);
        });

    // 重建索引
    http_server->post("/api/v1/services/admin/rebuild-index",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleAdminRebuildIndex(req, res);
        });

    // ==================== 健康检查 ====================

    http_server->get("/api/v1/services/health",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleHealth(req, res);
        });

    // ==================== 对局统计 ====================

    // 报告对局完成
    http_server->post("/api/v1/services/matches/report",
        [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
            handleReportMatch(req, res);
        });

    LOG_INFO("Service Registry HTTP routes registered (17 endpoints)");
}

// ==================== 服务注册相关 ====================

void RegistryHandler::handleRegister(const common::http::HttpRequest& req,
                                     common::http::HttpResponse& res) {
    setJsonHeaders(res);

    auto json = parseJsonBody(req);
    if (!json) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST", "Invalid JSON body").dump());
        return;
    }

    try {
        ServiceInfo service = buildServiceInfoFromJson(*json);

        if (registry_manager_->registerService(service)) {
            nlohmann::json data;
            data["service_name"] = service.service_name;
            data["instance_id"] = service.instanceId();
            data["redis_key"] = service.redisKey();

            res.setStatus(200);
            res.setBody(buildSuccessResponse("Service registered successfully", data).dump());
        } else {
            res.setStatus(500);
            res.setBody(buildErrorResponse("REGISTER_FAILED", "Failed to register service").dump());
        }
    } catch (const std::exception& e) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_DATA", e.what()).dump());
    }
}

void RegistryHandler::handleBatchRegister(const common::http::HttpRequest& req,
                                          common::http::HttpResponse& res) {
    setJsonHeaders(res);

    auto json = parseJsonBody(req);
    if (!json) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST", "Invalid JSON body").dump());
        return;
    }

    if (!json->contains("services") || !json->at("services").is_array()) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST",
            "Request must contain 'services' array").dump());
        return;
    }

    int success_count = 0;
    int failed_count = 0;
    nlohmann::json results = nlohmann::json::array();

    for (const auto& service_json : json->at("services")) {
        try {
            ServiceInfo service = buildServiceInfoFromJson(service_json);
            if (registry_manager_->registerService(service)) {
                success_count++;
                results.push_back({
                    {"instance_id", service.instanceId()},
                    {"success", true}
                });
            } else {
                failed_count++;
                results.push_back({
                    {"instance_id", service.instanceId()},
                    {"success", false},
                    {"error", "Registration failed"}
                });
            }
        } catch (const std::exception& e) {
            failed_count++;
            results.push_back({
                {"success", false},
                {"error", e.what()}
            });
        }
    }

    nlohmann::json data;
    data["total"] = success_count + failed_count;
    data["success_count"] = success_count;
    data["failed_count"] = failed_count;
    data["results"] = results;

    res.setStatus(200);
    res.setBody(buildSuccessResponse("Batch registration completed", data).dump());
}

void RegistryHandler::handleDeregister(const common::http::HttpRequest& req,
                                       common::http::HttpResponse& res) {
    setJsonHeaders(res);

    auto json = parseJsonBody(req);
    if (!json) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST", "Invalid JSON body").dump());
        return;
    }

    std::string service_name = json->value("service_name", "");
    std::string host = json->value("host", "");
    int port = json->value("port", 0);

    if (service_name.empty() || host.empty() || port == 0) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST",
            "service_name, host and port are required").dump());
        return;
    }

    if (registry_manager_->deregisterService(service_name, host, port)) {
        res.setStatus(200);
        res.setBody(buildSuccessResponse("Service deregistered successfully").dump());
    } else {
        res.setStatus(404);
        res.setBody(buildErrorResponse("NOT_FOUND", "Service not found").dump());
    }
}

void RegistryHandler::handleHeartbeat(const common::http::HttpRequest& req,
                                      common::http::HttpResponse& res) {
    setJsonHeaders(res);

    auto json = parseJsonBody(req);
    if (!json) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST", "Invalid JSON body").dump());
        return;
    }

    std::string service_name = json->value("service_name", "");
    std::string host = json->value("host", "");
    int port = json->value("port", 0);

    if (service_name.empty() || host.empty() || port == 0) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST",
            "service_name, host and port are required").dump());
        return;
    }

    // 获取更新的元数据
    std::unordered_map<std::string, std::string> metadata;
    if (json->contains("metadata") && json->at("metadata").is_object()) {
        for (auto& [key, value] : json->at("metadata").items()) {
            if (value.is_string()) {
                metadata[key] = value.get<std::string>();
            } else {
                metadata[key] = value.dump();
            }
        }
    }

    if (registry_manager_->updateHeartbeat(service_name, host, port, metadata)) {
        nlohmann::json data;
        data["next_heartbeat_deadline"] = getCurrentTimestampMs() + 60000;

        // 获取更新后的服务信息
        auto service = registry_manager_->getService(service_name, host, port);
        if (service) {
            data["health_score"] = service->health_score;
        }

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Heartbeat updated successfully", data).dump());
    } else {
        res.setStatus(404);
        res.setBody(buildErrorResponse("NOT_FOUND",
            "Service not found. Please register first.").dump());
    }
}

// ==================== 查询相关 ====================

void RegistryHandler::handleListServices(const common::http::HttpRequest& req,
                                         common::http::HttpResponse& res) {
    setJsonHeaders(res);

    try {
        ServiceFilter filter = buildFilterFromQuery(req.getParams());
        auto services = query_engine_->query(filter);

        nlohmann::json services_json = nlohmann::json::array();
        for (const auto& service : services) {
            services_json.push_back(service.toJson());
        }

        nlohmann::json data;
        data["services"] = services_json;
        data["count"] = services.size();
        data["timestamp"] = getCurrentTimestampMs();

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Query successful", data).dump());
    } catch (const std::exception& e) {
        res.setStatus(500);
        res.setBody(buildErrorResponse("QUERY_FAILED", e.what()).dump());
    }
}

void RegistryHandler::handleGetService(const common::http::HttpRequest& req,
                                       common::http::HttpResponse& res) {
    setJsonHeaders(res);

    // 从路径提取服务名称
    std::string path = req.getPath();
    std::string prefix = "/api/v1/services/";

    if (path.find(prefix) != 0) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_PATH", "Invalid request path").dump());
        return;
    }

    std::string rest = path.substr(prefix.length());

    // 检查是否是特定实例请求 (name/host:port)
    size_t slash_pos = rest.find('/');
    if (slash_pos != std::string::npos) {
        handleGetInstance(req, res);
        return;
    }

    std::string service_name = rest;

    // 移除尾部斜杠
    if (!service_name.empty() && service_name.back() == '/') {
        service_name.pop_back();
    }

    auto instances = registry_manager_->getServiceInstances(service_name);

    if (instances.empty()) {
        res.setStatus(404);
        res.setBody(buildErrorResponse("NOT_FOUND",
            "Service not found: " + service_name).dump());
        return;
    }

    nlohmann::json instances_json = nlohmann::json::array();
    for (const auto& instance : instances) {
        instances_json.push_back(instance.toJson());
    }

    nlohmann::json data;
    data["service_name"] = service_name;
    data["instances"] = instances_json;
    data["count"] = instances.size();

    res.setStatus(200);
    res.setBody(buildSuccessResponse("Query successful", data).dump());
}

void RegistryHandler::handleGetInstance(const common::http::HttpRequest& req,
                                        common::http::HttpResponse& res) {
    setJsonHeaders(res);

    std::string path = req.getPath();
    std::string prefix = "/api/v1/services/";

    std::string rest = path.substr(prefix.length());

    // 解析 service_name/host:port
    size_t slash_pos = rest.find('/');
    if (slash_pos == std::string::npos) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_PATH",
            "Invalid instance path. Use /api/v1/services/{name}/{host}:{port}").dump());
        return;
    }

    std::string service_name = rest.substr(0, slash_pos);
    std::string instance_id = rest.substr(slash_pos + 1);

    // 解析 host:port
    size_t colon_pos = instance_id.rfind(':');
    if (colon_pos == std::string::npos) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_PATH",
            "Invalid instance format. Use host:port").dump());
        return;
    }

    std::string host = instance_id.substr(0, colon_pos);
    int port = 0;
    try {
        port = std::stoi(instance_id.substr(colon_pos + 1));
    } catch (...) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_PORT", "Invalid port number").dump());
        return;
    }

    auto service = registry_manager_->getService(service_name, host, port);

    if (!service) {
        res.setStatus(404);
        res.setBody(buildErrorResponse("NOT_FOUND",
            "Service instance not found").dump());
        return;
    }

    nlohmann::json data;
    data["service"] = service->toJson();

    res.setStatus(200);
    res.setBody(buildSuccessResponse("Query successful", data).dump());
}

void RegistryHandler::handleServiceHealth(const common::http::HttpRequest& req,
                                          common::http::HttpResponse& res) {
    setJsonHeaders(res);

    // 从路径参数获取服务名称
    std::string service_name = req.getParam("service_name");

    if (service_name.empty()) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST",
            "service_name path parameter is required").dump());
        return;
    }

    auto instances = registry_manager_->getServiceInstances(service_name);

    if (instances.empty()) {
        res.setStatus(404);
        res.setBody(buildErrorResponse("NOT_FOUND",
            "Service not found: " + service_name).dump());
        return;
    }

    // 统计健康状态
    size_t healthy_count = 0;
    size_t unhealthy_count = 0;
    size_t degraded_count = 0;
    double avg_health_score = 0.0;

    nlohmann::json instances_health = nlohmann::json::array();
    for (const auto& instance : instances) {
        nlohmann::json health_info;
        health_info["instance_id"] = instance.instanceId();
        health_info["healthy"] = instance.healthy;
        health_info["health_score"] = instance.health_score;
        health_info["last_heartbeat_age"] = instance.heartbeatAgeSeconds();

        instances_health.push_back(health_info);

        avg_health_score += instance.health_score;
        if (instance.health_score >= 0.7) {
            healthy_count++;
        } else if (instance.health_score >= 0.3) {
            degraded_count++;
        } else {
            unhealthy_count++;
        }
    }

    if (!instances.empty()) {
        avg_health_score /= instances.size();
    }

    nlohmann::json data;
    data["service_name"] = service_name;
    data["total_instances"] = instances.size();
    data["healthy_instances"] = healthy_count;
    data["degraded_instances"] = degraded_count;
    data["unhealthy_instances"] = unhealthy_count;
    data["avg_health_score"] = avg_health_score;
    data["instances"] = instances_health;

    // 整体健康状态
    if (healthy_count == instances.size()) {
        data["overall_status"] = "healthy";
    } else if (healthy_count > 0) {
        data["overall_status"] = "degraded";
    } else {
        data["overall_status"] = "unhealthy";
    }

    res.setStatus(200);
    res.setBody(buildSuccessResponse("Service health retrieved", data).dump());
}

// ==================== 管理接口 ====================

void RegistryHandler::handleAdminCleanup(const common::http::HttpRequest& req,
                                         common::http::HttpResponse& res) {
    (void)req;  // 未使用
    setJsonHeaders(res);

    try {
        int cleaned_count = registry_manager_->cleanupExpiredServices();

        nlohmann::json data;
        data["cleaned_count"] = cleaned_count;
        data["timestamp"] = getCurrentTimestampMs();

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Cleanup completed", data).dump());
    } catch (const std::exception& e) {
        res.setStatus(500);
        res.setBody(buildErrorResponse("CLEANUP_FAILED", e.what()).dump());
    }
}

void RegistryHandler::handleAdminRebuildIndex(const common::http::HttpRequest& req,
                                              common::http::HttpResponse& res) {
    (void)req;  // 未使用
    setJsonHeaders(res);

    try {
        // 获取所有服务
        auto all_services = registry_manager_->getAllInstances();

        // 重建索引
        int rebuilt_count = registry_manager_->rebuildIndexes(all_services);

        nlohmann::json data;
        data["rebuilt_count"] = rebuilt_count;
        data["total_instances"] = all_services.size();
        data["timestamp"] = getCurrentTimestampMs();

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Index rebuilt successfully", data).dump());
    } catch (const std::exception& e) {
        res.setStatus(500);
        res.setBody(buildErrorResponse("REBUILD_FAILED", e.what()).dump());
    }
}

void RegistryHandler::handleGetStats(const common::http::HttpRequest& req,
                                     common::http::HttpResponse& res) {
    (void)req;  // 未使用，保留用于未来扩展
    setJsonHeaders(res);

    try {
        auto stats = registry_manager_->getStats();

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Statistics retrieved", stats).dump());
    } catch (const std::exception& e) {
        res.setStatus(500);
        res.setBody(buildErrorResponse("STATS_FAILED", e.what()).dump());
    }
}

void RegistryHandler::handleRecommend(const common::http::HttpRequest& req,
                                      common::http::HttpResponse& res) {
    setJsonHeaders(res);

    auto params = req.getParams();

    // 构建过滤器
    ServiceFilter filter;

    auto it = params.find("service_type");
    if (it != params.end()) {
        filter.service_type = it->second;
    }

    it = params.find("game_type");
    if (it != params.end()) {
        filter.game_type = it->second;
    }

    it = params.find("service_name");
    if (it != params.end()) {
        filter.service_name = it->second;
    }

    it = params.find("region");
    if (it != params.end()) {
        filter.region = it->second;
    }

    filter.healthy_only = true;

    // 获取推荐策略
    RecommendStrategy strategy = RecommendStrategy::LEAST_LOAD;
    it = params.find("strategy");
    if (it != params.end()) {
        if (it->second == "random") strategy = RecommendStrategy::RANDOM;
        else if (it->second == "round_robin") strategy = RecommendStrategy::ROUND_ROBIN;
        else if (it->second == "weighted") strategy = RecommendStrategy::WEIGHTED;
    }

    try {
        auto recommendation = query_engine_->recommend(filter, strategy);

        if (recommendation.recommended.service_name.empty()) {
            res.setStatus(404);
            res.setBody(buildErrorResponse("NO_AVAILABLE_SERVICE",
                "No available service found for the given criteria").dump());
            return;
        }

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Recommendation generated",
            recommendation.toJson()).dump());
    } catch (const std::exception& e) {
        res.setStatus(500);
        res.setBody(buildErrorResponse("RECOMMEND_FAILED", e.what()).dump());
    }
}

// ==================== 健康检查 ====================

void RegistryHandler::handleHealth(const common::http::HttpRequest& req,
                                   common::http::HttpResponse& res) {
    (void)req;  // 未使用，保留用于未来扩展
    setJsonHeaders(res);

    nlohmann::json health;
    health["status"] = "healthy";
    health["service"] = "service_registry";
    health["version"] = "1.0.0";
    health["timestamp"] = getCurrentTimestampMs();

    // 添加基本统计
    auto stats = registry_manager_->getStats();
    health["stats"] = stats;

    res.setStatus(200);
    res.setBody(health.dump());
}

// ==================== 对局统计 ====================

void RegistryHandler::handleReportMatch(const common::http::HttpRequest& req,
                                        common::http::HttpResponse& res) {
    setJsonHeaders(res);

    auto json = parseJsonBody(req);
    if (!json) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST", "Invalid JSON body").dump());
        return;
    }

    std::string game_type = json->value("game_type", "");
    std::string service_name = json->value("service_name", "");

    if (game_type.empty()) {
        res.setStatus(400);
        res.setBody(buildErrorResponse("INVALID_REQUEST", "game_type is required").dump());
        return;
    }

    if (registry_manager_->reportMatchCompletion(game_type, service_name)) {
        nlohmann::json data;
        data["game_type"] = game_type;
        data["service_name"] = service_name;
        data["today_matches"] = registry_manager_->getTodayMatches();

        res.setStatus(200);
        res.setBody(buildSuccessResponse("Match reported successfully", data).dump());
    } else {
        res.setStatus(500);
        res.setBody(buildErrorResponse("REPORT_FAILED", "Failed to report match completion").dump());
    }
}

// ==================== 辅助方法 ====================

nlohmann::json RegistryHandler::buildSuccessResponse(const std::string& message,
                                                      const nlohmann::json& data) {
    nlohmann::json response;
    response["success"] = true;
    response["message"] = message;
    response["timestamp"] = getCurrentTimestampMs();
    response["data"] = data;
    return response;
}

nlohmann::json RegistryHandler::buildErrorResponse(const std::string& error_code,
                                                    const std::string& error_message) {
    nlohmann::json response;
    response["success"] = false;
    response["error"]["code"] = error_code;
    response["error"]["message"] = error_message;
    response["timestamp"] = getCurrentTimestampMs();
    return response;
}

void RegistryHandler::setJsonHeaders(common::http::HttpResponse& res) {
    res.setHeader("Content-Type", "application/json; charset=utf-8");
}

std::optional<nlohmann::json> RegistryHandler::parseJsonBody(const common::http::HttpRequest& req) {
    try {
        std::string body = req.getBody();
        if (body.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse JSON body: " + std::string(e.what()));
        return std::nullopt;
    }
}

ServiceInfo RegistryHandler::buildServiceInfoFromJson(const nlohmann::json& json) {
    ServiceInfo info;

    info.service_name = json.value("service_name", "");
    info.service_version = json.value("service_version", "1.0.0");
    info.host = json.value("host", "");
    info.port = json.value("port", 0);

    // 自动生成 service_id（如果未提供）
    info.service_id = json.value("service_id", "");
    if (info.service_id.empty() && !info.service_name.empty() && !info.host.empty() && info.port > 0) {
        // 格式: {service_name}-{host}:{port}-{timestamp}
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        info.service_id = info.service_name + "-" + info.instanceId() + "-" + std::to_string(timestamp);
    }

    info.health_check_endpoint = json.value("health_check_endpoint", "/health");
    info.weight = json.value("weight", 100);

    if (json.contains("metadata") && json.at("metadata").is_object()) {
        for (auto& [key, value] : json.at("metadata").items()) {
            if (value.is_string()) {
                info.metadata[key] = value.get<std::string>();
            } else {
                info.metadata[key] = value.dump();
            }
        }
    }

    if (json.contains("endpoints") && json.at("endpoints").is_array()) {
        info.endpoints = json.at("endpoints").get<std::vector<std::string>>();
    }

    return info;
}

ServiceFilter RegistryHandler::buildFilterFromQuery(const std::unordered_map<std::string, std::string>& params) {
    ServiceFilter filter;

    auto it = params.find("service_name");
    if (it != params.end() && !it->second.empty()) {
        filter.service_name = it->second;
    }

    it = params.find("service_type");
    if (it != params.end() && !it->second.empty()) {
        filter.service_type = it->second;
    }

    it = params.find("game_type");
    if (it != params.end() && !it->second.empty()) {
        filter.game_type = it->second;
    }

    it = params.find("region");
    if (it != params.end() && !it->second.empty()) {
        filter.region = it->second;
    }

    it = params.find("healthy_only");
    if (it != params.end() && (it->second == "true" || it->second == "1")) {
        filter.healthy_only = true;
    }

    it = params.find("min_health_score");
    if (it != params.end()) {
        try {
            filter.min_health_score = std::stod(it->second);
        } catch (...) {}
    }

    it = params.find("limit");
    if (it != params.end()) {
        try {
            filter.limit = std::stoi(it->second);
        } catch (...) {}
    }

    it = params.find("offset");
    if (it != params.end()) {
        try {
            filter.offset = std::stoi(it->second);
        } catch (...) {}
    }

    it = params.find("sort_by");
    if (it != params.end()) {
        if (it->second == "health_score") filter.sort_by = ServiceFilter::SortBy::HEALTH_SCORE;
        else if (it->second == "load") filter.sort_by = ServiceFilter::SortBy::LOAD_ASC;
        else if (it->second == "random") filter.sort_by = ServiceFilter::SortBy::RANDOM;
    }

    return filter;
}

int64_t RegistryHandler::getCurrentTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace service_registry
} // namespace core_services
