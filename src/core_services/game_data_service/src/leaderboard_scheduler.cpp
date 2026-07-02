/**
 * @file leaderboard_scheduler.cpp
 * @brief 排行榜定时任务调度器实现
 */

#include "leaderboard_scheduler.h"
#include "common/logger/logger.h"
#include <ctime>

namespace core_services {
namespace game_service {

LeaderboardScheduler::LeaderboardScheduler(
    std::shared_ptr<LeaderboardManager> manager,
    const ScheduleTaskConfig& config)
    : manager_(std::move(manager))
    , config_(config) {
}

LeaderboardScheduler::~LeaderboardScheduler() {
    stop();
}

bool LeaderboardScheduler::start() {
    if (running_.exchange(true)) {
        LOG_WARNING("LeaderboardScheduler already running");
        return true;
    }

    if (!manager_) {
        LOG_ERROR("LeaderboardManager is null");
        running_ = false;
        return false;
    }

    stop_requested_ = false;
    scheduler_thread_ = std::make_unique<std::thread>(&LeaderboardScheduler::schedulerLoop, this);

    LOG_INFO("LeaderboardScheduler started");
    return true;
}

void LeaderboardScheduler::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    stop_requested_ = true;
    cv_.notify_all();

    if (scheduler_thread_ && scheduler_thread_->joinable()) {
        scheduler_thread_->join();
    }
    scheduler_thread_.reset();

    LOG_INFO("LeaderboardScheduler stopped");
}

void LeaderboardScheduler::updateConfig(const ScheduleTaskConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    LOG_INFO("LeaderboardScheduler config updated");
}

void LeaderboardScheduler::triggerWeeklyReset() {
    if (!manager_) return;

    LOG_INFO("Manually triggering weekly leaderboard reset");
    try {
        manager_->resetWeeklyLeaderboards();
        weekly_resets_.fetch_add(1);
        last_weekly_reset_ = std::chrono::system_clock::now();
        notifyTaskExecution(ScheduleTaskType::WEEKLY_RESET, true, "Weekly reset completed");
    } catch (const std::exception& e) {
        LOG_ERROR("Weekly reset failed: " + std::string(e.what()));
        notifyTaskExecution(ScheduleTaskType::WEEKLY_RESET, false, e.what());
    }
}

void LeaderboardScheduler::triggerMonthlyReset() {
    if (!manager_) return;

    LOG_INFO("Manually triggering monthly leaderboard reset");
    try {
        manager_->resetMonthlyLeaderboards();
        monthly_resets_.fetch_add(1);
        last_monthly_reset_ = std::chrono::system_clock::now();
        notifyTaskExecution(ScheduleTaskType::MONTHLY_RESET, true, "Monthly reset completed");
    } catch (const std::exception& e) {
        LOG_ERROR("Monthly reset failed: " + std::string(e.what()));
        notifyTaskExecution(ScheduleTaskType::MONTHLY_RESET, false, e.what());
    }
}

void LeaderboardScheduler::triggerDataCleanup() {
    if (!manager_) return;

    LOG_INFO("Manually triggering data cleanup");
    try {
        manager_->cleanupExpiredData();
        cleanup_runs_.fetch_add(1);
        notifyTaskExecution(ScheduleTaskType::DATA_CLEANUP, true, "Data cleanup completed");
    } catch (const std::exception& e) {
        LOG_ERROR("Data cleanup failed: " + std::string(e.what()));
        notifyTaskExecution(ScheduleTaskType::DATA_CLEANUP, false, e.what());
    }
}

std::chrono::system_clock::time_point LeaderboardScheduler::getNextWeeklyResetTime() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);

    // 计算到下一个重置日的天数
    int current_day = tm.tm_wday;  // 0=周日, 1=周一, ..., 6=周六
    if (current_day == 0) current_day = 7;  // 转换为 1-7

    int days_until_reset = config_.weekly_reset_day - current_day;
    if (days_until_reset <= 0) {
        days_until_reset += 7;
    }

    // 如果是重置日但已过重置时间，则等到下周
    if (days_until_reset == 7 && tm.tm_hour >= config_.weekly_reset_hour) {
        days_until_reset = 7;
    }

    // 计算下次重置时间
    tm.tm_hour = config_.weekly_reset_hour;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday += days_until_reset;

    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point LeaderboardScheduler::getNextMonthlyResetTime() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);

    // 设置为本月重置日
    tm.tm_mday = config_.monthly_reset_day;
    tm.tm_hour = config_.monthly_reset_hour;
    tm.tm_min = 0;
    tm.tm_sec = 0;

    auto reset_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    // 如果已过本月重置时间，则为下月
    if (reset_time <= now) {
        tm.tm_mon += 1;
        reset_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    return reset_time;
}

