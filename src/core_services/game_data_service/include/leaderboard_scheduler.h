/**
 * @file leaderboard_scheduler.h
 * @brief 排行榜定时任务调度器
 * @details 负责定时执行周榜重置、月榜重置、数据清理等任务
 * @author Game Server Team
 * @date 2026-02-23
 * @version 1.0.0
 */

#pragma once

#include "leaderboard_manager.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

/**
 * @brief 定时任务类型
 */
enum class ScheduleTaskType {
    WEEKLY_RESET,       // 周榜重置
    MONTHLY_RESET,      // 月榜重置
    DATA_CLEANUP,       // 数据清理
    CACHE_REFRESH       // 缓存刷新
};

/**
 * @brief 定时任务配置
 */
struct ScheduleTaskConfig {
    bool enabled = true;
    int interval_seconds = 3600;        // 检查间隔（秒）
    int weekly_reset_day = 1;           // 周榜重置日（1=周一，7=周日）
    int weekly_reset_hour = 0;          // 周榜重置小时（0-23）
    int monthly_reset_day = 1;          // 月榜重置日（1-28）
    int monthly_reset_hour = 0;         // 月榜重置小时（0-23）

    nlohmann::json toJson() const {
        return {
            {"enabled", enabled},
            {"interval_seconds", interval_seconds},
            {"weekly_reset_day", weekly_reset_day},
            {"weekly_reset_hour", weekly_reset_hour},
            {"monthly_reset_day", monthly_reset_day},
            {"monthly_reset_hour", monthly_reset_hour}
        };
    }
};

/**
 * @brief 任务执行回调
 */
using TaskExecutionCallback = std::function<void(ScheduleTaskType, bool success, const std::string& message)>;

/**
 * @brief 排行榜定时任务调度器
 */
class LeaderboardScheduler {
public:
    /**
     * @brief 构造函数
     * @param manager 排行榜管理器
     * @param config 调度配置
     */
    LeaderboardScheduler(
        std::shared_ptr<LeaderboardManager> manager,
        const ScheduleTaskConfig& config = ScheduleTaskConfig());

    ~LeaderboardScheduler();

    /**
     * @brief 启动调度器
     */
    bool start();

    /**
     * @brief 停止调度器
     */
    void stop();

    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief 更新配置
     */
    void updateConfig(const ScheduleTaskConfig& config);

    /**
     * @brief 获取配置
     */
    const ScheduleTaskConfig& getConfig() const { return config_; }

    /**
     * @brief 设置任务执行回调
     */
    void setTaskCallback(TaskExecutionCallback callback) {
        task_callback_ = std::move(callback);
    }

    /**
     * @brief 手动触发周榜重置
     */
    void triggerWeeklyReset();

    /**
     * @brief 手动触发月榜重置
     */
    void triggerMonthlyReset();

    /**
     * @brief 手动触发数据清理
     */
    void triggerDataCleanup();

    /**
     * @brief 获取下次周榜重置时间
     */
    std::chrono::system_clock::time_point getNextWeeklyResetTime() const;

    /**
     * @brief 获取下次月榜重置时间
     */
    std::chrono::system_clock::time_point getNextMonthlyResetTime() const;

    /**
     * @brief 获取统计信息
     */
    nlohmann::json getStatistics() const;

private:
    std::shared_ptr<LeaderboardManager> manager_;
    ScheduleTaskConfig config_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::unique_ptr<std::thread> scheduler_thread_;
    std::condition_variable cv_;
    std::mutex mutex_;

    TaskExecutionCallback task_callback_;

    // 统计
    std::atomic<int64_t> weekly_resets_{0};
    std::atomic<int64_t> monthly_resets_{0};
    std::atomic<int64_t> cleanup_runs_{0};
    std::chrono::system_clock::time_point last_weekly_reset_;
    std::chrono::system_clock::time_point last_monthly_reset_;

    /**
     * @brief 调度循环
     */
    void schedulerLoop();

    /**
     * @brief 检查并执行任务
     */
    void checkAndExecuteTasks();

    /**
     * @brief 检查是否应该执行周榜重置
     */
    bool shouldRunWeeklyReset() const;

    /**
     * @brief 检查是否应该执行月榜重置
     */
    bool shouldRunMonthlyReset() const;

    /**
     * @brief 通知任务执行
     */
    void notifyTaskExecution(ScheduleTaskType type, bool success, const std::string& message);

    /**
     * @brief 获取当前是一周的第几天（1=周一，7=周日）
     */
    int getCurrentDayOfWeek() const;

    /**
     * @brief 获取当前是月份的第几天
     */
    int getCurrentDayOfMonth() const;

    /**
     * @brief 获取当前小时
     */
    int getCurrentHour() const;
};

} // namespace game_service
} // namespace core_services
