/**
 * @file timer_manager.cpp
 * @brief 定时器管理工具类的实现
 * @author AI Assistant
 * @date 2025/09/13
 * @version 1.0
 */

#include "common/timer/timer_manager.h"
#include <algorithm>
#include <sstream>

namespace common {
    namespace timer {

        // ==================== 全局TimerManager管理 ====================
        static std::weak_ptr<TimerManager> g_global_timer_manager;
        static std::mutex g_global_timer_mutex;

        // ==================== TimerManager实现 ====================

        TimerManager::TimerManager(std::shared_ptr<common::network::EventLoop> event_loop)
            : event_loop_(event_loop) {
            if (!event_loop_) {
                throw std::invalid_argument("EventLoop cannot be null");
            }
            
            LOG_INFO("TimerManager created with EventLoop: " + 
                    std::to_string(reinterpret_cast<uintptr_t>(event_loop_.get())));
        }

        TimerManager::~TimerManager() {
            try {
                cleanup();
                LOG_INFO("TimerManager destroyed, statistics: " +
                        std::to_string(stats_.timers_created.load()) + " created, " +
                        std::to_string(stats_.timers_removed.load()) + " removed, " +
                        std::to_string(stats_.timers_executed.load()) + " executed");
            } catch (const std::exception& e) {
                // 析构函数不应抛出异常
                std::cerr << "[TimerManager] Exception in destructor: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[TimerManager] Unknown exception in destructor" << std::endl;
            }
        }

        bool TimerManager::addTimer(const std::string& name, int interval_ms, 
                                   std::function<void()> callback, bool periodic) {
            if (name.empty()) {
                LOG_ERROR("Timer name cannot be empty");
                return false;
            }

            if (interval_ms <= 0) {
                LOG_ERROR("Timer interval must be positive: " + std::to_string(interval_ms));
                return false;
            }

            if (!callback) {
                LOG_ERROR("Timer callback cannot be null for timer: " + name);
                return false;
            }

            std::lock_guard<std::mutex> lock(timers_mutex_);

            // 检查定时器是否已存在
            if (named_timers_.find(name) != named_timers_.end()) {
                logTimerOperation("ADD_FAILED", name, "Timer already exists");
                return false;
            }

            try {
                // 创建包装的回调函数
                auto wrapped_callback = wrapCallback(name, std::move(callback));

                // 调用EventLoop创建定时器
                uint64_t timer_id;
                if (periodic) {
                    timer_id = event_loop_->runEvery(interval_ms, wrapped_callback);
                } else {
                    timer_id = event_loop_->runAfter(interval_ms, wrapped_callback);
                }

                if (timer_id == 0) {
                    LOG_ERROR("Failed to create timer in EventLoop: " + name);
                    return false;
                }

                // 记录定时器
                named_timers_[name] = timer_id;
                stats_.timers_created++;

                logTimerOperation("ADD_SUCCESS", name, 
                    "interval=" + std::to_string(interval_ms) + "ms, " +
                    "periodic=" + (periodic ? "true" : "false") + ", " +
                    "timer_id=" + std::to_string(timer_id));

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception creating timer '" + name + "': " + std::string(e.what()));
                return false;
            }
        }

        bool TimerManager::addDelayedTimer(const std::string& name, int delay_ms, std::function<void()> callback) {
            return addTimer(name, delay_ms, std::move(callback), false);
        }

        bool TimerManager::removeTimer(const std::string& name) {
            std::lock_guard<std::mutex> lock(timers_mutex_);

            auto it = named_timers_.find(name);
            if (it == named_timers_.end()) {
                logTimerOperation("REMOVE_FAILED", name, "Timer not found");
                return false;
            }

            try {
                // 取消EventLoop中的定时器
                event_loop_->cancelTimer(it->second);
                
                // 从映射中移除
                named_timers_.erase(it);
                stats_.timers_removed++;

                logTimerOperation("REMOVE_SUCCESS", name, "timer_id=" + std::to_string(it->second));
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception removing timer '" + name + "': " + std::string(e.what()));
                // 即使出现异常，也要从映射中移除，避免悬空引用
                named_timers_.erase(it);
                return false;
            }
        }

        bool TimerManager::hasTimer(const std::string& name) const {
            std::lock_guard<std::mutex> lock(timers_mutex_);
            return named_timers_.find(name) != named_timers_.end();
        }

