/**
 * @file timer_manager.h
 * @brief 定时器管理工具类 - 基于EventLoop时间轮的统一定时任务管理
 * @author AI Assistant
 * @date 2025/09/13
 * @version 1.0
 *
 * 功能特性:
 * - 统一的定时任务管理接口
 * - 支持命名定时器，便于管理和调试
 * - RAII自动资源管理
 * - 线程安全的定时器操作
 * - 定时器状态监控和查询
 * - 基于EventLoop时间轮，高性能低开销
 *
 * 使用场景:
 * - 替代独立线程的定时任务
 * - 统一管理微服务的所有定时操作
 * - 提供便捷的定时器生命周期管理
 *
 * 使用示例:
 * @code
 * auto timer_manager = std::make_shared<TimerManager>(event_loop);
 * 
 * // 添加周期性任务
 * timer_manager->addTimer("heartbeat", 30000, []() {
 *     sendHeartbeat();
 * });
 * 
 * // 添加一次性延时任务
 * timer_manager->addTimer("cleanup", 5000, []() {
 *     doCleanup();
 * }, false);
 * 
 * // 自动RAII管理
 * {
 *     AutoTimer auto_timer(event_loop, 1000, []() { 
 *         LOG_INFO("Auto timer triggered"); 
 *     });
 *     // 作用域结束时自动清理
 * }
 * @endcode
 */

#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>

#include "common/network/event_loop.h"
#include "common/logger/logger.h"

namespace common {
    namespace timer {

        /**
         * @brief 定时器管理器 - 基于EventLoop时间轮的统一定时任务管理
         * 
         * 核心功能:
         * - 命名定时器管理：支持为定时器分配名称，便于管理和调试
         * - 生命周期管理：自动处理定时器的创建、执行和清理
         * - 状态监控：提供定时器状态查询和监控功能
         * - 线程安全：内置锁保护，支持多线程安全操作
         * 
         * 设计优势:
         * - 零线程开销：基于EventLoop，无需额外线程
         * - 高性能：利用时间轮O(1)复杂度
         * - 易用性：简化的接口，隐藏复杂的定时器管理细节
         * - 可观测性：内置监控和日志功能
         */
        class TimerManager {
        public:
            /**
             * @brief 构造函数
             * @param event_loop EventLoop实例，用于定时器调度
             */
            explicit TimerManager(std::shared_ptr<common::network::EventLoop> event_loop);

            /**
             * @brief 析构函数 - 自动清理所有定时器
             */
            ~TimerManager();

            // 禁用拷贝构造和赋值
            TimerManager(const TimerManager&) = delete;
            TimerManager& operator=(const TimerManager&) = delete;

            // ==================== 定时器管理接口 ====================

            /**
             * @brief 添加命名定时器
             * @param name 定时器名称（用于管理和调试）
             * @param interval_ms 执行间隔（毫秒）
             * @param callback 回调函数
             * @param periodic 是否为周期性定时器（true=周期性，false=一次性）
             * @return 添加成功返回true，定时器名称已存在返回false
             */
            bool addTimer(const std::string& name, int interval_ms, 
                         std::function<void()> callback, bool periodic = true);

            /**
             * @brief 添加一次性延时定时器
             * @param name 定时器名称
             * @param delay_ms 延时时间（毫秒）
             * @param callback 回调函数
             * @return 添加成功返回true
             */
            bool addDelayedTimer(const std::string& name, int delay_ms, std::function<void()> callback);

            /**
             * @brief 移除命名定时器
             * @param name 定时器名称
             * @return 移除成功返回true，定时器不存在返回false
             */
            bool removeTimer(const std::string& name);

            /**
             * @brief 检查定时器是否存在
             * @param name 定时器名称
             * @return 定时器存在返回true
             */
            bool hasTimer(const std::string& name) const;

            /**
             * @brief 清理所有定时器
             */
            void cleanup();

            // ==================== 状态查询接口 ====================

            /**
             * @brief 获取活跃定时器数量
             * @return 当前活跃的定时器数量
             */
            size_t getActiveTimerCount() const;

            /**
             * @brief 获取定时器状态信息
             * @return JSON格式的状态信息
             */
            nlohmann::json getTimerStatus() const;

            /**
             * @brief 获取所有定时器名称列表
             * @return 定时器名称列表
             */
            std::vector<std::string> getTimerNames() const;

            // ==================== 监控和调试接口 ====================

