/**
 * @file mysql_pool.h
 * @brief MySQL连接池 - 基于MySQL C API的高性能数据库连接池
 * @author 29108
 * @date 2025/7/5
 * @version 1.0
 *
 * 功能特性:
 * - 线程安全的连接池管理
 * - 自动连接健康检查和重连
 * - 连接超时和空闲超时管理
 * - RAII风格的连接管理器
 * - 支持事务管理
 * - 详细的连接统计信息
 *
 * 使用示例:
 * @code
 * MySQLPool::Config config;
 * config.host = "localhost";
 * config.user = "root";
 * config.password = "password";
 * config.database = "test_db";
 *
 * MySQLPool pool(config);
 * pool.start();
 *
 * // 使用RAII管理器
 * {
 *     MySQLConnectionGuard guard(pool);
 *     auto result = guard->executeQuery("SELECT * FROM users");
 *     // 连接自动归还到池中
 * }
 * @endcode
 */

#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

// 标准库头文件
#include <string>
#include <memory>
#include <queue>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <future>
#include <vector>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>
#include <stdexcept>
#include "./common/config/config_manager.h"
#include "./common/thread_pool/thread_pool.h"

namespace common {
    namespace database {

        class MySQLConnection {
        public:
            // 连接配置结构体
            struct ConnectionConfig {
                bool auto_reconnect = true;
                std::string charset = "utf8mb4";
                unsigned int connect_timeout = 10;  // 秒
                unsigned int read_timeout = 30;     // 秒
                unsigned int write_timeout = 30;    // 秒
            };

            MySQLConnection(const std::string& host, int port,
                           const std::string& user, const std::string& password,
                           const std::string& database);

            MySQLConnection(const std::string& host, int port,
                           const std::string& user, const std::string& password,
                           const std::string& database, const ConnectionConfig& config);
            ~MySQLConnection();

            // 连接管理
            bool connect();
            void disconnect();
            bool isConnected() const;
            bool ping();
            bool isValid() const;

            // SQL执行
            std::unique_ptr<sql::ResultSet> executeQuery(const std::string& sql);
            int executeUpdate(const std::string& sql);
            std::unique_ptr<sql::PreparedStatement> prepareStatement(const std::string& sql);

            // 事务管理
            void beginTransaction();
            void commit();
            void rollback();
            void setAutoCommit(bool autoCommit);

            // 连接信息
            std::chrono::system_clock::time_point getLastUsed() const { return last_used_; }
            void updateLastUsed() { last_used_ = std::chrono::system_clock::now(); }
            const std::string& getConnectionId() const { return connection_id_; }

        private:
            std::unique_ptr<sql::Connection> connection_;
            sql::mysql::MySQL_Driver* driver_;
            std::string host_;
            int port_;
            std::string user_;
            std::string password_;
            std::string database_;
            std::string connection_string_;
            std::string connection_id_;
            std::chrono::system_clock::time_point last_used_;
            mutable std::mutex connection_mutex_;

            // 连接配置选项
            bool auto_reconnect_ = true;
            std::string charset_ = "utf8mb4";
            unsigned int connect_timeout_ = 10;  // 秒
            unsigned int read_timeout_ = 30;     // 秒
            unsigned int write_timeout_ = 30;    // 秒

            void generateConnectionId();
            std::string buildConnectionString();
            bool isConnectedNoLock() const;  // 内部使用的无锁版本
        };

        class MySQLPool {
        public:

            explicit MySQLPool(common::config::DatabaseConfig config);
            explicit MySQLPool();
            ~MySQLPool();

            // 连接管理
            std::shared_ptr<MySQLConnection> getConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));
            void returnConnection(std::shared_ptr<MySQLConnection> conn);

            // 统计信息
            size_t getActiveConnections() const;
            size_t getIdleConnections() const;
            size_t getTotalConnections() const;

            // 控制方法
            void start();
            void stop();
            bool isRunning() const { return running_.load(); }

