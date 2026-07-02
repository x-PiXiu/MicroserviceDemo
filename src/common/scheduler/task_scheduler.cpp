//
// Created by 29108 on 2025/7/5.
//

#include "common/scheduler/task_scheduler.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"

namespace common {
    namespace scheduler {
        TaskScheduler::TaskScheduler() : TaskScheduler(Config::fromConfigManager())  {
        }

        TaskScheduler::TaskScheduler(const Config &config)  : config_(config), running_(false)  {
            config_.validate();

            // 初始化线程池
            if (config_.use_thread_pool) {
                initializeThreadPool();
            }


        }

        TaskScheduler::~TaskScheduler() {
            try {
                if (running_.load()) {
                    stop();
                }

                // 等待一小段时间确保所有任务完成
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                // 关闭线程池
                shutdownThreadPool();

            } catch (const std::exception& e) {
                // 析构函数不应抛出异常
                std::cerr << "Exception in TaskScheduler destructor: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception in TaskScheduler destructor" << std::endl;
            }
        }

        void TaskScheduler::start() {
            if (running_.exchange(true)) {
                return ;
            }

            //启动调度器线程
            scheduler_thread_ = std::thread(&TaskScheduler::schedulerLoop, this);


        }

        void TaskScheduler::stop() {
            if (!running_.exchange(false)) {
                return; // 已经停止
            }

            // 通知调度器线程退出
            condition_.notify_all();

            // 等待线程结束
            if (scheduler_thread_.joinable()) {
                scheduler_thread_.join();
            }

            // 清空任务队列
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            while (!tasks_.empty()) {
                tasks_.pop();
            }


        }

        bool TaskScheduler::isRunning() const {
            return running_.load();
        }

        size_t TaskScheduler::getPendingTaskCount() const {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            return tasks_.size();
        }

        void TaskScheduler::schedulerLoop() {
            while (running_.load()) {
                std::unique_lock<std::mutex> lock(tasks_mutex_);

                // 如果没有任务，等待新任务
                if (tasks_.empty()) {
                    condition_.wait(lock, [this] {
                        return !tasks_.empty() || !running_.load();
                    });
                    // 检查是否因为停止而醒来
                    if (!running_.load()) {
                        break;
                    }
                    continue;
                }

                // 获取最早需要执行的任务
                auto now = std::chrono::system_clock::now();
                const auto& next_task = tasks_.top();

                // 如果任务时间还没到，等待到指定时间
                if (next_task.execute_time > now) {
                    auto wait_time = next_task.execute_time - now;
                    condition_.wait_for(lock, wait_time);

                    // 检查是否因为停止而醒来
                    if (!running_.load()) {
                        break;
                    }
                    // 重新检查队列（可能有新任务或时间到了）
                    continue;
                }

                // 现在有任务需要执行，释放锁并处理
                lock.unlock();
                processScheduledTasks();
            }
        }

