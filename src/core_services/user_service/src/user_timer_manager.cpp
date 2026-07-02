/**
 * @file user_timer_manager.cpp
 * @brief 用户服务定时器管理器实现 - 基于分层时间轮
 * @details 高性能定时任务调度实现，支持毫秒级精度
 * @author AI Assistant
 * @date 2025-09-20
 * @version 2.0.0
 */

#include "include/user_timer_manager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#ifdef __linux__
#include <pthread.h>
#endif

namespace core_services {
namespace user_service {

/**
 * @brief 构造函数
 */
UserServiceTimerManager::UserServiceTimerManager(bool enable_monitoring)
    : enable_monitoring_(enable_monitoring)
    , start_time_(std::chrono::steady_clock::now()) {
    
    // 创建分层时间轮实例
    timing_wheel_ = std::make_unique<common::timer::HierarchicalTimingWheel>();
    
    LOG_INFO(std::string("📅 [TIMER_MANAGER] UserServiceTimerManager 已创建，监控状态: ") + 
             (enable_monitoring ? "已启用" : "已禁用"));
}

/**
 * @brief 析构函数
 */
UserServiceTimerManager::~UserServiceTimerManager() {
    if (running_) {
        stop();
    }
    LOG_INFO("📅 [TIMER_MANAGER] UserServiceTimerManager 已销毁");
}

/**
 * @brief 启动定时器管理器
 */
bool UserServiceTimerManager::start() {
    if (running_.exchange(true)) {
        LOG_WARNING("📅 [TIMER_MANAGER] 已在运行中，跳过启动");
        return true;
    }
    
    try {
        LOG_INFO("📅 [TIMER_MANAGER] 正在启动定时器管理器...");
        
        // 启动时间轮驱动线程
        stop_tick_thread_ = false;
        tick_thread_ = std::make_unique<std::thread>([this]() {
            this->tickLoop();
        });
        
        // 等待线程启动
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        LOG_INFO("✅ [TIMER_MANAGER] 定时器管理器启动成功");
        return true;
        
    } catch (const std::exception& e) {
        running_ = false;
        LOG_ERROR("❌ [TIMER_MANAGER] 启动失败: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 停止定时器管理器
 */
void UserServiceTimerManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    LOG_INFO("📅 [TIMER_MANAGER] 正在停止定时器管理器...");
    
    // 停止时间轮驱动线程
    stop_tick_thread_ = true;
    if (tick_thread_ && tick_thread_->joinable()) {
        tick_thread_->join();
    }
    
    // 取消所有任务
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& [task_id, task_info] : tasks_) {
            if (task_info->timer_wheel_id > 0) {
                timing_wheel_->cancelTimer(task_info->timer_wheel_id);
            }
        }
        tasks_.clear();
        active_tasks_count_ = 0;
    }
    
    // 清理暂停的任务
    {
        std::lock_guard<std::mutex> lock(paused_tasks_mutex_);
        paused_tasks_.clear();
    }
    
    LOG_INFO("✅ [TIMER_MANAGER] 定时器管理器已停止");
}

/**
 * @brief 添加周期性任务
 */
bool UserServiceTimerManager::addPeriodicTask(const std::string& task_id,
                                              const std::string& task_name,
                                              TaskType type,
                                              int interval_ms,
                                              std::function<void()> callback) {
    if (!running_) {
        LOG_ERROR("❌ [TIMER_MANAGER] 管理器未运行，无法添加任务: " + task_id);
        return false;
    }
    
    if (interval_ms <= 0) {
        LOG_ERROR("❌ [TIMER_MANAGER] 无效的间隔时间: " + std::to_string(interval_ms) + "ms");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // 检查任务是否已存在
        if (tasks_.find(task_id) != tasks_.end()) {
            LOG_WARNING("⚠️ [TIMER_MANAGER] 任务已存在，将覆盖: " + task_id);
            // 取消现有任务
            auto& existing_task = tasks_[task_id];
            if (existing_task->timer_wheel_id > 0) {
                timing_wheel_->cancelTimer(existing_task->timer_wheel_id);
            }
        }
        
        // 创建任务信息
        auto task_info = std::make_shared<TimerTaskInfo>(
            task_id, task_name, type, 
            std::chrono::milliseconds(interval_ms),
            std::move(callback), true
        );
        
        // 创建包装回调
        auto wrapped_callback = createTaskWrapper(task_info);
        
        // 添加到时间轮
        uint64_t timer_id = timing_wheel_->addTimer(
            std::chrono::milliseconds(interval_ms),
            wrapped_callback,
            std::chrono::milliseconds(interval_ms)  // 周期性
        );
        
        task_info->timer_wheel_id = timer_id;
        tasks_[task_id] = task_info;
        active_tasks_count_++;
        
        LOG_INFO("✅ [TIMER_MANAGER] 周期性任务已添加: " + task_id + 
                 " (" + task_name + "), 间隔: " + std::to_string(interval_ms) + "ms");
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [TIMER_MANAGER] 添加周期性任务失败 " + task_id + ": " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 添加一次性任务
 */
bool UserServiceTimerManager::addOneShotTask(const std::string& task_id,
                                             const std::string& task_name,
                                             TaskType type,
                                             int delay_ms,
                                             std::function<void()> callback) {
    if (!running_) {
        LOG_ERROR("❌ [TIMER_MANAGER] 管理器未运行，无法添加任务: " + task_id);
        return false;
    }
    
    if (delay_ms < 0) {
        LOG_ERROR("❌ [TIMER_MANAGER] 无效的延迟时间: " + std::to_string(delay_ms) + "ms");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // 检查任务是否已存在
        if (tasks_.find(task_id) != tasks_.end()) {
            LOG_WARNING("⚠️ [TIMER_MANAGER] 任务已存在，将覆盖: " + task_id);
            auto& existing_task = tasks_[task_id];
            if (existing_task->timer_wheel_id > 0) {
                timing_wheel_->cancelTimer(existing_task->timer_wheel_id);
            }
        }
        
        // 创建任务信息
        auto task_info = std::make_shared<TimerTaskInfo>(
            task_id, task_name, type,
            std::chrono::milliseconds(delay_ms),
            std::move(callback), false  // 非周期性
        );
        
        // 创建包装回调
        auto wrapped_callback = createTaskWrapper(task_info);
        
        // 添加到时间轮
        uint64_t timer_id = timing_wheel_->addTimer(
            std::chrono::milliseconds(delay_ms),
            wrapped_callback
            // 不传递第三个参数，表示一次性
        );
        
        task_info->timer_wheel_id = timer_id;
        tasks_[task_id] = task_info;
        active_tasks_count_++;
        
        LOG_INFO("✅ [TIMER_MANAGER] 一次性任务已添加: " + task_id + 
                 " (" + task_name + "), 延迟: " + std::to_string(delay_ms) + "ms");
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [TIMER_MANAGER] 添加一次性任务失败 " + task_id + ": " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 取消任务
 */
bool UserServiceTimerManager::cancelTask(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        LOG_WARNING("⚠️ [TIMER_MANAGER] 要取消的任务不存在: " + task_id);
        return false;
    }
    
    auto& task_info = it->second;
    if (task_info->timer_wheel_id > 0) {
        timing_wheel_->cancelTimer(task_info->timer_wheel_id);
    }
    
    tasks_.erase(it);
    active_tasks_count_--;
    
    LOG_INFO("✅ [TIMER_MANAGER] 任务已取消: " + task_id);
    return true;
}

/**
 * @brief 暂停任务
 */
bool UserServiceTimerManager::pauseTask(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        LOG_WARNING("⚠️ [TIMER_MANAGER] 要暂停的任务不存在: " + task_id);
        return false;
    }
    
    auto task_info = it->second;
    
    // 取消时间轮中的定时器
    if (task_info->timer_wheel_id > 0) {
        timing_wheel_->cancelTimer(task_info->timer_wheel_id);
        task_info->timer_wheel_id = 0;
    }
    
    // 移到暂停列表
    {
        std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
        paused_tasks_[task_id] = task_info;
    }
    
    tasks_.erase(it);
    active_tasks_count_--;
    
    LOG_INFO("⏸️ [TIMER_MANAGER] 任务已暂停: " + task_id);
    return true;
}

/**
 * @brief 恢复任务
 */
bool UserServiceTimerManager::resumeTask(const std::string& task_id) {
    std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
    
    auto it = paused_tasks_.find(task_id);
    if (it == paused_tasks_.end()) {
        LOG_WARNING("⚠️ [TIMER_MANAGER] 要恢复的任务不存在于暂停列表: " + task_id);
        return false;
    }
    
    auto task_info = it->second;
    
    try {
        // 创建包装回调
        auto wrapped_callback = createTaskWrapper(task_info);
        
        // 重新添加到时间轮
        uint64_t timer_id;
        if (task_info->is_periodic) {
            timer_id = timing_wheel_->addTimer(
                task_info->interval,
                wrapped_callback,
                task_info->interval
            );
        } else {
            timer_id = timing_wheel_->addTimer(
                task_info->interval,
                wrapped_callback
            );
        }
        
        task_info->timer_wheel_id = timer_id;
        
        // 移回活跃列表
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            tasks_[task_id] = task_info;
            active_tasks_count_++;
        }
        
        paused_tasks_.erase(it);
        
        LOG_INFO("▶️ [TIMER_MANAGER] 任务已恢复: " + task_id);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [TIMER_MANAGER] 恢复任务失败 " + task_id + ": " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 获取任务信息
 */
std::shared_ptr<TimerTaskInfo> UserServiceTimerManager::getTaskInfo(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return it->second;
    }
    
    // 检查暂停的任务
    std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
    auto paused_it = paused_tasks_.find(task_id);
    if (paused_it != paused_tasks_.end()) {
        return paused_it->second;
    }
    
    return nullptr;
}

/**
 * @brief 获取所有任务信息
 */
std::vector<std::shared_ptr<TimerTaskInfo>> UserServiceTimerManager::getAllTasks() {
    std::vector<std::shared_ptr<TimerTaskInfo>> result;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        result.reserve(tasks_.size());
        for (const auto& [task_id, task_info] : tasks_) {
            result.push_back(task_info);
        }
    }
    
    {
        std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
        result.reserve(result.size() + paused_tasks_.size());
        for (const auto& [task_id, task_info] : paused_tasks_) {
            result.push_back(task_info);
        }
    }
    
    return result;
}

/**
 * @brief 获取运行统计
 */
nlohmann::json UserServiceTimerManager::getStatistics() const {
    nlohmann::json stats;
    
    // 基本状态
    stats["running"] = running_.load();
    stats["monitoring_enabled"] = enable_monitoring_.load();
    
    // 时间统计
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    stats["uptime_seconds"] = uptime;
    
    // 任务统计
    stats["active_tasks"] = active_tasks_count_.load();
    stats["total_executed"] = total_tasks_executed_.load();
    stats["total_failed"] = total_tasks_failed_.load();
    
    // 暂停任务数
    {
        std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
        stats["paused_tasks"] = paused_tasks_.size();
    }
    
    // 按类型统计任务
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        std::unordered_map<std::string, int> type_counts;
        
        for (const auto& [task_id, task_info] : tasks_) {
            std::string type_str = taskTypeToString(task_info->type);
            type_counts[type_str]++;
        }
        
        stats["tasks_by_type"] = type_counts;
    }
    
    // 成功率
    uint64_t total_executed = total_tasks_executed_.load();
    uint64_t total_failed = total_tasks_failed_.load();
    if (total_executed > 0) {
        double success_rate = (double)(total_executed - total_failed) / total_executed * 100.0;
        stats["success_rate_percent"] = success_rate;
    } else {
        stats["success_rate_percent"] = 0.0;
    }
    
    return stats;
}

/**
 * @brief 时间轮驱动循环
 */
void UserServiceTimerManager::tickLoop() {
    LOG_INFO("📅 [TIMER_MANAGER] 时间轮驱动线程已启动");
    
    // 设置线程名称（可选，依赖系统支持）
    #ifdef __linux__
    pthread_setname_np(pthread_self(), "TimerWheel");
    #endif
    
    const auto tick_interval = std::chrono::milliseconds(1); // 1ms精度
    auto next_tick = std::chrono::steady_clock::now() + tick_interval;
    
    while (!stop_tick_thread_) {
        try {
            // 驱动时间轮
            if (timing_wheel_) {
                timing_wheel_->tick();
            }
            
            // 精确定时等待
            std::this_thread::sleep_until(next_tick);
            next_tick += tick_interval;
            
        } catch (const std::exception& e) {
            LOG_ERROR("❌ [TIMER_MANAGER] 时间轮驱动异常: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            next_tick = std::chrono::steady_clock::now() + tick_interval;
        }
    }
    
    LOG_INFO("📅 [TIMER_MANAGER] 时间轮驱动线程已退出");
}

/**
 * @brief 创建任务回调包装器
 */
std::function<void()> UserServiceTimerManager::createTaskWrapper(std::shared_ptr<TimerTaskInfo> task_info) {
    // 使用 weak_ptr 避免循环引用
    std::weak_ptr<TimerTaskInfo> weak_task_info = task_info;
    
    return [this, weak_task_info]() {
        auto task_info = weak_task_info.lock();
        if (!task_info) {
            return; // 任务已被销毁
        }
        
        safeExecuteCallback(task_info);
        
        // 如果是周期性任务，已经在时间轮中自动重新调度了
        // 这里只需要更新统计信息
    };
}

/**
 * @brief 任务类型转字符串
 */
std::string UserServiceTimerManager::taskTypeToString(TaskType type) const {
    switch (type) {
        case TaskType::STATISTICS: return "statistics";
        case TaskType::CACHE_CLEANUP: return "cache_cleanup";
        case TaskType::USER_STATUS_CHECK: return "user_status_check";
        case TaskType::HEALTH_CHECK: return "health_check";
        case TaskType::HEARTBEAT: return "heartbeat";
        case TaskType::SESSION_CLEANUP: return "session_cleanup";
        case TaskType::CUSTOM: return "custom";
        default: return "unknown";
    }
}

/**
 * @brief 安全执行回调
 */
void UserServiceTimerManager::safeExecuteCallback(const std::shared_ptr<TimerTaskInfo>& task_info) {
    if (task_info->is_running.exchange(true)) {
        // 任务正在执行中，跳过本次执行
        LOG_WARNING("⚠️ [TIMER_MANAGER] 任务正在执行中，跳过: " + task_info->task_id);
        return;
    }
    
    try {
        auto start_time = std::chrono::steady_clock::now();
        
        // 执行回调
        task_info->callback();
        
        // 更新执行统计
        task_info->execution_count++;
        task_info->last_executed = std::chrono::steady_clock::now();
        total_tasks_executed_++;
        
        if (enable_monitoring_) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                task_info->last_executed - start_time
            );
            LOG_DEBUG("✅ [TIMER_MANAGER] 任务执行完成: " + task_info->task_id + 
                     " (" + task_info->task_name + "), 耗时: " + std::to_string(duration.count()) + "ms");
        }
        
    } catch (const std::exception& e) {
        total_tasks_failed_++;
        LOG_ERROR("❌ [TIMER_MANAGER] 任务执行异常 " + task_info->task_id + 
                 " (" + task_info->task_name + "): " + std::string(e.what()));
    } catch (...) {
        total_tasks_failed_++;
        LOG_ERROR("❌ [TIMER_MANAGER] 任务执行未知异常: " + task_info->task_id + 
                 " (" + task_info->task_name + ")");
    }
    
    task_info->is_running = false;
}

} // namespace user_service
} // namespace core_services
