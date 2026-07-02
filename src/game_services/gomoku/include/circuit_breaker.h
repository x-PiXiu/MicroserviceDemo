#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <optional>
#include <mutex>
#include "common/logger/logger.h"

/**
 * @file circuit_breaker.h
 * @brief 熔断器模式实现
 * @details 提供自动容错机制，防止级联故障
 * @author AI Assistant
 * @date 2025-09-28
 * @version 1.1.0
 *
 * 修复: 使用 atomic<int64_t> 替代 atomic<time_point>，避免未定义行为
 */

namespace game_services {
namespace gomoku {

/**
 * @brief 熔断器状态
 */
enum class CircuitBreakerState {
    CLOSED,     // 关闭状态：正常工作
    OPEN,       // 开启状态：拒绝所有请求
    HALF_OPEN   // 半开状态：允许少量请求测试服务是否恢复
};

/**
 * @brief 熔断器配置
 */
struct CircuitBreakerConfig {
    int failure_threshold = 5;                          // 失败阈值
    int success_threshold = 3;                          // 成功阈值（半开状态）
    std::chrono::milliseconds timeout{30000};          // 超时时间
    std::chrono::milliseconds retry_timeout{60000};    // 重试超时时间
    int max_concurrent_requests = 10;                   // 最大并发请求数
};

/**
 * @brief 熔断器实现
 * @tparam T 返回值类型
 */
template<typename T>
class CircuitBreaker {
public:
    explicit CircuitBreaker(const CircuitBreakerConfig& config = CircuitBreakerConfig{})
        : config_(config)
        , state_(CircuitBreakerState::CLOSED)
        , failure_count_(0)
        , success_count_(0)
        , concurrent_requests_(0)
        , last_failure_time_ns_(currentTimeNs()) {
        LOG_INFO("熔断器创建，配置: 失败阈值=" + std::to_string(config_.failure_threshold) +
                 ", 成功阈值=" + std::to_string(config_.success_threshold) +
                 ", 超时时间=" + std::to_string(config_.timeout.count()) + "ms");
    }

    /**
     * @brief 执行受保护的函数
     * @param func 要执行的函数
     * @return 执行结果，如果熔断器开启则返回nullopt
     */
    std::optional<T> execute(std::function<T()> func) {
        // 检查并发请求数
        if (concurrent_requests_.load() >= config_.max_concurrent_requests) {
            LOG_WARNING("熔断器: 并发请求数超限，拒绝请求");
            recordFailure();
            return std::nullopt;
        }

        // 根据状态决定是否执行
        switch (state_.load()) {
            case CircuitBreakerState::OPEN:
                return handleOpenState(func);
            case CircuitBreakerState::HALF_OPEN:
                return handleHalfOpenState(func);
            case CircuitBreakerState::CLOSED:
            default:
                return handleClosedState(func);
        }
    }

    /**
     * @brief 获取当前状态
     */
    CircuitBreakerState getState() const {
        return state_.load();
    }

    /**
     * @brief 获取失败计数
     */
    int getFailureCount() const {
        return failure_count_.load();
    }

    /**
     * @brief 获取成功计数
     */
    int getSuccessCount() const {
        return success_count_.load();
    }

    /**
     * @brief 手动重置熔断器
     */
    void reset() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.store(CircuitBreakerState::CLOSED);
        failure_count_.store(0);
        success_count_.store(0);
        LOG_INFO("熔断器已手动重置");
    }

private:
    CircuitBreakerConfig config_;
    std::atomic<CircuitBreakerState> state_;
    std::atomic<int> failure_count_;
    std::atomic<int> success_count_;
    std::atomic<int> concurrent_requests_;

    // 修复: 使用 atomic<int64_t> 存储纳秒时间戳，而不是 atomic<time_point>
    // time_point 不是 trivially copyable，不能安全地用于 atomic
    std::atomic<int64_t> last_failure_time_ns_;

    // 用于保护复杂状态转换的互斥锁
    mutable std::mutex state_mutex_;

