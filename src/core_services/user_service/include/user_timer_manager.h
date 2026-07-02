#pragma once

/**
 * @file user_timer_manager.h
 * @brief 用户服务定时器管理器 - 基于分层时间轮实现
 * @details 提供高效的定时任务管理，支持毫秒级精度和大规模定时器
 * @author AI Assistant
 * @date 2025-09-20
 * @version 2.0.0
 */

#include "common/network/HierarchicalTimingWheel.h"
#include "common/logger/logger.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace core_services {
namespace user_service {

/**
 * @brief 定时任务类型
 */
enum class TaskType {
    STATISTICS,         // 统计任务
    CACHE_CLEANUP,     // 缓存清理
    USER_STATUS_CHECK, // 用户状态检查
    HEALTH_CHECK,      // 健康检查
    HEARTBEAT,         // 心跳任务
    SESSION_CLEANUP,   // 会话清理
    CUSTOM             // 自定义任务
};

/**
 * @brief 定时任务信息
 */
struct TimerTaskInfo {
    std::string task_id;                        // 任务唯一标识
    std::string task_name;                      // 任务名称
    TaskType type;                              // 任务类型
    std::chrono::milliseconds interval;        // 执行间隔
    std::function<void()> callback;            // 执行回调
    bool is_periodic;                          // 是否周期执行
    std::chrono::steady_clock::time_point created_at;   // 创建时间
    std::chrono::steady_clock::time_point last_executed;// 上次执行时间
    std::atomic<int> execution_count{0};       // 执行次数
    std::atomic<bool> is_running{false};       // 是否正在执行
    uint64_t timer_wheel_id{0};               // 时间轮定时器ID
    
    TimerTaskInfo(const std::string& id, const std::string& name, TaskType t,
                  std::chrono::milliseconds intv, std::function<void()> cb, bool periodic = true)
        : task_id(id), task_name(name), type(t), interval(intv), 
          callback(std::move(cb)), is_periodic(periodic),
          created_at(std::chrono::steady_clock::now()),
          last_executed(std::chrono::steady_clock::time_point::min()) {}
};

/**
 * @brief 用户服务定时器管理器
 * @details 基于分层时间轮实现高性能定时任务调度
 */
class UserServiceTimerManager {
public:
    /**
     * @brief 构造函数
     * @param enable_monitoring 是否启用监控统计
     */
    explicit UserServiceTimerManager(bool enable_monitoring = true);
    
    /**
     * @brief 析构函数
     */
    ~UserServiceTimerManager();
    
    /**
     * @brief 启动定时器管理器
     * @return 启动成功返回true
     */
    bool start();
    
    /**
     * @brief 停止定时器管理器
     */
    void stop();
    
    /**
     * @brief 添加周期性任务
     * @param task_id 任务ID
     * @param task_name 任务名称
     * @param type 任务类型
     * @param interval_ms 执行间隔（毫秒）
     * @param callback 回调函数
     * @return 添加成功返回true
     */
    bool addPeriodicTask(const std::string& task_id,
                         const std::string& task_name,
                         TaskType type,
                         int interval_ms,
                         std::function<void()> callback);
    
    /**
     * @brief 添加一次性任务
     * @param task_id 任务ID
     * @param task_name 任务名称
     * @param type 任务类型
     * @param delay_ms 延迟时间（毫秒）
     * @param callback 回调函数
     * @return 添加成功返回true
     */
    bool addOneShotTask(const std::string& task_id,
                        const std::string& task_name,
                        TaskType type,
                        int delay_ms,
                        std::function<void()> callback);
    
    /**
     * @brief 取消任务
     * @param task_id 任务ID
     * @return 取消成功返回true
     */
    bool cancelTask(const std::string& task_id);
    
    /**
     * @brief 暂停任务
     * @param task_id 任务ID
     * @return 暂停成功返回true
     */
    bool pauseTask(const std::string& task_id);
    
    /**
     * @brief 恢复任务
     * @param task_id 任务ID
     * @return 恢复成功返回true
     */
    bool resumeTask(const std::string& task_id);
    
    /**
     * @brief 获取任务信息
     * @param task_id 任务ID
     * @return 任务信息，不存在返回空
     */
    std::shared_ptr<TimerTaskInfo> getTaskInfo(const std::string& task_id);
    
    /**
     * @brief 获取所有任务信息
     * @return 任务信息列表
     */
    std::vector<std::shared_ptr<TimerTaskInfo>> getAllTasks();
    
    /**
     * @brief 获取运行统计
     * @return 统计信息JSON
     */
    nlohmann::json getStatistics() const;
    
    /**
     * @brief 检查是否运行中
     * @return 运行状态
     */
    bool isRunning() const { return running_; }

private:
    std::atomic<bool> running_{false};           // 运行状态
    std::atomic<bool> enable_monitoring_;       // 是否启用监控
    
    // 时间轮实例
    std::unique_ptr<common::timer::HierarchicalTimingWheel> timing_wheel_;
    
    // 任务管理
    std::unordered_map<std::string, std::shared_ptr<TimerTaskInfo>> tasks_;
    mutable std::mutex tasks_mutex_;
    
    // 暂停的任务
    std::unordered_map<std::string, std::shared_ptr<TimerTaskInfo>> paused_tasks_;
    mutable std::mutex paused_tasks_mutex_;
    
    // 时间轮驱动线程
    std::unique_ptr<std::thread> tick_thread_;
    std::atomic<bool> stop_tick_thread_{false};
    
    // 统计信息
    mutable std::atomic<uint64_t> total_tasks_executed_{0};
    mutable std::atomic<uint64_t> total_tasks_failed_{0};
    mutable std::atomic<uint64_t> active_tasks_count_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    /**
     * @brief 时间轮驱动循环
     */
    void tickLoop();
    
    /**
     * @brief 创建任务回调包装器
     * @param task_info 任务信息
     * @return 包装后的回调函数
     */
    std::function<void()> createTaskWrapper(std::shared_ptr<TimerTaskInfo> task_info);
    
    /**
     * @brief 任务类型转字符串
     */
    std::string taskTypeToString(TaskType type) const;
    
    /**
     * @brief 安全执行回调
     */
    void safeExecuteCallback(const std::shared_ptr<TimerTaskInfo>& task_info);
    
    /**
     * @brief 重新调度周期性任务
     */
    void reschedulePeriodicTask(const std::shared_ptr<TimerTaskInfo>& task_info);
};

} // namespace user_service
} // namespace core_services


