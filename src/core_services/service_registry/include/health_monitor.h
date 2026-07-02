/**
 * @file health_monitor.h
 * @brief 健康监控器接口
 * @details 服务健康状态评估和异常检测
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "registry_config.h"
#include "common/logger/logger.h"
#include <memory>
#include <vector>
#include <functional>

namespace core_services {
namespace service_registry {

/**
 * @brief 健康监控器
 * @details 评估服务健康状态，计算健康评分
 */
class HealthMonitor {
public:
    /**
     * @brief 构造函数
     * @param config 注册中心配置
     */
    explicit HealthMonitor(const RegistryConfig& config);

    /**
     * @brief 析构函数
     */
    ~HealthMonitor() = default;

    // ==================== 健康评估 ====================

    /**
     * @brief 计算服务健康评分
     * @param service 服务信息
     * @return 健康评分 (0.0-1.0)
     */
    double calculateHealthScore(const ServiceInfo& service);

    /**
     * @brief 判定健康状态
     * @param service 服务信息
     * @return 健康状态枚举
     */
    HealthStatus determineStatus(const ServiceInfo& service);

    /**
     * @brief 检查服务是否过期
     * @param service 服务信息
     * @return 是否过期
     */
    bool isExpired(const ServiceInfo& service);

    // ==================== 批量评估 ====================

    /**
     * @brief 更新多个服务的健康状态
     * @param services 服务列表（会被修改）
     * @return 状态变化的服务数量
     */
    int updateHealthStatuses(std::vector<ServiceInfo>& services);

    /**
     * @brief 获取不健康的服务列表
     * @param services 服务列表
     * @return 不健康的服务列表
     */
    std::vector<ServiceInfo> getUnhealthyServices(const std::vector<ServiceInfo>& services);

    /**
     * @brief 获取需要清理的服务列表
     * @param services 服务列表
     * @return 需要清理的服务列表
     */
    std::vector<ServiceInfo> getServicesToCleanup(const std::vector<ServiceInfo>& services);

    // ==================== 配置 ====================

    /**
     * @brief 设置心跳超时阈值
     * @param seconds 秒数
     */
    void setHeartbeatTimeout(int seconds) {
        heartbeat_timeout_s_ = seconds;
    }

    /**
     * @brief 设置心跳警告阈值
     * @param seconds 秒数
     */
    void setHeartbeatWarningThreshold(int seconds) {
        heartbeat_warning_threshold_s_ = seconds;
    }

    /**
     * @brief 设置响应时间阈值
     * @param ms 毫秒数
     */
    void setResponseTimeThreshold(int ms) {
        response_time_threshold_ms_ = ms;
    }

private:
    int heartbeat_timeout_s_;
    int heartbeat_warning_threshold_s_;
    int response_time_threshold_ms_;
};

} // namespace service_registry
} // namespace core_services
