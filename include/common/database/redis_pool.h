//
// Created by 29108 on 2025/7/6.
//

#ifndef REDIS_POOL_H
#define REDIS_POOL_H

#include <string>
#include <memory>
#include <queue>
#include <set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
#include <hiredis/hiredis.h>
#include "./common/config/config_manager.h"
#include "common/thread_pool/thread_pool.h"


namespace common {
    namespace database {

        class RedisConnection {
        public:
            RedisConnection(const std::string& host, int port, const std::string& password = "");
            ~RedisConnection();

            // 连接管理
            bool connect();
            void disconnect();
            bool isConnected() const;
            bool ping();
            std::string getConnectionId() const;

            // 基本操作
            std::string get(const std::string& key);
            bool set(const std::string& key, const std::string& value, int ttl = 0);
            bool del(const std::string& key);
            bool exists(const std::string& key);
            int64_t incr(const std::string& key);
            int64_t decr(const std::string& key);
            bool expire(const std::string& key, int seconds);
            int64_t ttl(const std::string& key);

            // 哈希操作
            std::string hget(const std::string& key, const std::string& field);
            bool hset(const std::string& key, const std::string& field, const std::string& value);
            bool hdel(const std::string& key, const std::string& field);

            std::map<std::string, std::string> hgetall(const std::string &key);

            bool hexists(const std::string& key, const std::string& field);
            std::vector<std::string> hkeys(const std::string& key);
            std::vector<std::string> hvals(const std::string& key);

            // 列表操作
            int64_t lpush(const std::string& key, const std::string& value);
            int64_t rpush(const std::string& key, const std::string& value);
            std::string lpop(const std::string& key);
            std::string rpop(const std::string& key);
            int64_t llen(const std::string& key);

            // 集合操作
            bool sadd(const std::string& key, const std::string& member);
            bool srem(const std::string& key, const std::string& member);
            bool sismember(const std::string& key, const std::string& member);
            std::vector<std::string> smembers(const std::string& key);
            int64_t scard(const std::string& key);

            // 有序集合操作
            bool zadd(const std::string& key, double score, const std::string& member);
            bool zrem(const std::string& key, const std::string& member);
            std::vector<std::string> zrange(const std::string& key, int64_t start, int64_t stop, bool withscores = false);
            std::vector<std::string> zrevrange(const std::string& key, int64_t start, int64_t stop, bool withscores = false);
            int64_t zcard(const std::string& key);
            double zscore(const std::string& key, const std::string& member);

            // 事务支持
            bool multi();
            std::vector<redisReply*> exec();
            bool discard();

            // 管道支持
            void pipelineStart();
            void pipelineAdd(const std::string& command);
            std::vector<redisReply*> pipelineExec();
            void PipelineDiscard();

            // 管道便利方法
            bool isPipelineActive() const;
            size_t getPipelineCommandCount() const;
            void pipelineAddSet(const std::string& key, const std::string& value);
            void pipelineAddGet(const std::string& key);
            void pipelineAddDel(const std::string& key);

            // 连接信息
            std::chrono::system_clock::time_point getLastUsed() const { return last_used_; }
            void updateLastUsed() { last_used_ = std::chrono::system_clock::now(); }

            // 内存管理
            void freeReply(redisReply* reply);

        private:
            redisContext* context_;
            std::string host_;
            int port_;
            std::string password_;
            std::string connection_id_;
            std::chrono::system_clock::time_point last_used_;
            mutable std::mutex connection_mutex_;
            bool in_transaction_;
            bool in_pipeline_;
            std::vector<std::string> pipeline_commands_;

            redisReply* executeCommand(const char* format, ...);
            bool authenticateIfNeeded();
        };

        class RedisPool {
        public:

            explicit RedisPool(const common::config::RedisConfig& config);
            explicit RedisPool();
            ~RedisPool();

            // 连接管理
            std::shared_ptr<RedisConnection> getConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
            void returnConnection(std::shared_ptr<RedisConnection> conn);

            // 统计信息
            size_t getActiveConnections() const;
            size_t getIdleConnections() const;
            size_t getTotalConnections() const;

            // 控制方法
            void start();
            void stop();
            bool isRunning() const { return running_.load(); }

            /**
            * @brief 启用Redis连接池配置热更新监听
            * @details 监听ConfigManager中Redis相关配置的变化
            */
            void enableConfigHotReload();

            // ==================== 线程池集成方法 ====================

            /**
             * @brief 初始化线程池
             * @details 根据配置创建和初始化线程池，启用异步操作支持
             */
            void initializeThreadPool();

            /**
             * @brief 关闭线程池
             * @details 优雅关闭线程池，等待所有任务完成
             */
            void shutdownThreadPool();

            /**
             * @brief 获取线程池状态信息
             * @return 包含线程池详细状态的字符串
             */
            std::string getThreadPoolStatus() const;

            /**
             * @brief 异步创建连接
             * @param count 要创建的连接数量
             * @return 返回future，包含创建的连接列表
             */
            std::future<std::vector<std::shared_ptr<RedisConnection>>> createConnectionsAsync(size_t count);

            /**
             * @brief 异步执行健康检查
             * @details 在线程池中异步执行连接健康检查
             */
            void performAsyncHealthCheck();

