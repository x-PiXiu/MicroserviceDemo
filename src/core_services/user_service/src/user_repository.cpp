/**
 * @file user_repository.cpp
 * @brief 用户数据访问层实现 - 符合推荐分层架构
 * @details 实现用户数据的MySQL和Redis访问逻辑，集成统一 Repository 模式
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0 - 集成统一 Redis Repository 模式
 */

#include "include/user_repository.h"
#include "common/logger/logger.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace core_services {
namespace user_service {

// ==================== Gender字段转换函数 ====================

/**
 * @brief 将API格式的gender转换为数据库格式
 * @param gender API格式 ("male", "female", "other", "unknown")
 * @return 数据库格式 ("M", "F", "O", "U")
 */
static std::string convertGenderToDbFormat(const std::string& gender) {
    if (gender == "male") return "M";
    if (gender == "female") return "F";
    if (gender == "other") return "O";
    if (gender == "unknown") return "U";
    return "U"; // 默认为未知
}

/**
 * @brief 将数据库格式的gender转换为API格式
 * @param db_gender 数据库格式 ("M", "F", "O", "U")
 * @return API格式 ("male", "female", "other", "unknown")
 */
static std::string convertGenderFromDbFormat(const std::string& db_gender) {
    if (db_gender == "M") return "male";
    if (db_gender == "F") return "female";
    if (db_gender == "O") return "other";
    if (db_gender == "U") return "unknown";
    return "unknown"; // 默认为未知
}

/**
 * @brief 构造函数
 * @param mysql_pool MySQL连接池
 * @param redis_pool Redis连接池
 */
UserRepository::UserRepository(
    std::shared_ptr<common::database::MySQLPool> mysql_pool,
    std::shared_ptr<common::database::RedisPool> redis_pool)
    : mysql_pool_(mysql_pool)
    , redis_pool_(redis_pool)
{
    LOG_INFO("初始化用户数据访问层");

    // 初始化新的 Redis Repository
    initializeRedisRepositories();
}

/**
 * @brief 析构函数
 */
UserRepository::~UserRepository() {
    LOG_INFO("用户数据访问层已销毁");
}

/**
 * @brief 初始化 Redis Repository
 * @details 创建类型化的 Redis Repository 实例
 */
void UserRepository::initializeRedisRepositories() {
    if (redis_pool_) {
        try {
            user_redis_ = std::make_unique<UserRedisRepository>(redis_pool_);
            profile_redis_ = std::make_unique<UserProfileRedisRepository>(redis_pool_);
            prefs_redis_ = std::make_unique<UserPreferencesRedisRepository>(redis_pool_);
            LOG_INFO("Redis Repository 初始化完成");
        } catch (const std::exception& e) {
            LOG_ERROR("Redis Repository 初始化失败: " + std::string(e.what()));
        }
    }
}

/**
 * @brief 初始化数据访问层
 * @return 成功返回true
 */
bool UserRepository::initialize() {
    try {
        LOG_INFO("初始化用户数据访问层...");

        // 验证数据库连接池
        if (!mysql_pool_ || !redis_pool_) {
            LOG_ERROR("数据库连接池为空");
            return false;
        }

        // 测试数据库连接
        if (!testDatabaseConnections()) {
            LOG_ERROR("数据库连接测试失败");
            return false;
        }

        // 初始化缓存键前缀（向后兼容）
        cache_prefix_ = "user:";

        // 确保 Redis Repository 已初始化
        if (!user_redis_) {
            initializeRedisRepositories();
        }

        LOG_INFO("用户数据访问层初始化完成");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("用户数据访问层初始化异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 测试数据库连接
 * @return 连接正常返回true
 */
bool UserRepository::testDatabaseConnections() {
    try {
        // 测试MySQL连接
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        // MySQL连接池使用RAII管理，不需要手动释放
        
        // 🔧 RAII优化：测试Redis连接
        {
            common::database::RedisConnectionGuard redis_guard(*redis_pool_);
            auto redis_conn = redis_guard.get();
            if (!redis_conn) {
                LOG_ERROR("获取Redis连接失败");
                return false;
            }
            // 🔧 RAII：连接会在redis_guard析构时自动归还
        }
        
        LOG_INFO("数据库连接测试通过");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库连接测试异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 根据用户ID获取用户信息
 * @param user_id 用户ID
 * @return 用户信息（可能为空）
 */
std::optional<UserInfo> UserRepository::getUserById(const std::string& user_id) {
    try {
        LOG_DEBUG("获取用户信息: user_id=" + user_id);

        // ==================== 新架构：使用类型化 Redis Repository ====================
        if (user_redis_) {
            auto cached_result = user_redis_->findById(user_id);
            if (cached_result.has_value() && cached_result->has_value()) {
                LOG_DEBUG("从 Redis Repository 缓存获取用户信息成功");
                return cached_result.value();
            }
        }

        // ==================== 旧架构：向后兼容 ====================
        // 先尝试从旧缓存获取
        std::string cache_key = cache_prefix_ + "info:" + user_id;
        auto cached_user = getFromCache<UserInfo>(cache_key);
        if (cached_user.has_value()) {
            LOG_DEBUG("从旧缓存获取用户信息成功");
            // 迁移到新缓存
            if (user_redis_) {
                user_redis_->save(user_id, cached_user.value());
            }
            return cached_user;
        }

        // 🔧 RAII修复：从数据库获取 - 使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return std::nullopt;
        }

        std::string query = R"(
            SELECT user_id, username, email, password_hash, salt, phone,
                   status, online_status, login_attempts, last_login_ip,
                   created_at, updated_at, last_login_at, locked_until
            FROM users
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user_id);

        auto result = stmt->executeQuery();

        UserInfo user;
        if (result->next()) {
            user.user_id = result->getString("user_id");
            user.username = result->getString("username");
            user.email = result->getString("email");
            user.password_hash = result->getString("password_hash");
            user.salt = result->getString("salt");
            user.phone = result->getString("phone");

            // 🔧 修复: status和online_status是TINYINT类型，使用getInt读取
            int status_int = result->getInt("status");
            user.status = static_cast<UserStatus>(status_int);

            int online_status_int = result->getInt("online_status");
            user.online_status = static_cast<OnlineStatus>(online_status_int);

            user.login_attempts = result->getInt("login_attempts");
            user.last_login_ip = result->getString("last_login_ip");

            // 时间字段处理（如果数据库中有值）
            if (!result->isNull("created_at")) {
                std::string created_at_str = result->getString("created_at");
                // 这里需要更完善的时间解析，暂时使用当前时间
            user.created_at = std::chrono::system_clock::now();
            } else {
                user.created_at = std::chrono::system_clock::now();
            }

            if (!result->isNull("updated_at")) {
                std::string updated_at_str = result->getString("updated_at");
            user.updated_at = std::chrono::system_clock::now();
            } else {
                user.updated_at = std::chrono::system_clock::now();
            }

            // ==================== 新架构：使用 Redis Repository 缓存 ====================
            if (user_redis_) {
                user_redis_->save(user_id, user);
            } else {
                // 旧缓存（向后兼容）
                setToCache(cache_key, user, cache_ttl_);
            }
            
            // MySQL连接池使用RAII管理，不需要手动释放
            LOG_DEBUG("从数据库获取用户信息成功");
            return user;
        }
        
        // MySQL连接池使用RAII管理，不需要手动释放
        LOG_DEBUG("用户不存在");
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 根据用户名获取用户信息
 * @param username 用户名
 * @return 用户信息（可能为空）
 */
std::optional<UserInfo> UserRepository::getUserByUsername(const std::string& username) {
    try {
        LOG_DEBUG("根据用户名获取用户信息: username=" + username);
        
        // 先尝试从缓存获取
        std::string cache_key = cache_prefix_ + "username:" + username;
        auto cached_user = getFromCache<UserInfo>(cache_key);
        if (cached_user.has_value()) {
            LOG_DEBUG("从缓存获取用户信息成功");
            return cached_user;
        }
        
        // 🔧 RAII修复：从数据库获取 - 使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return std::nullopt;
        }
        
        std::string query = R"(
            SELECT user_id, username, email, password_hash, salt, phone, 
                   status, online_status, login_attempts, last_login_ip,
                   created_at, updated_at, last_login_at, locked_until
            FROM users 
            WHERE username = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, username);
        
        auto result = stmt->executeQuery();
        
        UserInfo user;
        if (result->next()) {
            user.user_id = result->getString("user_id");
            user.username = result->getString("username");
            user.email = result->getString("email");
            user.password_hash = result->getString("password_hash");
            user.salt = result->getString("salt");
            user.phone = result->getString("phone");
            
            // 🔧 修复: status和online_status是TINYINT类型，使用getInt读取
            int status_int = result->getInt("status");
            user.status = static_cast<UserStatus>(status_int);
            
            int online_status_int = result->getInt("online_status");
            user.online_status = static_cast<OnlineStatus>(online_status_int);
            
            user.login_attempts = result->getInt("login_attempts");
            user.last_login_ip = result->getString("last_login_ip");
            
            // 时间字段处理
            user.created_at = std::chrono::system_clock::now();
            user.updated_at = std::chrono::system_clock::now();
            
            // 缓存结果（用两个key）
            setToCache(cache_key, user, cache_ttl_);
            setToCache(cache_prefix_ + "info:" + user.user_id, user, cache_ttl_);
            
            // MySQL连接池使用RAII管理，不需要手动释放
            LOG_DEBUG("根据用户名获取用户信息成功");
            return user;
        }
        
        // MySQL连接池使用RAII管理，不需要手动释放
        LOG_DEBUG("用户不存在");
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("根据用户名获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 根据邮箱获取用户信息
 * @param email 邮箱
 * @return 用户信息（可能为空）
 */
std::optional<UserInfo> UserRepository::getUserByEmail(const std::string& email) {
    try {
        LOG_DEBUG("根据邮箱获取用户信息: email=" + email);
        
        // 先尝试从缓存获取
        std::string cache_key = cache_prefix_ + "email:" + email;
        auto cached_user = getFromCache<UserInfo>(cache_key);
        if (cached_user.has_value()) {
            LOG_DEBUG("从缓存获取用户信息成功");
            return cached_user;
        }
        
        // 🔧 RAII修复：从数据库获取 - 使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return std::nullopt;
        }
        
        std::string query = R"(
            SELECT user_id, username, email, password_hash, salt, phone, 
                   status, online_status, login_attempts, last_login_ip,
                   created_at, updated_at, last_login_at, locked_until
            FROM users 
            WHERE email = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, email);
        
        auto result = stmt->executeQuery();
        
        UserInfo user;
        if (result->next()) {
            user.user_id = result->getString("user_id");
            user.username = result->getString("username");
            user.email = result->getString("email");
            user.password_hash = result->getString("password_hash");
            user.salt = result->getString("salt");
            user.phone = result->getString("phone");
            
            // 🔧 修复: status和online_status是TINYINT类型，使用getInt读取
            int status_int = result->getInt("status");
            user.status = static_cast<UserStatus>(status_int);
            
            int online_status_int = result->getInt("online_status");
            user.online_status = static_cast<OnlineStatus>(online_status_int);
            
            user.login_attempts = result->getInt("login_attempts");
            user.last_login_ip = result->getString("last_login_ip");
            
            // 时间字段处理
            user.created_at = std::chrono::system_clock::now();
            user.updated_at = std::chrono::system_clock::now();
            
            // 缓存结果（用多个key）
            setToCache(cache_key, user, cache_ttl_);
            setToCache(cache_prefix_ + "info:" + user.user_id, user, cache_ttl_);
            setToCache(cache_prefix_ + "username:" + user.username, user, cache_ttl_);
            
            // MySQL连接池使用RAII管理，不需要手动释放
            LOG_DEBUG("根据邮箱获取用户信息成功");
            return user;
        }
        
        // MySQL连接池使用RAII管理，不需要手动释放
        LOG_DEBUG("用户不存在");
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("根据邮箱获取用户信息异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 创建用户（完整版本：同时创建users、user_profiles、user_preferences）
 * @param user 用户信息
 * @return 成功返回true
 */
bool UserRepository::createUser(const UserInfo& user) {
    try {
        LOG_DEBUG("创建完整用户: user_id=" + user.user_id + ", username=" + user.username);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        // 🎯 关键修复：使用事务确保数据一致性
        mysql_conn->setAutoCommit(false);
        
        try {
            // 1. 创建用户基础信息（users表）
            std::string users_query = R"(
            INSERT INTO users 
                (user_id, username, email, password_hash, salt, phone, status, online_status) 
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            )";
            
            auto users_stmt = mysql_conn->prepareStatement(users_query);
            users_stmt->setString(1, user.user_id);
            users_stmt->setString(2, user.username);
            users_stmt->setString(3, user.email);
            users_stmt->setString(4, user.password_hash);
            // 🔧 修复: BCrypt兼容性处理 - 为BCrypt提供占位符salt
            std::string salt_for_db = user.salt;
            if (salt_for_db.empty()) {
                // 检查是否为BCrypt格式
                bool is_bcrypt = user.password_hash.length() >= 4 && 
                                (user.password_hash.substr(0, 4) == "$2a$" || 
                                 user.password_hash.substr(0, 4) == "$2b$" || 
                                 user.password_hash.substr(0, 4) == "$2x$" || 
                                 user.password_hash.substr(0, 4) == "$2y$");
                
                if (is_bcrypt) {
                    salt_for_db = "bcrypt_self_contained"; // BCrypt自包含salt的占位符
                }
            }
            users_stmt->setString(5, salt_for_db);
            users_stmt->setString(6, user.phone);
            users_stmt->setInt(7, static_cast<int>(user.status));
            users_stmt->setInt(8, static_cast<int>(user.online_status));
            
            int users_affected = users_stmt->executeUpdate();
            if (users_affected <= 0) {
                throw std::runtime_error("创建用户基础信息失败");
            }
            
            // 2. 创建用户档案（user_profiles表）- nickname是NOT NULL字段
            std::string profiles_query = R"(
                INSERT INTO user_profiles 
                (user_id, nickname, language, timezone) 
                VALUES (?, ?, ?, ?)
            )";
            
            auto profiles_stmt = mysql_conn->prepareStatement(profiles_query);
            profiles_stmt->setString(1, user.user_id);
            profiles_stmt->setString(2, user.username); // 默认使用username作为nickname
            profiles_stmt->setString(3, "zh-CN");       // 默认语言
            profiles_stmt->setString(4, "Asia/Shanghai"); // 默认时区
            
            int profiles_affected = profiles_stmt->executeUpdate();
            if (profiles_affected <= 0) {
                throw std::runtime_error("创建用户档案失败");
            }
            
            // 3. 创建用户偏好设置（user_preferences表）
            std::string preferences_query = R"(
                INSERT INTO user_preferences 
                (user_id) 
                VALUES (?)
            )";
            
            auto preferences_stmt = mysql_conn->prepareStatement(preferences_query);
            preferences_stmt->setString(1, user.user_id);
            
            int preferences_affected = preferences_stmt->executeUpdate();
            if (preferences_affected <= 0) {
                throw std::runtime_error("创建用户偏好设置失败");
            }
            
            // 🎯 提交事务
            mysql_conn->commit();
            mysql_conn->setAutoCommit(true);
            
            // 清除相关缓存
            clearUserCache(user.user_id, user.username, user.email);
            
            LOG_INFO("✅ 完整用户创建成功: " + user.user_id + " (用户基础信息 + 档案 + 偏好设置)");
            return true;
            
        } catch (const std::exception& inner_e) {
            // 🎯 回滚事务
            mysql_conn->rollback();
            mysql_conn->setAutoCommit(true);
            throw; // 重新抛出异常给外层处理
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建完整用户异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 创建完整用户（包含基础信息、档案、偏好设置）
 * @param user 用户信息
 * @param nickname 昵称
 * @param avatar_url 头像URL
 * @return 成功返回true
 */
bool UserRepository::createCompleteUser(const UserInfo& user, const std::string& nickname, const std::string& avatar_url) {
    try {
        LOG_DEBUG("创建完整用户（增强版）: user_id=" + user.user_id + ", username=" + user.username + ", nickname=" + nickname);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        // 🎯 关键修复：使用事务确保数据一致性
        mysql_conn->setAutoCommit(false);
        
        try {
            // 1. 创建用户基础信息（users表）
            std::string users_query = R"(
                INSERT INTO users 
                (user_id, username, email, password_hash, salt, phone, status, online_status) 
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            )";
            
            auto users_stmt = mysql_conn->prepareStatement(users_query);
            users_stmt->setString(1, user.user_id);
            users_stmt->setString(2, user.username);
            users_stmt->setString(3, user.email);
            users_stmt->setString(4, user.password_hash);
            // 🔧 修复: BCrypt兼容性处理 - 为BCrypt提供占位符salt
            std::string salt_for_db = user.salt;
            if (salt_for_db.empty()) {
                // 检查是否为BCrypt格式
                bool is_bcrypt = user.password_hash.length() >= 4 && 
                                (user.password_hash.substr(0, 4) == "$2a$" || 
                                 user.password_hash.substr(0, 4) == "$2b$" || 
                                 user.password_hash.substr(0, 4) == "$2x$" || 
                                 user.password_hash.substr(0, 4) == "$2y$");
                
                if (is_bcrypt) {
                    salt_for_db = "bcrypt_self_contained"; // BCrypt自包含salt的占位符
                }
            }
            users_stmt->setString(5, salt_for_db);
            users_stmt->setString(6, user.phone);
            users_stmt->setInt(7, static_cast<int>(user.status));
            users_stmt->setInt(8, static_cast<int>(user.online_status));
            
            int users_affected = users_stmt->executeUpdate();
            if (users_affected <= 0) {
                throw std::runtime_error("创建用户基础信息失败");
            }
            
            // 2. 创建用户档案（user_profiles表）- 使用传入的nickname
            std::string profiles_query = R"(
                INSERT INTO user_profiles 
                (user_id, nickname, avatar_url, language, timezone) 
                VALUES (?, ?, ?, ?, ?)
            )";
            
            auto profiles_stmt = mysql_conn->prepareStatement(profiles_query);
            profiles_stmt->setString(1, user.user_id);
            profiles_stmt->setString(2, nickname); // 🎯 使用传入的nickname
            profiles_stmt->setString(3, avatar_url); // 🎯 使用传入的avatar_url
            profiles_stmt->setString(4, "zh-CN");    // 默认语言
            profiles_stmt->setString(5, "Asia/Shanghai"); // 默认时区
            
            int profiles_affected = profiles_stmt->executeUpdate();
            if (profiles_affected <= 0) {
                throw std::runtime_error("创建用户档案失败");
            }
            
            // 3. 创建用户偏好设置（user_preferences表）
            std::string preferences_query = R"(
                INSERT INTO user_preferences 
                (user_id) 
                VALUES (?)
            )";
            
            auto preferences_stmt = mysql_conn->prepareStatement(preferences_query);
            preferences_stmt->setString(1, user.user_id);
            
            int preferences_affected = preferences_stmt->executeUpdate();
            if (preferences_affected <= 0) {
                throw std::runtime_error("创建用户偏好设置失败");
            }
            
            // 🎯 提交事务
            mysql_conn->commit();
            mysql_conn->setAutoCommit(true);
            
            // 清除相关缓存
            clearUserCache(user.user_id, user.username, user.email);
            
            LOG_INFO("✅ 完整用户创建成功（增强版）: " + user.user_id + 
                    " (用户=" + user.username + ", 昵称=" + nickname + ", 头像=" + 
                    (avatar_url.empty() ? "无" : "有") + ")");
            return true;
            
        } catch (const std::exception& inner_e) {
            // 🎯 回滚事务
            mysql_conn->rollback();
            mysql_conn->setAutoCommit(true);
            throw; // 重新抛出异常给外层处理
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建完整用户（增强版）异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 初始化数据库表结构
 * @return 初始化成功返回true
 */
bool UserRepository::initializeTables() {
    try {
        LOG_INFO("开始初始化数据库表结构...");
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        // 🎯 关键修复：使用事务确保DDL操作的原子性
        mysql_conn->setAutoCommit(false);
        
        try {
            // 1. 创建用户基础信息表 (users)
            std::string create_users_table = R"(
                CREATE TABLE IF NOT EXISTS `users` (
                    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (主键)',
                    `username` VARCHAR(50) NOT NULL COMMENT '用户名 (唯一)',
                    `email` VARCHAR(255) NOT NULL COMMENT '邮箱 (唯一)',
                    `password_hash` VARCHAR(255) NOT NULL COMMENT '密码哈希',
                    `salt` VARCHAR(64) NOT NULL COMMENT '密码盐值',
                    `phone` VARCHAR(20) DEFAULT NULL COMMENT '手机号',
                    `status` TINYINT NOT NULL DEFAULT 1 COMMENT '用户状态 (0:未激活 1:活跃 2:暂停 3:封禁 4:已删除)',
                    `online_status` TINYINT NOT NULL DEFAULT 0 COMMENT '在线状态 (0:离线 1:在线 2:离开 3:忙碌)',
                    `login_attempts` INT NOT NULL DEFAULT 0 COMMENT '登录失败次数',
                    `locked_until` TIMESTAMP NULL DEFAULT NULL COMMENT '锁定截止时间',
                    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    `last_login_at` TIMESTAMP NULL DEFAULT NULL COMMENT '最后登录时间',
                    `last_login_ip` VARCHAR(45) DEFAULT NULL COMMENT '最后登录IP',
                    PRIMARY KEY (`user_id`),
                    UNIQUE KEY `uk_users_username` (`username`),
                    UNIQUE KEY `uk_users_email` (`email`),
                    KEY `idx_users_status` (`status`),
                    KEY `idx_users_online_status` (`online_status`),
                    KEY `idx_users_created_at` (`created_at`),
                    KEY `idx_users_last_login_at` (`last_login_at`)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户基础信息表'
            )";
            
            auto users_stmt = mysql_conn->prepareStatement(create_users_table);
            users_stmt->execute();
            LOG_INFO("✅ users表创建成功");
            
            // 2. 创建用户档案表 (user_profiles)
            std::string create_profiles_table = R"(
                CREATE TABLE IF NOT EXISTS `user_profiles` (
                    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
                    `nickname` VARCHAR(100) NOT NULL COMMENT '昵称',
                    `real_name` VARCHAR(100) DEFAULT NULL COMMENT '真实姓名',
                    `avatar_url` TEXT DEFAULT NULL COMMENT '头像URL',
                    `bio` TEXT DEFAULT NULL COMMENT '个人简介',
                    `location` VARCHAR(255) DEFAULT NULL COMMENT '地理位置',
                    `website` VARCHAR(500) DEFAULT NULL COMMENT '个人网站',
                    `birth_date` DATE DEFAULT NULL COMMENT '生日',
                    `gender` CHAR(1) DEFAULT NULL COMMENT '性别 (M:男 F:女 O:其他)',
                    `language` VARCHAR(10) NOT NULL DEFAULT 'zh-CN' COMMENT '首选语言',
                    `timezone` VARCHAR(50) NOT NULL DEFAULT 'Asia/Shanghai' COMMENT '时区',
                    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    PRIMARY KEY (`user_id`),
                    KEY `idx_user_profiles_nickname` (`nickname`),
                    KEY `idx_user_profiles_location` (`location`),
                    KEY `idx_user_profiles_created_at` (`created_at`),
                    CONSTRAINT `fk_user_profiles_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户档案表'
            )";
            
            auto profiles_stmt = mysql_conn->prepareStatement(create_profiles_table);
            profiles_stmt->execute();
            LOG_INFO("✅ user_profiles表创建成功");
            
            // 3. 创建用户偏好设置表 (user_preferences)
            std::string create_preferences_table = R"(
                CREATE TABLE IF NOT EXISTS `user_preferences` (
                    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
                    `email_notifications` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '邮件通知',
                    `push_notifications` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '推送通知',
                    `sms_notifications` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '短信通知',
                    `theme` VARCHAR(20) NOT NULL DEFAULT 'light' COMMENT '主题 (light/dark)',
                    `language` VARCHAR(10) NOT NULL DEFAULT 'zh-CN' COMMENT '界面语言',
                    `sound_effects` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '音效开关',
                    `music` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '音乐开关',
                    `volume` TINYINT NOT NULL DEFAULT 80 COMMENT '音量 (0-100)',
                    `auto_login` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '自动登录',
                    `remember_me` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '记住我',
                    `session_timeout` INT NOT NULL DEFAULT 3600 COMMENT '会话超时时间(秒)',
                    `custom_settings` JSON DEFAULT NULL COMMENT '自定义设置JSON',
                    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    PRIMARY KEY (`user_id`),
                    KEY `idx_user_preferences_theme` (`theme`),
                    KEY `idx_user_preferences_language` (`language`),
                    CONSTRAINT `fk_user_preferences_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户偏好设置表'
            )";
            
            auto preferences_stmt = mysql_conn->prepareStatement(create_preferences_table);
            preferences_stmt->execute();
            LOG_INFO("✅ user_preferences表创建成功");
            
            // 4. 创建用户会话表 (user_sessions)
            std::string create_sessions_table = R"(
                CREATE TABLE IF NOT EXISTS `user_sessions` (
                    `session_id` VARCHAR(128) NOT NULL COMMENT '会话ID (主键)',
                    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
                    `device_id` VARCHAR(255) DEFAULT NULL COMMENT '设备ID',
                    `device_type` VARCHAR(50) DEFAULT NULL COMMENT '设备类型',
                    `client_ip` VARCHAR(45) NOT NULL COMMENT '客户端IP',
                    `user_agent` TEXT DEFAULT NULL COMMENT '用户代理',
                    `is_active` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '是否活跃',
                    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
                    `expired_at` TIMESTAMP NULL DEFAULT NULL COMMENT '过期时间',
                    PRIMARY KEY (`session_id`),
                    KEY `idx_user_sessions_user_id` (`user_id`),
                    KEY `idx_user_sessions_is_active` (`is_active`),
                    KEY `idx_user_sessions_created_at` (`created_at`),
                    KEY `idx_user_sessions_expired_at` (`expired_at`),
                    CONSTRAINT `fk_user_sessions_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户会话表'
            )";
            
            auto sessions_stmt = mysql_conn->prepareStatement(create_sessions_table);
            sessions_stmt->execute();
            LOG_INFO("✅ user_sessions表创建成功");
            
            // 5. 创建用户操作日志表 (user_audit_logs)
            std::string create_audit_logs_table = R"(
                CREATE TABLE IF NOT EXISTS `user_audit_logs` (
                    `log_id` BIGINT AUTO_INCREMENT COMMENT '日志ID (主键)',
                    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
                    `action` VARCHAR(100) NOT NULL COMMENT '操作类型',
                    `details` JSON DEFAULT NULL COMMENT '操作详情JSON',
                    `client_ip` VARCHAR(45) NOT NULL COMMENT '客户端IP',
                    `user_agent` TEXT DEFAULT NULL COMMENT '用户代理',
                    `result` VARCHAR(50) NOT NULL COMMENT '操作结果',
                    `error_message` TEXT DEFAULT NULL COMMENT '错误信息',
                    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
                    PRIMARY KEY (`log_id`),
                    KEY `idx_user_audit_logs_user_id` (`user_id`),
                    KEY `idx_user_audit_logs_action` (`action`),
                    KEY `idx_user_audit_logs_result` (`result`),
                    KEY `idx_user_audit_logs_created_at` (`created_at`)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户操作日志表'
            )";
            
            auto audit_logs_stmt = mysql_conn->prepareStatement(create_audit_logs_table);
            audit_logs_stmt->execute();
            LOG_INFO("✅ user_audit_logs表创建成功");
            
            // 🎯 提交所有DDL操作
            mysql_conn->commit();
            mysql_conn->setAutoCommit(true);
            
            LOG_INFO("🎉 数据库表结构初始化完成！所有表已成功创建");
            return true;
            
        } catch (const std::exception& inner_e) {
            // 🎯 回滚DDL操作
            mysql_conn->rollback();
            mysql_conn->setAutoCommit(true);
            throw; // 重新抛出异常给外层处理
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("数据库表结构初始化异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 更新用户信息
 * @param user 用户信息
 * @return 成功返回true
 */
bool UserRepository::updateUser(const UserInfo& user) {
    try {
        LOG_DEBUG("更新用户: user_id=" + user.user_id);

        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }

        // 🔧 修复：在更新前先获取旧的 username 和 email，用于清除旧的索引缓存
        std::string old_username, old_email;
        {
            std::string query_meta = "SELECT username, email FROM users WHERE user_id = ?";
            auto stmt_meta = mysql_conn->prepareStatement(query_meta);
            stmt_meta->setString(1, user.user_id);
            auto result_meta = stmt_meta->executeQuery();
            if (result_meta->next()) {
                old_username = result_meta->getString("username");
                old_email = result_meta->getString("email");
                LOG_INFO("更新前用户数据: old_username=" + old_username + ", old_email=" + old_email);
            }
        }

        std::string query = R"(
            UPDATE users
            SET username = ?, email = ?, password_hash = ?, salt = ?, phone = ?, status = ?, online_status = ?, updated_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user.username);
        stmt->setString(2, user.email);
        stmt->setString(3, user.password_hash);
        stmt->setString(4, user.salt);
        stmt->setString(5, user.phone);
        stmt->setInt(6, static_cast<int>(user.status));        // 🔧 修复: 使用整数而非字符串
        stmt->setInt(7, static_cast<int>(user.online_status)); // 🔧 修复: 使用整数而非字符串
        stmt->setString(8, user.user_id);

        int affected_rows = stmt->executeUpdate();
        // MySQL连接池使用RAII管理，不需要手动释放

        if (affected_rows > 0) {
            // 🔧 修复：清除所有相关缓存（包括旧的索引缓存）
            // 1. 清除新值的缓存索引
            clearUserCache(user.user_id, user.username, user.email);

            // 2. 清除旧值的缓存索引（如果 username 或 email 发生了变化）
            if (!old_username.empty() && old_username != user.username) {
                LOG_INFO("清除旧 username 索引缓存: " + old_username);
                if (user_redis_) {
                    user_redis_->removeIndex("username", old_username);
                }
                deleteFromCache(cache_prefix_ + "username:" + old_username);
            }
            if (!old_email.empty() && old_email != user.email) {
                LOG_INFO("清除旧 email 索引缓存: " + old_email);
                if (user_redis_) {
                    user_redis_->removeIndex("email", old_email);
                }
                deleteFromCache(cache_prefix_ + "email:" + old_email);
            }

            LOG_DEBUG("更新用户成功");
            return true;
        }

        LOG_ERROR("更新用户失败: 无影响行数");
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("更新用户异常: " + std::string(e.what()));
        return false;
    }
}

bool UserRepository::updatePassword(const std::string& user_id,
                                     const std::string& password_hash,
                                     const std::string& salt) {
    try {
        LOG_INFO("更新用户密码: user_id=" + user_id);

        auto mysql_conn = mysql_pool_->getConnection();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }

        // 先查出 username/email 用于清除索引缓存
        std::string cached_username, cached_email;
        {
            auto meta_stmt = mysql_conn->prepareStatement(
                "SELECT username, email FROM users WHERE user_id = ?");
            meta_stmt->setString(1, user_id);
            auto meta_result = meta_stmt->executeQuery();
            if (meta_result->next()) {
                cached_username = meta_result->getString("username");
                cached_email = meta_result->getString("email");
            }
        }

        std::string query = R"(
            UPDATE users
            SET password_hash = ?, salt = ?, updated_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, password_hash);
        stmt->setString(2, salt);
        stmt->setString(3, user_id);

        int affected_rows = stmt->executeUpdate();

        if (affected_rows > 0) {
            clearUserCache(user_id, cached_username, cached_email);
            LOG_INFO("更新用户密码成功: user_id=" + user_id);
            return true;
        }

        LOG_ERROR("更新用户密码失败: 无影响行数, user_id=" + user_id);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("更新用户密码异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 删除用户
 * @param user_id 用户ID
 * @return 成功返回true
 */
bool UserRepository::deleteUser(const std::string& user_id) {
    try {
        LOG_DEBUG("删除用户: user_id=" + user_id);
        
        // 先获取用户信息以清除缓存
        auto user = getUserById(user_id);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        std::string query = "DELETE FROM users WHERE user_id = ?";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user_id);
        
        int affected_rows = stmt->executeUpdate();
        // MySQL连接池使用RAII管理，不需要手动释放
        
        if (affected_rows > 0) {
            // 清除相关缓存
            if (user.has_value()) {
                clearUserCache(user->user_id, user->username, user->email);
            }
            
            LOG_DEBUG("删除用户成功");
            return true;
        }
        
        LOG_ERROR("删除用户失败: 无影响行数");
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("删除用户异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 检查用户名是否存在（接口实现）
 */
bool UserRepository::isUsernameExists(const std::string& username) {
    return usernameExists(username);  // 委托给现有的实现
}

/**
 * @brief 检查邮箱是否存在（接口实现）
 */
bool UserRepository::isEmailExists(const std::string& email) {
    return emailExists(email);  // 委托给现有的实现
}

/**
 * @brief 检查用户名是否存在
 * @param username 用户名
 * @return 存在返回true
 */
bool UserRepository::usernameExists(const std::string& username) {
    try {
        LOG_DEBUG("检查用户名是否存在: username=" + username);
        LOG_INFO("🔍 [DB_CHECK] 开始检查用户名: " + username);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败 - 无法检查用户名: " + username);
            // 🔧 修复：连接失败时抛出异常而不是返回false
            throw std::runtime_error("Database connection failed");
        }
        
        LOG_INFO("🔍 [DB_CHECK] MySQL连接获取成功，准备执行查询: " + username);
        
        std::string query = "SELECT COUNT(*) as count FROM users WHERE username = ?";
        LOG_INFO("🔍 [DB_CHECK] 执行SQL查询: " + query + ", 参数username=" + username);
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, username);
        
        auto result = stmt->executeQuery();
        LOG_INFO("🔍 [DB_CHECK] SQL查询执行完成，准备获取结果");
        
        if (result->next()) {
            int count = result->getInt("count");
            LOG_DEBUG("用户名检查结果: username=" + username + ", count=" + std::to_string(count));
            LOG_INFO("🔍 [USERNAME_CHECK] 用户名=" + username + ", 数据库count=" + std::to_string(count) + ", exists=" + (count > 0 ? "true" : "false"));
            // MySQL连接池使用RAII管理，不需要手动释放
            return count > 0;
        }
        
        LOG_WARNING("🔍 [DB_CHECK] 用户名检查查询无结果: " + username);
        LOG_ERROR("🚨 [DB_CHECK] 数据库查询没有返回结果，可能users表不存在或查询异常");
        // MySQL连接池使用RAII管理，不需要手动释放
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("检查用户名是否存在异常: " + std::string(e.what()) + ", username=" + username);
        // 🔧 修复：异常时抛出而不是返回false，让上层处理
        throw;
    }
}

/**
 * @brief 检查邮箱是否存在
 * @param email 邮箱
 * @return 存在返回true
 */
bool UserRepository::emailExists(const std::string& email) {
    try {
        LOG_DEBUG("检查邮箱是否存在: email=" + email);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败 - 无法检查邮箱: " + email);
            // 🔧 修复：连接失败时抛出异常而不是返回false
            throw std::runtime_error("Database connection failed");
        }
        
        std::string query = "SELECT COUNT(*) as count FROM users WHERE email = ?";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, email);
        
        auto result = stmt->executeQuery();
        
        if (result->next()) {
            int count = result->getInt("count");
            LOG_DEBUG("邮箱检查结果: email=" + email + ", count=" + std::to_string(count));
            // MySQL连接池使用RAII管理，不需要手动释放
            return count > 0;
        }
        
        LOG_WARNING("邮箱检查查询无结果: " + email);
        // MySQL连接池使用RAII管理，不需要手动释放
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("检查邮箱是否存在异常: " + std::string(e.what()) + ", email=" + email);
        // 🔧 修复：异常时抛出而不是返回false，让上层处理
        throw;
    }
}

/**
 * @brief 清除用户缓存
 * @param user_id 用户ID
 * @param username 用户名
 * @param email 邮箱
 */
void UserRepository::clearUserCache(const std::string& user_id, const std::string& username, const std::string& email) {
    try {
        // ==================== 新架构：使用 Redis Repository ====================
        if (user_redis_) {
            // 使新缓存失效
            user_redis_->invalidate(user_id);
            // 删除二级索引
            user_redis_->removeIndex("username", username);
            user_redis_->removeIndex("email", email);
            LOG_DEBUG("通过 Redis Repository 清除用户缓存完成");
        }

        // ==================== 旧架构：向后兼容 ====================
        std::vector<std::string> cache_keys = {
            cache_prefix_ + "info:" + user_id,
            cache_prefix_ + "username:" + username,
            cache_prefix_ + "email:" + email
        };

        for (const auto& key : cache_keys) {
            deleteFromCache(key);
        }

        LOG_DEBUG("清除用户缓存完成");

    } catch (const std::exception& e) {
        LOG_ERROR("清除用户缓存异常: " + std::string(e.what()));
    }
}

/**
 * @brief 从缓存获取数据
 * @tparam T 数据类型
 * @param key 缓存键
 * @return 缓存数据（可能为空）
 */
template<typename T>
std::optional<T> UserRepository::getFromCache(const std::string& key) {
    try {
        // 🔧 RAII优化：使用连接守护确保异常安全
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis_conn = conn_guard.get();
        if (!redis_conn) {
            LOG_WARNING("无法获取Redis连接，跳过缓存获取");
            return std::nullopt;
        }
        
        std::string cached_data = redis_conn->get(key);
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
        if (!cached_data.empty()) {
            try {
                // 反序列化JSON数据
                nlohmann::json json = nlohmann::json::parse(cached_data);
                T result = T::fromJson(json);
                return result;
            } catch (const std::exception& e) {
                LOG_ERROR("缓存数据反序列化失败: " + std::string(e.what()));
                return std::nullopt;
            }
        }
        
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("从缓存获取数据异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 设置缓存数据
 * @tparam T 数据类型
 * @param key 缓存键
 * @param value 缓存值
 * @param ttl 过期时间（秒）
 */
template<typename T>
void UserRepository::setToCache(const std::string& key, const T& value, int ttl) {
    try {
        // 🔧 RAII优化：使用连接守护确保异常安全
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis_conn = conn_guard.get();
        if (!redis_conn) {
            LOG_WARNING("无法获取Redis连接，跳过缓存存储");
            return;
        }
        
        // 序列化为JSON字符串
        // 🔧 修复：对于 UserInfo 类型，缓存时包含敏感字段（password_hash, salt）
        // 因为 user_service 是内部服务，缓存数据用于内部认证流程
        nlohmann::json json;
        if constexpr (std::is_same_v<T, UserInfo>) {
            json = value.toJson(true);  // 包含敏感字段用于认证
        } else {
            json = value.toJson();      // 其他类型使用默认序列化
        }
        std::string serialized_value = json.dump();
        
        redis_conn->set(key, serialized_value);
        redis_conn->expire(key, ttl);
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_ERROR("设置缓存数据异常: " + std::string(e.what()));
    }
}

/**
 * @brief 从缓存删除数据
 * @param key 缓存键
 */
void UserRepository::deleteFromCache(const std::string& key) {
    try {
        // 🔧 RAII优化：使用连接守护确保异常安全
        common::database::RedisConnectionGuard conn_guard(*redis_pool_);
        auto redis_conn = conn_guard.get();
        if (!redis_conn) {
            LOG_WARNING("无法获取Redis连接，跳过缓存删除");
            return;
        }
        
        redis_conn->del(key);
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_ERROR("从缓存删除数据异常: " + std::string(e.what()));
    }
}

// ==================== 用户档案操作实现 ====================

/**
 * @brief 获取用户档案
 */
std::optional<UserProfile> UserRepository::getUserProfile(const std::string& user_id) {
    try {
        LOG_DEBUG("获取用户档案: user_id=" + user_id);
        
        // 先尝试从缓存获取
        std::string cache_key = cache_prefix_ + "profile:" + user_id;
        auto cached_profile = getFromCache<UserProfile>(cache_key);
        if (cached_profile.has_value()) {
            LOG_DEBUG("从缓存获取用户档案成功");
            return cached_profile;
        }
        
        // 🔧 RAII修复：从数据库获取 - 使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return std::nullopt;
        }
        
        std::string query = R"(
            SELECT user_id, nickname, real_name, avatar_url, bio, location, 
                   website, birth_date, gender, language, timezone, 
                   created_at, updated_at
            FROM user_profiles 
            WHERE user_id = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user_id);
        auto result = stmt->executeQuery();
        
        if (result->next()) {
            UserProfile profile;
            profile.user_id = result->getString("user_id");
            profile.nickname = result->getString("nickname");
            profile.real_name = result->getString("real_name");
            profile.avatar_url = result->getString("avatar_url");
            profile.bio = result->getString("bio");
            profile.location = result->getString("location");
            profile.website = result->getString("website");
            profile.gender = convertGenderFromDbFormat(result->getString("gender"));
            profile.language = result->getString("language");
            profile.timezone = result->getString("timezone");
            // 时间字段处理简化，实际中需要从数据库时间戳转换
            profile.created_at = std::chrono::system_clock::now();
            profile.updated_at = std::chrono::system_clock::now();
            
            // 缓存结果
            setToCache(cache_key, profile, cache_ttl_);
            
            LOG_DEBUG("从数据库获取用户档案成功");
            return profile;
        }
        
        LOG_DEBUG("用户档案不存在");
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户档案异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 创建用户档案
 */
bool UserRepository::createUserProfile(const UserProfile& profile) {
    try {
        LOG_DEBUG("创建用户档案: user_id=" + profile.user_id);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        std::string query = R"(
            INSERT INTO user_profiles 
            (user_id, nickname, real_name, avatar_url, bio, location, 
             website, gender, language, timezone) 
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, profile.user_id);
        stmt->setString(2, profile.nickname);
        stmt->setString(3, profile.real_name);
        stmt->setString(4, profile.avatar_url);
        stmt->setString(5, profile.bio);
        stmt->setString(6, profile.location);
        stmt->setString(7, profile.website);
        stmt->setString(8, convertGenderToDbFormat(profile.gender));
        stmt->setString(9, profile.language);
        stmt->setString(10, profile.timezone);
        
        int affected_rows = stmt->executeUpdate();
        
        if (affected_rows > 0) {
            // 清除相关缓存
            deleteFromCache(cache_prefix_ + "profile:" + profile.user_id);
            
            LOG_DEBUG("创建用户档案成功");
            return true;
        }
        
        LOG_ERROR("创建用户档案失败: 无影响行数");
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建用户档案异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 更新用户档案
 */
bool UserRepository::updateUserProfile(const UserProfile& profile) {
    try {
        LOG_DEBUG("更新用户档案: user_id=" + profile.user_id);

        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }

        std::string query = R"(
            UPDATE user_profiles
            SET nickname = ?, real_name = ?, avatar_url = ?, bio = ?,
                location = ?, website = ?, gender = ?, language = ?,
                timezone = ?, updated_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, profile.nickname);
        stmt->setString(2, profile.real_name);
        stmt->setString(3, profile.avatar_url);
        stmt->setString(4, profile.bio);
        stmt->setString(5, profile.location);
        stmt->setString(6, profile.website);
        stmt->setString(7, convertGenderToDbFormat(profile.gender));
        stmt->setString(8, profile.language);
        stmt->setString(9, profile.timezone);
        stmt->setString(10, profile.user_id);

        int affected_rows = stmt->executeUpdate();

        if (affected_rows > 0) {
            // 🔧 修复：同时清除新架构和旧架构的缓存
            if (user_redis_) {
                user_redis_->invalidate(profile.user_id);
            }
            deleteFromCache(cache_prefix_ + "profile:" + profile.user_id);

            LOG_DEBUG("更新用户档案成功");
            return true;
        }

        LOG_ERROR("更新用户档案失败: 无影响行数");
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("更新用户档案异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 用户偏好设置操作实现 ====================

/**
 * @brief 获取用户偏好设置
 */
std::optional<UserPreferences> UserRepository::getUserPreferences(const std::string& user_id) {
    try {
        LOG_DEBUG("获取用户偏好设置: user_id=" + user_id);
        
        // 先尝试从缓存获取
        std::string cache_key = cache_prefix_ + "preferences:" + user_id;
        auto cached_preferences = getFromCache<UserPreferences>(cache_key);
        if (cached_preferences.has_value()) {
            LOG_DEBUG("从缓存获取用户偏好设置成功");
            return cached_preferences;
        }
        
        // 🔧 RAII修复：从数据库获取 - 使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return std::nullopt;
        }
        
        std::string query = R"(
            SELECT user_id, email_notifications, push_notifications, sms_notifications,
                   theme, language, sound_effects, music, volume, auto_login,
                   remember_me, session_timeout, custom_settings,
                   created_at, updated_at
            FROM user_preferences 
            WHERE user_id = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user_id);
        auto result = stmt->executeQuery();
        
        if (result->next()) {
            UserPreferences preferences;
            preferences.user_id = result->getString("user_id");
            preferences.email_notifications = result->getBoolean("email_notifications");
            preferences.push_notifications = result->getBoolean("push_notifications");
            preferences.sms_notifications = result->getBoolean("sms_notifications");
            preferences.theme = result->getString("theme");
            preferences.language = result->getString("language");
            preferences.sound_effects = result->getBoolean("sound_effects");
            preferences.music = result->getBoolean("music");
            preferences.volume = result->getInt("volume");
            preferences.auto_login = result->getBoolean("auto_login");
            preferences.remember_me = result->getBoolean("remember_me");
            preferences.session_timeout = result->getInt("session_timeout");
            
            // 解析custom_settings JSON
            std::string custom_settings_str = result->getString("custom_settings");
            if (!custom_settings_str.empty()) {
                try {
                    preferences.custom_settings = nlohmann::json::parse(custom_settings_str);
                } catch (const std::exception& e) {
                    LOG_WARNING("解析custom_settings JSON失败: " + std::string(e.what()));
                    preferences.custom_settings = nlohmann::json::object();
                }
            }
            
            // 时间字段处理简化
            preferences.created_at = std::chrono::system_clock::now();
            preferences.updated_at = std::chrono::system_clock::now();
            
            // 缓存结果
            setToCache(cache_key, preferences, cache_ttl_);
            
            LOG_DEBUG("从数据库获取用户偏好设置成功");
            return preferences;
        }
        
        LOG_DEBUG("用户偏好设置不存在");
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户偏好设置异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

/**
 * @brief 创建用户偏好设置
 */
bool UserRepository::createUserPreferences(const UserPreferences& preferences) {
    try {
        LOG_DEBUG("创建用户偏好设置: user_id=" + preferences.user_id);
        
        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        std::string query = R"(
            INSERT INTO user_preferences 
            (user_id, email_notifications, push_notifications, sms_notifications,
             theme, language, sound_effects, music, volume, auto_login,
             remember_me, session_timeout, custom_settings) 
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, preferences.user_id);
        stmt->setBoolean(2, preferences.email_notifications);
        stmt->setBoolean(3, preferences.push_notifications);
        stmt->setBoolean(4, preferences.sms_notifications);
        stmt->setString(5, preferences.theme);
        stmt->setString(6, preferences.language);
        stmt->setBoolean(7, preferences.sound_effects);
        stmt->setBoolean(8, preferences.music);
        stmt->setInt(9, preferences.volume);
        stmt->setBoolean(10, preferences.auto_login);
        stmt->setBoolean(11, preferences.remember_me);
        stmt->setInt(12, preferences.session_timeout);
        stmt->setString(13, preferences.custom_settings.dump());
        
        int affected_rows = stmt->executeUpdate();
        
        if (affected_rows > 0) {
            // 清除相关缓存
            deleteFromCache(cache_prefix_ + "preferences:" + preferences.user_id);
            
            LOG_DEBUG("创建用户偏好设置成功");
            return true;
        }
        
        LOG_ERROR("创建用户偏好设置失败: 无影响行数");
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建用户偏好设置异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 更新用户偏好设置
 */
bool UserRepository::updateUserPreferences(const UserPreferences& preferences) {
    try {
        LOG_DEBUG("更新用户偏好设置: user_id=" + preferences.user_id);

        // 🔧 RAII修复：使用连接守护确保异常安全
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }

        std::string query = R"(
            UPDATE user_preferences
            SET email_notifications = ?, push_notifications = ?, sms_notifications = ?,
                theme = ?, language = ?, sound_effects = ?, music = ?, volume = ?,
                auto_login = ?, remember_me = ?, session_timeout = ?,
                custom_settings = ?, updated_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setBoolean(1, preferences.email_notifications);
        stmt->setBoolean(2, preferences.push_notifications);
        stmt->setBoolean(3, preferences.sms_notifications);
        stmt->setString(4, preferences.theme);
        stmt->setString(5, preferences.language);
        stmt->setBoolean(6, preferences.sound_effects);
        stmt->setBoolean(7, preferences.music);
        stmt->setInt(8, preferences.volume);
        stmt->setBoolean(9, preferences.auto_login);
        stmt->setBoolean(10, preferences.remember_me);
        stmt->setInt(11, preferences.session_timeout);
        stmt->setString(12, preferences.custom_settings.dump());
        stmt->setString(13, preferences.user_id);

        int affected_rows = stmt->executeUpdate();

        if (affected_rows > 0) {
            // 🔧 修复：同时清除新架构和旧架构的缓存
            if (user_redis_) {
                user_redis_->invalidate(preferences.user_id);
            }
            deleteFromCache(cache_prefix_ + "preferences:" + preferences.user_id);

            LOG_DEBUG("更新用户偏好设置成功");
            return true;
        }

        LOG_ERROR("更新用户偏好设置失败: 无影响行数");
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("更新用户偏好设置异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 用户状态管理操作实现 ====================

/**
 * @brief 更新用户在线状态
 */
bool UserRepository::updateOnlineStatus(const std::string& user_id, OnlineStatus status) {
    try {
        LOG_DEBUG("更新用户在线状态: user_id=" + user_id + ", 长度=" + std::to_string(user_id.length()));
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }

        // 🔧 调试：先检查用户是否存在
        {
            std::string check_query = "SELECT user_id, username FROM users WHERE user_id = ?";
            auto check_stmt = mysql_conn->prepareStatement(check_query);
            check_stmt->setString(1, user_id);
            auto check_result = check_stmt->executeQuery();
            if (check_result->next()) {
                LOG_DEBUG("用户存在检查: user_id=" + check_result->getString("user_id") +
                         ", username=" + check_result->getString("username"));
            } else {
                LOG_ERROR("用户不存在: user_id=" + user_id);
                return false;
            }
        }

        // 🔧 关键修复：使用数字而不是字符串设置online_status（TINYINT类型）
        // 数据库中：0=离线, 1=在线, 2=离开, 3=忙碌
        int status_value;
        switch (status) {
            case OnlineStatus::OFFLINE: status_value = 0; break;
            case OnlineStatus::ONLINE: status_value = 1; break;
            case OnlineStatus::AWAY: status_value = 2; break;
            case OnlineStatus::BUSY: status_value = 3; break;
            default: status_value = 0; break;
        }

        LOG_DEBUG("准备执行UPDATE: status_value=" + std::to_string(status_value));

        std::string query = R"(
            UPDATE users
            SET online_status = ?, updated_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setInt(1, status_value);
        stmt->setString(2, user_id);

        int affected_rows = stmt->executeUpdate();

        if (affected_rows > 0) {
            // 🔧 修复：获取用户的 username 和 email 以清除所有相关缓存
            // 先查询用户的 username 和 email
            std::string username, email;
            {
                std::string query_meta = "SELECT username, email FROM users WHERE user_id = ?";
                auto stmt_meta = mysql_conn->prepareStatement(query_meta);
                stmt_meta->setString(1, user_id);
                auto result_meta = stmt_meta->executeQuery();
                if (result_meta->next()) {
                    username = result_meta->getString("username");
                    email = result_meta->getString("email");
                }
            }

            // 使用 clearUserCache 方法清除所有相关缓存
            clearUserCache(user_id, username, email);

            LOG_DEBUG("更新用户在线状态成功");
            return true;
        }

        // affected_rows == 0 表示行存在但值未变化（已是目标状态），视为成功
        LOG_DEBUG("用户在线状态无需更新（已是目标状态）: user_id=" + user_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("更新用户在线状态异常: " + std::string(e.what()));
        return false;
    }
}

// ==================== 统计方法实现 ====================

/**
 * @brief 获取用户总数
 */
int UserRepository::getUserCount() {
    try {
        LOG_DEBUG("获取用户总数统计");
        
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return 0;
        }
        
        // 🔧 关键修复：使用数字而不是字符串查询status（TINYINT类型）
        // 数据库中：0=正常, 1=未激活, 2=暂停, 3=封禁, 4=已删除
        std::string query = "SELECT COUNT(*) as count FROM users WHERE status != 4";
        auto stmt = mysql_conn->prepareStatement(query);
        auto result = stmt->executeQuery();
        
        if (result->next()) {
            int count = result->getInt("count");
            LOG_DEBUG("用户总数统计: " + std::to_string(count));
            return count;
        }
        
        return 0;
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户总数异常: " + std::string(e.what()));
        return 0;
    }
}

/**
 * @brief 获取在线用户数
 */
int UserRepository::getOnlineUserCount() {
    try {
        LOG_DEBUG("获取在线用户数统计");
        
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return 0;
        }
        
        // 🔧 关键修复：使用数字而不是字符串查询online_status（TINYINT类型）
        // 数据库中：0=离线, 1=在线, 2=离开, 3=忙碌
        std::string query = R"(
            SELECT COUNT(*) as count 
            FROM users 
            WHERE online_status = 1 
            AND status != 4
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        auto result = stmt->executeQuery();
        
        if (result->next()) {
            int count = result->getInt("count");
            LOG_DEBUG("在线用户数统计: " + std::to_string(count));
            return count;
        }
        
        return 0;
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取在线用户数异常: " + std::string(e.what()));
        return 0;
    }
}

/**
 * @brief 获取今日注册用户数
 */
int UserRepository::getTodayRegisteredUserCount() {
    try {
        LOG_DEBUG("获取今日注册用户数统计");
        
        // 🔧 RAII修复：使用连接守护确保连接自动归还
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return 0;
        }
        
        std::string query = R"(
            SELECT COUNT(*) as count 
            FROM users 
            WHERE DATE(created_at) = CURDATE()
            AND status != 'deleted'
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        auto result = stmt->executeQuery();
        
        if (result->next()) {
            int count = result->getInt("count");
            LOG_DEBUG("今日注册用户数统计: " + std::to_string(count));
            return count;
        }
        
        return 0;
        // 🔧 RAII：连接会在conn_guard析构时自动归还
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取今日注册用户数异常: " + std::string(e.what()));
        return 0;
    }
}

/**
 * @brief 更新最后登录信息
 */
bool UserRepository::updateLastLogin(const std::string& user_id, const std::string& login_ip) {
    try {
        LOG_DEBUG("更新最后登录信息: user_id=" + user_id);
        
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
      
        
        std::string query = R"(
            UPDATE users 
            SET last_login_at = NOW(), last_login_ip = ?, updated_at = NOW()
            WHERE user_id = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, login_ip);
        stmt->setString(2, user_id);
        
        int affected_rows = stmt->executeUpdate();
        
        if (affected_rows > 0) {
            // 清除相关缓存
            // 🔧 修复：同时清除新架构和旧架构的缓存
            if (user_redis_) {
                user_redis_->invalidate(user_id);
            }
            deleteFromCache(cache_prefix_ + "info:" + user_id);

            LOG_DEBUG("更新最后登录信息成功");
            return true;
        }

        // affected_rows == 0 表示行存在但值未变化，视为成功
        LOG_DEBUG("最后登录信息无需更新（值未变化）: user_id=" + user_id);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新最后登录信息异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 增加登录失败次数
 */
int UserRepository::incrementLoginAttempts(const std::string& user_id) {
    try {
        LOG_DEBUG("增加登录失败次数: user_id=" + user_id);
        
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return 0;
        }
        
        std::string query = R"(
            UPDATE users 
            SET login_attempts = login_attempts + 1, updated_at = NOW()
            WHERE user_id = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user_id);
        
        int affected_rows = stmt->executeUpdate();
        
        if (affected_rows > 0) {
            // 查询更新后的失败次数
            std::string query_select = "SELECT login_attempts FROM users WHERE user_id = ?";
            auto stmt_select = mysql_conn->prepareStatement(query_select);
            stmt_select->setString(1, user_id);
            auto result = stmt_select->executeQuery();
            
            if (result->next()) {
                int attempts = result->getInt("login_attempts");

                // 清除相关缓存
                // 🔧 修复：同时清除新架构和旧架构的缓存
                if (user_redis_) {
                    user_redis_->invalidate(user_id);
                }
                deleteFromCache(cache_prefix_ + "info:" + user_id);

                LOG_DEBUG("增加登录失败次数成功，当前次数: " + std::to_string(attempts));
                return attempts;
            }
        }
        
        LOG_ERROR("增加登录失败次数失败");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("增加登录失败次数异常: " + std::string(e.what()));
        return 0;
    }
}

/**
 * @brief 重置登录失败次数
 */
bool UserRepository::resetLoginAttempts(const std::string& user_id) {
    try {
        LOG_DEBUG("重置登录失败次数: user_id=" + user_id + ", 长度=" + std::to_string(user_id.length()));

        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }

        // 🔧 调试：先检查用户是否存在
        {
            std::string check_query = "SELECT user_id, username FROM users WHERE user_id = ?";
            auto check_stmt = mysql_conn->prepareStatement(check_query);
            check_stmt->setString(1, user_id);
            auto check_result = check_stmt->executeQuery();
            if (check_result->next()) {
                LOG_DEBUG("用户存在检查: user_id=" + check_result->getString("user_id") +
                         ", username=" + check_result->getString("username"));
            } else {
                LOG_ERROR("用户不存在: user_id=" + user_id);
                return false;
            }
        }

        std::string query = R"(
            UPDATE users
            SET login_attempts = 0, updated_at = NOW()
            WHERE user_id = ?
        )";

        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, user_id);
        
        int affected_rows = stmt->executeUpdate();

        // 🔧 修复：MySQL 在设置列为当前值时返回 affected_rows = 0
        // 对于新用户，login_attempts 默认为 0，所以 SET login_attempts = 0 不影响任何行
        // 这不是错误，而是目标已经达成
        if (affected_rows > 0) {
            // 清除相关缓存
            // 🔧 修复：同时清除新架构和旧架构的缓存
            if (user_redis_) {
                user_redis_->invalidate(user_id);
            }
            deleteFromCache(cache_prefix_ + "info:" + user_id);

            LOG_DEBUG("重置登录失败次数成功");
            return true;
        }

        // affected_rows = 0 可能有两种情况：
        // 1. 用户不存在（真正的错误）
        // 2. login_attempts 已经是 0（成功）
        // 由于调用此方法前已经验证用户存在（在 handleLoginRequest 中），
        // 所以这里 affected_rows = 0 意味着 login_attempts 已经是 0
        LOG_DEBUG("重置登录失败次数成功（值未变化，login_attempts 已为 0）");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("重置登录失败次数异常: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 锁定用户直到指定时间
 */
bool UserRepository::lockUserUntil(const std::string& user_id, const std::chrono::system_clock::time_point& lock_until) {
    try {
        LOG_DEBUG("锁定用户: user_id=" + user_id);
        
        common::database::MySQLConnectionGuard conn_guard(*mysql_pool_);
        auto mysql_conn = conn_guard.get();
        if (!mysql_conn) {
            LOG_ERROR("获取MySQL连接失败");
            return false;
        }
        
        // 转换时间点为字符串
        auto time_t = std::chrono::system_clock::to_time_t(lock_until);
        auto tm = *std::localtime(&time_t);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        
        std::string query = R"(
            UPDATE users 
            SET locked_until = ?, updated_at = NOW()
            WHERE user_id = ?
        )";
        
        auto stmt = mysql_conn->prepareStatement(query);
        stmt->setString(1, buffer);
        stmt->setString(2, user_id);
        
        int affected_rows = stmt->executeUpdate();
        
        if (affected_rows > 0) {
            // 清除相关缓存
            // 🔧 修复：同时清除新架构和旧架构的缓存
            if (user_redis_) {
                user_redis_->invalidate(user_id);
            }
            deleteFromCache(cache_prefix_ + "info:" + user_id);

            LOG_DEBUG("锁定用户成功，锁定至: " + std::string(buffer));
            return true;
        }

        LOG_ERROR("锁定用户失败: 无影响行数");
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("锁定用户异常: " + std::string(e.what()));
        return false;
    }
}

} // namespace user_service
} // namespace core_services