        void TimerManager::cleanup() {
            std::lock_guard<std::mutex> lock(timers_mutex_);

            LOG_INFO("Cleaning up " + std::to_string(named_timers_.size()) + " timers");

            // 逐个取消所有定时器
            for (const auto& [name, timer_id] : named_timers_) {
                try {
                    event_loop_->cancelTimer(timer_id);
                    if (verbose_logging_) {
                        logTimerOperation("CLEANUP", name, "timer_id=" + std::to_string(timer_id));
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception cleaning up timer '" + name + "': " + std::string(e.what()));
                }
            }

            // 更新统计信息
            stats_.timers_removed += named_timers_.size();
            
            // 清空映射
            named_timers_.clear();

            LOG_INFO("Timer cleanup completed");
        }

        size_t TimerManager::getActiveTimerCount() const {
            std::lock_guard<std::mutex> lock(timers_mutex_);
            return named_timers_.size();
        }

        nlohmann::json TimerManager::getTimerStatus() const {
            std::lock_guard<std::mutex> lock(timers_mutex_);

            nlohmann::json status;
            status["active_timers"] = named_timers_.size();
            status["statistics"] = {
                {"timers_created", stats_.timers_created.load()},
                {"timers_removed", stats_.timers_removed.load()},
                {"timers_executed", stats_.timers_executed.load()},
                {"execution_errors", stats_.execution_errors.load()}
            };

            nlohmann::json timers_list = nlohmann::json::array();
            for (const auto& [name, timer_id] : named_timers_) {
                timers_list.push_back({
                    {"name", name},
                    {"timer_id", timer_id}
                });
            }
            status["timers"] = timers_list;

            return status;
        }

        std::vector<std::string> TimerManager::getTimerNames() const {
            std::lock_guard<std::mutex> lock(timers_mutex_);

            std::vector<std::string> names;
            names.reserve(named_timers_.size());
            
            for (const auto& [name, timer_id] : named_timers_) {
                names.push_back(name);
            }

            // 排序便于查看
            std::sort(names.begin(), names.end());
            return names;
        }

        std::function<void()> TimerManager::wrapCallback(const std::string& name, std::function<void()> original_callback) {
            return [this, name, original_callback = std::move(original_callback)]() {
                try {
                    // 执行原始回调
                    original_callback();
                    
                    // 更新统计信息
                    stats_.timers_executed++;
                    
                    if (verbose_logging_) {
                        logTimerOperation("EXECUTE", name, "successful");
                    }

                } catch (const std::exception& e) {
                    stats_.execution_errors++;
                    LOG_ERROR("Timer callback exception for '" + name + "': " + std::string(e.what()));
                } catch (...) {
                    stats_.execution_errors++;
                    LOG_ERROR("Unknown exception in timer callback for '" + name + "'");
                }
            };
        }

        void TimerManager::logTimerOperation(const std::string& operation, const std::string& name, 
                                           const std::string& additional_info) {
            if (!verbose_logging_ && operation.find("SUCCESS") == std::string::npos && 
                operation.find("FAILED") == std::string::npos) {
                return; // 非详细模式下只记录成功/失败日志
            }

            std::ostringstream log_msg;
            log_msg << "Timer[" << operation << "] " << name;
            if (!additional_info.empty()) {
                log_msg << " (" << additional_info << ")";
            }

            if (operation.find("FAILED") != std::string::npos || operation.find("ERROR") != std::string::npos) {
                LOG_ERROR(log_msg.str());
            } else if (operation.find("SUCCESS") != std::string::npos) {
                LOG_INFO(log_msg.str());
            } else {
                LOG_DEBUG(log_msg.str());
            }
        }

        // ==================== AutoTimer实现 ====================

        AutoTimer::AutoTimer(std::shared_ptr<common::network::EventLoop> event_loop, 
                           int interval_ms, std::function<void()> callback, bool periodic)
            : event_loop_(event_loop), timer_id_(0), active_(false) {
            
            if (!event_loop_) {
                LOG_ERROR("AutoTimer: EventLoop cannot be null");
                return;
            }

            if (interval_ms <= 0) {
                LOG_ERROR("AutoTimer: Interval must be positive: " + std::to_string(interval_ms));
                return;
            }

            if (!callback) {
                LOG_ERROR("AutoTimer: Callback cannot be null");
                return;
            }

            try {
                if (periodic) {
                    timer_id_ = event_loop_->runEvery(interval_ms, callback);
                } else {
                    timer_id_ = event_loop_->runAfter(interval_ms, callback);
                }
                
                active_ = (timer_id_ != 0);
                
                if (active_) {
                    LOG_DEBUG("AutoTimer created: timer_id=" + std::to_string(timer_id_) + 
                             ", interval=" + std::to_string(interval_ms) + "ms" +
                             ", periodic=" + (periodic ? "true" : "false"));
                } else {
                    LOG_ERROR("AutoTimer: Failed to create timer in EventLoop");
                }

            } catch (const std::exception& e) {
                LOG_ERROR("AutoTimer: Exception creating timer: " + std::string(e.what()));
                active_ = false;
                timer_id_ = 0;
            }
        }

        AutoTimer::~AutoTimer() {
            cancel();
        }

        AutoTimer::AutoTimer(AutoTimer&& other) noexcept
            : event_loop_(std::move(other.event_loop_))
            , timer_id_(other.timer_id_)
            , active_(other.active_) {
            
            // 重置源对象
            other.active_ = false;
            other.timer_id_ = 0;
        }

        AutoTimer& AutoTimer::operator=(AutoTimer&& other) noexcept {
            if (this != &other) {
                // 先清理当前定时器
                cancel();
                
                // 移动资源
                event_loop_ = std::move(other.event_loop_);
                timer_id_ = other.timer_id_;
                active_ = other.active_;
                
                // 重置源对象
                other.active_ = false;
                other.timer_id_ = 0;
            }
            return *this;
        }

        void AutoTimer::cancel() {
            if (active_ && event_loop_ && timer_id_ != 0) {
                try {
                    event_loop_->cancelTimer(timer_id_);
                    LOG_DEBUG("AutoTimer cancelled: timer_id=" + std::to_string(timer_id_));
                } catch (const std::exception& e) {
                    LOG_ERROR("AutoTimer: Exception cancelling timer: " + std::string(e.what()));
                }
                
                active_ = false;
                timer_id_ = 0;
            }
        }

        // ==================== 全局TimerManager函数实现 ====================

        std::shared_ptr<TimerManager> createGlobalTimerManager(
            std::shared_ptr<common::network::EventLoop> event_loop) {
            
            std::lock_guard<std::mutex> lock(g_global_timer_mutex);
            
            auto manager = std::make_shared<TimerManager>(event_loop);
            g_global_timer_manager = manager;
            
            LOG_INFO("Global TimerManager created");
            return manager;
        }

        std::shared_ptr<TimerManager> getGlobalTimerManager() {
            std::lock_guard<std::mutex> lock(g_global_timer_mutex);
            return g_global_timer_manager.lock();
        }

    } // namespace timer
} // namespace common









