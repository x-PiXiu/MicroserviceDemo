/**
 * @file health_monitor.cpp
 * @brief 健康监控器实现
 * @details 实现服务健康状态评估和异常检测
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "health_monitor.h"
#include <cmath>
#include <algorithm>

namespace core_services {
namespace service_registry {

HealthMonitor::HealthMonitor(const RegistryConfig& config)
    : heartbeat_timeout_s_(config.heartbeat_timeout_s)
    , heartbeat_warning_threshold_s_(config.heartbeat_warning_threshold_s)
    , response_time_threshold_ms_(config.response_time_threshold_ms) {
}

// ==================== 健康评估 ====================

double HealthMonitor::calculateHealthScore(const ServiceInfo& service) {
    double score = 1.0;

    // 1. 心跳延迟因子 (0.0 - 1.0)
    int64_t heartbeat_age = service.heartbeatAgeSeconds();

    if (heartbeat_age > heartbeat_warning_threshold_s_) {
        double delay_range = heartbeat_timeout_s_ - heartbeat_warning_threshold_s_;
        if (delay_range > 0) {
            double delay_excess = heartbeat_age - heartbeat_warning_threshold_s_;
            double delay_factor = 1.0 - std::min(1.0, delay_excess / delay_range);
            score *= delay_factor;
        }
    }

    // 2. 失败率因子
    if (service.total_requests > 0) {
        double failure_rate = static_cast<double>(service.failed_requests) /
                             service.total_requests;
        double failure_factor = 1.0 - failure_rate;
        score *= failure_factor;
    }

    // 3. 响应时间因子
    if (service.avg_response_time_ms > response_time_threshold_ms_) {
        double latency_factor = static_cast<double>(response_time_threshold_ms_) /
                               service.avg_response_time_ms;
        score *= latency_factor;
    }

    // 4. 连续失败惩罚
    if (service.consecutive_failures > 0) {
        double penalty = std::pow(0.8, service.consecutive_failures);
        score *= penalty;
    }

    return std::max(0.0, std::min(1.0, score));
}

HealthStatus HealthMonitor::determineStatus(const ServiceInfo& service) {
    // 心跳超时直接判定不健康
    if (isExpired(service)) {
        return HealthStatus::UNHEALTHY;
    }

    // 根据评分判定
    double score = calculateHealthScore(service);
    if (score >= 0.7) return HealthStatus::HEALTHY;
    if (score >= 0.3) return HealthStatus::DEGRADED;
    return HealthStatus::UNHEALTHY;
}

bool HealthMonitor::isExpired(const ServiceInfo& service) {
    return service.heartbeatAgeSeconds() > heartbeat_timeout_s_;
}

// ==================== 批量评估 ====================

int HealthMonitor::updateHealthStatuses(std::vector<ServiceInfo>& services) {
    int changed = 0;

    for (auto& service : services) {
        bool was_healthy = service.healthy;
        double old_score = service.health_score;

        // 计算新评分
        service.health_score = calculateHealthScore(service);

        // 判定健康状态
        HealthStatus status = determineStatus(service);
        service.healthy = (status == HealthStatus::HEALTHY || status == HealthStatus::DEGRADED);

        // 检查是否变化
        if (was_healthy != service.healthy ||
            std::abs(old_score - service.health_score) > 0.1) {
            changed++;
        }
    }

    return changed;
}

std::vector<ServiceInfo> HealthMonitor::getUnhealthyServices(const std::vector<ServiceInfo>& services) {
    std::vector<ServiceInfo> unhealthy;

    for (const auto& service : services) {
        HealthStatus status = determineStatus(service);
        if (status == HealthStatus::UNHEALTHY || status == HealthStatus::DEGRADED) {
            unhealthy.push_back(service);
        }
    }

    return unhealthy;
}

std::vector<ServiceInfo> HealthMonitor::getServicesToCleanup(const std::vector<ServiceInfo>& services) {
    std::vector<ServiceInfo> to_cleanup;

    for (const auto& service : services) {
        if (isExpired(service)) {
            to_cleanup.push_back(service);
        }
    }

    return to_cleanup;
}

} // namespace service_registry
} // namespace core_services
