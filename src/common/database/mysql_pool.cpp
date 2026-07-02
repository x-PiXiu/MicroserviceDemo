//
// Created by 29108 on 2025/7/5.
//

#include "common/database/mysql_pool.h"
#include "common/logger/logger.h"

#ifdef MYSQL_STUB
// Stub implementation when MySQL is not available
namespace sql {
    namespace mysql {
        class MySQL_Driver {
        public:
            void* connect(const std::string&, const std::string&, const std::string&) { return nullptr; }
        };
        MySQL_Driver* get_mysql_driver_instance() { static MySQL_Driver driver; return &driver; }
    }

    class Connection {
    public:
        void setSchema(const std::string&) {}
        bool isValid() { return false; }
        void close() {}
        void* createStatement() { return nullptr; }
        void* prepareStatement(const std::string&) { return nullptr; }
        void setAutoCommit(bool) {}
        void commit() {}
        void rollback() {}
    };

    class Statement {
    public:
        void* executeQuery(const std::string&) { return nullptr; }
        int executeUpdate(const std::string&) { return 0; }
        bool execute(const std::string&) { return false; }
        void close() {}
    };

    class PreparedStatement {
    public:
        void setString(int, const std::string&) {}
        void setInt(int, int) {}
        void setDouble(int, double) {}
        void setBoolean(int, bool) {}
        void* executeQuery() { return nullptr; }
        int executeUpdate() { return 0; }
        bool execute() { return false; }
        void close() {}
    };

    class ResultSet {
    public:
        bool next() { return false; }
        std::string getString(int) { return ""; }
        std::string getString(const std::string&) { return ""; }
        int getInt(int) { return 0; }
        int getInt(const std::string&) { return 0; }
        double getDouble(int) { return 0.0; }
        double getDouble(const std::string&) { return 0.0; }
        bool getBoolean(int) { return false; }
        bool getBoolean(const std::string&) { return false; }
        void close() {}
    };

    class SQLException : public std::exception {
    public:
        SQLException(const std::string& msg) : message_(msg) {}
        const char* what() const noexcept override { return message_.c_str(); }
    private:
        std::string message_;
    };
}
#else
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>
#endif

namespace common {
    namespace database {
        MySQLConnection::MySQLConnection(const std::string &host, int port, const std::string &user,
            const std::string &password, const std::string &database)
        : driver_(nullptr), host_(host), port_(port), user_(user),
            password_(password), database_(database),
            auto_reconnect_(true), charset_("utf8mb4"), connect_timeout_(10),
            read_timeout_(30), write_timeout_(30) {

#ifndef MYSQL_STUB
            driver_ = sql::mysql::get_mysql_driver_instance();
#endif
            connection_string_ = buildConnectionString();
            generateConnectionId();
            last_used_ = std::chrono::system_clock::now();
        }


        MySQLConnection::MySQLConnection(const std::string &host, int port, const std::string &user,
            const std::string &password, const std::string &database, const ConnectionConfig &config)
        : driver_(nullptr), host_(host), port_(port), user_(user),
            password_(password), database_(database),
            auto_reconnect_(config.auto_reconnect), charset_(config.charset),
            connect_timeout_(config.connect_timeout), read_timeout_(config.read_timeout),
            write_timeout_(config.write_timeout) {

            driver_ = sql::mysql::get_mysql_driver_instance();
            connection_string_ = buildConnectionString();
            generateConnectionId();
            last_used_ = std::chrono::system_clock::now();
        }

        MySQLConnection::~MySQLConnection() {
            disconnect();
        }

                /**
         * @brief 建立与MySQL服务器的数据库连接
         * @return true 连接成功建立，false 连接失败
         *
         * @details 这是一个线程安全的连接建立方法，执行以下步骤：
         *          1. 获取连接互斥锁确保线程安全
         *          2. 使用MySQL Connector/C++驱动建立TCP连接
         *          3. 设置目标数据库schema
         *          4. 配置连接选项（重连、超时等）
         *          5. 设置数据库字符集
         *          6. 更新连接使用时间并记录日志
         *
         * @note 使用MySQL Connector/C++实现，建议迁移到MySQL C API以减少依赖
         * @thread_safety 线程安全 - 使用互斥锁保护连接操作
         */
        bool MySQLConnection::connect() {
            // ==================== 线程安全保护 ====================
            /**
             * @brief 获取连接互斥锁
             * @details 使用RAII风格的lock_guard确保在整个连接过程中
             *          只有一个线程能够操作连接对象，防止并发连接导致的竞态条件
             */
            std::lock_guard<std::mutex> lock(connection_mutex_);

            try {
                // ==================== 建立TCP连接 ====================
                /**
                 * @brief 使用MySQL驱动建立数据库连接
                 * @details driver_->connect()执行以下操作：
                 *          - 解析connection_string_（格式：tcp://host:port）
                 *          - 建立到MySQL服务器的TCP套接字连接
                 *          - 执行MySQL握手协议
                 *          - 进行用户身份验证
                 * @param connection_string_ TCP连接字符串，如"tcp://localhost:3306"
                 * @param user_ 数据库用户名
                 * @param password_ 数据库密码
                 */
                LOG_DEBUG("正在连接MySQL服务器: " + connection_string_ + " (用户: " + user_ + ")");
                connection_.reset(driver_->connect(connection_string_, user_, password_));
                LOG_DEBUG("MySQL TCP连接建立成功");

                /**
                 * @brief 设置默认数据库schema
                 * @details 相当于执行"USE database_name"命令，
                 *          后续的SQL语句将在此数据库上下文中执行
                 */
                connection_->setSchema(database_);

                // ==================== 配置连接选项 ====================
                /**
                 * @brief 设置自动重连选项
                 * @details 当连接意外断开时，MySQL客户端库会自动尝试重新连接
                 *          这对于长时间运行的应用程序非常重要
                 */
                connection_->setClientOption("OPT_RECONNECT", &auto_reconnect_);

                /**
                 * @brief 设置连接建立超时时间
                 * @details 防止在网络不稳定时连接操作无限期阻塞
                 */
                connection_->setClientOption("OPT_CONNECT_TIMEOUT", &connect_timeout_);

                /**
                 * @brief 设置读操作超时时间
                 * @details 防止SELECT查询等读操作无限期等待服务器响应
                 */
                connection_->setClientOption("OPT_READ_TIMEOUT", &read_timeout_);

                /**
                 * @brief 设置写操作超时时间
                 * @details 防止INSERT/UPDATE/DELETE等写操作无限期阻塞
                 */
                connection_->setClientOption("OPT_WRITE_TIMEOUT", &write_timeout_);

                // ==================== 设置数据库字符集 ====================
                /**
                 * @brief 构建字符集设置SQL命令
                 * @details "SET NAMES charset"命令同时设置：
                 *          - character_set_client（客户端字符集）
                 *          - character_set_connection（连接字符集）
                 *          - character_set_results（结果字符集）
                 *          确保中文、emoji等多字节字符正确处理
                 */
                std::string charset_sql = "SET NAMES " + charset_;

                /**
                 * @brief 创建SQL语句执行器并设置字符集
                 * @details 使用RAII智能指针管理Statement生命周期，
                 *          确保资源正确释放
                 */
                std::unique_ptr<sql::Statement> stmt(connection_->createStatement());
                stmt->execute(charset_sql);

                // ==================== 连接成功后的清理工作 ====================
                /**
                 * @brief 更新连接最后使用时间
                 * @details 用于连接池的空闲时间管理和健康检查
                 */
                updateLastUsed();

                /**
                 * @brief 记录连接成功日志
                 * @details 包含连接ID便于问题追踪和调试
                 */
                LOG_DEBUG("MySQL connection established: " + connection_id_);

                return true;

            } catch (sql::SQLException& e) {
                // ==================== 异常处理 ====================
                /**
                 * @brief 捕获MySQL连接异常
                 * @details sql::SQLException包含详细的错误信息：
                 *          - 错误代码（如1045: Access denied）
                 *          - 错误消息（如"Unknown database 'test'"）
                 *          - SQL状态码（如"28000"表示认证失败）
                 *
                 * 常见连接失败原因：
                 * - 网络不可达（连接超时）
                 * - 认证失败（用户名/密码错误）
                 * - 数据库不存在
                 * - 权限不足
                 * - 服务器连接数已满
                 */
                // 特别处理文件描述符耗尽错误 (errno 24: Too many open files)
                std::string error_msg = e.what();
                if (error_msg.find("Can't create TCP/IP socket (24)") != std::string::npos) {
                    LOG_ERROR("❌ MySQL连接失败：系统文件描述符已耗尽 (errno 24: Too many open files)");
                    LOG_ERROR("   错误详情: " + error_msg);
                    LOG_ERROR("   建议解决方案:");
                    LOG_ERROR("   1. 检查系统限制: ulimit -n");
                    LOG_ERROR("   2. 临时增加限制: ulimit -n 65535");
                    LOG_ERROR("   3. 永久修改 /etc/security/limits.conf");
                    LOG_ERROR("   4. 减小配置中的 mysql.pool.initial_size 值");
                } else {
                    LOG_ERROR("MySQL connection failed: " + error_msg);
                }
                return false;
            }
        }