        void TaskScheduler::processScheduledTasks() {
            std::unique_lock<std::mutex> lock(tasks_mutex_);

            // 检查是否有待执行的任务
            while (!tasks_.empty() && running_.load()) {
                auto now = std::chrono::system_clock::now();
                const auto& next_task = tasks_.top();

                // 如果任务时间还没到，退出处理循环
                if (next_task.execute_time > now) {
                    break;
                }

                // 取出要执行的任务
                auto task_to_execute = tasks_.top();
                tasks_.pop();

                // 如果是周期性任务且调度器仍在运行，重新安排下次执行
                if (task_to_execute.is_periodic && running_.load()) {
                    auto next_execution = task_to_execute.execute_time + task_to_execute.interval;
                    tasks_.emplace(next_execution, task_to_execute.task, task_to_execute.interval);
                }

                // 释放锁后执行任务，避免阻塞其他操作
                lock.unlock();

                // 根据配置选择执行方式
                if (config_.use_thread_pool && thread_pool_) {
                    // 使用线程池异步执行任务，需要安全地更新统计信息
                    // 使用原子操作来更新统计，避免访问可能已销毁的对象
                    auto total_tasks_ptr = &total_tasks_executed_;
                    auto failed_tasks_ptr = &failed_tasks_count_;
                    auto exception_tasks_ptr = &exception_tasks_count_;

                    thread_pool_->submit([task_to_execute, total_tasks_ptr, failed_tasks_ptr, exception_tasks_ptr]() {
                        // 更新总任务数
                        total_tasks_ptr->fetch_add(1);

                        bool task_failed = false;
                        try {
                            task_to_execute.task();
                        } catch (const std::exception& e) {
                            task_failed = true;
                            failed_tasks_ptr->fetch_add(1);
                            exception_tasks_ptr->fetch_add(1);
                            std::cerr << "Task execution failed: " << e.what() << std::endl;
                        } catch (...) {
                            task_failed = true;
                            failed_tasks_ptr->fetch_add(1);
                            exception_tasks_ptr->fetch_add(1);
                            std::cerr << "Task execution failed with unknown exception" << std::endl;
                        }
                    });
                } else {
                    // 在调度线程中同步执行任务
                    executeTaskSafely(task_to_execute);
                }

                // 重新获取锁以继续处理下一个任务
                lock.lock();
            }
        }

        void TaskScheduler::executeTaskSafely(const ScheduledTask& task) {
            total_tasks_executed_.fetch_add(1);

            #ifdef DEBUG
            auto start_time = std::chrono::steady_clock::now();
            #endif
            bool task_failed = false;

            try {
                task.task();

                #ifdef DEBUG
                auto duration = std::chrono::steady_clock::now() - start_time;
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                LOG_DEBUG("Task executed successfully in " + std::to_string(duration_ms) + "ms" );
                #endif

            } catch (const std::exception& e) {
                task_failed = true;
                failed_tasks_count_.fetch_add(1);
                exception_tasks_count_.fetch_add(1);
                LOG_ERROR("Task execution failed: " + std::string(e.what()));

            } catch (...) {
                task_failed = true;
                failed_tasks_count_.fetch_add(1);
                exception_tasks_count_.fetch_add(1);
                LOG_ERROR("Task execution failed with unknown exception");
            }

            #ifdef DEBUG
            if (task_failed) {
                LOG_DEBUG("Task marked as failed" );
            }
            #else
            // 避免未使用变量警告
            (void)task_failed;
            #endif
        }





        std::uint64_t TaskScheduler::getTotalTasksExecuted() const {
            return total_tasks_executed_.load();
        }

        std::uint64_t TaskScheduler::getFailedTasksCount() const {
            return failed_tasks_count_.load();
        }

        std::uint64_t TaskScheduler::getExceptionTasksCount() const {
            return exception_tasks_count_.load();
        }

        double TaskScheduler::getTaskSuccessRate() const {
            std::uint64_t total = total_tasks_executed_.load();
            if (total == 0) {
                return 1.0; // 没有任务时认为成功率为100%
            }

            std::uint64_t failed = failed_tasks_count_.load();
            std::uint64_t successful = total - failed;
            return static_cast<double>(successful) / static_cast<double>(total);
        }

        void TaskScheduler::initializeThreadPool() {
            if (!config_.use_thread_pool) {
                return;
            }

            try {
                // 创建线程池配置
                common::config::ThreadPoolConfig pool_config;
                pool_config.core_pool_size = config_.thread_pool_core_size;
                pool_config.maximum_pool_size = config_.thread_pool_max_size;
                pool_config.queue_capacity = config_.thread_pool_queue_capacity;
                pool_config.enable_monitoring = config_.enable_thread_pool_monitoring;
                pool_config.rejection_policy = "caller_runs";  // 调度器线程执行被拒绝的任务

                // 创建线程池实例
                thread_pool_ = std::make_unique<common::thread_pool::ThreadPool>(pool_config);



            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize ThreadPool: " + std::string(e.what()));
                thread_pool_.reset();
                config_.use_thread_pool = false;  // 降级到同步执行
            }
        }

