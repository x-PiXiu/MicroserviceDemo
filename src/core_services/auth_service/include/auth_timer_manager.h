#pragma once

/**
 * @file auth_timer_manager.h
 * @brief 认证服务定时器管理器 - 基于分层时间轮实现
 * @details 提供高效的定时任务管理，支持毫秒级精度和大规模定时器
 * @author AI Assistant
 * @date 2025-09-22
 * @version 1.0.0
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
namespace auth_service {

/**
 * @brief 定时任务类型
 */
enum class AuthTaskType {
    SESSION_CLEANUP,   // 会话清理
    TOKEN_CLEANUP,     // Token清理
    HEARTBEAT,         // 心跳任务
    SECURITY_CHECK,    // 安全检查
    STATISTICS,        // 统计任务
    CUSTOM             // 自定义任务
};

/**
 * @brief 定时任务信息
 */
struct AuthTimerTaskInfo {
    std::string task_id;                        // 任务唯一标识
    std::string task_name;                      // 任务名称
    AuthTaskType type;                          // 任务类型
    std::chrono::milliseconds interval;        // 执行间隔
    std::function<void()> callback;            // 执行回调
    bool is_periodic;                          // 是否周期执行
    std::chrono::steady_clock::time_point created_at;   // 创建时间
    std::chrono::steady_clock::time_point last_executed;// 上次执行时间
    std::atomic<int> execution_count{0};       // 执行次数
    std::atomic<bool> is_running{false};       // 是否正在执行
    uint64_t timer_wheel_id{0};               // 时间轮定时器ID
    
    AuthTimerTaskInfo(const std::string& id, const std::string& name, AuthTaskType t,
                     std::chrono::milliseconds ms, std::function<void()> cb, bool periodic = true)
        : task_id(id), task_name(name), type(t), interval(ms), callback(std::move(cb))
        , is_periodic(periodic), created_at(std::chrono::steady_clock::now()) {}
};

/**
 * @brief 认证服务定时器管理器
 * @details 基于分层时间轮的高性能定时任务调度器
 */
class AuthServiceTimerManager {
public:
    /**
     * @brief 构造函数
     * @param enable_monitoring 是否启用监控
     */
    explicit AuthServiceTimerManager(bool enable_monitoring = true);
    
    /**
     * @brief 析构函数
     */
    ~AuthServiceTimerManager();
    
    // 禁止拷贝和赋值
    AuthServiceTimerManager(const AuthServiceTimerManager&) = delete;
    AuthServiceTimerManager& operator=(const AuthServiceTimerManager&) = delete;
    
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
     * @brief 检查管理器是否运行中
     * @return 运行中返回true
     */
    bool isRunning() const { return running_.load(); }
    
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
                        AuthTaskType type,
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
    bool addOnceTask(const std::string& task_id,
                    const std::string& task_name,
                    AuthTaskType type,
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
     * @brief 获取任务统计信息
     * @return JSON格式的统计信息
     */
    std::string getStatistics() const;
    
private:
    // 运行状态
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_tick_thread_{false};
    bool enable_monitoring_;
    
    // 启动时间
    std::chrono::steady_clock::time_point start_time_;
    
    // 时间轮驱动线程
    std::unique_ptr<std::thread> tick_thread_;
    
    // 分层时间轮
    std::unique_ptr<common::timer::HierarchicalTimingWheel> timing_wheel_;
    
    // 任务管理
    mutable std::mutex tasks_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AuthTimerTaskInfo>> tasks_;
    std::atomic<int> active_tasks_count_{0};
    
    // 暂停的任务
    mutable std::mutex paused_tasks_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AuthTimerTaskInfo>> paused_tasks_;
    
    // 统计信息
    std::atomic<uint64_t> total_tasks_executed_{0};
    std::atomic<uint64_t> total_execution_time_ms_{0};
    std::atomic<uint64_t> failed_executions_{0};
    
    /**
     * @brief 时间轮驱动循环
     */
    void tickLoop();
    
    /**
     * @brief 创建任务回调包装器
     * @param task_info 任务信息
     * @return 包装后的回调函数
     */
    std::function<void()> createTaskWrapper(std::shared_ptr<AuthTimerTaskInfo> task_info);
    
    /**
     * @brief 安全执行任务回调
     * @param task_info 任务信息
     */
    void safeExecuteCallback(std::shared_ptr<AuthTimerTaskInfo> task_info);
    
    /**
     * @brief 任务类型转字符串
     * @param type 任务类型
     * @return 类型字符串
     */
    std::string taskTypeToString(AuthTaskType type) const;
};

} // namespace auth_service
} // namespace core_services
