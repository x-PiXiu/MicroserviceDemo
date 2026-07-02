// src/common/websocket/delivery_task_router.cpp
#include "common/websocket/delivery_task_router.h"
#include "common/websocket/merchant_connection_manager.h"
#include "common/logger/logger.h"
#include "common/network/event_loop.h"
#include <sstream>
#include <iomanip>
#include <random>

namespace common {
namespace websocket {

DeliveryTaskRouter::DeliveryTaskRouter(
    std::shared_ptr<common::database::MySQLPool> db_pool,
    std::shared_ptr<common::network::EventLoop> event_loop
) : db_pool_(db_pool), event_loop_(event_loop) {
    LOG_INFO("发货任务路由器已创建");
}

DeliveryTaskRouter::~DeliveryTaskRouter() {
    stopTimeoutChecker();
    
    std::lock_guard<std::mutex> lock(mutex_);
    pending_tasks_.clear();
    
    LOG_INFO("发货任务路由器已销毁");
}

// ==================== 任务分发 ====================

std::string DeliveryTaskRouter::dispatchDeliveryTask(
    const std::string& order_id,
    const std::string& merchant_code,
    int64_t game_id,
    int64_t server_id,
    const std::string& player_account,
    const std::string& reward_items,
    DeliveryCallback callback
) {
    // 生成任务ID
    std::string task_id = generateTaskId();
    
    // 检查商户是否在线
    auto& conn_mgr = MerchantConnectionManager::getInstance();
    if (!conn_mgr.isOnline(merchant_code)) {
        LOG_WARNING("商户离线,任务将保存到队列: " + merchant_code);
        
        // 触发回调告知离线
        if (callback) {
            callback(order_id, false, "商户客户端离线");
        }
        
        return task_id;
    }
    
    // 创建任务
    DeliveryTask task;
    task.task_id = task_id;
    task.order_id = order_id;
    task.merchant_code = merchant_code;
    task.game_id = game_id;
    task.server_id = server_id;
    task.player_account = player_account;
    task.reward_items = reward_items;
    task.callback = callback;
    task.created_at = std::chrono::system_clock::now();
    task.sent_at = std::chrono::system_clock::now();
    task.retry_count = 0;
    
    // 保存任务到pending列表
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_tasks_[task_id] = task;
    }
    
    // 创建发货消息
    WebSocketMessage msg = WebSocketMessage::createDeliveryTask(
        task_id,
        order_id,
        game_id,
        server_id,
        player_account,
        reward_items
    );
    
    // 发送到商户
    bool sent = conn_mgr.sendToMerchant(merchant_code, msg);
    
    if (sent) {
        LOG_INFO("发货任务已发送: task_id=" + task_id + ", order_id=" + order_id + 
                 ", merchant=" + merchant_code);
    } else {
        LOG_ERROR("发货任务发送失败: task_id=" + task_id);
        
        // 从pending列表移除
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_tasks_.erase(task_id);
        }
        
        // 触发回调
        if (callback) {
            callback(order_id, false, "发送失败");
        }
    }
    
    return task_id;
}

void DeliveryTaskRouter::handleDeliveryResult(
    const std::string& task_id,
    const std::string& result_json
) {
    DeliveryTask task;
    bool found = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_tasks_.find(task_id);
        if (it != pending_tasks_.end()) {
            task = it->second;
            pending_tasks_.erase(it);
            found = true;
        }
    }
    
    if (!found) {
        LOG_WARNING("收到未知任务的结果: task_id=" + task_id);
        return;
    }
    
    // 解析结果
    try {
        json result = json::parse(result_json);
        
        std::string status = result.value("status", "failed");
        bool success = (status == "success");
        std::string error_msg = result.value("error_msg", "");
        int affected_rows = result.value("affected_rows", 0);
        
        // 触发回调
        if (task.callback) {
            task.callback(task.order_id, success, error_msg);
        }
        
        if (success) {
            LOG_INFO("发货成功: task_id=" + task_id + ", order_id=" + task.order_id + 
                     ", affected_rows=" + std::to_string(affected_rows));
        } else {
            LOG_ERROR("发货失败: task_id=" + task_id + ", order_id=" + task.order_id + 
                      ", error=" + error_msg);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("解析发货结果失败: " + std::string(e.what()) + ", json=" + result_json);
        
        // 触发回调
        if (task.callback) {
            task.callback(task.order_id, false, "结果解析失败");
        }
    }
}

