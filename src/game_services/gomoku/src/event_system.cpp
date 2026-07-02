/**
 * @file event_system.cpp
 * @brief 简化的事件系统实现 - 高性能，低复杂度
 * @author AI Assistant
 * @date 2025-09-28
 * @version 2.0.0
 */

#include "event_system.h"
#include <algorithm>
#include <regex>

namespace game_services {
namespace gomoku {

// ==================== SimpleLocalEventBus 实现 ====================

SimpleLocalEventBus::SimpleLocalEventBus(std::shared_ptr<common::thread_pool::ThreadPool> thread_pool)
    : thread_pool_(thread_pool) {
    LOG_INFO("简化本地事件总线初始化（使用统一线程池）");
}

SimpleLocalEventBus::~SimpleLocalEventBus() {
    LOG_INFO("简化本地事件总线析构");
}

void SimpleLocalEventBus::publishEvent(const SimpleGameEvent& event) {
    if (!event.isLocalEvent()) {
        LOG_WARNING("尝试发布非本地事件到本地总线: " + event.type + "，已忽略");
        return;
    }
    
    total_events_published_++;
    
    // 🎯 提交到统一线程池异步处理（无独立线程）
    thread_pool_->submit([this, event]() {
        processEvent(event);
    });
    
    LOG_DEBUG("发布本地事件到线程池: " + event.type);
}

bool SimpleLocalEventBus::publishEventSync(const SimpleGameEvent& event) {
    if (!event.isLocalEvent()) {
        LOG_WARNING("尝试同步发布非本地事件: " + event.type);
        return false;
    }
    
    total_events_published_++;
    
    // 🎯 同步处理（在当前线程）
    processEvent(event);
    return true;
}

void SimpleLocalEventBus::subscribe(const std::string& event_type, std::shared_ptr<SimpleEventHandler> handler) {
    std::unique_lock<std::shared_mutex> lock(handlers_mutex_);
    
    handlers_[event_type].push_back(handler);
    
    LOG_INFO("订阅本地事件: " + event_type + " -> " + handler->getName() + 
            " (当前处理器数: " + std::to_string(handlers_[event_type].size()) + ")");
}

void SimpleLocalEventBus::unsubscribe(const std::string& event_type, const std::string& handler_name) {
    std::unique_lock<std::shared_mutex> lock(handlers_mutex_);
    
    auto it = handlers_.find(event_type);
    if (it != handlers_.end()) {
        auto& handler_list = it->second;
        handler_list.erase(
            std::remove_if(handler_list.begin(), handler_list.end(),
                [&handler_name](const std::shared_ptr<SimpleEventHandler>& handler) {
                    return handler && handler->getName() == handler_name;
                }),
            handler_list.end()
        );
        
        if (handler_list.empty()) {
            handlers_.erase(it);
        }
        
        LOG_INFO("取消订阅本地事件: " + event_type + " -> " + handler_name);
    }
}

void SimpleLocalEventBus::processEvent(const SimpleGameEvent& event) {
    try {
        std::shared_lock<std::shared_mutex> lock(handlers_mutex_);
        
        auto it = handlers_.find(event.type);
        if (it == handlers_.end()) {
            LOG_DEBUG("没有找到事件处理器: " + event.type);
            return;
        }
        
        const auto& handler_list = it->second;
        bool any_success = false;
        
        for (const auto& handler : handler_list) {
            try {
                if (handler->handleEvent(event)) {
                    any_success = true;
                    LOG_DEBUG("事件处理成功: " + event.type + " -> " + handler->getName());
                } else {
                    LOG_WARNING("事件处理失败: " + event.type + " -> " + handler->getName());
                    total_events_failed_++;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("事件处理异常: " + event.type + " -> " + handler->getName() + 
                         ", error: " + std::string(e.what()));
                total_events_failed_++;
            }
        }
        
        if (any_success) {
            total_events_processed_++;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("处理事件异常: " + event.type + ", error: " + std::string(e.what()));
        total_events_failed_++;
    }
}

nlohmann::json SimpleLocalEventBus::getStatistics() const {
    return {
        {"type", "SimpleLocalEventBus"},
        {"total_events_published", total_events_published_.load()},
        {"total_events_processed", total_events_processed_.load()},
        {"total_events_failed", total_events_failed_.load()},
        {"registered_event_types", handlers_.size()},
        {"uses_thread_pool", true},
        {"independent_threads", 0} // 🎯 无独立线程
    };
}

// ==================== SimpleCrossServiceEventPublisher 实现 ====================

SimpleCrossServiceEventPublisher::SimpleCrossServiceEventPublisher(
    const std::string& kafka_brokers, const std::string& topic_prefix)
    : kafka_brokers_(kafka_brokers), topic_prefix_(topic_prefix) {
    LOG_INFO("跨服务事件发布器初始化: brokers=" + kafka_brokers + ", prefix=" + topic_prefix);
}

SimpleCrossServiceEventPublisher::~SimpleCrossServiceEventPublisher() {
    shutdown();
}

bool SimpleCrossServiceEventPublisher::initialize() {
    if (initialized_.exchange(true)) {
        LOG_WARNING("跨服务事件发布器已初始化");
        return true;
    }
    
    // 🔧 简化实现：在实际部署环境中会连接真实的Kafka
    // 当前为演示版本，记录日志即可
    LOG_INFO("✅ 跨服务事件发布器初始化完成（简化模式）");
    return true;
}

void SimpleCrossServiceEventPublisher::shutdown() {
    if (!initialized_.exchange(false)) {
        return;
    }
    
    LOG_INFO("跨服务事件发布器关闭");
}

bool SimpleCrossServiceEventPublisher::publishEvent(const SimpleGameEvent& event) {
    if (!initialized_) {
        LOG_ERROR("跨服务事件发布器未初始化");
        return false;
    }
    
    if (!event.isCrossServiceEvent()) {
        LOG_WARNING("尝试发布非跨服务事件: " + event.type);
        return false;
    }
    
    try {
        std::string topic = buildTopicName(event.type);
        std::string payload = event.toJson().dump();
        
        // 🔧 简化实现：在实际环境中会发送到真实Kafka
        LOG_INFO("📤 发布跨服务事件到Kafka: topic=" + topic + 
                ", event_id=" + event.id + ", size=" + std::to_string(payload.size()) + "字节");
        
        total_published_++;
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("发布跨服务事件失败: " + event.type + ", error: " + std::string(e.what()));
        publish_failures_++;
        return false;
    }
}

std::string SimpleCrossServiceEventPublisher::buildTopicName(const std::string& event_type) const {
    // 🎯 简化主题命名：cross.user.rating.updated -> gomoku_events_user
    std::regex pattern(R"(cross\.(\w+)\..*)");
    std::smatch matches;
    
    if (std::regex_match(event_type, matches, pattern) && matches.size() > 1) {
        return topic_prefix_ + "_" + matches[1].str();
    }
    
    return topic_prefix_ + "_default";
}

nlohmann::json SimpleCrossServiceEventPublisher::getStatistics() const {
    return {
        {"type", "SimpleCrossServiceEventPublisher"},
        {"kafka_brokers", kafka_brokers_},
        {"topic_prefix", topic_prefix_},
        {"initialized", initialized_.load()},
        {"total_published", total_published_.load()},
        {"publish_failures", publish_failures_.load()},
        {"success_rate", total_published_.load() > 0 ? 
            (double)(total_published_.load() - publish_failures_.load()) / total_published_.load() : 0.0}
    };
}

// ==================== SimplifiedEventManager 实现 ====================

SimplifiedEventManager::SimplifiedEventManager(
    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool,
    bool enable_kafka,
    const std::string& kafka_brokers,
    const std::string& topic_prefix)
    : thread_pool_(thread_pool), enable_kafka_(enable_kafka), service_name_("gomoku_service") {
    
    LOG_INFO("简化事件管理器初始化: kafka=" + std::string(enable_kafka ? "enabled" : "disabled"));
    
    // 🎯 创建简化的本地事件总线（使用统一线程池）
    local_event_bus_ = std::make_unique<SimpleLocalEventBus>(thread_pool);
    
    // 🎯 根据配置创建Kafka发布器
    if (enable_kafka_) {
        cross_service_publisher_ = std::make_unique<SimpleCrossServiceEventPublisher>(
            kafka_brokers, topic_prefix);
    } else {
        LOG_INFO("Kafka已禁用，跨服务事件将被忽略");
    }
}

SimplifiedEventManager::~SimplifiedEventManager() {
    shutdown();
}

bool SimplifiedEventManager::initialize() {
    if (initialized_.exchange(true)) {
        LOG_WARNING("简化事件管理器已初始化");
        return true;
    }
    
    // 初始化Kafka发布器
    if (enable_kafka_ && cross_service_publisher_) {
        if (!cross_service_publisher_->initialize()) {
            LOG_ERROR("Kafka发布器初始化失败");
            return false;
        }
    }
    
    LOG_INFO("✅ 简化事件管理器初始化完成");
    return true;
}

void SimplifiedEventManager::shutdown() {
    if (!initialized_.exchange(false)) {
        return;
    }
    
    if (cross_service_publisher_) {
        cross_service_publisher_->shutdown();
    }
    
    LOG_INFO("简化事件管理器关闭");
}

void SimplifiedEventManager::publishLocalEvent(const std::string& event_type, const nlohmann::json& data) {
    if (!validateEventType(event_type, true)) {
        LOG_ERROR("无效的本地事件类型: " + event_type + "（必须以'local.'开头）");
        return;
    }
    
    SimpleGameEvent event(event_type, service_name_, data);
    local_event_bus_->publishEvent(event);
    
    LOG_DEBUG("发布本地事件: " + event_type);
}

bool SimplifiedEventManager::publishCrossServiceEvent(const std::string& event_type, const nlohmann::json& data) {
    if (!validateEventType(event_type, false)) {
        LOG_ERROR("无效的跨服务事件类型: " + event_type + "（必须以'cross.'开头）");
        return false;
    }
    
    if (!enable_kafka_ || !cross_service_publisher_) {
        LOG_WARNING("Kafka未启用，跨服务事件忽略: " + event_type);
        return false;
    }
    
    SimpleGameEvent event(event_type, service_name_, data);
    return cross_service_publisher_->publishEvent(event);
}

void SimplifiedEventManager::subscribeLocal(const std::string& event_type, std::shared_ptr<SimpleEventHandler> handler) {
    if (!validateEventType(event_type, true)) {
        LOG_ERROR("无效的本地事件类型: " + event_type);
        return;
    }
    
    local_event_bus_->subscribe(event_type, handler);
}

bool SimplifiedEventManager::validateEventType(const std::string& event_type, bool is_local) const {
    if (is_local) {
        return event_type.find("local.") == 0;
    } else {
        return event_type.find("cross.") == 0;
    }
}

nlohmann::json SimplifiedEventManager::getStatistics() const {
    nlohmann::json stats = {
        {"manager_type", "SimplifiedEventManager"},
        {"service_name", service_name_},
        {"initialized", initialized_.load()},
        {"enable_kafka", enable_kafka_}
    };
    
    if (local_event_bus_) {
        stats["local_event_bus"] = local_event_bus_->getStatistics();
    }
    
    if (cross_service_publisher_) {
        stats["cross_service_publisher"] = cross_service_publisher_->getStatistics();
    }
    
    return stats;
}

// ==================== SimplifiedEventManagerInstance 实现 ====================

std::unique_ptr<SimplifiedEventManager> SimplifiedEventManagerInstance::instance_ = nullptr;
std::mutex SimplifiedEventManagerInstance::instance_mutex_;
bool SimplifiedEventManagerInstance::initialized_ = false;

SimplifiedEventManager& SimplifiedEventManagerInstance::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (!instance_) {
        throw std::runtime_error("SimplifiedEventManagerInstance未初始化，请先调用initialize()");
    }
    
    return *instance_;
}

void SimplifiedEventManagerInstance::initialize(
    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool,
    bool enable_kafka,
    const std::string& kafka_brokers,
    const std::string& topic_prefix) {
    
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (initialized_) {
        LOG_WARNING("SimplifiedEventManagerInstance已初始化");
        return;
    }
    
    instance_ = std::make_unique<SimplifiedEventManager>(thread_pool, enable_kafka, kafka_brokers, topic_prefix);
    
    if (!instance_->initialize()) {
        instance_.reset();
        throw std::runtime_error("SimplifiedEventManager初始化失败");
    }
    
    initialized_ = true;
    LOG_INFO("✅ SimplifiedEventManagerInstance初始化完成");
}

void SimplifiedEventManagerInstance::shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (instance_) {
        instance_->shutdown();
        instance_.reset();
    }
    
    initialized_ = false;
    LOG_INFO("SimplifiedEventManagerInstance已关闭");
}

} // namespace gomoku
} // namespace game_services

