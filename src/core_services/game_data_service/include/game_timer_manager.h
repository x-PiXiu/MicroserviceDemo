//
// Created by Microservice Team
// 游戏数据服务独立定时器管理器 - 避免EventLoop timerfd问题
//

#ifndef GAME_TIMER_MANAGER_H
#define GAME_TIMER_MANAGER_H

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

// 前向声明
namespace common {
namespace network {
class EventLoop;
}
}

namespace core_services {
namespace game_service {

/**
 * 游戏数据服务智能定时器管理器
 * 
 * 设计目标：
 * - 支持两种模式：独立线程模式 和 EventLoop时间轮模式
 * - 根据环境自动选择最适合的定时器实现
 * - 提供统一的定时器接口
 * - 保证高性能和稳定性
 */
class GameTimerManager {
public:
    using TimerCallback = std::function<void()>;
    using TimerId = uint64_t;
    
    enum class TimerMode {
        THREAD_BASED,    // 基于独立线程的定时器
        EVENTLOOP_BASED  // 基于EventLoop时间轮的定时器
    };

    /**
     * 构造函数
     * @param mode 定时器模式
     * @param event_loop EventLoop实例（仅在EVENTLOOP_BASED模式下使用）
     */
    explicit GameTimerManager(TimerMode mode = TimerMode::THREAD_BASED, 
                             std::shared_ptr<common::network::EventLoop> event_loop = nullptr);
    
    /**
     * 析构函数
     */
    ~GameTimerManager();

    // 禁用拷贝构造和赋值
    GameTimerManager(const GameTimerManager&) = delete;
    GameTimerManager& operator=(const GameTimerManager&) = delete;

    /**
     * 启动定时器管理器
     */
    void start();

    /**
     * 停止定时器管理器
     */
    void stop();

    /**
     * 添加单次定时任务
     * @param delay_ms 延迟时间（毫秒）
     * @param callback 回调函数
     * @return 定时器ID
     */
    TimerId addOneShotTimer(int delay_ms, TimerCallback callback);

    /**
     * 添加周期性定时任务
     * @param interval_ms 间隔时间（毫秒）
     * @param callback 回调函数
     * @return 定时器ID
     */
    TimerId addPeriodicTimer(int interval_ms, TimerCallback callback);

    /**
     * 取消定时器
     * @param timer_id 定时器ID
     */
    void cancelTimer(TimerId timer_id);

    /**
     * 获取活跃定时器数量
     */
    size_t getActiveTimerCount() const;

    /**
     * 是否正在运行
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * 获取当前定时器模式
     */
    TimerMode getTimerMode() const { return timer_mode_; }
    
    /**
     * 获取定时器统计信息
     */
    std::string getTimerStats() const;

private:
    struct TimerInfo {
        TimerId id;
        std::chrono::steady_clock::time_point next_run_time;
        std::chrono::milliseconds interval;
        TimerCallback callback;
        bool is_periodic;
        bool cancelled;

        TimerInfo(TimerId timer_id, std::chrono::steady_clock::time_point next_time,
                  std::chrono::milliseconds timer_interval, TimerCallback timer_callback, 
                  bool periodic)
            : id(timer_id), next_run_time(next_time), interval(timer_interval),
              callback(std::move(timer_callback)), is_periodic(periodic), cancelled(false) {}
    };

    // === 核心状态 ===
    TimerMode timer_mode_;
    std::atomic<bool> running_{false};
    std::atomic<TimerId> next_timer_id_{1};
    
    // === 线程模式相关 ===
    std::unordered_map<TimerId, std::unique_ptr<TimerInfo>> timers_;
    mutable std::mutex timers_mutex_;
    std::unique_ptr<std::thread> worker_thread_;
    std::condition_variable condition_;
    
    // === EventLoop模式相关 ===
    std::shared_ptr<common::network::EventLoop> event_loop_;
    std::unordered_map<TimerId, uint64_t> eventloop_timer_map_; // 映射到EventLoop定时器ID
    mutable std::mutex eventloop_timers_mutex_;
    
    // === 统计信息 ===
    std::atomic<size_t> total_timers_created_{0};
    std::atomic<size_t> total_timers_executed_{0};

    /**
     * 定时器工作线程主循环
     */
    void timerWorkerLoop();

    /**
     * 执行到期的定时器
     */
    void processExpiredTimers();

    /**
     * 获取下一个定时器的执行时间
     */
    std::chrono::steady_clock::time_point getNextTimerTime() const;
};

} // namespace game_service
} // namespace core_services

#endif // GAME_TIMER_MANAGER_H
