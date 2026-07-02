/**
 * @file task_scheduler.h
 * @brief 任务调度器 - 支持延时、定时和周期性任务执行
 * @author 29108
 * @date 2025/7/5
 * @version 1.0
 *
 * 功能特性:
 * - 延时任务执行 (scheduleAfter)
 * - 定时任务执行 (scheduleAt)
 * - 周期性任务执行 (scheduleEvery)
 * - 线程安全的任务队列管理
 * - 基于优先队列的高效任务调度
 * - 支持任务统计和状态查询
 *
 * 使用示例:
 * @code
 * TaskScheduler scheduler;
 * scheduler.start();
 *
 * // 延时执行
 * scheduler.scheduleAfter(std::chrono::seconds(5), []() {
 *     std::cout << "5秒后执行" << std::endl;
 * });
 *
 * // 周期性执行
 * scheduler.scheduleEvery(std::chrono::seconds(10), []() {
 *     std::cout << "每10秒执行一次" << std::endl;
 * });
 * @endcode
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

// 标准库头文件
#include <chrono>        // 时间相关类型
#include <functional>    // std::function
#include <queue>         // std::priority_queue
#include <thread>        // std::thread
#include <mutex>         // std::mutex, std::lock_guard
#include <condition_variable>  // std::condition_variable
#include <atomic>        // std::atomic
#include <memory>        // std::shared_ptr, std::unique_ptr
#include <stdexcept>
#include "./common/config/config_manager.h"
#include "./common/thread_pool/thread_pool.h"


namespace common {
    namespace scheduler {

        /**
         * @class TaskScheduler
         * @brief 高性能任务调度器
         *
         * 提供线程安全的任务调度功能，支持延时、定时和周期性任务执行。
         * 内部使用优先队列管理任务，确保任务按时间顺序执行。
         *
         * 线程安全性:
         * - 所有公共方法都是线程安全的
         * - 内部使用mutex保护共享数据
         * - 支持多线程并发调用
         *
         * 性能特点:
         * - 基于优先队列的O(log n)插入复杂度
         * - 使用条件变量避免忙等待
         * - 最小化锁竞争
         */
        class TaskScheduler {
        public:


            struct Config {
                // ==================== 调度器基础配置 ====================
                int worker_threads = 4;
                int max_pending_tasks = 10000;
                int task_queue_size = 1000;
                int tick_interval_ms = 100;

                // ==================== ThreadPool集成配置 ====================
                bool use_thread_pool = true;                    ///< 是否使用线程池执行任务
                int thread_pool_core_size = 4;                  ///< 线程池核心线程数
                int thread_pool_max_size = 8;                   ///< 线程池最大线程数
                int thread_pool_queue_capacity = 1000;          ///< 线程池队列容量
                bool enable_thread_pool_monitoring = true;      ///< 是否启用线程池监控

                // ==================== 功能开关配置 ====================
                bool enable_metrics = true;
                bool enable_task_timeout = true;
                bool enable_priority_queue = true;
                bool enable_task_history = true;

                // ==================== 超时和重试配置 ====================
                int default_task_timeout_ms = 30000;
                int max_retry_attempts = 3;
                int retry_delay_ms = 1000;

                // ==================== 监控配置 ====================
                int metrics_interval_ms = 5000;
                int max_history_size = 1000;
                int high_priority_threshold = 100;

                /**
                 * @brief 从ConfigManager加载任务调度器配置
                 */
                static Config fromConfigManager() {
                    auto& config = common::config::ConfigManager::getInstance();
                    Config result;

                    // 基础配置
                    result.worker_threads = config.get<int>("scheduler.worker_threads", 4);
                    result.max_pending_tasks = config.get<int>("scheduler.max_pending_tasks", 10000);
                    result.task_queue_size = config.get<int>("scheduler.task_queue_size", 1000);
                    result.tick_interval_ms = config.get<int>("scheduler.tick_interval_ms", 100);

                    // ThreadPool集成配置
                    result.use_thread_pool = config.get<bool>("scheduler.use_thread_pool", true);
                    result.thread_pool_core_size = config.get<int>("scheduler.thread_pool_core_size", 4);
                    result.thread_pool_max_size = config.get<int>("scheduler.thread_pool_max_size", 8);
                    result.thread_pool_queue_capacity = config.get<int>("scheduler.thread_pool_queue_capacity", 1000);
                    result.enable_thread_pool_monitoring = config.get<bool>("scheduler.enable_thread_pool_monitoring", true);

                    // 功能开关
                    result.enable_metrics = config.get<bool>("scheduler.enable_metrics", true);
                    result.enable_task_timeout = config.get<bool>("scheduler.enable_task_timeout", true);
                    result.enable_priority_queue = config.get<bool>("scheduler.enable_priority_queue", true);
                    result.enable_task_history = config.get<bool>("scheduler.enable_task_history", true);

                    // 超时和重试
                    result.default_task_timeout_ms = config.get<int>("scheduler.default_task_timeout_ms", 30000);
                    result.max_retry_attempts = config.get<int>("scheduler.max_retry_attempts", 3);
                    result.retry_delay_ms = config.get<int>("scheduler.retry_delay_ms", 1000);

                    // 监控配置
                    result.metrics_interval_ms = config.get<int>("scheduler.metrics_interval_ms", 5000);
                    result.max_history_size = config.get<int>("scheduler.max_history_size", 1000);
                    result.high_priority_threshold = config.get<int>("scheduler.high_priority_threshold", 100);

                    return result;
                }

                /**
                 * @brief 验证调度器配置
                 */
                void validate() const {
                    if (worker_threads <= 0) {
                        throw std::runtime_error("Scheduler worker_threads must be > 0");
                    }
                    if (max_pending_tasks <= 0) {
                        throw std::runtime_error("Scheduler max_pending_tasks must be > 0");
                    }
                    if (task_queue_size <= 0) {
                        throw std::runtime_error("Scheduler task_queue_size must be > 0");
                    }
                    if (tick_interval_ms <= 0) {
                        throw std::runtime_error("Scheduler tick_interval_ms must be > 0");
                    }
                    if (default_task_timeout_ms <= 0) {
                        throw std::runtime_error("Scheduler default_task_timeout_ms must be > 0");
                    }
                    if (max_retry_attempts < 0) {
                        throw std::runtime_error("Scheduler max_retry_attempts must be >= 0");
                    }

                    // ThreadPool配置验证
                    if (use_thread_pool) {
                        if (thread_pool_core_size <= 0) {
                            throw std::runtime_error("Scheduler thread_pool_core_size must be > 0");
                        }
                        if (thread_pool_max_size < thread_pool_core_size) {
                            throw std::runtime_error("Scheduler thread_pool_max_size must be >= thread_pool_core_size");
                        }
                        if (thread_pool_queue_capacity <= 0) {
                            throw std::runtime_error("Scheduler thread_pool_queue_capacity must be > 0");
                        }
                    }
                }
            };



            /// @brief 任务函数类型定义
            using Task = std::function<void()>;

            /// @brief 时间点类型定义
            using TimePoint = std::chrono::system_clock::time_point;

            /// @brief 时间间隔类型定义
            using Duration = std::chrono::milliseconds;

            /**
             * @brief 构造函数
             * @details 创建任务调度器实例，但不启动调度线程
             */
            TaskScheduler();

            TaskScheduler(const Config& config);

            /**
             * @brief 析构函数
             * @details 自动停止调度器并清理资源
             */
            ~TaskScheduler();

            /**
             * @brief 延时执行任务
             * @tparam F 可调用对象类型 (函数、lambda、函数对象等)
             * @param delay 延时时间
             * @param task 要执行的任务
             *
             * @details 在指定延时后执行任务，任务只执行一次
             *
             * @example
             * @code
             * scheduler.scheduleAfter(std::chrono::seconds(5), []() {
             *     std::cout << "5秒后执行" << std::endl;
             * });
             * @endcode
             *
             * @note 线程安全，可以从任意线程调用
             */
            template<typename F>
            void scheduleAfter(Duration delay, F&& task);

            /**
             * @brief 定时执行任务
             * @tparam F 可调用对象类型
             * @param time 执行时间点
             * @param task 要执行的任务
             *
             * @details 在指定时间点执行任务，任务只执行一次
             *
             * @example
             * @code
             * auto future_time = std::chrono::system_clock::now() + std::chrono::hours(1);
             * scheduler.scheduleAt(future_time, []() {
             *     std::cout << "1小时后执行" << std::endl;
             * });
             * @endcode
             *
             * @note 如果指定时间已过，任务将立即执行
             */
            template<typename F>
            void scheduleAt(TimePoint time, F&& task);

            /**
             * @brief 周期性执行任务
             * @tparam F 可调用对象类型
             * @param interval 执行间隔
             * @param task 要执行的任务
             *
             * @details 按指定间隔周期性执行任务，直到调度器停止
             *
             * @example
             * @code
             * scheduler.scheduleEvery(std::chrono::seconds(10), []() {
             *     std::cout << "每10秒执行一次" << std::endl;
             * });
             * @endcode
             *
             * @warning 周期性任务会一直执行，需要手动停止调度器
             */
            template<typename F>
            void scheduleEvery(Duration interval, F&& task);

            /**
             * @brief 启动任务调度器
             * @details 启动后台调度线程，开始执行已调度的任务
             * @note 重复调用无效果
             */
            void start();

            /**
             * @brief 停止任务调度器
             * @details 停止后台调度线程，清空待执行任务队列
             * @note 已在执行的任务会完成，但不会执行新任务
             */
            void stop();

            /**
             * @brief 检查调度器是否正在运行
             * @return true 如果调度器正在运行，false 否则
             */
            bool isRunning() const;

            /**
             * @brief 获取待执行任务数量
             * @return 当前队列中待执行的任务数量
             * @note 线程安全，可用于监控和调试
             */
            size_t getPendingTaskCount() const;

            /**
             * @brief 获取总执行任务数
             * @return 调度器启动以来执行的总任务数
             * @note 线程安全，包括成功和失败的任务
             */
            std::uint64_t getTotalTasksExecuted() const;

            /**
             * @brief 获取失败任务数
             * @return 执行失败的任务数量
             * @note 线程安全，包括抛出异常的任务
             */
            std::uint64_t getFailedTasksCount() const;

            /**
             * @brief 获取异常任务数
             * @return 抛出异常的任务数量
             * @note 线程安全，用于异常监控
             */
            std::uint64_t getExceptionTasksCount() const;

            /**
             * @brief 获取任务成功率
             * @return 任务成功率（0.0-1.0）
             * @note 线程安全，成功任务数/总任务数
             */
            double getTaskSuccessRate() const;

            /**
            * @brief 启用配置热更新监听
            */
            void enableConfigHotReload();

            /**
             * @brief 获取线程池状态信息
             * @return 线程池状态字符串，如果未使用线程池则返回空字符串
             */
            std::string getThreadPoolStatus() const;

            /**
             * @brief 获取线程池活跃线程数
             * @return 活跃线程数，如果未使用线程池则返回0
             */
            size_t getActiveThreadCount() const;

            /**
             * @brief 获取线程池总线程数
             * @return 总线程数，如果未使用线程池则返回0
             */
            size_t getTotalThreadCount() const;

            /**
             * @brief 动态调整线程池大小
             * @param core_size 新的核心线程数
             * @param max_size 新的最大线程数
             * @return 是否调整成功
             */
            bool adjustThreadPoolSize(int core_size, int max_size);

        private:

            Config config_; // 配置成员变量

            /// @brief 线程池实例（用于并行执行任务）
            std::unique_ptr<common::thread_pool::ThreadPool> thread_pool_;
            /**
             * @struct ScheduledTask
             * @brief 调度任务的内部表示
             *
             * 封装了任务的执行时间、任务函数、执行间隔等信息。
             * 支持一次性任务和周期性任务。
             */
            struct ScheduledTask {
                /// @brief 任务执行时间
                TimePoint execute_time;

                /// @brief 要执行的任务函数
                Task task;

                /// @brief 执行间隔 (Duration::zero() 表示一次性任务)
                Duration interval;

                /// @brief 是否为周期性任务
                bool is_periodic;

                /**
                 * @brief 构造函数
                 * @param time 执行时间
                 * @param t 任务函数
                 * @param inter 执行间隔，默认为0（一次性任务）
                 */
                ScheduledTask(TimePoint time, Task t, Duration inter = Duration::zero())
                    : execute_time(time), task(std::move(t)), interval(inter),
                      is_periodic(inter > Duration::zero()) {}

                /**
                 * @brief 比较操作符，用于优先队列排序
                 * @param other 另一个任务
                 * @return true 如果当前任务执行时间晚于other
                 * @note 优先队列使用greater比较器，执行时间早的任务优先级高
                 */
                bool operator>(const ScheduledTask& other) const {
                    return execute_time > other.execute_time;
                }
            };

            /// @brief 任务优先队列 (最小堆，执行时间早的任务在顶部)
            std::priority_queue<ScheduledTask, std::vector<ScheduledTask>,
                               std::greater<ScheduledTask>> tasks_;

            /// @brief 保护任务队列的互斥锁 (mutable允许在const方法中使用)
            mutable std::mutex tasks_mutex_;

            /// @brief 条件变量，用于通知调度线程有新任务或需要停止
            std::condition_variable condition_;

            /// @brief 后台调度线程
            std::thread scheduler_thread_;

            /// @brief 运行状态标志 (原子操作保证线程安全)
            std::atomic<bool> running_;

            /// @brief 异常统计信息 (原子操作保证线程安全)
            std::atomic<std::uint64_t> total_tasks_executed_{0};      ///< 总执行任务数
            std::atomic<std::uint64_t> failed_tasks_count_{0};        ///< 失败任务数
            std::atomic<std::uint64_t> exception_tasks_count_{0};     ///< 异常任务数

            /**
             * @brief 调度器主循环
             * @details 在后台线程中运行，负责：
             *          - 检查任务执行时间
             *          - 执行到期任务
             *          - 重新调度周期性任务
             *          - 处理停止信号
             *          - 统计异常信息
             */
            void schedulerLoop();

            /**
             * @brief 处理调度任务
             * @details 这是TaskScheduler的核心调度逻辑，负责任务队列管理、时间检查和任务执行
             */
            void processScheduledTasks();

            /**
             * @brief 安全执行任务
             * @param task 要执行的任务
             * @details 执行任务并处理异常，记录执行统计信息
             */
            void executeTaskSafely(const ScheduledTask& task);

            /**
             * @brief 初始化线程池
             * @details 根据配置创建和配置线程池
             */
            void initializeThreadPool();

            /**
             * @brief 关闭线程池
             * @details 安全关闭线程池并等待任务完成
             */
            void shutdownThreadPool();


        };

        // 模板方法实现
        template<typename F>
        void TaskScheduler::scheduleAfter(Duration delay, F&& task) {
            auto execute_time = std::chrono::system_clock::now() + delay;
            scheduleAt(execute_time, std::forward<F>(task));
        }

        template<typename F>
        void TaskScheduler::scheduleAt(TimePoint time, F&& task) {
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                tasks_.emplace(time, std::forward<F>(task));
            }
            condition_.notify_one();
        }

        template<typename F>
        void TaskScheduler::scheduleEvery(Duration interval, F&& task) {
            auto execute_time = std::chrono::system_clock::now() + interval;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                tasks_.emplace(execute_time, std::forward<F>(task), interval);
            }
            condition_.notify_one();
        }


    }
}


#endif //SCHEDULER_H
