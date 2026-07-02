/**
 * @file service_registry_client.cpp
 * @brief 统一的服务注册中心客户端实现
 * @details 实现服务注册、发现、健康检查等功能
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "common/service_registry/service_registry_client.h"
#include <sstream>

namespace common {
namespace service_registry {

ServiceRegistryClient::ServiceRegistryClient(const ServiceRegistryClientConfig& config)
    : config_(config) {
    http_client_.setDefaultTimeout(config.request_timeout_ms);
    http_client_.setDefaultHeader("Content-Type", "application/json");
    LOG_INFO("ServiceRegistryClient initialized, registry: " +
             config.registry_host + ":" + std::to_string(config.registry_port));
}

ServiceRegistryClient::~ServiceRegistryClient() {
    stopHeartbeat();
    LOG_INFO("ServiceRegistryClient destroyed");
}

// ==================== 服务注册 ====================

bool ServiceRegistryClient::registerService(const ServiceInstance& instance) {
    std::string url = buildUrl("/api/v1/services/register");

    nlohmann::json body = instance.toJson();

    auto response = http_client_.post(url, body.dump());

    auto result = parseResponse(response);
    if (!result) {
        LOG_ERROR("Failed to register service: " + instance.service_name +
                 ", error: " + response.error_message);
        return false;
    }

    if (!(*result)["success"].get<bool>()) {
        LOG_ERROR("Service registration failed: " +
                 (*result)["error"]["message"].get<std::string>());
        return false;
    }

    LOG_INFO("Service registered successfully: " + instance.service_name +
             " at " + instance.instanceId());
    return true;
}

bool ServiceRegistryClient::deregisterService(const std::string& service_name,
                                               const std::string& host, int port) {
    std::string url = buildUrl("/api/v1/services/deregister");

    nlohmann::json body;
    body["service_name"] = service_name;
    body["host"] = host;
    body["port"] = port;

    auto response = http_client_.del(url);

    auto result = parseResponse(response);
    if (!result) {
        LOG_ERROR("Failed to deregister service: " + service_name +
                 ", error: " + response.error_message);
        return false;
    }

    LOG_INFO("Service deregistered: " + service_name + " at " + host + ":" + std::to_string(port));
    return true;
}

bool ServiceRegistryClient::sendHeartbeat(const std::string& service_name,
                                          const std::string& host, int port,
                                          const std::unordered_map<std::string, std::string>& metadata) {
    std::string url = buildUrl("/api/v1/services/heartbeat");

    nlohmann::json body;
    body["service_name"] = service_name;
    body["host"] = host;
    body["port"] = port;

    if (!metadata.empty()) {
        body["metadata"] = metadata;
    }

    auto response = http_client_.post(url, body.dump());

    auto result = parseResponse(response);
    if (!result) {
        LOG_WARNING("Heartbeat failed for " + service_name + ": " + response.error_message);
        return false;
    }

    if (!(*result)["success"].get<bool>()) {
        LOG_WARNING("Heartbeat rejected for " + service_name + ": " +
                   (*result)["error"]["message"].get<std::string>());
        return false;
    }

    LOG_DEBUG("Heartbeat sent for " + service_name + " at " + host + ":" + std::to_string(port));
    return true;
}

// ==================== 心跳管理 ====================

void ServiceRegistryClient::startHeartbeat(const ServiceInstance& instance) {
    if (heartbeat_running_.load()) {
        LOG_WARNING("Heartbeat already running");
        return;
    }

    registered_instance_ = instance;
    heartbeat_running_.store(true);

    heartbeat_thread_ = std::thread([this]() {
        heartbeatLoop();
    });

    LOG_INFO("Heartbeat started for " + instance.service_name +
             " (interval: " + std::to_string(config_.heartbeat_interval_s) + "s)");
}

void ServiceRegistryClient::stopHeartbeat() {
    if (!heartbeat_running_.load()) {
        return;
    }

    heartbeat_running_.store(false);

    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    LOG_INFO("Heartbeat stopped");
}

bool ServiceRegistryClient::isHeartbeatRunning() const {
    return heartbeat_running_.load();
}

void ServiceRegistryClient::setHeartbeatCallback(HeartbeatCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    heartbeat_callback_ = std::move(callback);
    LOG_DEBUG("Heartbeat callback set");
}

void ServiceRegistryClient::updateHeartbeatMetadata(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    registered_instance_.metadata[key] = value;
}

void ServiceRegistryClient::updateHeartbeatMetadata(const std::unordered_map<std::string, std::string>& metadata) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [key, value] : metadata) {
        registered_instance_.metadata[key] = value;
    }
}

void ServiceRegistryClient::heartbeatLoop() {
    while (heartbeat_running_.load()) {
        // 准备心跳 metadata
        std::unordered_map<std::string, std::string> heartbeat_metadata;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 复制基础 metadata
            heartbeat_metadata = registered_instance_.metadata;

            // 调用回调获取动态 metadata
            if (heartbeat_callback_) {
                try {
                    auto dynamic_metadata = heartbeat_callback_();
                    // 合并动态 metadata（覆盖同名的静态值）
                    for (const auto& [key, value] : dynamic_metadata) {
                        heartbeat_metadata[key] = value;
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("Heartbeat callback exception: " + std::string(e.what()));
                }
            }
        }

        // 发送心跳
        bool success = sendHeartbeat(
            registered_instance_.service_name,
            registered_instance_.host,
            registered_instance_.port,
            heartbeat_metadata
        );

        if (!success && config_.auto_reconnect) {
            // 心跳失败，尝试重新注册
            LOG_WARNING("Heartbeat failed, attempting to re-register...");
            std::this_thread::sleep_for(std::chrono::seconds(config_.reconnect_delay_s));

            if (registerService(registered_instance_)) {
                LOG_INFO("Re-registration successful");
            }
        }

        // 等待下一次心跳
        for (int i = 0; i < config_.heartbeat_interval_s && heartbeat_running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// ==================== 服务发现 ====================

std::vector<ServiceInstance> ServiceRegistryClient::queryServices(const ServiceQueryFilter& filter) {
    std::string url = buildUrl("/api/v1/services", filter.toQueryString());

    auto response = http_client_.get(url);

    auto result = parseResponse(response);
    if (!result) {
        LOG_ERROR("Failed to query services: " + response.error_message);
        return {};
    }

    if (!(*result)["success"].get<bool>()) {
        LOG_ERROR("Query failed: " + (*result)["error"]["message"].get<std::string>());
        return {};
    }

    std::vector<ServiceInstance> instances;
    if (result->contains("data") && (*result)["data"].contains("services")) {
        for (const auto& svc_json : (*result)["data"]["services"]) {
            instances.push_back(ServiceInstance::fromJson(svc_json));
        }
    }

    LOG_DEBUG("Queried " + std::to_string(instances.size()) + " services");
    return instances;
}

std::vector<ServiceInstance> ServiceRegistryClient::getServiceInstances(const std::string& service_name) {
    std::string url = buildUrl("/api/v1/services/" + service_name);

    auto response = http_client_.get(url);

    auto result = parseResponse(response);
    if (!result) {
        LOG_ERROR("Failed to get service instances: " + response.error_message);
        return {};
    }

    if (!(*result)["success"].get<bool>()) {
        LOG_ERROR("Get service failed: " + (*result)["error"]["message"].get<std::string>());
        return {};
    }

    std::vector<ServiceInstance> instances;
    if (result->contains("data") && (*result)["data"].contains("instances")) {
        for (const auto& svc_json : (*result)["data"]["instances"]) {
            instances.push_back(ServiceInstance::fromJson(svc_json));
        }
    }

    return instances;
}

std::optional<ServiceInstance> ServiceRegistryClient::getServiceInstance(
    const std::string& service_name, const std::string& host, int port) {

    std::string url = buildUrl("/api/v1/services/" + service_name + "/" + host + ":" + std::to_string(port));

    auto response = http_client_.get(url);

    auto result = parseResponse(response);
    if (!result || !(*result)["success"].get<bool>()) {
        return std::nullopt;
    }

    if (result->contains("data") && (*result)["data"].contains("service")) {
        return ServiceInstance::fromJson((*result)["data"]["service"]);
    }

    return std::nullopt;
}

std::vector<ServiceInstance> ServiceRegistryClient::queryGameServices(
    const std::string& game_type, bool healthy_only) {

    ServiceQueryFilter filter;
    filter.service_type = "game";
    filter.healthy_only = healthy_only;

    if (!game_type.empty()) {
        filter.game_type = game_type;
    }

    return queryServices(filter);
}

std::vector<ServiceInstance> ServiceRegistryClient::queryCoreServices(
    const std::string& service_name, bool healthy_only) {

    ServiceQueryFilter filter;
    filter.service_type = "core";
    filter.healthy_only = healthy_only;

    if (!service_name.empty()) {
        filter.service_name = service_name;
    }

    return queryServices(filter);
}

// ==================== 服务推荐 ====================

ServiceRecommendation ServiceRegistryClient::recommendService(
    const std::string& service_type, const std::string& game_type, RecommendStrategy strategy) {

    std::string strategy_str;
    switch (strategy) {
        case RecommendStrategy::LEAST_LOAD: strategy_str = "least_load"; break;
        case RecommendStrategy::RANDOM: strategy_str = "random"; break;
        case RecommendStrategy::ROUND_ROBIN: strategy_str = "round_robin"; break;
        case RecommendStrategy::WEIGHTED: strategy_str = "weighted"; break;
    }

    std::string query = "?service_type=" + service_type + "&strategy=" + strategy_str;
    if (!game_type.empty()) {
        query += "&game_type=" + game_type;
    }

    std::string url = buildUrl("/api/v1/services/recommend", query);

    auto response = http_client_.get(url);

    auto result = parseResponse(response);
    if (!result) {
        LOG_ERROR("Failed to get recommendation: " + response.error_message);
        return {};
    }

    if (!(*result)["success"].get<bool>()) {
        LOG_WARNING("No recommendation available: " +
                   (*result)["error"]["message"].get<std::string>());
        return {};
    }

    ServiceRecommendation recommendation;
    recommendation.valid = true;

    if (result->contains("data")) {
        const auto& data = (*result)["data"];

        if (data.contains("recommended")) {
            recommendation.recommended = ServiceInstance::fromJson(data["recommended"]);
        }
        if (data.contains("load_score")) {
            recommendation.load_score = data["load_score"].get<double>();
        }
        if (data.contains("reason")) {
            recommendation.reason = data["reason"].get<std::string>();
        }
    }

    return recommendation;
}

ServiceRecommendation ServiceRegistryClient::recommendGameService(
    const std::string& game_type, RecommendStrategy strategy) {

    return recommendService("game", game_type, strategy);
}

// ==================== 健康检查 ====================

nlohmann::json ServiceRegistryClient::checkServiceHealth(const std::string& service_name) {
    std::string url = buildUrl("/api/v1/services/" + service_name + "/health");

    auto response = http_client_.get(url);

    auto result = parseResponse(response);
    if (!result) {
        return {{"error", response.error_message}};
    }

    return *result;
}

bool ServiceRegistryClient::checkRegistryHealth() {
    std::string url = buildUrl("/api/v1/services/health");

    auto response = http_client_.get(url);

    return response.success && response.status_code == 200;
}

// ==================== 统计信息 ====================

nlohmann::json ServiceRegistryClient::getStats() {
    std::string url = buildUrl("/api/v1/services/stats");

    auto response = http_client_.get(url);

    auto result = parseResponse(response);
    if (!result) {
        return {{"error", response.error_message}};
    }

    if (result->contains("data")) {
        return (*result)["data"];
    }

    return *result;
}

// ==================== 对局统计 ====================

bool ServiceRegistryClient::reportMatchCompletion(const std::string& game_type, const std::string& service_name) {
    std::string url = buildUrl("/api/v1/services/matches/report");

    nlohmann::json request_body = {
        {"game_type", game_type},
        {"service_name", service_name}
    };

    auto response = http_client_.post(url, request_body.dump(), {
        {"Content-Type", "application/json"}
    });

    if (!response.success) {
        LOG_WARNING("Failed to report match completion: " + response.error_message);
        return false;
    }

    auto result = parseResponse(response);
    if (!result) {
        return false;
    }

    return result->value("success", false);
}

// ==================== 私有方法 ====================

std::string ServiceRegistryClient::buildUrl(const std::string& path, const std::string& query) const {
    std::string url = "http://" + config_.registry_host + ":" + std::to_string(config_.registry_port);
    url += path;
    if (!query.empty()) {
        url += query;
    }
    return url;
}

std::optional<nlohmann::json> ServiceRegistryClient::parseResponse(const http::HttpClientResponse& response) {
    if (!response.success) {
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(response.body);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse JSON response: " + std::string(e.what()));
        return std::nullopt;
    }
}

} // namespace service_registry
} // namespace common
