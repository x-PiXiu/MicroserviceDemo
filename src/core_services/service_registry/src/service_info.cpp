/**
 * @file service_info.cpp
 * @brief 服务信息数据结构实现
 * @details 包含需要单独实现的方法
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "service_info.h"
#include <algorithm>
#include <random>
#include <ctime>

namespace core_services {
namespace service_registry {

// 健康状态转换为字符串
std::string healthStatusToString(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY: return "healthy";
        case HealthStatus::DEGRADED: return "degraded";
        case HealthStatus::UNHEALTHY: return "unhealthy";
        case HealthStatus::UNKNOWN: return "unknown";
        default: return "unknown";
    }
}

// 字符串转换为健康状态
HealthStatus stringToHealthStatus(const std::string& str) {
    if (str == "healthy") return HealthStatus::HEALTHY;
    if (str == "degraded") return HealthStatus::DEGRADED;
    if (str == "unhealthy") return HealthStatus::UNHEALTHY;
    return HealthStatus::UNKNOWN;
}

// 事件类型转换为字符串
std::string serviceEventTypeToString(ServiceEventType type) {
    switch (type) {
        case ServiceEventType::REGISTERED: return "registered";
        case ServiceEventType::DEREGISTERED: return "deregistered";
        case ServiceEventType::HEARTBEAT_MISSED: return "heartbeat_missed";
        case ServiceEventType::HEALTH_CHANGED: return "health_changed";
        case ServiceEventType::RECOVERED: return "recovered";
        case ServiceEventType::METADATA_UPDATED: return "metadata_updated";
        default: return "unknown";
    }
}

// 字符串转换为事件类型
ServiceEventType stringToServiceEventType(const std::string& str) {
    if (str == "registered") return ServiceEventType::REGISTERED;
    if (str == "deregistered") return ServiceEventType::DEREGISTERED;
    if (str == "heartbeat_missed") return ServiceEventType::HEARTBEAT_MISSED;
    if (str == "health_changed") return ServiceEventType::HEALTH_CHANGED;
    if (str == "recovered") return ServiceEventType::RECOVERED;
    if (str == "metadata_updated") return ServiceEventType::METADATA_UPDATED;
    return ServiceEventType::REGISTERED;
}

// 获取当前时间戳（毫秒）
int64_t getCurrentTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// 获取当前 ISO 8601 格式时间字符串
std::string getCurrentTimestampIso() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&now_time_t);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

// 生成随机实例 ID
std::string generateInstanceId(const std::string& host, int port) {
    return host + ":" + std::to_string(port);
}

// 计算两个时间点之间的秒数
int64_t secondsBetween(
    const std::chrono::system_clock::time_point& from,
    const std::chrono::system_clock::time_point& to
) {
    return std::chrono::duration_cast<std::chrono::seconds>(to - from).count();
}

// 判断服务是否过期（心跳超时）
bool isServiceExpired(const ServiceInfo& service, int64_t timeout_seconds) {
    return service.heartbeatAgeSeconds() > timeout_seconds;
}

// 根据健康分数判定健康状态
HealthStatus determineHealthStatus(const ServiceInfo& service, int64_t timeout_seconds) {
    // 心跳超时直接判定不健康
    if (service.heartbeatAgeSeconds() > timeout_seconds) {
        return HealthStatus::UNHEALTHY;
    }

    // 根据评分判定
    if (service.health_score >= 0.7) return HealthStatus::HEALTHY;
    if (service.health_score >= 0.3) return HealthStatus::DEGRADED;
    return HealthStatus::UNHEALTHY;
}

} // namespace service_registry
} // namespace core_services
