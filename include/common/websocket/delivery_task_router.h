// include/common/websocket/delivery_task_router.h
#pragma once

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include "websocket_message.h"

namespace common {

// 前向声明
namespace database {
    class MySQLPool;
}

namespace network {
    class EventLoop;
}

namespace websocket {

/**
 * @brief 发货任务路由器
 *
 * 功能:
 * 1. 接收Payment Service的发货请求
 * 2. 路由到对应商户的WebSocket连接
 * 3. 超时重试机制(30秒超时, 最多重试3次)
 * 4. 结果回调通知
 */
class DeliveryTaskRouter {
public:
    /**
     * @brief 发货结果回调函数
     * @param order_id 订单ID
     * @param success 是否成功
     * @param error_msg 错误信息(成功时为空)
     */
    using DeliveryCallback = std::function<void(
        const std::string& order_id,
        bool success,
        const std::string& error_msg
    )>;

    explicit DeliveryTaskRouter(
        std::shared_ptr<common::database::MySQLPool> db_pool,
        std::shared_ptr<common::network::EventLoop> event_loop
    );
    ~DeliveryTaskRouter();

    // ==================== 任务分发 ====================

    /**
     * @brief 分发发货任务
     * @param order_id 订单ID
     * @param merchant_code 商户编码
     * @param game_id 游戏ID
     * @param server_id 分区ID
     * @param player_account 玩家账号
     * @param reward_items 奖励道具(JSON字符串)
     * @param callback 结果回调
     * @return 任务ID(用于追踪)
     */
    std::string dispatchDeliveryTask(
        const std::string& order_id,
        const std::string& merchant_code,
        int64_t game_id,
        int64_t server_id,
        const std::string& player_account,
        const std::string& reward_items,
        DeliveryCallback callback
    );

    /**
     * @brief 处理客户端返回的发货结果
     * @param task_id 任务ID
     * @param result_json 结果JSON(包含status, affected_rows等)
     */
    void handleDeliveryResult(const std::string& task_id, const std::string& result_json);

    // ==================== 重试机制 ====================

    /**
     * @brief 启动超时检测线程
     * @note 使用EventLoop的时间轮，不需要独立线程
     */
    void startTimeoutChecker();

    /**
     * @brief 停止超时检测
     */
    void stopTimeoutChecker();

    // ==================== 统计查询 ====================

    /**
     * @brief 获取待处理任务数量
     */
    size_t getPendingTaskCount();

private:
    /**
     * @brief 发货任务结构
     */
    struct DeliveryTask {
        std::string task_id;            // 任务ID
        std::string order_id;           // 订单ID
        std::string merchant_code;      // 商户编码
        int64_t game_id;                // 游戏ID
        int64_t server_id;              // 分区ID
        std::string player_account;     // 玩家账号
        std::string reward_items;       // 奖励道具JSON
        DeliveryCallback callback;      // 回调函数
        
        std::chrono::system_clock::time_point created_at;  // 创建时间
        std::chrono::system_clock::time_point sent_at;     // 发送时间
        int retry_count;                // 重试次数
    };

    /**
     * @brief 检查超时任务并重试
     */
    void checkTimeout();

    /**
     * @brief 生成唯一任务ID
     */
    std::string generateTaskId();

    std::shared_ptr<common::database::MySQLPool> db_pool_;
    std::shared_ptr<common::network::EventLoop> event_loop_;  // EventLoop实例(用于定时器)

    std::unordered_map<std::string, DeliveryTask> pending_tasks_; // task_id -> Task
    std::mutex mutex_;

    uint64_t timeout_timer_id_{0};                      // 超时检测定时器ID
    std::atomic<bool> running_{false};
};

} // namespace websocket
} // namespace common
