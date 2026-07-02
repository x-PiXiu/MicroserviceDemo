/**
 * @file auth_timer_manager.cpp
 * @brief 认证服务定时器管理器实现 - 基于分层时间轮
 * @details 高性能定时任务调度实现，支持毫秒级精度
 * @author AI Assistant
 * @date 2025-09-22
 * @version 1.0.0
 */

#include "include/auth_timer_manager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#ifdef __linux__
#include <pthread.h>
#endif

namespace core_services {
namespace auth_service {

/**
 * @brief 构造函数
 */
AuthServiceTimerManager::AuthServiceTimerManager(bool enable_monitoring)
    : enable_monitoring_(enable_monitoring)
    , start_time_(std::chrono::steady_clock::now()) {
    
    // 创建分层时间轮实例
    timing_wheel_ = std::make_unique<common::timer::HierarchicalTimingWheel>();
    
    LOG_INFO(std::string("📅 [AUTH_TIMER_MANAGER] AuthServiceTimerManager 已创建，监控状态: ") + 
             (enable_monitoring ? "已启用" : "已禁用"));
}

/**
 * @brief 析构函数
 */
AuthServiceTimerManager::~AuthServiceTimerManager() {
    if (running_) {
        stop();
    }
    LOG_INFO("📅 [AUTH_TIMER_MANAGER] AuthServiceTimerManager 已销毁");
}

/**
 * @brief 启动定时器管理器
 */
bool AuthServiceTimerManager::start() {
    if (running_.exchange(true)) {
        LOG_WARNING("📅 [AUTH_TIMER_MANAGER] 已在运行中，跳过启动");
        return true;
    }
    
    try {
        LOG_INFO("📅 [AUTH_TIMER_MANAGER] 正在启动认证服务定时器管理器...");
        
        // 启动时间轮驱动线程
        stop_tick_thread_ = false;
        tick_thread_ = std::make_unique<std::thread>([this]() {
            this->tickLoop();
        });
        
        // 等待线程启动
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        LOG_INFO("✅ [AUTH_TIMER_MANAGER] 认证服务定时器管理器启动成功");
        return true;
        
    } catch (const std::exception& e) {
        running_ = false;
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 启动失败: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 停止定时器管理器
 */
void AuthServiceTimerManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    LOG_INFO("📅 [AUTH_TIMER_MANAGER] 正在停止认证服务定时器管理器...");
    
    try {
        // 🔧 关键修复：先设置停止标志，再join线程，避免竞争条件
        stop_tick_thread_ = true;
        
        // 等待线程安全退出，设置超时防止无限等待
        if (tick_thread_ && tick_thread_->joinable()) {
            LOG_DEBUG("📅 [AUTH_TIMER_MANAGER] 等待时间轮驱动线程退出...");
            tick_thread_->join();
            LOG_DEBUG("📅 [AUTH_TIMER_MANAGER] 时间轮驱动线程已退出");
        }
        
        // 清理线程对象
        tick_thread_.reset();
        
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
        
        LOG_INFO("✅ [AUTH_TIMER_MANAGER] 认证服务定时器管理器已停止");
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 停止定时器管理器时发生异常: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 停止定时器管理器时发生未知异常");
    }
}

/**
 * @brief 添加周期性任务
 */
bool AuthServiceTimerManager::addPeriodicTask(const std::string& task_id,
                                              const std::string& task_name,
                                              AuthTaskType type,
                                              int interval_ms,
                                              std::function<void()> callback) {
    if (!running_) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 管理器未运行，无法添加任务: " + task_id);
        return false;
    }
    
    if (interval_ms <= 0) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 任务间隔无效: " + std::to_string(interval_ms) + "ms");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // 检查任务是否已存在
        if (tasks_.find(task_id) != tasks_.end()) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 任务已存在，取消旧任务: " + task_id);
            auto old_task = tasks_[task_id];
            if (old_task->timer_wheel_id > 0) {
                timing_wheel_->cancelTimer(old_task->timer_wheel_id);
            }
        }
        
