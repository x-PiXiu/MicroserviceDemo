//
// Created by Microservice Team
// 游戏数据服务独立定时器管理器实现
//

#include "game_timer_manager.h"
#include <common/logger/logger.h>
#include <common/network/event_loop.h>
#include <algorithm>
#include <sstream>

#ifdef __linux__
#include <signal.h>
#include <pthread.h>
#endif

namespace core_services {
namespace game_service {

GameTimerManager::GameTimerManager(TimerMode mode, std::shared_ptr<common::network::EventLoop> event_loop)
    : timer_mode_(mode), event_loop_(event_loop) {
    
    if (timer_mode_ == TimerMode::EVENTLOOP_BASED && !event_loop_) {
        LOG_WARNING("EventLoop mode requested but no EventLoop provided, falling back to thread-based mode");
        timer_mode_ = TimerMode::THREAD_BASED;
    }
    
    std::string mode_str = (timer_mode_ == TimerMode::EVENTLOOP_BASED) ? "EventLoop时间轮" : "独立线程";
    std::cout << "[GameTimerManager] GameTimerManager created - 使用" << mode_str << "模式" << std::endl;
}

GameTimerManager::~GameTimerManager() {
    stop();
    //LOG_INFO("GameTimerManager destroyed");
}

void GameTimerManager::start() {
    if (running_.load()) {
        LOG_WARNING("GameTimerManager already running");
        return;
    }

    LOG_INFO("Starting GameTimerManager...");
    
    running_.store(true);
    
    if (timer_mode_ == TimerMode::THREAD_BASED) {
        // 启动独立线程模式
        worker_thread_ = std::make_unique<std::thread>(&GameTimerManager::timerWorkerLoop, this);
        LOG_INFO("✅ GameTimerManager started (Thread-based mode)");
    } else {
        // EventLoop模式不需要独立线程
        LOG_INFO("✅ GameTimerManager started (EventLoop-based mode)");
    }
}

void GameTimerManager::stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO("Stopping GameTimerManager...");

    running_.store(false);

    if (timer_mode_ == TimerMode::THREAD_BASED) {
        // 先清空所有定时器，确保 worker 线程不会再执行任何回调
        {
            std::lock_guard<std::mutex> lock(timers_mutex_);
            timers_.clear();
        }

        // 唤醒 worker 线程，让它检查 running_ 并退出
        condition_.notify_all();

        if (worker_thread_ && worker_thread_->joinable()) {
            worker_thread_->join();
        }
    } else {
        // 停止EventLoop模式定时器
        if (event_loop_) {
            std::lock_guard<std::mutex> lock(eventloop_timers_mutex_);
            for (const auto& [timer_id, eventloop_timer_id] : eventloop_timer_map_) {
                event_loop_->cancelTimer(eventloop_timer_id);
            }
            eventloop_timer_map_.clear();
        }
    }

    LOG_INFO("✅ GameTimerManager stopped successfully");
}

GameTimerManager::TimerId GameTimerManager::addOneShotTimer(int delay_ms, TimerCallback callback) {
    if (!running_.load()) {
        LOG_ERROR("GameTimerManager is not running");
        return 0;
    }

    if (delay_ms <= 0) {
        LOG_ERROR("Timer delay must be positive: " + std::to_string(delay_ms));
        return 0;
    }

    if (!callback) {
        LOG_ERROR("Timer callback cannot be null");
        return 0;
    }

    TimerId timer_id = next_timer_id_.fetch_add(1);
    total_timers_created_++;

    if (timer_mode_ == TimerMode::EVENTLOOP_BASED && event_loop_) {
        // 使用EventLoop时间轮定时器
        try {
            auto wrapped_callback = [this, timer_id, callback]() {
                total_timers_executed_++;
                LOG_DEBUG("执行单次定时器: id=" + std::to_string(timer_id));
                callback();
            };
            
            uint64_t eventloop_timer_id = event_loop_->runAfter(delay_ms, std::move(wrapped_callback));
            
            if (eventloop_timer_id > 0) {
                std::lock_guard<std::mutex> lock(eventloop_timers_mutex_);
                eventloop_timer_map_[timer_id] = eventloop_timer_id;
                LOG_DEBUG("Added EventLoop one-shot timer: id=" + std::to_string(timer_id) + 
                         ", delay=" + std::to_string(delay_ms) + "ms");
                return timer_id;
            } else {
                LOG_ERROR("Failed to add EventLoop timer, falling back to thread mode");
                // 回退到线程模式
            }
        } catch (const std::exception& e) {
            LOG_ERROR("EventLoop timer creation failed: " + std::string(e.what()) + ", falling back to thread mode");
        }
    }
    
    // 线程模式（默认或回退）
    auto next_run_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);

    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        auto timer_info = std::make_unique<TimerInfo>(
            timer_id, next_run_time, std::chrono::milliseconds(delay_ms), 
            std::move(callback), false
        );
        timers_[timer_id] = std::move(timer_info);
    }

    condition_.notify_one();
    LOG_DEBUG("Added thread-based one-shot timer: id=" + std::to_string(timer_id) + 
              ", delay=" + std::to_string(delay_ms) + "ms");
    
    return timer_id;
}