            /**
             * @brief 启用/禁用详细日志
             * @param enabled 是否启用详细日志
             */
            void setVerboseLogging(bool enabled) { verbose_logging_ = enabled; }

            /**
             * @brief 获取统计信息
             * @return 包含创建、移除、执行次数等统计信息
             */
            struct Statistics {
                std::atomic<uint64_t> timers_created{0};      ///< 已创建定时器总数
                std::atomic<uint64_t> timers_removed{0};      ///< 已移除定时器总数
                std::atomic<uint64_t> timers_executed{0};     ///< 定时器执行总次数
                std::atomic<uint64_t> execution_errors{0};    ///< 执行错误次数
            };

            const Statistics& getStatistics() const { return stats_; }

        private:
            // ==================== 私有成员变量 ====================
            std::shared_ptr<common::network::EventLoop> event_loop_;  ///< EventLoop实例
            std::unordered_map<std::string, uint64_t> named_timers_;  ///< 命名定时器映射
            mutable std::mutex timers_mutex_;                         ///< 定时器锁
            std::atomic<bool> verbose_logging_{false};                ///< 是否启用详细日志
            Statistics stats_;                                         ///< 统计信息

            // ==================== 私有方法 ====================

            /**
             * @brief 创建包装的回调函数（用于统计和错误处理）
             * @param name 定时器名称
             * @param original_callback 原始回调函数
             * @return 包装后的回调函数
             */
            std::function<void()> wrapCallback(const std::string& name, std::function<void()> original_callback);

            /**
             * @brief 记录定时器操作日志
             * @param operation 操作类型
             * @param name 定时器名称
             * @param additional_info 附加信息
             */
            void logTimerOperation(const std::string& operation, const std::string& name, 
                                  const std::string& additional_info = "");
        };

        // ==================== RAII定时器自动管理类 ====================

        /**
         * @brief RAII定时器包装器 - 自动生命周期管理
         * 
         * 功能特性:
         * - RAII资源管理：构造时创建定时器，析构时自动清理
         * - 移动语义支持：支持高效的资源转移
         * - 状态查询：提供定时器状态查询功能
         * - 手动控制：支持提前取消定时器
         * 
         * 使用场景:
         * - 作用域绑定的定时器
         * - 临时性定时任务
         * - 自动清理的定时器管理
         */
        class AutoTimer {
        public:
            /**
             * @brief 构造函数 - 创建定时器
             * @param event_loop EventLoop实例
             * @param interval_ms 执行间隔（毫秒）
             * @param callback 回调函数
             * @param periodic 是否为周期性定时器
             */
            AutoTimer(std::shared_ptr<common::network::EventLoop> event_loop, 
                     int interval_ms, std::function<void()> callback, bool periodic = true);

            /**
             * @brief 析构函数 - 自动清理定时器
             */
            ~AutoTimer();

            // 禁用拷贝构造和赋值
            AutoTimer(const AutoTimer&) = delete;
            AutoTimer& operator=(const AutoTimer&) = delete;

            // 支持移动语义
            AutoTimer(AutoTimer&& other) noexcept;
            AutoTimer& operator=(AutoTimer&& other) noexcept;

            // ==================== 状态查询和控制接口 ====================

            /**
             * @brief 手动取消定时器
             */
            void cancel();

            /**
             * @brief 检查定时器是否活跃
             * @return 定时器活跃返回true
             */
            bool isActive() const { return active_; }

            /**
             * @brief 获取定时器ID
             * @return 定时器ID，0表示无效
             */
            uint64_t getTimerId() const { return timer_id_; }

        private:
            std::shared_ptr<common::network::EventLoop> event_loop_;  ///< EventLoop实例
            uint64_t timer_id_;                                       ///< 定时器ID
            bool active_;                                             ///< 是否活跃
        };

        // ==================== 便捷函数 ====================

        /**
         * @brief 创建全局TimerManager单例
         * @param event_loop EventLoop实例
         * @return TimerManager实例
         * @note 这是一个便捷函数，用于创建全局的定时器管理器
         */
        std::shared_ptr<TimerManager> createGlobalTimerManager(
            std::shared_ptr<common::network::EventLoop> event_loop);

        /**
         * @brief 获取全局TimerManager实例
         * @return 全局TimerManager实例，未初始化时返回nullptr
         */
        std::shared_ptr<TimerManager> getGlobalTimerManager();

    } // namespace timer
} // namespace common

#endif // TIMER_MANAGER_H