        // 创建任务信息
        auto task_info = std::make_shared<AuthTimerTaskInfo>(
            task_id, task_name, type, std::chrono::milliseconds(interval_ms), std::move(callback), true
        );
        
        // 创建包装回调
        auto wrapped_callback = createTaskWrapper(task_info);
        
        // 添加到时间轮（周期性任务）
        uint64_t timer_id = timing_wheel_->addTimer(
            std::chrono::milliseconds(interval_ms),
            wrapped_callback,
            std::chrono::milliseconds(interval_ms) // 周期间隔
        );
        
        if (timer_id > 0) {
            task_info->timer_wheel_id = timer_id;
            tasks_[task_id] = task_info;
            active_tasks_count_++;
            
            LOG_INFO("✅ [AUTH_TIMER_MANAGER] 周期性任务已添加: " + task_id + 
                    " (" + task_name + "), 间隔: " + std::to_string(interval_ms) + "ms, " +
                    "类型: " + taskTypeToString(type));
            return true;
        } else {
            LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 时间轮添加任务失败: " + task_id);
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 添加任务异常: " + task_id + ", 错误: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 添加一次性任务
 */
bool AuthServiceTimerManager::addOnceTask(const std::string& task_id,
                                          const std::string& task_name,
                                          AuthTaskType type,
                                          int delay_ms,
                                          std::function<void()> callback) {
    if (!running_) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 管理器未运行，无法添加任务: " + task_id);
        return false;
    }
    
    if (delay_ms < 0) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 任务延迟无效: " + std::to_string(delay_ms) + "ms");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // 检查任务是否已存在
        if (tasks_.find(task_id) != tasks_.end()) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 任务已存在，取消旧任务: " + task_id);
            auto old_task = tasks_[task_id];
            if (old_task->timer_wheel_id > 0) {
                timing_wheel_->cancelTimer(old_task->timer_wheel_id);
            }
        }
        
        // 创建任务信息
        auto task_info = std::make_shared<AuthTimerTaskInfo>(
            task_id, task_name, type, std::chrono::milliseconds(delay_ms), std::move(callback), false
        );
        
        // 创建包装回调
        auto wrapped_callback = createTaskWrapper(task_info);
        
        // 添加到时间轮（一次性任务）
        uint64_t timer_id = timing_wheel_->addTimer(
            std::chrono::milliseconds(delay_ms),
            wrapped_callback
        );
        
