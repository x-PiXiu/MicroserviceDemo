/**
 * @file event_publisher.cpp
 * @brief 事件发布器实现
 * @details 实现基于 Redis 列表的服务变更事件存储
 * @note 当前实现为简化版本，使用 Redis 列表存储事件，后续可扩展为真正的 Pub/Sub
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#include "event_publisher.h"

namespace core_services {
namespace service_registry {

// 全局事件列表 Key
static const std::string GLOBAL_EVENT_LIST = "service:events:list";

EventPublisher::EventPublisher(std::shared_ptr<common::database::RedisPool> redis_pool)
    : redis_pool_(redis_pool) {
    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }
}

// ==================== 事件发布 ====================

void EventPublisher::publish(ServiceEventType type, const ServiceInfo& service) {
    ServiceEvent event;
    event.type = type;
    event.service_name = service.service_name;
    event.instance_id = service.instanceId();
    event.timestamp = std::chrono::system_clock::now();
    event.payload = service.toJson();

    // 发布到全局事件列表
    publishToChannel(GLOBAL_EVENT_LIST, event);

    published_count_++;
}

void EventPublisher::publishToChannel(const std::string& channel, const ServiceEvent& event) {
    try {
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::RedisConnectionGuard guard(*redis_pool_);
        auto conn = guard.get();
        if (!conn) {
            LOG_ERROR("Failed to get Redis connection for event publish");
            return;
        }

        // 使用 Redis 列表存储事件 (lpush)
        std::string message = event.toJson().dump();
        conn->lpush(channel, message);

        LOG_DEBUG("Published event to list: " + channel);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to publish event: " + std::string(e.what()));
    }
}

// ==================== 事件订阅 ====================

void EventPublisher::subscribe(const std::string& service_name, EventCallback callback) {
    // 当前实现不支持实时订阅，仅记录日志
    // 后续可以实现轮询机制或使用专门的 Redis Pub/Sub 连接
    LOG_WARNING("Event subscription is not supported in current implementation for: " + service_name);
}

void EventPublisher::unsubscribe(const std::string& service_name) {
    // 当前实现不支持取消订阅
    LOG_WARNING("Event unsubscription is not supported in current implementation for: " + service_name);
}

// ==================== 私有方法 ====================

// 🔧 已移除 getConnection() 方法，所有调用改为使用 RedisConnectionGuard
// std::shared_ptr<common::database::RedisConnection> EventPublisher::getConnection() {
//     if (!redis_pool_) {
//         return nullptr;
//     }
//     return redis_pool_->getConnection();
// }

std::string EventPublisher::buildChannelName(const std::string& service_name) {
    if (service_name.empty()) {
        return GLOBAL_EVENT_LIST;
    }
    return "service:events:" + service_name + ":list";
}

} // namespace service_registry
} // namespace core_services