            /**
            * @brief 启用配置热更新监听
            * @details 监听ConfigManager中MySQL相关配置的变化
            */
            void enableConfigHotReload();

            // ==================== 线程池相关方法 ====================

            /**
             * @brief 初始化线程池
             * @details 根据配置创建和配置线程池
             */
            void initializeThreadPool();

            /**
             * @brief 关闭线程池
             * @details 优雅关闭所有线程池
             */
            void shutdownThreadPool();

            /**
             * @brief 异步创建连接
             * @param count 需要创建的连接数量
             * @return 返回future，可以等待创建完成
             */
            std::future<std::vector<std::shared_ptr<MySQLConnection>>> createConnectionsAsync(size_t count);

            /**
             * @brief 异步健康检查
             * @details 并行检查所有连接的健康状态
             */
            void performAsyncHealthCheck();

            /**
             * @brief 异步清理过期连接
             * @details 在后台线程中清理过期和无效连接
             */
            void cleanupExpiredConnectionsAsync();

            /**
             * @brief 获取线程池状态信息
             * @return 线程池状态字符串
             */
            std::string getThreadPoolStatus() const;

        private:
            common::config::DatabaseConfig manager_config_;
            std::queue<std::shared_ptr<MySQLConnection>> idle_connections_;
            std::set<std::shared_ptr<MySQLConnection>> active_connections_;
            mutable std::mutex pool_mutex_;
            std::condition_variable condition_;
            std::thread health_check_thread_;
            std::atomic<bool> running_;

            // 内部方法
            void healthCheck();
            std::shared_ptr<MySQLConnection> createConnection();
            void removeExpiredConnections();
            void ensureMinimumConnections();
            bool validateConnection(std::shared_ptr<MySQLConnection> conn);

            std::atomic<bool> restart_required_{false};                     ///< 是否需要重启连接池
            std::atomic<bool> accepting_new_connections_{true};             ///< 是否接受新连接

            // ==================== 线程池集成 ====================
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;  ///< 主线程池，用于各种异步操作
            std::shared_ptr<common::thread_pool::ThreadPool> restart_thread_pool_; ///< 专用于重启操作的线程池
            std::atomic<bool> thread_pool_initialized_{false};              ///< 线程池是否已初始化

            /**
 * @brief 标记Redis连接池需要重启
 * @details 某些关键配置变化需要重启整个连接池
 */
            void markForRestart();

            /**
 * @brief 调度重启检查任务
 * @details 异步执行重启检查，避免阻塞配置更新线程
 */
            void scheduleRestartCheck();

            /**
             * @brief 检查并处理重启需求
             * @details 实际执行重启逻辑的核心方法
             */
            void checkAndHandleRestart();

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

            /**
            * @brief 动态调整Redis连接池大小
            * @param new_max_size 新的最大连接数
            * @details 可以在运行时增加连接池大小，但不能减少（避免影响现有连接）
            */
            void adjustPoolSize(size_t new_max_size);

        };

        // RAII连接管理器
        class MySQLConnectionGuard {
        public:
            MySQLConnectionGuard(MySQLPool& pool, std::chrono::milliseconds timeout = std::chrono::milliseconds(15000))
                : pool_(pool), connection_(pool.getConnection(timeout)) {}

            ~MySQLConnectionGuard() {
                if (connection_) {
                    pool_.returnConnection(connection_);
                }
            }

            MySQLConnection* operator->() { return connection_.get(); }
            MySQLConnection& operator*() { return *connection_; }
            MySQLConnection* get() { return connection_.get(); }
            bool isValid() const { return connection_ != nullptr; }

            // 禁止拷贝
            MySQLConnectionGuard(const MySQLConnectionGuard&) = delete;
            MySQLConnectionGuard& operator=(const MySQLConnectionGuard&) = delete;

        private:
            MySQLPool& pool_;
            std::shared_ptr<MySQLConnection> connection_;
        };

    } // namespace database
} // namespace common

#endif // MYSQL_POOL_H
