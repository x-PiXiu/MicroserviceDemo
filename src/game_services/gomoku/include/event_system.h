/**
 * @file event_system.h
 * @brief 简化的事件系统 - 移除复杂性，提升性能
 * @details 
 * - 移除事件优先级机制，使用简单FIFO队列
 * - 合并到统一线程池，无独立事件线程
 * - 明确事件分工：本地事件→本地处理，跨服务事件→直接Kafka
 * - 移除智能路由，简化事件流
 * @author AI Assistant
 * @date 2025-09-28
 * @version 2.0.0
 */

#pragma once

#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <atomic>
#include <chrono>
#include <random>

namespace game_services {
namespace gomoku {

/**
 * @brief 简化的事件类型定义
 */
namespace EventTypes {
    // 本地事件（仅在当前服务内处理）
    namespace Local {
        const std::string GAME_MOVE_MADE = "local.game.move.made";
        const std::string GAME_STATE_CHANGED = "local.game.state.changed";
        const std::string PLAYER_CONNECTED = "local.player.connected";
        const std::string PLAYER_DISCONNECTED = "local.player.disconnected";
        const std::string ROOM_STATE_CHANGED = "local.room.state.changed";
    }
    
    // 跨服务事件（直接发送到Kafka）
    namespace CrossService {
        const std::string USER_RATING_UPDATED = "cross.user.rating.updated";
        const std::string GAME_ENDED = "cross.game.ended";
        const std::string ACHIEVEMENT_UNLOCKED = "cross.achievement.unlocked";
        const std::string SERVICE_HEALTH_CHANGED = "cross.service.health.changed";
    }
}

/**
 * @brief 简化的游戏事件 - 移除优先级
 */
struct SimpleGameEvent {
    std::string id;                                           // 事件唯一ID
    std::string type;                                         // 事件类型
    std::string source_service;                               // 源服务名
    nlohmann::json data;                                      // 事件数据
    std::chrono::system_clock::time_point timestamp;          // 时间戳

    /**
     * @brief 构造函数
     */
    SimpleGameEvent(const std::string& event_type,
                   const std::string& source,
                   const nlohmann::json& event_data)
        : type(event_type), source_service(source), data(event_data),
          timestamp(std::chrono::system_clock::now()) {
        // P1 修复: 使用UUID格式生成唯一事件ID (避免时间戳重复)
        id = generateEventId(source);
    }

    /**
     * @brief 生成唯一事件ID (UUID v4 格式)
     * @param source 源服务名
     * @return 唯一事件ID
     */
    static std::string generateEventId(const std::string& source) {
        auto now = std::chrono::system_clock::now();
        auto time_since_epoch = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();

        // 添加随机数避免同一毫秒内的重复
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 999999);

        // 格式: source_timestamp_random (类似UUID但更简洁)
        return source + "_" + std::to_string(millis) + "_" + std::to_string(dis(gen));
    }
    
    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"type", type},
            {"source_service", source_service},
            {"data", data},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count()}
        };
    }
    
    /**
     * @brief 从JSON构造
     */
    static SimpleGameEvent fromJson(const nlohmann::json& json) {
        std::string type = json.value("type", "");
        std::string source_service = json.value("source_service", "");
        nlohmann::json data = json.value("data", nlohmann::json{});
        
        SimpleGameEvent event(type, source_service, data);
        
        if (json.contains("id")) {
            event.id = json.value("id", "");
        }
        if (json.contains("timestamp")) {
            auto timestamp_ms = json["timestamp"].get<int64_t>();
            event.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp_ms));
        }
        return event;
    }
    
    /**
     * @brief 判断是否为本地事件
     */
    bool isLocalEvent() const {
        return type.find("local.") == 0;
    }
    
    /**
     * @brief 判断是否为跨服务事件
     */
    bool isCrossServiceEvent() const {
        return type.find("cross.") == 0;
    }
};

/**
 * @brief 简化的事件处理器接口
 */
class SimpleEventHandler {
public:
    virtual ~SimpleEventHandler() = default;
    
    /**
     * @brief 处理事件
     * @param event 事件对象
     * @return 处理是否成功
     */
    virtual bool handleEvent(const SimpleGameEvent& event) = 0;
    
    /**
     * @brief 获取处理器名称
     */
    virtual std::string getName() const = 0;
};

/**
 * @brief 简化的本地事件总线 - 使用统一线程池
 */
class SimpleLocalEventBus {
public:
    /**
     * @brief 构造函数
     * @param thread_pool 统一线程池引用
     */
    explicit SimpleLocalEventBus(std::shared_ptr<common::thread_pool::ThreadPool> thread_pool);
    
    /**
     * @brief 析构函数
     */
    ~SimpleLocalEventBus();
    
    /**
     * @brief 发布本地事件（异步处理）
     * @param event 事件对象
     */
    void publishEvent(const SimpleGameEvent& event);
    
    /**
     * @brief 发布本地事件（同步处理）
     * @param event 事件对象
     * @return 处理结果
     */
    bool publishEventSync(const SimpleGameEvent& event);
    
