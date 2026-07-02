#pragma once

/**
 * @file user_repository.h
 * @brief 用户数据访问层 - 符合推荐分层架构
 * @details 专门负责user_service_db数据库操作，使用统一的 Repository 模式管理缓存
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0 - 集成统一 Redis Repository 模式
 */

#include "user_models.h"
#include "user_redis_repository.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include <memory>
#include <vector>
#include <optional>

namespace core_services {
namespace user_service {

/**
 * @brief 用户数据访问层
 * @details 专门负责user_service_db数据库的CRUD操作
 */
class UserRepository {
public:
    /**
     * @brief 构造函数
     * @param mysql_pool MySQL连接池
     * @param redis_pool Redis连接池
     */
    UserRepository(std::shared_ptr<common::database::MySQLPool> mysql_pool,
                   std::shared_ptr<common::database::RedisPool> redis_pool);
    
    /**
     * @brief 析构函数
     */
    ~UserRepository();
    
    /**
     * @brief 初始化数据库表结构
     * @return 初始化成功返回true
     */
    bool initializeTables();
    
    /**
     * @brief 初始化数据库连接
     * @return 初始化成功返回true
     */
    bool initialize();
    
    /**
     * @brief 测试数据库连接
     * @return 连接正常返回true
     */
    bool testDatabaseConnections();
    
    // ==================== 用户基础信息操作 (users表) ====================
    
    /**
     * @brief 创建用户
     * @param user_info 用户信息
     * @return 创建成功返回true
     */
    bool createUser(const UserInfo& user_info);
    
    /**
     * @brief 创建完整用户（包含基础信息、档案、偏好设置）
     * @param user_info 用户信息
     * @param nickname 昵称
     * @param avatar_url 头像URL
     * @return 创建成功返回true
     */
    bool createCompleteUser(const UserInfo& user_info, const std::string& nickname, const std::string& avatar_url = "");
    
    /**
     * @brief 根据用户ID获取用户信息
     * @param user_id 用户ID
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getUserById(const std::string& user_id);
    
    /**
     * @brief 根据用户名获取用户信息
     * @param username 用户名
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getUserByUsername(const std::string& username);
    
    /**
     * @brief 根据邮箱获取用户信息
     * @param email 邮箱
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getUserByEmail(const std::string& email);
    
    /**
     * @brief 更新用户信息
     * @param user_info 用户信息
     * @return 更新成功返回true
     */
    bool updateUser(const UserInfo& user_info);

    /**
     * @brief 更新用户密码
     * @param user_id 用户ID
     * @param password_hash 新密码哈希
     * @param salt 盐值
     * @return 更新成功返回true
     */
    bool updatePassword(const std::string& user_id, const std::string& password_hash, const std::string& salt);

    /**
     * @brief 检查用户名是否存在
     * @param username 用户名
     * @return 存在返回true
     */
    bool isUsernameExists(const std::string& username);
    
    /**
     * @brief 检查邮箱是否存在
     * @param email 邮箱
     * @return 存在返回true
     */
    bool isEmailExists(const std::string& email);
    
    /**
     * @brief 删除用户
     * @param user_id 用户ID
     * @return 删除成功返回true
     */
    bool deleteUser(const std::string& user_id);
    
    /**
     * @brief 检查用户名是否存在
     * @param username 用户名
     * @return 存在返回true
     */
    bool usernameExists(const std::string& username);
    
    /**
     * @brief 检查邮箱是否存在
     * @param email 邮箱
     * @return 存在返回true
     */
    bool emailExists(const std::string& email);
    
    /**
     * @brief 更新用户在线状态
     * @param user_id 用户ID
     * @param status 在线状态
     * @return 更新成功返回true
     */
    bool updateOnlineStatus(const std::string& user_id, OnlineStatus status);
    
    /**
     * @brief 更新最后登录信息
     * @param user_id 用户ID
     * @param login_ip 登录IP
     * @return 更新成功返回true
     */
    bool updateLastLogin(const std::string& user_id, const std::string& login_ip);
    
    /**
     * @brief 增加登录失败次数
     * @param user_id 用户ID
     * @return 当前失败次数
     */
    int incrementLoginAttempts(const std::string& user_id);
    