GameTimerManager::TimerId GameTimerManager::addPeriodicTimer(int interval_ms, TimerCallback callback) {
    if (!running_.load()) {
        LOG_ERROR("GameTimerManager is not running");
        return 0;
    }

    if (interval_ms <= 0) {
        LOG_ERROR("Timer interval must be positive: " + std::to_string(interval_ms));
        return 0;
    }

    if (!callback) {
        LOG_ERROR("Timer callback cannot be null");
        return 0;
    }

    TimerId timer_id = next_timer_id_.fetch_add(1);
    total_timers_created_++;

    if (timer_mode_ == TimerMode::EVENTLOOP_BASED && event_loop_) {
        // 使用EventLoop时间轮定时器
        try {
            auto wrapped_callback = [this, timer_id, callback]() {
                total_timers_executed_++;
                LOG_DEBUG("执行周期定时器: id=" + std::to_string(timer_id));
                callback();
            };
            
            uint64_t eventloop_timer_id = event_loop_->runEvery(interval_ms, std::move(wrapped_callback));
            
            if (eventloop_timer_id > 0) {
                std::lock_guard<std::mutex> lock(eventloop_timers_mutex_);
                eventloop_timer_map_[timer_id] = eventloop_timer_id;
                LOG_DEBUG("Added EventLoop periodic timer: id=" + std::to_string(timer_id) + 
                         ", interval=" + std::to_string(interval_ms) + "ms");
                return timer_id;
            } else {
                LOG_ERROR("Failed to add EventLoop periodic timer, falling back to thread mode");
                // 回退到线程模式
            }
        } catch (const std::exception& e) {
            LOG_ERROR("EventLoop periodic timer creation failed: " + std::string(e.what()) + ", falling back to thread mode");
        }
    }
    
    // 线程模式（默认或回退）
    auto next_run_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);

    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        auto timer_info = std::make_unique<TimerInfo>(
            timer_id, next_run_time, std::chrono::milliseconds(interval_ms), 
            std::move(callback), true
        );
        timers_[timer_id] = std::move(timer_info);
    }

    condition_.notify_one();
    LOG_DEBUG("Added thread-based periodic timer: id=" + std::to_string(timer_id) + 
              ", interval=" + std::to_string(interval_ms) + "ms");
    
    return timer_id;
}

void GameTimerManager::cancelTimer(TimerId timer_id) {
    if (timer_mode_ == TimerMode::EVENTLOOP_BASED && event_loop_) {
        // EventLoop模式：取消EventLoop定时器
        std::lock_guard<std::mutex> lock(eventloop_timers_mutex_);
        auto it = eventloop_timer_map_.find(timer_id);
        if (it != eventloop_timer_map_.end()) {
            event_loop_->cancelTimer(it->second);
            eventloop_timer_map_.erase(it);
            LOG_DEBUG("EventLoop timer cancelled: id=" + std::to_string(timer_id));
        } else {
            LOG_DEBUG("EventLoop timer not found for cancellation: id=" + std::to_string(timer_id));
        }
    } else {
        // 线程模式：标记为取消
        std::lock_guard<std::mutex> lock(timers_mutex_);
        auto it = timers_.find(timer_id);
        if (it != timers_.end()) {
            it->second->cancelled = true;
            LOG_DEBUG("Thread-based timer cancelled: id=" + std::to_string(timer_id));
        } else {
            LOG_DEBUG("Thread-based timer not found for cancellation: id=" + std::to_string(timer_id));
        }
    }
}