    /**
     * @brief 获取当前时间的纳秒时间戳
     */
    static int64_t currentTimeNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    /**
     * @brief 获取上次失败时间
     */
    std::chrono::steady_clock::time_point getLastFailureTime() const {
        auto ns = last_failure_time_ns_.load();
        return std::chrono::steady_clock::time_point{
            std::chrono::nanoseconds{ns}
        };
    }

    /**
     * @brief 设置上次失败时间
     */
    void setLastFailureTime(std::chrono::steady_clock::time_point time) {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            time.time_since_epoch()
        ).count();
        last_failure_time_ns_.store(ns);
    }

    /**
     * @brief 处理关闭状态
     */
    std::optional<T> handleClosedState(std::function<T()> func) {
        concurrent_requests_++;
        auto start_time = std::chrono::steady_clock::now();

        try {
            T result = func();

            // 检查执行时间
            auto duration = std::chrono::steady_clock::now() - start_time;
            if (duration > config_.timeout) {
                LOG_WARNING("熔断器: 请求超时，执行时间=" + std::to_string(duration.count()) + "ms");
                concurrent_requests_--;
                recordFailure();
                return std::nullopt;
            }

            // 执行成功，重置失败计数
            failure_count_.store(0);
            concurrent_requests_--;
            return result;

        } catch (const std::exception& e) {
            LOG_ERROR("熔断器: 请求执行失败: " + std::string(e.what()));
            concurrent_requests_--;
            recordFailure();
            return std::nullopt;
        }
    }

    /**
     * @brief 处理开启状态
     */
    std::optional<T> handleOpenState(std::function<T()> func) {
        auto now = std::chrono::steady_clock::now();
        auto last_failure = getLastFailureTime();

        if (now - last_failure > config_.retry_timeout) {
            // 进入半开状态（使用锁保护状态转换）
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                // 双重检查，避免多线程同时转换
                if (state_.load() == CircuitBreakerState::OPEN) {
                    state_.store(CircuitBreakerState::HALF_OPEN);
                    success_count_.store(0);
                    LOG_INFO("熔断器: 从开启状态进入半开状态");
                }
            }
            return handleHalfOpenState(func);
        }

        LOG_DEBUG("熔断器: 开启状态，拒绝请求");
        return std::nullopt;
    }

    /**
     * @brief 处理半开状态
     */
    std::optional<T> handleHalfOpenState(std::function<T()> func) {
        concurrent_requests_++;
        auto start_time = std::chrono::steady_clock::now();

        try {
            T result = func();

            // 检查执行时间
            auto duration = std::chrono::steady_clock::now() - start_time;
            if (duration > config_.timeout) {
                LOG_WARNING("熔断器: 半开状态请求超时");
                concurrent_requests_--;
                recordFailure();
                return std::nullopt;
            }

            // 执行成功（使用锁保护状态转换）
            int current_success = success_count_.fetch_add(1) + 1;
            concurrent_requests_--;

            if (current_success >= config_.success_threshold) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                // 双重检查
                if (state_.load() == CircuitBreakerState::HALF_OPEN) {
                    state_.store(CircuitBreakerState::CLOSED);
                    failure_count_.store(0);
                    success_count_.store(0);
                    LOG_INFO("熔断器: 从半开状态恢复到关闭状态");
                }
            }

            return result;

        } catch (const std::exception& e) {
            LOG_ERROR("熔断器: 半开状态请求失败: " + std::string(e.what()));
            concurrent_requests_--;
            recordFailure();
            return std::nullopt;
        }
    }

    /**
     * @brief 记录失败
     */
    void recordFailure() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        int current_failures = failure_count_.fetch_add(1) + 1;
        setLastFailureTime(std::chrono::steady_clock::now());

        if (current_failures >= config_.failure_threshold) {
            if (state_.load() != CircuitBreakerState::OPEN) {
                state_.store(CircuitBreakerState::OPEN);
                LOG_WARNING("熔断器: 失败次数达到阈值(" + std::to_string(config_.failure_threshold) +
                           ")，进入开启状态");
            }
        }
    }
};

} // namespace gomoku
} // namespace game_services


