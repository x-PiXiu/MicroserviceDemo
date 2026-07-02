/**
 * @file database_initializer.h
 * @brief 数据库初始化器 - 负责检查和创建数据库表结构
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 *
 * 功能特性:
 * - 自动检查数据库表是否存在
 * - 自动创建缺失的数据库表
 * - 数据库版本管理
 * - 数据库结构验证
 * - 初始数据插入
 *
 * 使用场景:
 * - 服务首次启动时自动初始化数据库
 * - 数据库结构升级和迁移
 * - 开发环境快速搭建
 * - 容器化部署自动化
 */

#pragma once

#include "common/database/mysql_pool.h"
#include "common/logger/logger.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace core_services {
    namespace auth_service {

        /**
         * @brief 数据库表信息结构体
         */
        struct TableInfo {
            std::string name;                    ///< 表名
            std::string create_sql;              ///< 创建表的SQL语句
            std::vector<std::string> indexes;    ///< 索引创建SQL列表
            bool required = true;                ///< 是否为必需表
            std::string description;             ///< 表描述
        };

        /**
         * @brief 数据库初始化结果
         */
        struct InitializationResult {
            bool success = false;                           ///< 初始化是否成功
            std::vector<std::string> created_tables;        ///< 已创建的表列表
            std::vector<std::string> existing_tables;       ///< 已存在的表列表
            std::vector<std::string> errors;                ///< 错误信息列表
            std::chrono::milliseconds execution_time{0};    ///< 执行时间
        };

        /**
         * @brief 数据库初始化器类
         * @details 负责检查和创建Auth Service所需的数据库表结构
         */
        class DatabaseInitializer {
        public:
            /**
             * @brief 构造函数
             * @param mysql_pool MySQL连接池
             */
            explicit DatabaseInitializer(std::shared_ptr<common::database::MySQLPool> mysql_pool);

            /**
             * @brief 析构函数
             */
            ~DatabaseInitializer() = default;

            /**
             * @brief 初始化数据库
             * @param force_recreate 是否强制重新创建表（危险操作）
             * @return 初始化结果
             */
            InitializationResult initializeDatabase(bool force_recreate = false);

            /**
             * @brief 检查数据库表是否存在
             * @param table_name 表名
             * @return 存在返回true
             */
            bool checkTableExists(const std::string& table_name);

            /**
             * @brief 检查所有必需表是否存在
             * @return 所有必需表都存在返回true
             */
            bool checkAllRequiredTablesExist();

            /**
             * @brief 获取缺失的表列表
             * @return 缺失的表名列表
             */
            std::vector<std::string> getMissingTables();

            /**
             * @brief 创建单个表
             * @param table_info 表信息
             * @return 创建成功返回true
             */
            bool createTable(const TableInfo& table_info);

            /**
             * @brief 验证表结构
             * @param table_name 表名
             * @return 验证通过返回true
             */
            bool validateTableStructure(const std::string& table_name);

            /**
             * @brief 插入初始数据
             * @return 插入成功返回true
             */
            bool insertInitialData();

            /**
             * @brief 获取数据库版本
             * @return 数据库版本号
             */
            std::string getDatabaseVersion();

            /**
             * @brief 更新数据库版本
             * @param version 新版本号
             * @return 更新成功返回true
             */
            bool updateDatabaseVersion(const std::string& version);

        private:
            std::shared_ptr<common::database::MySQLPool> mysql_pool_;  ///< MySQL连接池
            std::unordered_map<std::string, TableInfo> table_definitions_;  ///< 表定义映射

            /**
             * @brief 初始化表定义
             */
            void initializeTableDefinitions();

            /**
             * @brief 获取表创建的正确顺序（考虑外键依赖）
             * @return 按依赖顺序排列的表名列表
             */
            std::vector<std::string> getTableCreationOrder();

            /**
             * @brief 执行SQL语句
             * @param sql SQL语句
             * @return 执行成功返回true
             */
            bool executeSQL(const std::string& sql);

            /**
             * @brief 执行SQL文件
             * @param file_path SQL文件路径
             * @return 执行成功返回true
             */
            bool executeSQLFile(const std::string& file_path);

            /**
             * @brief 创建数据库（如果不存在）
             * @param database_name 数据库名
             * @return 创建成功返回true
             */
            bool createDatabaseIfNotExists(const std::string& database_name);

            /**
             * @brief 获取表的列信息
             * @param table_name 表名
             * @return 列信息列表
             */
            std::vector<std::string> getTableColumns(const std::string& table_name);

            /**
             * @brief 获取表的索引信息
             * @param table_name 表名
             * @return 索引信息列表
             */
            std::vector<std::string> getTableIndexes(const std::string& table_name);

            /**
             * @brief 记录初始化日志
             * @param message 日志消息
             * @param level 日志级别
             */
            void logInitialization(const std::string& message, const std::string& level = "INFO");
        };

    } // namespace auth_service
} // namespace core_services