size_t GameTimerManager::getActiveTimerCount() const {
    if (timer_mode_ == TimerMode::EVENTLOOP_BASED) {
        std::lock_guard<std::mutex> lock(eventloop_timers_mutex_);
        return eventloop_timer_map_.size();
    } else {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        size_t active_count = 0;
        for (const auto& [id, timer_info] : timers_) {
            if (!timer_info->cancelled) {
                active_count++;
            }
        }
        return active_count;
    }
}

std::string GameTimerManager::getTimerStats() const {
    std::ostringstream oss;
    std::string mode_str = (timer_mode_ == TimerMode::EVENTLOOP_BASED) ? "EventLoop时间轮" : "独立线程";
    
    oss << "定时器管理器统计信息:\n";
    oss << "  模式: " << mode_str << "\n";
    oss << "  活跃定时器数: " << getActiveTimerCount() << "\n";
    oss << "  总创建定时器数: " << total_timers_created_.load() << "\n";
    oss << "  总执行次数: " << total_timers_executed_.load();
    
    return oss.str();
}

void GameTimerManager::timerWorkerLoop() {
    LOG_INFO("GameTimerManager worker thread started");

    // 阻塞 SIGPIPE 信号，防止 Redis/MySQL 连接断开时终止本线程
    // （SIGPIPE 默认行为是终止进程，会导致持有 mutex 的线程异常退出）
#ifdef __linux__
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);
#endif

    while (running_.load()) {
        try {
            processExpiredTimers();

            // 计算下次检查时间
            auto next_timer_time = getNextTimerTime();
            auto now = std::chrono::steady_clock::now();

            if (next_timer_time > now) {
                // 等待直到下一个定时器时间或被唤醒
                std::unique_lock<std::mutex> lock(timers_mutex_);
                condition_.wait_until(lock, next_timer_time, [this]() {
                    return !running_.load();
                });
            } else {
                // 有定时器已过期，短暂休息后继续处理
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

        } catch (const std::exception& e) {
            LOG_ERROR("Exception in timer worker loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            LOG_ERROR("Unknown exception in timer worker loop");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    LOG_INFO("GameTimerManager worker thread stopped");
}

void GameTimerManager::processExpiredTimers() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::unique_ptr<TimerInfo>> expired_timers;
    
    // 收集过期的定时器
    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        
        auto it = timers_.begin();
        while (it != timers_.end()) {
            auto& timer_info = it->second;
            
            if (timer_info->cancelled) {
                // 移除已取消的定时器
                it = timers_.erase(it);
                continue;
            }
            
            if (timer_info->next_run_time <= now) {
                // 定时器过期
                expired_timers.push_back(std::move(timer_info));
                it = timers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 执行过期的定时器回调（在锁外执行，避免阻塞其他线程添加定时器）
    for (auto& timer_info : expired_timers) {
        try {
            // 执行回调
            total_timers_executed_++;
            timer_info->callback();

            // 如果是周期性定时器，重新安排
            if (timer_info->is_periodic && running_.load()) {
                timer_info->next_run_time = std::chrono::steady_clock::now() + timer_info->interval;
                timer_info->cancelled = false;

                std::lock_guard<std::mutex> lock(timers_mutex_);
                timers_[timer_info->id] = std::move(timer_info);
            }

        } catch (const std::exception& e) {
            LOG_ERROR("Exception in timer callback (id=" +
                     std::to_string(timer_info->id) + "): " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("Unknown exception in timer callback (id=" +
                     std::to_string(timer_info->id) + ")");
        }
    }
}

std::chrono::steady_clock::time_point GameTimerManager::getNextTimerTime() const {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    
    if (timers_.empty()) {
        // 如果没有定时器，返回较远的未来时间
        return std::chrono::steady_clock::now() + std::chrono::seconds(10);
    }
    
    auto next_time = std::chrono::steady_clock::time_point::max();
    
    for (const auto& [id, timer_info] : timers_) {
        if (!timer_info->cancelled && timer_info->next_run_time < next_time) {
            next_time = timer_info->next_run_time;
        }
    }
    
    // 如果所有定时器都已取消，返回较远的未来时间
    if (next_time == std::chrono::steady_clock::time_point::max()) {
        return std::chrono::steady_clock::now() + std::chrono::seconds(10);
    }
    
    return next_time;
}

} // namespace game_service
} // namespace core_services