        void TaskScheduler::shutdownThreadPool() {
            if (thread_pool_) {
                try {
                    thread_pool_->shutdown();
                    thread_pool_.reset();
                } catch (const std::exception& e) {
                    // 避免在析构时使用日志系统，使用标准错误输出
                    std::cerr << "[TaskScheduler] Error during ThreadPool shutdown: " << e.what() << std::endl;
                    thread_pool_.reset();
                }
            }
        }

        std::string TaskScheduler::getThreadPoolStatus() const {
            if (!thread_pool_) {
                return "ThreadPool not available";
            }

            return "ThreadPool Status: " +
                   std::to_string(thread_pool_->getActiveThreadCount()) + "/" +
                   std::to_string(thread_pool_->getTotalThreadCount()) + " threads active, " +
                   std::to_string(thread_pool_->getQueueSize()) + " tasks queued";
        }

        size_t TaskScheduler::getActiveThreadCount() const {
            return thread_pool_ ? thread_pool_->getActiveThreadCount() : 0;
        }

        size_t TaskScheduler::getTotalThreadCount() const {
            return thread_pool_ ? thread_pool_->getTotalThreadCount() : 0;
        }

        bool TaskScheduler::adjustThreadPoolSize(int core_size, int max_size) {
            if (!thread_pool_) {
                LOG_WARNING("Cannot adjust ThreadPool size: ThreadPool not available");
                return false;
            }

            try {
                thread_pool_->adjustCorePoolSize(core_size);
                thread_pool_->adjustMaximumPoolSize(max_size);

                // 更新配置
                config_.thread_pool_core_size = core_size;
                config_.thread_pool_max_size = max_size;


                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to adjust ThreadPool size: " + std::string(e.what()));
                return false;
            }
        }

        void TaskScheduler::enableConfigHotReload() {
            try {
                auto& config_manager = common::config::ConfigManager::getInstance();

                // 监听调度器相关配置变更
                config_manager.addChangeListener("scheduler.worker_threads",
                    [this](const std::string& /*key*/, const std::string& old_val, const std::string& new_val) {

                        try {
                            int new_threads = std::stoi(new_val);
                            if (new_threads > 0) {
                                config_.worker_threads = new_threads;
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to update worker_threads: " + std::string(e.what()));
                        }
                    });

                config_manager.addChangeListener("scheduler.max_pending_tasks",
                    [this](const std::string& /*key*/, const std::string& old_val, const std::string& new_val) {

                        try {
                            int new_max = std::stoi(new_val);
                            if (new_max > 0) {
                                config_.max_pending_tasks = new_max;
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to update max_pending_tasks: " + std::string(e.what()));
                        }
                    });

                // 监听ThreadPool相关配置变更
                if (config_.use_thread_pool) {
                    config_manager.addChangeListener("scheduler.thread_pool_core_size",
                        [this](const std::string& /*key*/, const std::string& old_val, const std::string& new_val) {

                            try {
                                int new_core_size = std::stoi(new_val);
                                if (new_core_size > 0 && thread_pool_) {
                                    thread_pool_->adjustCorePoolSize(new_core_size);
                                    config_.thread_pool_core_size = new_core_size;
                                }
                            } catch (const std::exception& e) {
                                LOG_ERROR("Failed to adjust thread_pool_core_size: " + std::string(e.what()));
                            }
                        });

                    config_manager.addChangeListener("scheduler.thread_pool_max_size",
                        [this](const std::string& /*key*/, const std::string& old_val, const std::string& new_val) {

                            try {
                                int new_max_size = std::stoi(new_val);
                                if (new_max_size >= config_.thread_pool_core_size && thread_pool_) {
                                    thread_pool_->adjustMaximumPoolSize(new_max_size);
                                    config_.thread_pool_max_size = new_max_size;
                                }
                            } catch (const std::exception& e) {
                                LOG_ERROR("Failed to adjust thread_pool_max_size: " + std::string(e.what()));
                            }
                        });
                }



            } catch (const std::exception& e) {
                LOG_ERROR("Failed to enable config hot reload: " + std::string(e.what()));
            }
        }
    }
}