nlohmann::json LeaderboardScheduler::getStatistics() const {
    auto weekly_count = weekly_resets_.load();
    auto monthly_count = monthly_resets_.load();
    auto cleanup_count = cleanup_runs_.load();

    return {
        {"running", running_.load()},
        {"config", config_.toJson()},
        {"statistics", {
            {"weekly_resets", weekly_count},
            {"monthly_resets", monthly_count},
            {"cleanup_runs", cleanup_count}
        }},
        {"next_weekly_reset", std::chrono::system_clock::to_time_t(getNextWeeklyResetTime())},
        {"next_monthly_reset", std::chrono::system_clock::to_time_t(getNextMonthlyResetTime())}
    };
}

void LeaderboardScheduler::schedulerLoop() {
    LOG_INFO("LeaderboardScheduler loop started");

    while (!stop_requested_) {
        // 等待或超时
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(config_.interval_seconds), [this] {
            return stop_requested_.load();
        });

        if (stop_requested_) {
            break;
        }

        // 检查并执行任务
        checkAndExecuteTasks();
    }

    LOG_INFO("LeaderboardScheduler loop exited");
}

void LeaderboardScheduler::checkAndExecuteTasks() {
    if (!config_.enabled || !manager_) {
        return;
    }

    try {
        // 检查周榜重置
        if (config_.weekly_reset_day > 0 && shouldRunWeeklyReset()) {
            LOG_INFO("Executing scheduled weekly reset");
            manager_->resetWeeklyLeaderboards();
            weekly_resets_.fetch_add(1);
            last_weekly_reset_ = std::chrono::system_clock::now();
            notifyTaskExecution(ScheduleTaskType::WEEKLY_RESET, true, "Scheduled weekly reset completed");
        }

        // 检查月榜重置
        if (config_.monthly_reset_day > 0 && shouldRunMonthlyReset()) {
            LOG_INFO("Executing scheduled monthly reset");
            manager_->resetMonthlyLeaderboards();
            monthly_resets_.fetch_add(1);
            last_monthly_reset_ = std::chrono::system_clock::now();
            notifyTaskExecution(ScheduleTaskType::MONTHLY_RESET, true, "Scheduled monthly reset completed");
        }

        // 定期数据清理（每次检查都执行）
        manager_->cleanupExpiredData();
        cleanup_runs_.fetch_add(1);

    } catch (const std::exception& e) {
        LOG_ERROR("Error in scheduler task: " + std::string(e.what()));
    }
}

bool LeaderboardScheduler::shouldRunWeeklyReset() const {
    int current_day = getCurrentDayOfWeek();
    int current_hour = getCurrentHour();

    // 检查是否是重置日和重置时间（允许1小时误差）
    if (current_day == config_.weekly_reset_day &&
        current_hour >= config_.weekly_reset_hour &&
        current_hour < config_.weekly_reset_hour + 1) {

        // 检查今天是否已经重置过
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_weekly_reset_);
        return duration.count() >= 24;  // 距上次重置至少24小时
    }

    return false;
}

bool LeaderboardScheduler::shouldRunMonthlyReset() const {
    int current_day = getCurrentDayOfMonth();
    int current_hour = getCurrentHour();

    // 检查是否是重置日和重置时间（允许1小时误差）
    if (current_day == config_.monthly_reset_day &&
        current_hour >= config_.monthly_reset_hour &&
        current_hour < config_.monthly_reset_hour + 1) {

        // 检查本月是否已经重置过
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_monthly_reset_);
        return duration.count() >= 24 * 28;  // 距上次重置至少28天
    }

    return false;
}

void LeaderboardScheduler::notifyTaskExecution(ScheduleTaskType type, bool success, const std::string& message) {
    if (task_callback_) {
        try {
            task_callback_(type, success, message);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in task callback: " + std::string(e.what()));
        }
    }
}

int LeaderboardScheduler::getCurrentDayOfWeek() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);

    int day = tm.tm_wday;  // 0=周日, 1=周一, ..., 6=周六
    return day == 0 ? 7 : day;  // 转换为 1=周一, ..., 7=周日
}

int LeaderboardScheduler::getCurrentDayOfMonth() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);
    return tm.tm_mday;
}

int LeaderboardScheduler::getCurrentHour() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);
    return tm.tm_hour;
}

} // namespace game_service
} // namespace core_services
