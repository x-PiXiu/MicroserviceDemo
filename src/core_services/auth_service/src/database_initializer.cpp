/**
 * @file database_initializer.cpp
 * @brief 数据库初始化器实现 - 负责检查和创建数据库表结构
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 */

#include "database_initializer.h"
#include "common/logger/logger.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace core_services {
    namespace auth_service {

        /**
         * @brief 构造函数
         * @param mysql_pool MySQL连接池
         */
        DatabaseInitializer::DatabaseInitializer(std::shared_ptr<common::database::MySQLPool> mysql_pool)
            : mysql_pool_(mysql_pool) {
            initializeTableDefinitions();
        }

        /**
         * @brief 初始化数据库
         * @param force_recreate 是否强制重新创建表（危险操作）
         * @return 初始化结果
         */
        InitializationResult DatabaseInitializer::initializeDatabase(bool force_recreate) {
            auto start_time = std::chrono::high_resolution_clock::now();
            InitializationResult result;

            try {
                logInitialization("=== 开始数据库初始化 ===");

                // 1. 检查MySQL连接
                if (!mysql_pool_ || !mysql_pool_->isRunning()) {
                    result.errors.push_back("MySQL连接池未初始化或未运行");
                    return result;
                }

                // 2. 创建数据库（如果不存在）
                if (!createDatabaseIfNotExists("auth_sessions_db")) {
                    result.errors.push_back("创建数据库失败");
                    return result;
                }

                // 3. 按依赖顺序获取表创建列表
                std::vector<std::string> table_creation_order = getTableCreationOrder();

                // 4. 临时禁用外键检查（确保表创建顺序不受影响）
                logInitialization("临时禁用外键检查");
                executeSQL("SET FOREIGN_KEY_CHECKS = 0");

                // 5. 检查现有表和创建缺失的表
                for (const std::string& table_name : table_creation_order) {
                    const auto& table_info = table_definitions_.at(table_name);

                    if (checkTableExists(table_name)) {
                        result.existing_tables.push_back(table_name);
                        logInitialization("表 " + table_name + " 已存在");

                        if (force_recreate) {
                            logInitialization("强制重新创建表: " + table_name, "WARNING");
                            if (!executeSQL("DROP TABLE IF EXISTS " + table_name)) {
                                result.errors.push_back("删除表 " + table_name + " 失败");
                                continue;
                            }
                        } else {
                            // 验证表结构
                            if (!validateTableStructure(table_name)) {
                                result.errors.push_back("表 " + table_name + " 结构验证失败");
                            }
                            continue;
                        }
                    }

                    // 5. 创建缺失的表
                    if (!checkTableExists(table_name) || force_recreate) {
                        logInitialization("创建表: " + table_name);
                        if (createTable(table_info)) {
                            result.created_tables.push_back(table_name);
                            logInitialization("表 " + table_name + " 创建成功");
                        } else {
                            result.errors.push_back("创建表 " + table_name + " 失败");
                        }
                    }
                }

                // 6. 重新启用外键检查
                logInitialization("重新启用外键检查");
                executeSQL("SET FOREIGN_KEY_CHECKS = 1");

                // 7. 插入初始数据（仅在有新表创建时）
                if (!result.created_tables.empty()) {
                    logInitialization("插入初始数据");
                    if (!insertInitialData()) {
                        result.errors.push_back("插入初始数据失败");
                    }
                }

                // 8. 更新数据库版本
                if (!updateDatabaseVersion("1.0.0")) {
                    result.errors.push_back("更新数据库版本失败");
                }

                result.success = result.errors.empty();

                auto end_time = std::chrono::high_resolution_clock::now();
                result.execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                if (result.success) {
                    logInitialization("=== 数据库初始化完成 ===");
                    logInitialization("创建表数量: " + std::to_string(result.created_tables.size()));
                    logInitialization("现有表数量: " + std::to_string(result.existing_tables.size()));
                    logInitialization("执行时间: " + std::to_string(result.execution_time.count()) + "ms");
                } else {
                    logInitialization("=== 数据库初始化失败 ===", "ERROR");
                    for (const auto& error : result.errors) {
                        logInitialization("错误: " + error, "ERROR");
                    }
                }

            } catch (const std::exception& e) {
                result.success = false;
                result.errors.push_back("数据库初始化异常: " + std::string(e.what()));
                logInitialization("数据库初始化异常: " + std::string(e.what()), "ERROR");
            }

            return result;
        }

        /**
         * @brief 检查数据库表是否存在
         * @param table_name 表名
         * @return 存在返回true
         */
        bool DatabaseInitializer::checkTableExists(const std::string& table_name) {
            try {
                logInitialization("检查表是否存在: " + table_name, "DEBUG");

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    logInitialization("无法获取MySQL连接", "ERROR");
                    return false;
                }

                // 使用简单的SQL查询，避免预处理语句的复杂性
                std::string sql = "SELECT COUNT(*) FROM information_schema.tables "
                                 "WHERE table_schema = 'auth_sessions_db' AND table_name = '" + table_name + "'";

                logInitialization("执行SQL: " + sql, "DEBUG");

                auto result = mysql_conn->executeQuery(sql);

                if (result && result->next()) {
                    int count = result->getInt(1);
                    bool exists = count > 0;
                    logInitialization("表 " + table_name + " 检查结果: " + (exists ? "存在" : "不存在"));

                    return exists;
                }

                logInitialization("表 " + table_name + " 查询结果为空", "WARNING");
                return false;

            } catch (const std::exception& e) {
                logInitialization("检查表 " + table_name + " 是否存在时发生异常: " + std::string(e.what()), "ERROR");
                return false;
            }
        }

        /**
         * @brief 检查所有必需表是否存在
         * @return 所有必需表都存在返回true
         */
        bool DatabaseInitializer::checkAllRequiredTablesExist() {
            std::vector<std::string> table_order = getTableCreationOrder();

            for (const std::string& table_name : table_order) {
                const auto& table_info = table_definitions_.at(table_name);
                if (table_info.required && !checkTableExists(table_name)) {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 获取缺失的表列表
         * @return 缺失的表名列表（按依赖顺序排列）
         */
        std::vector<std::string> DatabaseInitializer::getMissingTables() {
            std::vector<std::string> missing_tables;
            std::vector<std::string> table_order = getTableCreationOrder();

            for (const std::string& table_name : table_order) {
                if (!checkTableExists(table_name)) {
                    missing_tables.push_back(table_name);
                }
            }

            return missing_tables;
        }

        /**
         * @brief 创建单个表
         * @param table_info 表信息
         * @return 创建成功返回true
         */
        bool DatabaseInitializer::createTable(const TableInfo& table_info) {
            try {
                // 1. 创建表
                if (!executeSQL(table_info.create_sql)) {
                    logInitialization("创建表 " + table_info.name + " 失败", "ERROR");
                    return false;
                }

                // 2. 创建索引
                for (const auto& index_sql : table_info.indexes) {
                    if (!executeSQL(index_sql)) {
                        logInitialization("创建表 " + table_info.name + " 的索引失败: " + index_sql, "WARNING");
                        // 索引创建失败不影响表创建成功
                    }
                }

                return true;

            } catch (const std::exception& e) {
                logInitialization("创建表 " + table_info.name + " 时发生异常: " + std::string(e.what()), "ERROR");
                return false;
            }
        }

        /**
         * @brief 验证表结构
         * @param table_name 表名
         * @return 验证通过返回true
         */
        bool DatabaseInitializer::validateTableStructure(const std::string& table_name) {
            try {
                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    return false;
                }

                std::string sql = "SELECT 1 FROM " + table_name + " LIMIT 1";
                auto stmt = mysql_conn->prepareStatement(sql);
                stmt->executeQuery();

                return true;

            } catch (const std::exception& e) {
                logInitialization("验证表 " + table_name + " 结构时发生异常: " + std::string(e.what()), "WARNING");
                return false;
            }
        }

        /**
         * @brief 插入初始数据
         * @return 插入成功返回true
         */
        bool DatabaseInitializer::insertInitialData() {
            try {
                // 这里可以插入一些初始数据，比如默认角色、系统配置等
                // 目前暂时返回true，表示成功
                logInitialization("初始数据插入完成");
                return true;

            } catch (const std::exception& e) {
                logInitialization("插入初始数据时发生异常: " + std::string(e.what()), "ERROR");
                return false;
            }
        }

        /**
         * @brief 获取数据库版本
         * @return 数据库版本号
         */
        std::string DatabaseInitializer::getDatabaseVersion() {
            try {
                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    return "unknown";
                }

                // 检查版本表是否存在
                if (!checkTableExists("database_version")) {
                    return "0.0.0";
                }

                std::string sql = "SELECT version FROM database_version ORDER BY created_at DESC LIMIT 1";
                auto stmt = mysql_conn->prepareStatement(sql);
                auto result = stmt->executeQuery();

                if (result && result->next()) {
                    return result->getString(1);
                }

                return "0.0.0";

            } catch (const std::exception& e) {
                logInitialization("获取数据库版本时发生异常: " + std::string(e.what()), "ERROR");
                return "unknown";
            }
        }

        /**
         * @brief 更新数据库版本
         * @param version 新版本号
         * @return 更新成功返回true
         */
        bool DatabaseInitializer::updateDatabaseVersion(const std::string& version) {
            try {
                // 创建版本表（如果不存在）
                std::string create_version_table_sql = R"(
                    CREATE TABLE IF NOT EXISTS database_version (
                        id INT AUTO_INCREMENT PRIMARY KEY,
                        version VARCHAR(20) NOT NULL,
                        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                        description TEXT
                    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
                )";

                if (!executeSQL(create_version_table_sql)) {
                    return false;
                }

                // 插入新版本记录
                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    return false;
                }

                std::string sql = "INSERT INTO database_version (version, description) VALUES (?, ?)";
                auto stmt = mysql_conn->prepareStatement(sql);
                stmt->setString(1, version);
                stmt->setString(2, "Auth Service database schema version " + version);
                stmt->executeUpdate();

                logInitialization("数据库版本更新为: " + version);
                return true;

            } catch (const std::exception& e) {
                logInitialization("更新数据库版本时发生异常: " + std::string(e.what()), "ERROR");
                return false;
            }
        }

        /**
         * @brief 获取表创建的正确顺序（考虑外键依赖）
         * @return 按依赖顺序排列的表名列表
         */
        std::vector<std::string> DatabaseInitializer::getTableCreationOrder() {
            // 认证服务专用表 - 只创建认证相关的表，不创建用户数据表和会话表
            // 用户数据和会话由user_service管理，通过user_service_client访问
            return {
                // 1. 认证令牌表 - 存储JWT刷新令牌等
                "auth_tokens",
                
                // 2. 登录尝试记录表 - 存储登录失败记录，防止暴力破解
                "login_attempts",
                
                // 3. 设备信息表 - 存储用户登录设备信息
                "user_devices",
                
                // 4. 认证审计日志表 - 存储认证相关操作日志
                "auth_audit_logs"
            };
        }

        /**
         * @brief 初始化表定义
         */
        void DatabaseInitializer::initializeTableDefinitions() {
            // ==================== 认证服务专用表定义 ====================
            // 注意：会话管理由user_service负责，auth_service专注于认证和安全
            
            // 1. 认证令牌表 - 存储JWT刷新令牌等
            table_definitions_["auth_tokens"] = {
                "auth_tokens",
                R"(CREATE TABLE IF NOT EXISTS auth_tokens (
                    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '令牌ID',
                    token_id VARCHAR(128) NOT NULL UNIQUE COMMENT '令牌标识符',
                    user_id VARCHAR(50) NOT NULL COMMENT '用户ID（来自user_service）',
                    token_type VARCHAR(20) NOT NULL COMMENT '令牌类型：refresh/access/reset',
                    token_hash VARCHAR(255) NOT NULL COMMENT '令牌哈希值',
                    expires_at TIMESTAMP NOT NULL COMMENT '过期时间',
                    is_revoked BOOLEAN DEFAULT FALSE COMMENT '是否已撤销',
                    revoked_at TIMESTAMP DEFAULT NULL COMMENT '撤销时间',
                    last_used_at TIMESTAMP DEFAULT NULL COMMENT '最后使用时间',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    INDEX idx_token_id (token_id),
                    INDEX idx_user_id (user_id),
                    INDEX idx_token_type (token_type),
                    INDEX idx_expires_at (expires_at),
                    INDEX idx_is_revoked (is_revoked)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='认证令牌表')",
                {
                    "CREATE INDEX idx_auth_tokens_token_id ON auth_tokens(token_id)",
                    "CREATE INDEX idx_auth_tokens_user_id ON auth_tokens(user_id)",
                    "CREATE INDEX idx_auth_tokens_token_type ON auth_tokens(token_type)",
                    "CREATE INDEX idx_auth_tokens_expires_at ON auth_tokens(expires_at)"
                },
                true,
                "认证令牌表"
            };

            // 2. 登录尝试记录表 - 防止暴力破解
            table_definitions_["login_attempts"] = {
                "login_attempts",
                R"(CREATE TABLE IF NOT EXISTS login_attempts (
                    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '记录ID',
                    user_id VARCHAR(50) DEFAULT NULL COMMENT '用户ID（如果识别出用户）',
                    username VARCHAR(50) DEFAULT NULL COMMENT '尝试的用户名',
                    ip_address VARCHAR(45) NOT NULL COMMENT '客户端IP地址',
                    user_agent VARCHAR(500) DEFAULT '' COMMENT '用户代理字符串',
                    attempt_result VARCHAR(20) NOT NULL COMMENT '结果：success/failed/blocked',
                    failure_reason VARCHAR(100) DEFAULT '' COMMENT '失败原因',
                    attempt_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '尝试时间',
                    device_fingerprint VARCHAR(255) DEFAULT '' COMMENT '设备指纹',
                    INDEX idx_user_id (user_id),
                    INDEX idx_username (username),
                    INDEX idx_ip_address (ip_address),
                    INDEX idx_attempt_time (attempt_time),
                    INDEX idx_attempt_result (attempt_result)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='登录尝试记录表')",
                {
                    "CREATE INDEX idx_login_attempts_user_id ON login_attempts(user_id)",
                    "CREATE INDEX idx_login_attempts_ip_address ON login_attempts(ip_address)",
                    "CREATE INDEX idx_login_attempts_attempt_time ON login_attempts(attempt_time)"
                },
                true,
                "登录尝试记录表"
            };

            // 3. 用户设备信息表 - 存储用户登录设备
            table_definitions_["user_devices"] = {
                "user_devices",
                R"(CREATE TABLE IF NOT EXISTS user_devices (
                    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '设备ID',
                    device_id VARCHAR(100) NOT NULL UNIQUE COMMENT '设备唯一标识',
                    user_id VARCHAR(50) NOT NULL COMMENT '用户ID（来自user_service）',
                    device_name VARCHAR(200) DEFAULT '' COMMENT '设备名称',
                    device_type VARCHAR(50) DEFAULT '' COMMENT '设备类型：web/mobile/desktop',
                    os_name VARCHAR(50) DEFAULT '' COMMENT '操作系统',
                    os_version VARCHAR(50) DEFAULT '' COMMENT '系统版本',
                    browser_name VARCHAR(50) DEFAULT '' COMMENT '浏览器名称',
                    browser_version VARCHAR(50) DEFAULT '' COMMENT '浏览器版本',
                    ip_address VARCHAR(45) DEFAULT '' COMMENT '最后登录IP',
                    is_trusted BOOLEAN DEFAULT FALSE COMMENT '是否受信任设备',
                    last_login_at TIMESTAMP DEFAULT NULL COMMENT '最后登录时间',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    INDEX idx_device_id (device_id),
                    INDEX idx_user_id (user_id),
                    INDEX idx_device_type (device_type),
                    INDEX idx_is_trusted (is_trusted),
                    INDEX idx_last_login_at (last_login_at)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户设备信息表')",
                {
                    "CREATE INDEX idx_user_devices_device_id ON user_devices(device_id)",
                    "CREATE INDEX idx_user_devices_user_id ON user_devices(user_id)",
                    "CREATE INDEX idx_user_devices_last_login_at ON user_devices(last_login_at)"
                },
                true,
                "用户设备信息表"
            };

            // 4. 认证审计日志表 - 存储认证相关操作日志
            table_definitions_["auth_audit_logs"] = {
                "auth_audit_logs",
                R"(CREATE TABLE IF NOT EXISTS auth_audit_logs (
                    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '日志ID',
                    user_id VARCHAR(50) DEFAULT NULL COMMENT '用户ID（来自user_service）',
                    username VARCHAR(50) DEFAULT NULL COMMENT '尝试登录的用户名',
                    action VARCHAR(50) NOT NULL COMMENT '操作类型：login/logout/token_refresh/token_revoke',
                    result VARCHAR(20) NOT NULL COMMENT '操作结果：success/failed/blocked',
                    ip_address VARCHAR(45) NOT NULL COMMENT '客户端IP地址',
                    user_agent VARCHAR(500) DEFAULT '' COMMENT '用户代理字符串',
                    device_id VARCHAR(100) DEFAULT '' COMMENT '设备标识',
                    error_code VARCHAR(50) DEFAULT NULL COMMENT '错误代码',
                    error_message TEXT DEFAULT NULL COMMENT '错误详细信息',
                    additional_data JSON DEFAULT NULL COMMENT '附加数据JSON',
                    processing_time_ms INT DEFAULT 0 COMMENT '处理时间（毫秒）',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    INDEX idx_user_id (user_id),
                    INDEX idx_username (username),
                    INDEX idx_action (action),
                    INDEX idx_result (result),
                    INDEX idx_ip_address (ip_address),
                    INDEX idx_created_at (created_at)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='认证审计日志表')",
                {
                    "CREATE INDEX idx_auth_audit_logs_user_id ON auth_audit_logs(user_id)",
                    "CREATE INDEX idx_auth_audit_logs_action ON auth_audit_logs(action)",
                    "CREATE INDEX idx_auth_audit_logs_result ON auth_audit_logs(result)",
                    "CREATE INDEX idx_auth_audit_logs_created_at ON auth_audit_logs(created_at)"
                },
                true,
                "认证审计日志表"
            };

            logInitialization("认证服务表定义初始化完成，共定义 " + std::to_string(table_definitions_.size()) + " 个表");
        }

        /**
         * @brief 执行SQL语句
         * @param sql SQL语句
         * @return 执行成功返回true
         */
        bool DatabaseInitializer::executeSQL(const std::string& sql) {
            try {
                logInitialization("准备执行SQL: " + sql, "DEBUG");

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    logInitialization("无法获取MySQL连接", "ERROR");
                    return false;
                }

                logInitialization("获取MySQL连接成功，开始执行SQL");

                // 使用executeUpdate执行DDL语句（CREATE, DROP, ALTER等）
                int affected_rows = mysql_conn->executeUpdate(sql);

                logInitialization("SQL执行成功，影响行数: " + std::to_string(affected_rows));

                return true;

            } catch (const std::exception& e) {
                logInitialization("执行SQL失败: " + std::string(e.what()) + " SQL: " + sql, "ERROR");
                return false;
            }
        }

        /**
         * @brief 执行SQL文件
         * @param file_path SQL文件路径
         * @return 执行成功返回true
         */
        bool DatabaseInitializer::executeSQLFile(const std::string& file_path) {
            try {
                std::ifstream file(file_path);
                if (!file.is_open()) {
                    logInitialization("无法打开SQL文件: " + file_path, "ERROR");
                    return false;
                }

                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string sql_content = buffer.str();

                // 简单的SQL语句分割（按分号分割）
                std::stringstream ss(sql_content);
                std::string sql_statement;

                while (std::getline(ss, sql_statement, ';')) {
                    // 去除空格和换行符
                    sql_statement.erase(0, sql_statement.find_first_not_of(" \t\n\r"));
                    sql_statement.erase(sql_statement.find_last_not_of(" \t\n\r") + 1);

                    if (!sql_statement.empty()) {
                        if (!executeSQL(sql_statement + ";")) {
                            return false;
                        }
                    }
                }

                return true;

            } catch (const std::exception& e) {
                logInitialization("执行SQL文件失败: " + std::string(e.what()), "ERROR");
                return false;
            }
        }

        /**
         * @brief 创建数据库（如果不存在）
         * @param database_name 数据库名
         * @return 创建成功返回true
         */
        bool DatabaseInitializer::createDatabaseIfNotExists(const std::string& database_name) {
            try {
                logInitialization("开始创建数据库: " + database_name);

                std::string sql = "CREATE DATABASE IF NOT EXISTS " + database_name +
                                 " CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";

                logInitialization("执行创建数据库SQL: " + sql);
                if (!executeSQL(sql)) {
                    logInitialization("创建数据库SQL执行失败", "ERROR");
                    return false;
                }

                // 切换到目标数据库
                std::string use_sql = "USE " + database_name;
                logInitialization("执行切换数据库SQL: " + use_sql);
                if (!executeSQL(use_sql)) {
                    logInitialization("切换数据库SQL执行失败", "ERROR");
                    return false;
                }

                logInitialization("数据库 " + database_name + " 准备就绪");
                return true;

            } catch (const std::exception& e) {
                logInitialization("创建数据库失败: " + std::string(e.what()), "ERROR");
                return false;
            }
        }

        /**
         * @brief 获取表的列信息
         * @param table_name 表名
         * @return 列名列表
         */
        std::vector<std::string> DatabaseInitializer::getTableColumns(const std::string& table_name) {
            std::vector<std::string> columns;
            try {
                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    return columns;
                }

                std::string sql = "SHOW COLUMNS FROM " + table_name;
                auto stmt = mysql_conn->prepareStatement(sql);
                auto result = stmt->executeQuery();

                while (result && result->next()) {
                    columns.push_back(result->getString(1));
                }

            } catch (const std::exception& e) {
                logInitialization("获取表列信息失败: " + std::string(e.what()), "ERROR");
            }

            return columns;
        }

        /**
         * @brief 获取表的索引信息
         * @param table_name 表名
         * @return 索引名列表
         */
        std::vector<std::string> DatabaseInitializer::getTableIndexes(const std::string& table_name) {
            std::vector<std::string> indexes;
            try {
                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
                auto mysql_conn = conn_guard.get();
                if (!mysql_conn) {
                    return indexes;
                }

                std::string sql = "SHOW INDEX FROM " + table_name;
                auto stmt = mysql_conn->prepareStatement(sql);
                auto result = stmt->executeQuery();

                while (result && result->next()) {
                    indexes.push_back(result->getString(3)); // Key_name
                }

            } catch (const std::exception& e) {
                logInitialization("获取表索引信息失败: " + std::string(e.what()), "ERROR");
            }

            return indexes;
        }

        /**
         * @brief 记录初始化日志
         * @param message 日志消息
         * @param level 日志级别
         */
        void DatabaseInitializer::logInitialization(const std::string& message, const std::string& level) {
            std::string log_message = "[DatabaseInitializer] " + message;

            if (level == "ERROR") {
                LOG_ERROR(log_message);
            } else if (level == "WARNING") {
                LOG_WARNING(log_message);
            } else if (level == "DEBUG") {
                LOG_DEBUG(log_message);
            } else {
                LOG_INFO(log_message);
            }
        }

    } // namespace auth_service
} // namespace core_services