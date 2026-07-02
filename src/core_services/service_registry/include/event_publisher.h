/**
 * @file event_publisher.h
 * @brief 事件发布器接口
 * @details 基于 Redis Pub/Sub 的服务变更事件通知
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <memory>
#include <string>
#include <functional>

namespace core_services {
namespace service_registry {

/**
 * @brief 事件发布器
 * @details 发布服务变更事件到 Redis Pub/Sub
 */
class EventPublisher {
public:
    /**
     * @brief 事件回调类型
     */
    using EventCallback = std::function<void(const ServiceEvent&)>;

    /**
     * @brief 构造函数
     * @param redis_pool Redis 连接池
     */
    explicit EventPublisher(std::shared_ptr<common::database::RedisPool> redis_pool);

    /**
     * @brief 析构函数
     */
    ~EventPublisher() = default;

    // ==================== 事件发布 ====================

    /**
     * @brief 发布服务事件
     * @param type 事件类型
     * @param service 服务信息
     */
    void publish(ServiceEventType type, const ServiceInfo& service);

    /**
     * @brief 发布自定义事件
     * @param channel 频道名称
     * @param event 事件对象
     */
    void publishToChannel(const std::string& channel, const ServiceEvent& event);

    // ==================== 事件订阅 ====================

    /**
     * @brief 订阅服务事件
     * @param service_name 服务名称（空字符串表示订阅所有）
     * @param callback 事件回调
     */
    void subscribe(const std::string& service_name, EventCallback callback);

    /**
     * @brief 取消订阅
     * @param service_name 服务名称
     */
    void unsubscribe(const std::string& service_name);

    // ==================== 统计 ====================

    /**
     * @brief 获取已发布事件计数
     * @return 事件计数
     */
    int64_t getPublishedCount() const { return published_count_; }

private:
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    int64_t published_count_ = 0;

    // 获取 Redis 连接
    std::shared_ptr<common::database::RedisConnection> getConnection();

    // 构建事件频道名称
    std::string buildChannelName(const std::string& service_name);
};

} // namespace service_registry
} // namespace core_services