    /**
     * @brief 重置登录失败次数
     * @param user_id 用户ID
     * @return 重置成功返回true
     */
    bool resetLoginAttempts(const std::string& user_id);
    
    /**
     * @brief 锁定用户直到指定时间
     * @param user_id 用户ID
     * @param lock_until 锁定截止时间
     * @return 锁定成功返回true
     */
    bool lockUserUntil(const std::string& user_id, const std::chrono::system_clock::time_point& lock_until);
    
    // ==================== 用户档案操作 (user_profiles表) ====================
    
    /**
     * @brief 创建用户档案
     * @param profile 用户档案
     * @return 创建成功返回true
     */
    bool createUserProfile(const UserProfile& profile);
    
    /**
     * @brief 获取用户档案
     * @param user_id 用户ID
     * @return 用户档案，不存在返回空
     */
    std::optional<UserProfile> getUserProfile(const std::string& user_id);
    
    /**
     * @brief 更新用户档案
     * @param profile 用户档案
     * @return 更新成功返回true
     */
    bool updateUserProfile(const UserProfile& profile);
    
    /**
     * @brief 删除用户档案
     * @param user_id 用户ID
     * @return 删除成功返回true
     */
    bool deleteUserProfile(const std::string& user_id);
    
    // ==================== 用户偏好设置操作 (user_preferences表) ====================
    
    /**
     * @brief 创建用户偏好设置
     * @param preferences 偏好设置
     * @return 创建成功返回true
     */
    bool createUserPreferences(const UserPreferences& preferences);
    
    /**
     * @brief 获取用户偏好设置
     * @param user_id 用户ID
     * @return 偏好设置，不存在返回空
     */
    std::optional<UserPreferences> getUserPreferences(const std::string& user_id);
    
    /**
     * @brief 更新用户偏好设置
     * @param preferences 偏好设置
     * @return 更新成功返回true
     */
    bool updateUserPreferences(const UserPreferences& preferences);
    
    /**
     * @brief 删除用户偏好设置
     * @param user_id 用户ID
     * @return 删除成功返回true
     */
    bool deleteUserPreferences(const std::string& user_id);
    
    // ==================== 用户会话操作 (user_sessions表) ====================
    
    /**
     * @brief 创建用户会话
     * @param session 会话信息
     * @return 创建成功返回true
     */
    bool createUserSession(const UserSession& session);
    
    /**
     * @brief 获取用户会话
     * @param session_id 会话ID
     * @return 会话信息，不存在返回空
     */
    std::optional<UserSession> getUserSession(const std::string& session_id);
    
    /**
     * @brief 获取用户的所有活跃会话
     * @param user_id 用户ID
     * @return 会话列表
     */
    std::vector<UserSession> getUserActiveSessions(const std::string& user_id);
    
    /**
     * @brief 更新会话信息
     * @param session 会话信息
     * @return 更新成功返回true
     */
    bool updateUserSession(const UserSession& session);
    
    /**
     * @brief 删除会话
     * @param session_id 会话ID
     * @return 删除成功返回true
     */
    bool deleteUserSession(const std::string& session_id);
    
    /**
     * @brief 删除用户的所有会话
     * @param user_id 用户ID
     * @return 删除成功返回true
     */
    bool deleteAllUserSessions(const std::string& user_id);
    
    /**
     * @brief 清理过期会话
     * @return 清理的会话数量
     */
    int cleanupExpiredSessions();
    
    // ==================== 缓存操作 ====================
    
    /**
     * @brief 缓存用户信息
     * @param user_info 用户信息
     * @param ttl_seconds 缓存时间(秒)
     * @return 缓存成功返回true
     */
    bool cacheUserInfo(const UserInfo& user_info, int ttl_seconds = 300);
    
    /**
     * @brief 从缓存获取用户信息
     * @param user_id 用户ID
     * @return 用户信息，不存在返回空
     */
    std::optional<UserInfo> getCachedUserInfo(const std::string& user_id);
    
    /**
     * @brief 清除用户缓存
     * @param user_id 用户ID
     * @return 清除成功返回true
     */
    bool clearUserCache(const std::string& user_id);
    