        if (timer_id > 0) {
            task_info->timer_wheel_id = timer_id;
            tasks_[task_id] = task_info;
            active_tasks_count_++;
            
            LOG_INFO("✅ [AUTH_TIMER_MANAGER] 一次性任务已添加: " + task_id + 
                    " (" + task_name + "), 延迟: " + std::to_string(delay_ms) + "ms, " +
                    "类型: " + taskTypeToString(type));
            return true;
        } else {
            LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 时间轮添加任务失败: " + task_id);
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 添加任务异常: " + task_id + ", 错误: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 取消任务
 */
bool AuthServiceTimerManager::cancelTask(const std::string& task_id) {
    try {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 要取消的任务不存在: " + task_id);
            return false;
        }
        
        auto task_info = it->second;
        if (task_info->timer_wheel_id > 0) {
            timing_wheel_->cancelTimer(task_info->timer_wheel_id);
        }
        
        tasks_.erase(it);
        active_tasks_count_--;
        
        LOG_INFO("✅ [AUTH_TIMER_MANAGER] 任务已取消: " + task_id);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 取消任务异常: " + task_id + ", 错误: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 暂停任务
 */
bool AuthServiceTimerManager::pauseTask(const std::string& task_id) {
    try {
        std::lock_guard<std::mutex> tasks_lock(tasks_mutex_);
        std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
        
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 要暂停的任务不存在: " + task_id);
            return false;
        }
        
        auto task_info = it->second;
        if (task_info->timer_wheel_id > 0) {
            timing_wheel_->cancelTimer(task_info->timer_wheel_id);
        }
        
        // 移动到暂停列表
        paused_tasks_[task_id] = task_info;
        tasks_.erase(it);
        active_tasks_count_--;
        
        LOG_INFO("⏸️ [AUTH_TIMER_MANAGER] 任务已暂停: " + task_id);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 暂停任务异常: " + task_id + ", 错误: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 恢复任务
 */
bool AuthServiceTimerManager::resumeTask(const std::string& task_id) {
    try {
        std::lock_guard<std::mutex> tasks_lock(tasks_mutex_);
        std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
        
        auto it = paused_tasks_.find(task_id);
        if (it == paused_tasks_.end()) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 要恢复的任务不在暂停列表中: " + task_id);
            return false;
        }
        
        auto task_info = it->second;
        
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
        
        if (timer_id > 0) {
            task_info->timer_wheel_id = timer_id;
            tasks_[task_id] = task_info;
            paused_tasks_.erase(it);
            active_tasks_count_++;
            
            LOG_INFO("▶️ [AUTH_TIMER_MANAGER] 任务已恢复: " + task_id);
            return true;
        } else {
            LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 恢复任务失败，时间轮添加失败: " + task_id);
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 恢复任务异常: " + task_id + ", 错误: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 获取任务统计信息
 */
std::string AuthServiceTimerManager::getStatistics() const {
    try {
        nlohmann::json stats;
        
        // 基本信息
        stats["status"] = running_ ? "running" : "stopped";
        stats["enable_monitoring"] = enable_monitoring_;
        
        // 运行时间
        auto runtime = std::chrono::steady_clock::now() - start_time_;
        stats["runtime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(runtime).count();
        
        // 任务统计
        {
            std::lock_guard<std::mutex> tasks_lock(tasks_mutex_);
            std::lock_guard<std::mutex> paused_lock(paused_tasks_mutex_);
            
            stats["tasks"]["active_count"] = active_tasks_count_.load();
            stats["tasks"]["paused_count"] = static_cast<int>(paused_tasks_.size());
            stats["tasks"]["total_executed"] = total_tasks_executed_.load();
            stats["tasks"]["failed_executions"] = failed_executions_.load();
            
            // 平均执行时间
            auto total_exec = total_tasks_executed_.load();
            if (total_exec > 0) {
                stats["tasks"]["avg_execution_time_ms"] = 
                    static_cast<double>(total_execution_time_ms_.load()) / total_exec;
            } else {
                stats["tasks"]["avg_execution_time_ms"] = 0.0;
            }
            
            // 按类型统计
            std::unordered_map<AuthTaskType, int> type_counts;
            for (const auto& [task_id, task_info] : tasks_) {
                type_counts[task_info->type]++;
            }
            for (const auto& [task_id, task_info] : paused_tasks_) {
                type_counts[task_info->type]++;
            }
            
            nlohmann::json type_stats;
            for (const auto& [type, count] : type_counts) {
                type_stats[taskTypeToString(type)] = count;
            }
            stats["tasks"]["by_type"] = type_stats;
        }
        
        return stats.dump(4);
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 获取统计信息异常: " + std::string(e.what()));
        return "{\"error\":\"" + std::string(e.what()) + "\"}";
    }
}

/**
 * @brief 时间轮驱动循环
 */
void AuthServiceTimerManager::tickLoop() {
    try {
        LOG_INFO("📅 [AUTH_TIMER_MANAGER] 认证服务时间轮驱动线程已启动");
        
        // 🔧 关键修复：安全设置线程名称，防止EINVAL错误
        #ifdef __linux__
        int setname_result = pthread_setname_np(pthread_self(), "AuthTimerWheel");
        if (setname_result != 0) {
            // pthread_setname_np 可能返回 EINVAL，但不影响功能
            LOG_DEBUG("📅 [AUTH_TIMER_MANAGER] 设置线程名称失败: " + std::to_string(setname_result));
        }
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
                LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 时间轮驱动异常: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                next_tick = std::chrono::steady_clock::now() + tick_interval;
            } catch (...) {
                LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 时间轮驱动未知异常");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                next_tick = std::chrono::steady_clock::now() + tick_interval;
            }
        }
        
        LOG_INFO("📅 [AUTH_TIMER_MANAGER] 认证服务时间轮驱动线程已退出");
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 时间轮驱动线程致命异常: " + std::string(e.what()));
        running_ = false; // 设置运行状态为false
    } catch (...) {
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 时间轮驱动线程未知致命异常");
        running_ = false; // 设置运行状态为false
    }
}

/**
 * @brief 创建任务回调包装器
 */
std::function<void()> AuthServiceTimerManager::createTaskWrapper(std::shared_ptr<AuthTimerTaskInfo> task_info) {
    // 使用 weak_ptr 避免循环引用
    std::weak_ptr<AuthTimerTaskInfo> weak_task_info = task_info;
    
    return [this, weak_task_info]() {
        auto task_info = weak_task_info.lock();
        if (!task_info) {
            return; // 任务已被销毁
        }
        
        safeExecuteCallback(task_info);
        
        // 如果是一次性任务，执行完成后自动清理
        if (!task_info->is_periodic) {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto it = tasks_.find(task_info->task_id);
            if (it != tasks_.end()) {
                tasks_.erase(it);
                active_tasks_count_--;
            }
        }
    };
}

/**
 * @brief 安全执行任务回调
 */
void AuthServiceTimerManager::safeExecuteCallback(std::shared_ptr<AuthTimerTaskInfo> task_info) {
    if (!task_info || !task_info->callback) {
        return;
    }
    
    // 防止重复执行
    bool expected = false;
    if (!task_info->is_running.compare_exchange_strong(expected, true)) {
        if (enable_monitoring_) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 任务正在执行中，跳过本次调度: " + task_info->task_id);
        }
        return;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // 执行任务回调
        task_info->callback();
        
        // 更新统计信息
        task_info->execution_count++;
        task_info->last_executed = start_time;
        total_tasks_executed_++;
        
        auto execution_time = std::chrono::steady_clock::now() - start_time;
        auto execution_ms = std::chrono::duration_cast<std::chrono::milliseconds>(execution_time).count();
        total_execution_time_ms_ += execution_ms;
        
        if (enable_monitoring_ && execution_ms > 100) {
            LOG_WARNING("⚠️ [AUTH_TIMER_MANAGER] 任务执行时间较长: " + task_info->task_id + 
                       " (" + task_info->task_name + "), 耗时: " + std::to_string(execution_ms) + "ms");
        }
        
    } catch (const std::exception& e) {
        failed_executions_++;
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 任务执行异常: " + task_info->task_id + 
                 " (" + task_info->task_name + "), 错误: " + std::string(e.what()));
    } catch (...) {
        failed_executions_++;
        LOG_ERROR("❌ [AUTH_TIMER_MANAGER] 任务执行未知异常: " + task_info->task_id + 
                 " (" + task_info->task_name + ")");
    }
    
    task_info->is_running = false;
}

/**
 * @brief 任务类型转字符串
 */
std::string AuthServiceTimerManager::taskTypeToString(AuthTaskType type) const {
    switch (type) {
        case AuthTaskType::SESSION_CLEANUP:   return "SESSION_CLEANUP";
        case AuthTaskType::TOKEN_CLEANUP:     return "TOKEN_CLEANUP";
        case AuthTaskType::HEARTBEAT:         return "HEARTBEAT";
        case AuthTaskType::SECURITY_CHECK:    return "SECURITY_CHECK";
        case AuthTaskType::STATISTICS:        return "STATISTICS";
        case AuthTaskType::CUSTOM:            return "CUSTOM";
        default:                              return "UNKNOWN";
    }
}

} // namespace auth_service
} // namespace core_services