    /**
     * @brief 订阅事件
     * @param event_type 事件类型
     * @param handler 事件处理器
     */
    void subscribe(const std::string& event_type, std::shared_ptr<SimpleEventHandler> handler);
    
    /**
     * @brief 取消订阅
     * @param event_type 事件类型
     * @param handler_name 处理器名称
     */
    void unsubscribe(const std::string& event_type, const std::string& handler_name);
    
    /**
     * @brief 获取统计信息
     */
    nlohmann::json getStatistics() const;

private:
    // 统一线程池（共享）
    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;
    
    // 事件处理器映射
    std::unordered_map<std::string, std::vector<std::shared_ptr<SimpleEventHandler>>> handlers_;
    mutable std::shared_mutex handlers_mutex_;
    
    // 统计信息
    std::atomic<uint64_t> total_events_published_{0};
    std::atomic<uint64_t> total_events_processed_{0};
    std::atomic<uint64_t> total_events_failed_{0};
    
    /**
     * @brief 处理单个事件
     * @param event 事件对象
     */
    void processEvent(const SimpleGameEvent& event);
};

/**
 * @brief Kafka跨服务事件发布器 - 简化版本
 */
class SimpleCrossServiceEventPublisher {
public:
    /**
     * @brief 构造函数
     * @param kafka_brokers Kafka broker地址
     * @param topic_prefix 主题前缀
     */
    SimpleCrossServiceEventPublisher(const std::string& kafka_brokers, 
                                   const std::string& topic_prefix);
    
    /**
     * @brief 析构函数
     */
    ~SimpleCrossServiceEventPublisher();
    
    /**
     * @brief 初始化
     */
    bool initialize();
    
    /**
     * @brief 关闭
     */
    void shutdown();
    
    /**
     * @brief 发布跨服务事件
     * @param event 事件对象
     * @return 发布是否成功
     */
    bool publishEvent(const SimpleGameEvent& event);
    
    /**
     * @brief 获取统计信息
     */
    nlohmann::json getStatistics() const;

private:
    std::string kafka_brokers_;
    std::string topic_prefix_;
    std::atomic<bool> initialized_{false};
    
    // 统计信息
    std::atomic<uint64_t> total_published_{0};
    std::atomic<uint64_t> publish_failures_{0};
    
    /**
     * @brief 构建主题名称
     */
    std::string buildTopicName(const std::string& event_type) const;
};

/**
 * @brief 简化的事件管理器 - 明确分工，无智能路由
 */
class SimplifiedEventManager {
public:
    /**
     * @brief 构造函数
     * @param thread_pool 统一线程池
     * @param enable_kafka 是否启用Kafka
     * @param kafka_brokers Kafka地址
     * @param topic_prefix 主题前缀
     */
    SimplifiedEventManager(std::shared_ptr<common::thread_pool::ThreadPool> thread_pool,
                          bool enable_kafka = false,
                          const std::string& kafka_brokers = "localhost:9092",
                          const std::string& topic_prefix = "gomoku_events");
    
    /**
     * @brief 析构函数
     */
    ~SimplifiedEventManager();
    
    /**
     * @brief 初始化
     */
    bool initialize();
    
    /**
     * @brief 关闭
     */
    void shutdown();
    
    /**
     * @brief 发布本地事件 - 明确指定本地处理
     * @param event_type 事件类型（必须以"local."开头）
     * @param data 事件数据
     */
    void publishLocalEvent(const std::string& event_type, const nlohmann::json& data);
    
    /**
     * @brief 发布跨服务事件 - 明确指定Kafka处理  
     * @param event_type 事件类型（必须以"cross."开头）
     * @param data 事件数据
     * @return 发布是否成功
     */
    bool publishCrossServiceEvent(const std::string& event_type, const nlohmann::json& data);
    
    /**
     * @brief 订阅本地事件
     * @param event_type 事件类型
     * @param handler 事件处理器
     */
    void subscribeLocal(const std::string& event_type, std::shared_ptr<SimpleEventHandler> handler);
    
    /**
     * @brief 获取统计信息
     */
    nlohmann::json getStatistics() const;

private:
    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;
    bool enable_kafka_;
    std::string service_name_;
    
    // 本地事件总线
    std::unique_ptr<SimpleLocalEventBus> local_event_bus_;
    
    // 跨服务事件发布器（可选）
    std::unique_ptr<SimpleCrossServiceEventPublisher> cross_service_publisher_;
    
    std::atomic<bool> initialized_{false};
    
    /**
     * @brief 验证事件类型
     */
    bool validateEventType(const std::string& event_type, bool is_local) const;
};

/**
 * @brief 简化的事件管理器单例
 */
class SimplifiedEventManagerInstance {
public:
    static SimplifiedEventManager& getInstance();
    static void initialize(std::shared_ptr<common::thread_pool::ThreadPool> thread_pool,
                          bool enable_kafka = false,
                          const std::string& kafka_brokers = "localhost:9092",
                          const std::string& topic_prefix = "gomoku_events");
    static void shutdown();

private:
    static std::unique_ptr<SimplifiedEventManager> instance_;
    static std::mutex instance_mutex_;
    static bool initialized_;
};

} // namespace gomoku  
} // namespace game_services