    /**
     * @brief 清除用户缓存 (多个键)
     * @param user_id 用户ID
     * @param username 用户名
     * @param email 邮箱
     */
    void clearUserCache(const std::string& user_id, const std::string& username, const std::string& email);
    
    /**
     * @brief 缓存用户档案
     * @param profile 用户档案
     * @param ttl_seconds 缓存时间(秒)
     * @return 缓存成功返回true
     */
    bool cacheUserProfile(const UserProfile& profile, int ttl_seconds = 600);
    
    /**
     * @brief 从缓存获取用户档案
     * @param user_id 用户ID
     * @return 用户档案，不存在返回空
     */
    std::optional<UserProfile> getCachedUserProfile(const std::string& user_id);
    
    // ==================== 统计和查询 ====================
    
    /**
     * @brief 获取用户总数
     * @return 用户总数
     */
    int getUserCount();
    
    /**
     * @brief 获取在线用户数
     * @return 在线用户数
     */
    int getOnlineUserCount();
    
    /**
     * @brief 获取今日注册用户数
     * @return 今日注册用户数
     */
    int getTodayRegisteredUserCount();
    
    /**
     * @brief 根据状态获取用户列表
     * @param status 用户状态
     * @param offset 偏移量
     * @param limit 限制数量
     * @return 用户列表
     */
    std::vector<UserInfo> getUsersByStatus(UserStatus status, int offset = 0, int limit = 100);
    
    /**
     * @brief 搜索用户
     * @param keyword 关键词
     * @param offset 偏移量
     * @param limit 限制数量
     * @return 用户列表
     */
    std::vector<UserInfo> searchUsers(const std::string& keyword, int offset = 0, int limit = 50);

private:
    std::shared_ptr<common::database::MySQLPool> mysql_pool_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    std::string cache_prefix_ = "user_service:";
    int cache_ttl_ = 3600;  // 默认缓存1小时

    // ==================== 新架构：统一 Redis Repository ====================
    // 使用类型化的 Redis Repository 替代直接 Redis 操作
    std::unique_ptr<UserRedisRepository> user_redis_;              ///< 用户信息缓存
    std::unique_ptr<UserProfileRedisRepository> profile_redis_;    ///< 用户档案缓存
    std::unique_ptr<UserPreferencesRedisRepository> prefs_redis_;  ///< 用户偏好缓存

    // ==================== 旧架构（向后兼容，逐步废弃） ====================
    // 缓存相关的模板方法（将逐步迁移到 Redis Repository）
    template<typename T>
    std::optional<T> getFromCache(const std::string& key);

    template<typename T>
    void setToCache(const std::string& key, const T& value, int ttl);

    void deleteFromCache(const std::string& key);

    // 缓存键生成（将逐步使用 RedisKeyBuilder 替代）
    std::string getUserCacheKey(const std::string& user_id) const;
    std::string getUserProfileCacheKey(const std::string& user_id) const;
    std::string getUserPreferencesCacheKey(const std::string& user_id) const;
    std::string getUsernameCacheKey(const std::string& username) const;
    std::string getEmailCacheKey(const std::string& email) const;

    // 数据库操作辅助方法
    bool executeQuery(const std::string& query, const std::vector<std::string>& params = {});
    std::optional<UserInfo> parseUserInfoFromResultSet(void* result_set);
    std::optional<UserProfile> parseUserProfileFromResultSet(void* result_set);
    std::optional<UserPreferences> parseUserPreferencesFromResultSet(void* result_set);
    std::optional<UserSession> parseUserSessionFromResultSet(void* result_set);

    // 时间转换辅助方法
    std::string timePointToString(const std::chrono::system_clock::time_point& time_point) const;
    std::chrono::system_clock::time_point stringToTimePoint(const std::string& time_str) const;

    // ==================== 新架构初始化方法 ====================
    /**
     * @brief 初始化 Redis Repository
     * @details 创建类型化的 Redis Repository 实例
     */
    void initializeRedisRepositories();

    // 禁用拷贝构造和赋值
    UserRepository(const UserRepository&) = delete;
    UserRepository& operator=(const UserRepository&) = delete;
};

} // namespace user_service
} // namespace core_services