            /**
             * @brief 异步清理过期连接
             * @details 在线程池中异步清理空闲超时的连接
             */
            void cleanupExpiredConnectionsAsync();

            /**
             * @brief 异步预热连接池
             * @param target_size 目标连接数
             * @details 异步创建连接直到达到目标数量
             */
            void warmupPoolAsync(size_t target_size);

            /**
             * @brief 检查是否启用了异步操作
             * @return true如果启用了异步操作
             */
            bool isAsyncOperationsEnabled() const { return manager_config_.enable_async_operations; }

        private:
            common::config::RedisConfig manager_config_;
            std::queue<std::shared_ptr<RedisConnection>> idle_connections_;
            std::set<std::shared_ptr<RedisConnection>> active_connections_;
            mutable std::mutex pool_mutex_;
            std::condition_variable condition_;
            std::thread health_check_thread_;
            std::atomic<bool> running_;

            // 内部方法
            void healthCheck();
            std::shared_ptr<RedisConnection> createConnection();
            void removeExpiredConnections();
            void ensureMinimumConnections();
            bool validateConnection(std::shared_ptr<RedisConnection> conn);
            bool lightweightValidateConnection(std::shared_ptr<RedisConnection> conn);
            void performSafeConnectionMaintenance();
            void validatePoolHealth();
            // 添加到private成员变量
            std::atomic<bool> restart_required_{false};  ///< 是否需要重启连接池
            std::atomic<bool> accepting_new_connections_{true};            ///< 是否接受新连接
            std::shared_ptr<common::thread_pool::ThreadPool> restart_thread_pool_; ///< 用于异步重启的线程池

            // ==================== 线程池集成私有成员 ====================
            std::shared_ptr<common::thread_pool::ThreadPool> main_thread_pool_;    ///< 主线程池，用于异步操作
            std::atomic<bool> thread_pool_initialized_{false};                     ///< 线程池是否已初始化
            mutable std::mutex thread_pool_mutex_;                                 ///< 线程池操作互斥锁

            /**
            * @brief 标记Redis连接池需要重启
            * @details 某些关键配置变化需要重启整个连接池
            */
            void markForRestart() ;
            /**
             * @brief 动态调整Redis连接池大小
            * @param new_max_size 新的最大连接数
            * @details 可以在运行时增加连接池大小，但不能减少（避免影响现有连接）
            */
            void adjustPoolSize(size_t new_max_size);
            /**
             * @brief 更新空闲连接超时时间
             * @param new_timeout 新的空闲超时时间
             * @details 动态调整空闲连接的超时时间
             */
            void updateIdleTimeout(std::chrono::seconds new_timeout);

            /**
             * @brief 更新健康检查间隔
             * @param new_interval 新的健康检查间隔
             * @details 动态调整连接健康检查的频率
             */
            void updateHealthCheckInterval(std::chrono::seconds new_interval) ;

            /**
             * @brief 预创建指定数量的连接
             * @param count 要创建的连接数量
             */
            void preCreateConnections(size_t count);

            /**
             * @brief 触发空闲连接检查
             * @details 立即检查并清理超时的空闲连接
             */
            void triggerIdleConnectionCheck();

            /**
             * @brief 重新调度健康检查任务
             * @details 使用新的间隔重新安排健康检查
             */
            void rescheduleHealthCheck() ;

            /**
             * @brief 调度重启检查任务
             * @details 异步执行重启检查，避免阻塞配置更新线程
             */
            void scheduleRestartCheck() ;

            /**
             * @brief 检查并处理重启需求
             * @details 实际执行重启逻辑的核心方法
             */
            void checkAndHandleRestart() ;

            /**
             * @brief 设置是否接受新连接
             * @param accepting 是否接受新连接
             */
            void setAcceptingNewConnections(bool accepting);

            /**
             * @brief 等待活跃连接完成当前操作
             * @details 优雅等待，避免强制中断正在进行的操作
             */
            void waitForActiveConnectionsToFinish();

            /**
             * @brief 关闭所有现有连接
             * @details 强制关闭所有连接，包括空闲和活跃的连接
             */
            void closeAllConnections();
            /**
             * @brief 重新加载配置
             * @details 从ConfigManager重新加载最新配置
             */
            void reloadConfiguration();

            /**
             * @brief 重新初始化连接池
             * @return 是否初始化成功
             */
            bool reinitializePool();


        };

        // RAII连接管理器
        class RedisConnectionGuard {
        public:
            RedisConnectionGuard(RedisPool& pool, std::chrono::milliseconds timeout = std::chrono::milliseconds(10000))
                : pool_(pool), connection_(pool.getConnection(timeout)) {}

            ~RedisConnectionGuard() {
                if (connection_) {
                    pool_.returnConnection(connection_);
                }
            }

            RedisConnection* operator->() { return connection_.get(); }
            RedisConnection& operator*() { return *connection_; }
            RedisConnection* get() { return connection_.get(); }
            bool isValid() const { return connection_ != nullptr; }

            // 禁止拷贝
            RedisConnectionGuard(const RedisConnectionGuard&) = delete;
            RedisConnectionGuard& operator=(const RedisConnectionGuard&) = delete;

        private:
            RedisPool& pool_;
            std::shared_ptr<RedisConnection> connection_;
        };

    }
}


#endif //REDIS_POOL_H