// ==================== 重试机制 ====================

void DeliveryTaskRouter::startTimeoutChecker() {
    if (running_.exchange(true)) {
        LOG_WARNING("超时检测已在运行中");
        return;
    }
    
    if (!event_loop_) {
        LOG_ERROR("EventLoop为空,无法启动超时检测");
        running_.store(false);
        return;
    }
    
    // ⭐⭐⭐ 使用EventLoop的时间轮,每30秒检测一次
    event_loop_->queueInLoop([this]() {
        timeout_timer_id_ = event_loop_->runEvery(
            30 * 1000,  // 30秒
            [this]() {
                try {
                    checkTimeout();
                } catch (const std::exception& e) {
                    LOG_ERROR("超时检测异常: " + std::string(e.what()));
                }
            }
        );
        
        LOG_INFO("✅ 发货任务超时检测已启动(每30秒), timer_id=" + 
                 std::to_string(timeout_timer_id_));
    });
}

void DeliveryTaskRouter::stopTimeoutChecker() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (event_loop_ && timeout_timer_id_ > 0) {
        event_loop_->cancelTimer(timeout_timer_id_);
        timeout_timer_id_ = 0;
        LOG_INFO("发货任务超时检测已停止");
    }
}

void DeliveryTaskRouter::checkTimeout() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();
    std::vector<std::string> timeout_tasks;
    std::vector<std::string> retry_tasks;

    for (auto& [task_id, task] : pending_tasks_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - task.sent_at
        ).count();

        if (elapsed > 30) {  // 30秒超时
            if (task.retry_count < 3) {
                // 未达到最大重试次数，加入重试队列
                retry_tasks.push_back(task_id);
                LOG_WARNING("发货任务超时,准备重试: task_id=" + task_id +
                           ", retry_count=" + std::to_string(task.retry_count) +
                           ", merchant=" + task.merchant_code);
            } else {
                // 超过最大重试次数
                timeout_tasks.push_back(task_id);
                LOG_ERROR("发货任务超时且达到最大重试次数: task_id=" + task_id);
            }
        }
    }

    // 处理重试任务(先解锁,避免死锁)
    for (const auto& task_id : retry_tasks) {
        auto& task = pending_tasks_[task_id];
        task.retry_count++;
        task.sent_at = now;

        // 重新发送
        auto& conn_mgr = MerchantConnectionManager::getInstance();
        if (conn_mgr.isOnline(task.merchant_code)) {
            WebSocketMessage msg = WebSocketMessage::createDeliveryTask(
                task.task_id,
                task.order_id,
                task.game_id,
                task.server_id,
                task.player_account,
                task.reward_items
            );

            conn_mgr.sendToMerchant(task.merchant_code, msg);
            LOG_INFO("✅ 发货任务已自动重试: task_id=" + task_id +
                     ", retry_count=" + std::to_string(task.retry_count) +
                     ", merchant=" + task.merchant_code);
        } else {
            LOG_WARNING("商户离线,跳过重试: " + task.merchant_code);
        }
    }

    // 处理超时失败任务
    for (const auto& task_id : timeout_tasks) {
        auto it = pending_tasks_.find(task_id);
        if (it != pending_tasks_.end()) {
            auto task = it->second;
            pending_tasks_.erase(it);

            // 触发回调
            if (task.callback) {
                std::string error_msg = "超时且达到最大重试次数";
                task.callback(task.order_id, false, error_msg);
            }
        }
    }

    if (!timeout_tasks.empty() || !retry_tasks.empty()) {
        LOG_INFO("超时检测完成: 超时=" + std::to_string(timeout_tasks.size()) +
                 ", 重试=" + std::to_string(retry_tasks.size()) +
                 ", 待处理=" + std::to_string(pending_tasks_.size()));
    }
}

size_t DeliveryTaskRouter::getPendingTaskCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_tasks_.size();
}

// ==================== 私有方法 ====================

std::string DeliveryTaskRouter::generateTaskId() {
    // 生成格式: T_YYYYMMDDHHMMSS_XXXX (随机4位数)
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto tm_now = *std::localtime(&time_t_now);
    
    std::ostringstream oss;
    oss << "T_"
        << std::put_time(&tm_now, "%Y%m%d%H%M%S")
        << "_";
    
    // 添加随机数
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    oss << std::setw(4) << std::setfill('0') << dis(gen);
    
    return oss.str();
}

} // namespace websocket
} // namespace common