        void MySQLConnection::disconnect() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (connection_ && !connection_->isClosed()) {
                try {
                    connection_->close();
                    LOG_DEBUG("MySQL connection closed: " + connection_id_);
                } catch (sql::SQLException& e) {
                    LOG_WARNING("Error closing MySQL connection: " + std::string(e.what()));
                }
            }
            connection_.reset();
        }

        bool MySQLConnection::isConnected() const {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            return connection_ && !connection_->isClosed();
        }

        // 内部使用的无锁版本，避免死锁
        bool MySQLConnection::isConnectedNoLock() const {
            return connection_ && !connection_->isClosed();
        }

        bool MySQLConnection::ping() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!connection_ || connection_->isClosed()) {
                LOG_ERROR("The database connection is disconnected, the connection ID is: " + connection_id_);
                return false;
            }

            try {
                // 执行简单查询测试连接
                std::unique_ptr<sql::Statement> stmt(connection_->createStatement());
                std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
                updateLastUsed();
                return true;
            } catch (sql::SQLException& e) {
                LOG_WARNING("MySQL ping failed: " + std::string(e.what()));
                return false;
            }
        }

        bool MySQLConnection::isValid() const {
            if (!isConnected()) {
                LOG_ERROR("The database connection is disconnected, the connection ID is: " + connection_id_);
                return false;
            }

            // 检查连接是否在合理时间内使用过
            auto now = std::chrono::system_clock::now();
            auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(now - last_used_);

            return idle_time.count() < 30; // 30分钟内使用过认为有效
        }

        std::unique_ptr<sql::ResultSet> MySQLConnection::executeQuery(const std::string &sql) {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                LOG_ERROR("The database connection is disconnected, the connection ID is: " + connection_id_);
                throw std::runtime_error("Connection is not available");
            }

            try {
                LOG_DEBUG("执行MySQL查询: " + sql);
                std::unique_ptr<sql::Statement> stmt(connection_->createStatement());
                updateLastUsed();
                auto result = std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql));
                LOG_DEBUG("MySQL查询执行成功");
                return result;
            } catch (sql::SQLException& e) {
                LOG_ERROR("MySQL query failed: " + std::string(e.what()) + ", SQL: " + sql);
                throw;
            }
        }

        int MySQLConnection::executeUpdate(const std::string &sql) {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                LOG_ERROR("The database connection is disconnected, the connection ID is: " + connection_id_);
                throw std::runtime_error("Connection is not available");
            }

            try {
                std::unique_ptr<sql::Statement> stmt(connection_->createStatement());
                updateLastUsed();
                return stmt->executeUpdate(sql);
            } catch (sql::SQLException& e) {
                LOG_ERROR("MySQL update failed: " + std::string(e.what()) + ", SQL: " + sql);
                throw;
            }
        }

        std::unique_ptr<sql::PreparedStatement> MySQLConnection::prepareStatement(const std::string &sql) {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                LOG_ERROR("The database connection is disconnected, the connection ID is: " + connection_id_);
                throw std::runtime_error("Connection is not available");
            }

            try {
                updateLastUsed();
                return std::unique_ptr<sql::PreparedStatement>(connection_->prepareStatement(sql));
            } catch (sql::SQLException& e) {
                LOG_ERROR("MySQL prepare statement failed: " + std::string(e.what()) + ", SQL: " + sql);
                throw;
            }
        }

        void MySQLConnection::beginTransaction() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                throw std::runtime_error("Connection is not available");
            }

            try {
                connection_->setAutoCommit(false);
                updateLastUsed();
                LOG_DEBUG("Transaction started: " + connection_id_);
            } catch (sql::SQLException& e) {
                LOG_ERROR("Begin transaction failed: " + std::string(e.what()));
                throw;
            }
        }

        void MySQLConnection::commit() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                throw std::runtime_error("Connection is not available");
            }

            try {
                connection_->commit();
                connection_->setAutoCommit(true);
                updateLastUsed();
                LOG_DEBUG("Transaction committed: " + connection_id_);
            } catch (sql::SQLException& e) {
                LOG_ERROR("Commit failed: " + std::string(e.what()));
                throw;
            }
        }

        void MySQLConnection::rollback() {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                throw std::runtime_error("Connection is not available");
            }

            try {
                connection_->rollback();
                connection_->setAutoCommit(true);
                updateLastUsed();
                LOG_DEBUG("Transaction rolled back: " + connection_id_);
            } catch (sql::SQLException& e) {
                LOG_ERROR("Rollback failed: " + std::string(e.what()));
                throw;
            }
        }

        void MySQLConnection::setAutoCommit(bool autoCommit) {
            std::lock_guard<std::mutex> lock(connection_mutex_);

            if (!isConnectedNoLock()) {  // 使用无锁版本避免死锁
                throw std::runtime_error("Connection is not available");
            }

            try {
                connection_->setAutoCommit(autoCommit);
                updateLastUsed();
            } catch (sql::SQLException& e) {
                LOG_ERROR("Set auto commit failed: " + std::string(e.what()));
                throw;
            }
        }

        void MySQLConnection::generateConnectionId() {
            // 生成唯一的连接ID
            static std::atomic<uint64_t> counter{0};
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            connection_id_ = "mysql_conn_" + std::to_string(timestamp) + "_" + std::to_string(counter.fetch_add(1));
        }

        std::string MySQLConnection::buildConnectionString() {
            return "tcp://" + host_ + ":" + std::to_string(port_);
        }

        // MySQLPool实现
        MySQLPool::MySQLPool(common::config::DatabaseConfig config) : manager_config_(config), running_(false) {
            // 验证配置
            manager_config_.validate();

            // 初始化线程池
            if (manager_config_.enable_async_operations) {
                initializeThreadPool();
            }
        }

        MySQLPool::MySQLPool() : MySQLPool(common::config::DatabaseConfig::fromConfigManager())  {
            LOG_INFO("MySQL连接池初始化，配置来源：ConfigManager");
        }

        MySQLPool::~MySQLPool() {
            try {
                stop();
                // shutdownThreadPool(); // 已经在stop()中调用了

                // 如果健康检查线程仍然joinable，强制detach
                if (health_check_thread_.joinable()) {
                    health_check_thread_.detach();
                }

                // 使用标准输出，避免在析构时使用日志系统
                std::cout << "[MySQLPool] MySQLPool destroyed" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[MySQLPool] Exception in destructor: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[MySQLPool] Unknown exception in destructor" << std::endl;
            }
        }

        std::shared_ptr<MySQLConnection> MySQLPool::getConnection(std::chrono::milliseconds timeout) {
            LOG_DEBUG("开始获取MySQL连接，超时时间: " + std::to_string(timeout.count()) + "ms");

            if (!running_.load()) {
                LOG_ERROR("MySQL连接池未运行，无法获取连接");
                throw std::runtime_error("MySQL connection pool is not running");
            }

            std::shared_ptr<MySQLConnection> conn;
            bool need_create_connection = false;
            LOG_DEBUG("连接池状态检查通过，开始获取连接");

            {
                std::unique_lock<std::mutex> lock(pool_mutex_);

                auto deadline = std::chrono::steady_clock::now() + timeout;

                // 等待可用连接或可以创建新连接
                LOG_DEBUG("检查连接可用性 - 空闲连接: " + std::to_string(idle_connections_.size()) +
                        ", 活跃连接: " + std::to_string(active_connections_.size()) +
                        ", 最大连接数: " + std::to_string(manager_config_.mysql_max_pool_size));

                while (idle_connections_.empty() && active_connections_.size() >= manager_config_.mysql_max_pool_size) {
                    LOG_WARNING("需要等待可用连接... 空闲连接: " + std::to_string(idle_connections_.size()) +
                             ", 活跃连接: " + std::to_string(active_connections_.size()) +
                             ", 最大连接数: " + std::to_string(manager_config_.mysql_max_pool_size));

                    if (condition_.wait_until(lock, deadline) == std::cv_status::timeout) {
                        LOG_ERROR("获取MySQL连接超时 - 空闲连接: " + std::to_string(idle_connections_.size()) +
                                 ", 活跃连接: " + std::to_string(active_connections_.size()) +
                                 ", 最大连接数: " + std::to_string(manager_config_.mysql_max_pool_size));
                        throw std::runtime_error("Connection pool timeout");
                    }
                }

                if (!idle_connections_.empty()) {
                    conn = idle_connections_.front();
                    idle_connections_.pop();
                    LOG_DEBUG("从连接池获取空闲连接，剩余空闲连接: " + std::to_string(idle_connections_.size()));
                } else if (active_connections_.size() < manager_config_.mysql_max_pool_size) {
                    // 修复死锁风险：使用"预留槽位"模式
                    // 标记需要创建连接，但不在这里释放锁
                    need_create_connection = true;
                    LOG_INFO("需要创建新的MySQL连接，当前活跃连接: " + std::to_string(active_connections_.size()));
                }

                // 如果获取到空闲连接，直接加入活跃集合
                if (conn && !need_create_connection) {
                    active_connections_.insert(conn);
                    conn->updateLastUsed();
                    LOG_DEBUG("成功获取MySQL连接，当前活跃连接数: " + std::to_string(active_connections_.size()));
                }
            } // 释放pool_mutex_

            // 修复：在锁外创建连接，避免死锁
            if (need_create_connection && !conn) {
                LOG_INFO("在锁外创建新的MySQL连接");
                conn = createConnection();

                if (!conn) {
                    LOG_ERROR("创建新MySQL连接失败");
                    throw std::runtime_error("Failed to create new MySQL connection");
                }

                // 重新获取锁，进行二次检查
                std::lock_guard<std::mutex> lock(pool_mutex_);

                // 二次检查：确保仍有空间添加连接
                if (active_connections_.size() < manager_config_.mysql_max_pool_size) {
                    active_connections_.insert(conn);
                    conn->updateLastUsed();
                    LOG_INFO("新连接创建成功并加入池，当前活跃连接数: " + std::to_string(active_connections_.size()));
                } else {
                    // 池已满（其他线程创建了连接），直接使用连接但不加入池
                    // 这种情况下连接由调用者管理，不会归还到池中
                    LOG_WARNING("连接池已满，新连接将在池外管理");
                    // 连接仍然可用，只是不被池管理
                }
            }

            // 在池锁外验证连接，避免死锁
            if (conn) {
                // 强化连接验证
                if (!validateConnection(conn)) {
                    LOG_WARNING("Retrieved invalid connection, creating new one");
                    // 归还无效连接并清空指针
                    returnConnection(conn);
                    conn.reset();

                    // 重试创建连接，最多3次
                    int retry_count = 0;
                    const int max_retries = 3;

                    while (retry_count < max_retries && !conn) {
                        retry_count++;
                        LOG_INFO("尝试创建新连接，重试次数: " + std::to_string(retry_count));
                        conn = createConnection();

                        if (conn && validateConnection(conn)) {
                            std::lock_guard<std::mutex> lock(pool_mutex_);
                            // 检查是否仍有空间
                            if (active_connections_.size() < manager_config_.mysql_max_pool_size) {
                                active_connections_.insert(conn);
                                conn->updateLastUsed();
                            }
                            LOG_INFO("重试创建连接成功");
                            break;
                        } else if (conn) {
                            LOG_WARNING("创建的连接无效，释放并重试");
                            conn.reset();
                        }

                        if (retry_count < max_retries) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100 * retry_count));
                        }
                    }

                    if (!conn) {
                        LOG_ERROR("重试" + std::to_string(max_retries) + "次后仍无法创建有效连接");
                        throw std::runtime_error("Failed to create valid MySQL connection after retries");
                    }
                }
            }

            if (conn) {
                LOG_DEBUG("连接获取成功，返回连接");
            } else {
                LOG_ERROR("连接获取失败，返回空连接");
            }
            return conn;
        }

        void MySQLPool::returnConnection(std::shared_ptr<MySQLConnection> conn) {
            if (!conn) return;

            std::lock_guard<std::mutex> lock(pool_mutex_);

            auto it = active_connections_.find(conn);
            if (it != active_connections_.end()) {
                active_connections_.erase(it);

                if (validateConnection(conn) && idle_connections_.size() < manager_config_.mysql_max_pool_size) {
                    idle_connections_.push(conn);
                }
                // 如果连接无效或池已满，连接会被自动销毁
            }

            condition_.notify_one();
        }

        size_t MySQLPool::getActiveConnections() const {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            return active_connections_.size();
        }

        size_t MySQLPool::getIdleConnections() const {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            return idle_connections_.size();
        }

        size_t MySQLPool::getTotalConnections() const {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            return active_connections_.size() + idle_connections_.size();
        }

        void MySQLPool::start() {
            if (running_.exchange(true)) {
                return; // 已经启动
            }

            // 检查并调整初始连接数，避免文件描述符耗尽
            size_t target_initial_size = manager_config_.mysql_pool_size;
            LOG_INFO("🔧 MySQL连接池配置: initial_size=" + std::to_string(target_initial_size) + 
                    ", max_size=" + std::to_string(manager_config_.mysql_max_pool_size));
            
            // 如果initial_size过大，分批创建，避免一次性占用过多文件描述符
            size_t safe_batch_size = std::min(target_initial_size, static_cast<size_t>(10));
            if (target_initial_size > 10) {
                LOG_WARNING("⚠️  initial_size(" + std::to_string(target_initial_size) + 
                           ") 过大，为避免文件描述符耗尽，将分批创建连接");
                LOG_WARNING("   第一批创建 " + std::to_string(safe_batch_size) + " 个连接，其余按需创建");
            }

            // 创建初始连接
            size_t successful_connections = 0;
            {
                std::lock_guard<std::mutex> lock(pool_mutex_);
                for (size_t i = 0; i < safe_batch_size; ++i) {
                    auto conn = createConnection();
                    if (conn) {
                        idle_connections_.push(conn);
                        successful_connections++;
                    } else {
                        LOG_WARNING("❌ 创建第 " + std::to_string(i + 1) + " 个MySQL连接失败");
                        
                        // 如果是文件描述符问题，允许部分成功
                        if (successful_connections > 0) {
                            LOG_WARNING("⚠️  检测到可能的文件描述符限制，停止继续创建");
                            LOG_WARNING("   已成功创建 " + std::to_string(successful_connections) + " 个连接，将以降级模式运行");
                            break;
                        }
                    }
                }
            }

            // 检查是否至少有一个连接成功
            if (successful_connections == 0) {
                LOG_ERROR("❌ 无法创建任何MySQL连接，连接池启动失败");
                LOG_ERROR("💡 请检查:");
                LOG_ERROR("   1. MySQL服务是否运行: systemctl status mysql");
                LOG_ERROR("   2. 系统文件描述符限制: ulimit -n");
                LOG_ERROR("   3. 配置文件中的连接信息是否正确");
                running_ = false;
                return;
            }

            // 启动健康检查线程
            health_check_thread_ = std::thread(&MySQLPool::healthCheck, this);

            LOG_INFO("✅ MySQL连接池启动成功! 当前连接数: " + std::to_string(successful_connections) +
                    "/" + std::to_string(manager_config_.mysql_max_pool_size) + " (已创建/最大)");
            
            if (successful_connections < target_initial_size) {
                LOG_WARNING("⚠️  实际创建连接数(" + std::to_string(successful_connections) + 
                           ") < 配置的initial_size(" + std::to_string(target_initial_size) + ")");
                LOG_WARNING("   可能原因:");
                LOG_WARNING("   - 系统文件描述符限制(ulimit -n 过小)");
                LOG_WARNING("   - MySQL服务器连接数限制(max_connections)");
                LOG_WARNING("   解决方案:");
                LOG_WARNING("   - 增加系统限制: ulimit -n 65535");
                LOG_WARNING("   - 减小配置: mysql.pool.initial_size");
                LOG_WARNING("   连接池将按需创建更多连接，最大: " + std::to_string(manager_config_.mysql_max_pool_size));
            }
        }

        void MySQLPool::stop() {
            if (!running_.exchange(false)) {
                return; // 已经停止
            }

            // 首先停止线程池，避免新的异步任务
            if (thread_pool_initialized_.load() && thread_pool_) {

                try {
                    thread_pool_->shutdown();
                    thread_pool_initialized_.store(false);

                } catch (const std::exception& e) {
                    LOG_ERROR("停止MySQL线程池时发生异常: " + std::string(e.what()));
                }
            }

            // 停止健康检查线程，使用激进的超时机制
            if (health_check_thread_.joinable()) {
                try {


                    // 使用很短的超时时间，快速放弃等待
                    std::atomic<bool> join_completed{false};

                    std::thread join_thread([&]() {
                        try {
                            health_check_thread_.join();
                            join_completed.store(true);
                        } catch (...) {
                            // 忽略join异常
                        }
                    });

                    // 只等待1秒
                    std::this_thread::sleep_for(std::chrono::seconds(1));

                    if (join_completed.load()) {
                        LOG_INFO("MySQL健康检查线程已正常结束");
                        if (join_thread.joinable()) {
                            join_thread.join();
                        }
                    } else {
                        LOG_WARNING("MySQL健康检查线程结束超时，强制继续");
                        if (join_thread.joinable()) {
                            join_thread.detach();
                        }
                        // 不等待健康检查线程，让析构函数处理
                    }

                } catch (const std::exception& e) {
                    LOG_ERROR("等待MySQL健康检查线程结束时发生异常: " + std::string(e.what()));
                }
            }

            // 清理所有连接
            try {
                std::lock_guard<std::mutex> lock(pool_mutex_);

                // 清空空闲连接
                while (!idle_connections_.empty()) {
                    idle_connections_.pop();
                }

                // 清空活跃连接
                active_connections_.clear();
            } catch (const std::exception& e) {
                LOG_ERROR("清理连接时发生异常: " + std::string(e.what()));
            }

            LOG_INFO("MySQL connection pool stopped");
        }

        void MySQLPool::enableConfigHotReload() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            // 监听主机地址变化
            config_manager.addChangeListener("database.mysql.host",
                [this](const auto& /* key */, const auto& old_val, const auto& new_val) {
                    LOG_WARNING("MySQL host changed from " + old_val + " to " + new_val +
                               ". Pool restart required for changes to take effect.");
                    // 可以选择标记需要重启
                    this->markForRestart();
                });

            // 监听连接池大小变化
            config_manager.addChangeListener("database.mysql.pool.max_size",
                [this](const auto& /* key */, const auto& old_val, const auto& new_val) {
                    LOG_INFO("MySQL pool max_size changed from " + old_val + " to " + new_val);
                    try {
                        size_t new_max_size = std::stoull(new_val);
                        this->adjustPoolSize(new_max_size);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to adjust pool size: " + std::string(e.what()));
                    }
                });
        }

        void MySQLPool::healthCheck() {
            long min_valid_interval = 100;

            while (running_.load()) {
                // 使用较短的睡眠间隔，以便快速响应停止信号
                auto sleep_duration = std::chrono::milliseconds(manager_config_.mysql_pool_validation_interval_ms);
                auto sleep_start = std::chrono::steady_clock::now();

                // 分段睡眠，每100ms检查一次running状态
                while (running_.load()) {
                    auto elapsed = std::chrono::steady_clock::now() - sleep_start;
                    if (elapsed >= sleep_duration) {
                        // LOG_ERROR("[healthCheck] 100ms检查出来了" + std::to_string(elapsed));
                        break;
                    }

                    auto remaining = sleep_duration - elapsed;
                    auto sleep_time = min(remaining, std::chrono::duration_cast<decltype(remaining)>(std::chrono::milliseconds(min_valid_interval)));
                    std::this_thread::sleep_for(sleep_time);
                }

                if (!running_.load()) {
                    break; // 快速退出检查
                }

                try {
                    // 如果启用了异步操作且线程池可用，使用异步健康检查
                    if (manager_config_.enable_async_operations && thread_pool_initialized_.load() && running_.load()) {
                        performAsyncHealthCheck();
                    } else if (running_.load()) {
                        // 同步健康检查，使用try_lock避免长时间阻塞
                        std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                        if (lock.owns_lock() && running_.load()) {
                            removeExpiredConnections();
                            ensureMinimumConnections();
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("健康检查过程中发生异常: " + std::string(e.what()));
                    // 发生异常时短暂休息，避免快速重试
                    if (running_.load()) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }


        }

        std::shared_ptr<MySQLConnection> MySQLPool::createConnection() {
            try {
                LOG_DEBUG("正在创建MySQL连接: " + manager_config_.mysql_host + ":" + std::to_string(manager_config_.mysql_port) +
                         "/" + manager_config_.mysql_database + " (用户: " + manager_config_.mysql_user + ")");

                // 创建连接配置
                MySQLConnection::ConnectionConfig conn_config;
                conn_config.auto_reconnect = manager_config_.mysql_auto_reconnect;
                conn_config.charset = manager_config_.mysql_charset;
                conn_config.connect_timeout = manager_config_.mysql_connect_timeout_ms;
                conn_config.read_timeout = manager_config_.mysql_read_timeout_ms;
                conn_config.write_timeout = manager_config_.mysql_write_timeout_ms;

                auto conn = std::make_shared<MySQLConnection>(
                    manager_config_.mysql_host, manager_config_.mysql_port, manager_config_.mysql_user, manager_config_.mysql_password, manager_config_.mysql_database, conn_config);

                if (conn->connect()) {
                    LOG_DEBUG("Created new MySQL connection successfully");
                    // 立即测试连接
                    if (conn->ping()) {
                        LOG_DEBUG("MySQL连接ping测试成功");
                        return conn;
                    } else {
                        LOG_ERROR("MySQL连接ping测试失败");
                        return nullptr;
                    }
                } else {
                    LOG_ERROR("Failed to create MySQL connection to " + manager_config_.mysql_host + ":" +
                             std::to_string(manager_config_.mysql_port) + "/" + manager_config_.mysql_database);
                    LOG_ERROR("💡 提示：如果错误是 'Can't create TCP/IP socket (24)'，这是系统文件描述符限制问题");
                    return nullptr;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Exception creating MySQL connection to " + manager_config_.mysql_host + ":" +
                         std::to_string(manager_config_.mysql_port) + ": " + std::string(e.what()));
                return nullptr;
            }
        }

        void MySQLPool::removeExpiredConnections() {
            auto now = std::chrono::system_clock::now();

            // 检查空闲连接
            std::queue<std::shared_ptr<MySQLConnection>> valid_connections;

            while (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();

                if (conn && validateConnection(conn)) {
                    // 检查是否超过最大空闲时间
                    auto idle_time = (now - conn->getLastUsed()).count();
                    if (idle_time <= manager_config_.mysql_pool_max_idle_time_ms) {
                        valid_connections.push(conn);
                    } else {
                        LOG_DEBUG("Removed expired idle connection");
                    }
                } else {
                    LOG_DEBUG("Removed invalid idle connection");
                }
            }

            // 将有效连接放回队列
            idle_connections_ = std::move(valid_connections);

            // ⭐⭐⭐ 修复：不主动移除活跃连接，避免与正在使用的连接冲突
            // 活跃连接由 returnConnection() 时自然验证，无效连接不会放回池中
            // 这样可以避免在用户代码正在使用连接时被移除
            
            // 只记录活跃连接数量，不进行移除操作
            if (active_connections_.size() > 0) {
                LOG_DEBUG("当前有 " + std::to_string(active_connections_.size()) + " 个活跃连接");
            }
        }

        void MySQLPool::ensureMinimumConnections() {
            size_t current_total = active_connections_.size() + idle_connections_.size();

            if (current_total < manager_config_.mysql_min_pool_size) {
                size_t needed = manager_config_.mysql_min_pool_size - current_total;

                // 限制一次创建的连接数，避免超过最大连接数
                needed = std::min(needed, manager_config_.mysql_max_pool_size - current_total);

                if (needed == 0) {
                    return; // 已达到最大连接数
                }

                // ⭐⭐⭐ 关键修复：限制单次创建数量，避免阻塞线程池
                // 每次最多创建2个连接，剩余的由下次健康检查补充
                size_t create_now = std::min(needed, static_cast<size_t>(2));
                
                LOG_DEBUG("需要创建 " + std::to_string(needed) + " 个连接，本次创建 " + 
                         std::to_string(create_now) + " 个以维持最小连接池大小");

                // ⭐⭐⭐ 关键修复：始终使用同步方式创建连接，避免在健康检查任务中再提交异步任务
                // 这可以防止任务递归提交导致的队列堆积问题
                for (size_t i = 0; i < create_now; ++i) {
                    auto conn = createConnection();
                    if (conn) {
                        idle_connections_.push(conn);
                        current_total++;
                        LOG_DEBUG("Added connection to maintain minimum pool size (" +
                                 std::to_string(i + 1) + "/" + std::to_string(create_now) + ")");
                    } else {
                        LOG_WARNING("Failed to create connection for minimum pool size (" +
                                   std::to_string(i + 1) + "/" + std::to_string(create_now) + ")");
                        break;
                    }
                }
            }
        }

        bool MySQLPool::validateConnection(std::shared_ptr<MySQLConnection> conn) {
            if (!conn || !conn->isConnected()) {
                LOG_DEBUG("连接验证失败：连接为空或未连接");
                return false;
            }

            // 检查连接是否超时
            auto now = std::chrono::system_clock::now();
            auto idle_time = (now - conn->getLastUsed()).count();

            if (idle_time > manager_config_.mysql_pool_max_idle_time_ms) {
                LOG_DEBUG("连接验证失败：连接空闲时间超时");
                return false;
            }

            // 尝试ping测试连接
            try {
                if (!conn->ping()) {
                    LOG_DEBUG("连接验证失败：ping测试失败");
                    return false;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("连接验证失败：ping测试异常: " + std::string(e.what()));
                return false;
            }

            return true;
        }

        void MySQLPool::markForRestart() {
            restart_required_ = true;
            LOG_INFO("Redis pool marked for restart" );

            // 立即触发重启检查（异步执行）
            scheduleRestartCheck();
        }

        void MySQLPool::scheduleRestartCheck() {
            if (restart_thread_pool_) {
                restart_thread_pool_->submit([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 延迟500ms，避免频繁重启
                    checkAndHandleRestart();
                });
            } else {
                // 如果没有线程池，创建临时线程
                std::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    checkAndHandleRestart();
                }).detach();
            }
        }

        void MySQLPool::checkAndHandleRestart() {
            if (!restart_required_.load()) {
                return;
            }

            LOG_INFO("Starting MySQL pool restart process" );

            try {
                // 1. 停止接受新的连接请求
                setAcceptingNewConnections(false);

                // 2. 等待现有连接完成当前事务
                waitForActiveConnectionsToFinish();

                // 3. 关闭所有现有连接
                closeAllConnections();

                // 4. 重新加载配置
                reloadConfiguration();

                // 5. 重新初始化连接池
                if (!reinitializePool()) {
                    LOG_ERROR("Failed to reinitialize MySQL pool" );
                    return;
                }

                // 6. 恢复接受新连接
                setAcceptingNewConnections(true);

                // 7. 重置重启标志
                restart_required_ = false;

                LOG_INFO("MySQL pool restart completed successfully" );

            } catch (const std::exception& e) {
                LOG_ERROR("MySQL pool restart failed: " + std::string(e.what()) );

                // 重启失败，稍后重试
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (restart_required_.load()) {
                    LOG_INFO("Retrying MySQL pool restart" );
                    checkAndHandleRestart();
                }
            }
        }

        void MySQLPool::setAcceptingNewConnections(bool accepting) {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            accepting_new_connections_ = accepting;

            if (accepting) {
                LOG_INFO("MySQL pool now accepting new connections" );
            } else {
                LOG_INFO("MySQL pool stopped accepting new connections" );
            }

        }

        void MySQLPool::waitForActiveConnectionsToFinish() {
            LOG_INFO("Waiting for active MySQL transactions to finish" );

            const int max_wait_seconds = 60; // MySQL事务可能较长，等待60秒
            const int check_interval_ms = 200;

            for (int i = 0; i < max_wait_seconds * 1000 / check_interval_ms; ++i) {
                {
                    std::lock_guard<std::mutex> lock(pool_mutex_);
                    if (active_connections_.empty()) {
                        LOG_INFO("All active MySQL transactions finished" );
                        return;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            }

            LOG_WARNING("Timeout waiting for MySQL transactions to finish, proceeding with restart" );
        }

        void MySQLPool::closeAllConnections() {
            std::lock_guard<std::mutex> lock(pool_mutex_);

            LOG_INFO("Closing all MySQL connections" );

            // 关闭空闲连接
            while (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();
                if (conn) {
                    // 回滚任何未提交的事务
                    try {
                        conn->rollback();
                    } catch (...) {
                        // 忽略回滚错误
                    }
                    conn->disconnect();
                }
            }

            // 关闭活跃连接
            for (auto& conn : active_connections_) {
                if (conn) {
                    try {
                        conn->rollback();
                    } catch (...) {
                        // 忽略回滚错误
                    }
                    conn->disconnect();
                }
            }
            active_connections_.clear();

            LOG_INFO("All MySQL connections closed" );
        }

        void MySQLPool::reloadConfiguration() {
            LOG_INFO("Reloading MySQL configuration" );

            try {
                auto new_config = common::config::DatabaseConfig::fromConfigManager();
                new_config.validate();

                manager_config_ = new_config;

                LOG_INFO("MySQL configuration reloaded successfully" );
                LOG_DEBUG("New MySQL host: " + manager_config_.mysql_host + ":" + std::to_string(manager_config_.mysql_port) );

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to reload MySQL configuration: " + std::string(e.what()) );
                throw;
            }
        }

        bool MySQLPool::reinitializePool() {
            std::lock_guard<std::mutex> lock(pool_mutex_);

            try {
                LOG_INFO("Reinitializing MySQL pool with new configuration" );

                // 创建初始连接
                for (size_t i = 0; i < manager_config_.mysql_pool_size; ++i) {
                    auto conn = createConnection();
                    if (!conn || !conn->connect()) {
                        LOG_ERROR("Failed to create initial MySQL connection " + std::to_string(i + 1) );
                        return false;
                    }
                    idle_connections_.push(conn);
                }

                LOG_INFO("MySQL pool reinitialized with " + std::to_string(manager_config_.mysql_pool_size) + " connections" );
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception while reinitializing MySQL pool: " + std::string(e.what()) );
                return false;
            }
        }

        void MySQLPool::adjustPoolSize(size_t new_max_size) {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (new_max_size > manager_config_.mysql_max_pool_size) {
                manager_config_.mysql_max_pool_size = new_max_size;
                LOG_INFO("MySQL pool max_size increased to " + std::to_string(new_max_size) );
            }
        }

        // ==================== 线程池相关方法实现 ====================

        void MySQLPool::initializeThreadPool() {
            if (thread_pool_initialized_.load()) {
                return; // 已经初始化
            }

            if (!manager_config_.enable_async_operations) {
                LOG_INFO("MySQL连接池异步操作未启用，跳过线程池初始化");
                return;
            }

            try {
                // 创建主线程池配置
                common::config::ThreadPoolConfig main_config;
                main_config.core_pool_size = manager_config_.thread_pool_core_size;
                main_config.maximum_pool_size = manager_config_.thread_pool_max_size;
                main_config.keep_alive_time_ms = manager_config_.thread_pool_keep_alive_ms;
                main_config.queue_capacity = manager_config_.thread_pool_queue_capacity;
                main_config.enable_monitoring = manager_config_.thread_pool_enable_monitoring;
                main_config.thread_name_prefix = "mysql-pool-";

                thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(main_config);

                // 创建重启专用线程池配置（较小的线程池）
                common::config::ThreadPoolConfig restart_config;
                restart_config.core_pool_size = 1;
                restart_config.maximum_pool_size = 2;
                restart_config.queue_capacity = 10;
                restart_config.keep_alive_time_ms = 30000;
                restart_config.thread_name_prefix = "mysql-restart-";

                restart_thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(restart_config);

                thread_pool_initialized_ = true;
                LOG_INFO("MySQL - 主线程池(" +
                        std::to_string(manager_config_.thread_pool_core_size) + "/" +
                        std::to_string(manager_config_.thread_pool_max_size) + ")");

            } catch (const std::exception& e) {
                LOG_ERROR("MySQL连接池线程池初始化失败: " + std::string(e.what()));
                thread_pool_initialized_ = false;
                throw;
            }
        }

        void MySQLPool::shutdownThreadPool() {
            if (!thread_pool_initialized_.load()) {
                return; // 未初始化或已关闭
            }

            try {
                LOG_INFO("开始关闭MySQL连接池线程池");

                // 关闭主线程池
                if (thread_pool_) {
                    thread_pool_->shutdown();
                    thread_pool_.reset();
                }

                // 关闭重启线程池
                if (restart_thread_pool_) {
                    restart_thread_pool_->shutdown();
                    restart_thread_pool_.reset();
                }

                thread_pool_initialized_ = false;
                LOG_INFO("MySQL连接池线程池已关闭");

            } catch (const std::exception& e) {
                LOG_ERROR("关闭MySQL连接池线程池时发生错误: " + std::string(e.what()));
            }
        }

        std::future<std::vector<std::shared_ptr<MySQLConnection>>> MySQLPool::createConnectionsAsync(size_t count) {
            if (!thread_pool_initialized_.load() || !thread_pool_ || !running_.load()) {
                // 如果线程池未初始化，返回一个立即完成的future
                std::promise<std::vector<std::shared_ptr<MySQLConnection>>> promise;
                std::vector<std::shared_ptr<MySQLConnection>> connections;

                // 同步创建连接
                for (size_t i = 0; i < count; ++i) {
                    auto conn = createConnection();
                    if (conn) {
                        connections.push_back(conn);
                    }
                }

                promise.set_value(std::move(connections));
                return promise.get_future();
            }

            // 使用线程池异步创建连接
            auto promise = std::make_shared<std::promise<std::vector<std::shared_ptr<MySQLConnection>>>>();
            auto future = promise->get_future();

            try {
                thread_pool_->submit([this, count, promise]() {
                    std::vector<std::shared_ptr<MySQLConnection>> connections;
                    connections.reserve(count);

                    LOG_DEBUG("开始异步创建 " + std::to_string(count) + " 个MySQL连接");

                    for (size_t i = 0; i < count; ++i) {
                        try {
                            auto conn = createConnection();
                            if (conn) {
                                connections.push_back(conn);
                                LOG_DEBUG("异步创建MySQL连接成功 (" + std::to_string(i + 1) + "/" + std::to_string(count) + ")");
                            } else {
                                LOG_WARNING("异步创建MySQL连接失败 (" + std::to_string(i + 1) + "/" + std::to_string(count) + ")");
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("异步创建MySQL连接异常: " + std::string(e.what()));
                        }
                    }

                    LOG_INFO("异步创建MySQL连接完成，成功创建 " + std::to_string(connections.size()) + "/" + std::to_string(count) + " 个连接");
                    promise->set_value(std::move(connections));
                });
            } catch (const std::exception& e) {
                LOG_WARNING("异步连接创建任务提交失败，回退到同步模式: " + std::string(e.what()));
                // 回退到同步创建
                std::vector<std::shared_ptr<MySQLConnection>> connections;
                for (size_t i = 0; i < count; ++i) {
                    auto conn = createConnection();
                    if (conn) {
                        connections.push_back(conn);
                    }
                }
                auto promise_ptr = std::make_shared<std::promise<std::vector<std::shared_ptr<MySQLConnection>>>>();
                promise_ptr->set_value(std::move(connections));
                return promise_ptr->get_future();
            }

            return future;
        }

        // ⭐⭐⭐ 静态原子标志，用于防止重复提交健康检查任务
        static std::atomic<bool> s_health_check_in_progress{false};
        
        void MySQLPool::performAsyncHealthCheck() {
            if (!thread_pool_initialized_.load() || !thread_pool_ || !running_.load()) {
                // 如果线程池未初始化或连接池已停止，直接返回，不执行任何检查
                return;
            }

            // ⭐⭐⭐ 关键修复：使用原子标志防止重复提交健康检查任务
            // 如果已有健康检查任务在执行，跳过本次
            bool expected = false;
            if (!s_health_check_in_progress.compare_exchange_strong(expected, true)) {
                LOG_DEBUG("已有健康检查任务在执行，跳过本次提交");
                return;
            }

            LOG_DEBUG("开始提交异步健康检查任务");

            // 异步执行健康检查
            try {
                thread_pool_->submit([this]() {
                    // ⭐⭐⭐ 使用RAII确保标志在任务完成后重置
                    struct HealthCheckGuard {
                        ~HealthCheckGuard() { s_health_check_in_progress.store(false); }
                    };
                    HealthCheckGuard guard;
                    
                    try {
                        // 在异步任务开始时再次检查running状态
                        if (!running_.load()) {
                            return;
                        }

                        LOG_DEBUG("开始异步MySQL连接健康检查");

                        // ⭐⭐⭐ 修复死锁：复制连接快照后立即释放锁
                        std::vector<std::shared_ptr<MySQLConnection>> all_connections;
                        {
                            std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                            if (!lock.owns_lock() || !running_.load()) {
                                // 无法获取锁或连接池已停止，直接返回
                                LOG_DEBUG("无法获取pool_mutex_或连接池已停止，跳过本次健康检查");
                                return;
                            }

                            // 复制空闲连接
                            std::queue<std::shared_ptr<MySQLConnection>> temp_queue = idle_connections_;
                            while (!temp_queue.empty()) {
                                all_connections.push_back(temp_queue.front());
                                temp_queue.pop();
                            }

                            // ⭐⭐⭐ 不再复制活跃连接，活跃连接由用户代码负责
                            // 只检查空闲连接，避免与正在使用的连接冲突
                        }  // ⭐⭐⭐ lock在这里释放！不再持有pool_mutex_

                        // ⭐⭐⭐ 限制检查数量，避免长时间占用线程
                        size_t max_check_count = std::min(all_connections.size(), static_cast<size_t>(10));
                        size_t healthy_count = 0;
                        
                        for (size_t i = 0; i < max_check_count && running_.load(); ++i) {
                            auto& conn = all_connections[i];
                            try {
                                if (validateConnection(conn)) {
                                    healthy_count++;
                                }
                            } catch (const std::exception& e) {
                                LOG_WARNING("连接健康检查异常: " + std::string(e.what()));
                            }
                        }

                        LOG_DEBUG("异步健康检查完成，健康连接: " + std::to_string(healthy_count) + 
                                 "/" + std::to_string(max_check_count));

                        // ✅ 重新获取锁，执行清理和维护
                        {
                            std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                            if (!lock.owns_lock() || !running_.load()) {
                                return;
                            }
                            
                            // 执行清理和维护（这些操作需要持有锁）
                            removeExpiredConnections();
                            ensureMinimumConnections();
                        }  // ⭐⭐⭐ 再次释放锁

                    } catch (const std::exception& e) {
                        LOG_ERROR("异步健康检查失败: " + std::string(e.what()));
                    }
                });
            } catch (const std::exception& e) {
                // 提交失败时重置标志
                s_health_check_in_progress.store(false);
                LOG_WARNING("异步健康检查任务提交失败，回退到同步模式: " + std::string(e.what()));
                
                // 回退到同步模式（也要避免长时间持锁）
                try {
                    std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                    if (lock.owns_lock() && running_.load()) {
                        removeExpiredConnections();
                        ensureMinimumConnections();
                    }
                } catch (const std::exception& ex) {
                    LOG_ERROR("同步健康检查失败: " + std::string(ex.what()));
                }
            }
        }

        void MySQLPool::cleanupExpiredConnectionsAsync() {
            if (!thread_pool_initialized_.load() || !thread_pool_ || !running_.load()) {
                // 如果线程池未初始化或连接池已停止，执行同步清理
                std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                if (lock.owns_lock()) {
                    removeExpiredConnections();
                }
                return;
            }

            // 异步执行连接清理
            try {
                thread_pool_->submit([this]() {
                    try {
                        if (!running_.load()) {
                            return;
                        }
                        
                        LOG_DEBUG("开始异步清理过期MySQL连接");

                        auto start_time = std::chrono::steady_clock::now();
                        size_t initial_idle_count = 0;
                        size_t cleaned_count = 0;

                        // ⭐⭐⭐ 获取锁后执行所有操作
                        {
                            std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                            if (!lock.owns_lock() || !running_.load()) {
                                LOG_DEBUG("无法获取锁或连接池已停止，跳过清理");
                                return;
                            }
                            
                            initial_idle_count = idle_connections_.size();
                            
                            // 执行清理
                            removeExpiredConnections();
                            
                            cleaned_count = initial_idle_count - idle_connections_.size();
                            
                            // 确保最小连接数（在持有锁的情况下）
                            ensureMinimumConnections();
                        }  // 释放锁

                        auto end_time = std::chrono::steady_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                        if (cleaned_count > 0) {
                            LOG_INFO("异步连接清理完成，耗时: " + std::to_string(duration.count()) + "ms, " +
                                    "清理连接数: " + std::to_string(cleaned_count));
                        }

                    } catch (const std::exception& e) {
                        LOG_ERROR("异步连接清理失败: " + std::string(e.what()));
                    }
                });
            } catch (const std::exception& e) {
                LOG_WARNING("异步连接清理任务提交失败，回退到同步模式: " + std::string(e.what()));
                std::unique_lock<std::mutex> lock(pool_mutex_, std::try_to_lock);
                if (lock.owns_lock()) {
                    removeExpiredConnections();
                }
            }
        }

        std::string MySQLPool::getThreadPoolStatus() const {
            if (!thread_pool_initialized_.load()) {
                return "线程池未初始化";
            }

            std::string status = "MySQL连接池线程池状态:\n";

            if (thread_pool_) {
                status += "  主线程池:\n";
                status += "    活跃线程: " + std::to_string(thread_pool_->getActiveThreadCount()) + "\n";
                status += "    总线程数: " + std::to_string(thread_pool_->getTotalThreadCount()) + "\n";
                status += "    队列大小: " + std::to_string(thread_pool_->getQueueSize()) + "\n";
            }

            if (restart_thread_pool_) {
                status += "  重启线程池:\n";
                status += "    活跃线程: " + std::to_string(restart_thread_pool_->getActiveThreadCount()) + "\n";
                status += "    总线程数: " + std::to_string(restart_thread_pool_->getTotalThreadCount()) + "\n";
                status += "    队列大小: " + std::to_string(restart_thread_pool_->getQueueSize()) + "\n";
            }

            return status;
        }
    }
}