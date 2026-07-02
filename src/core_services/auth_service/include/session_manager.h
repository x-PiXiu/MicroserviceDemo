/**
 * @file session_manager.h
 * @brief 会话管理器 - 负责用户会话的创建、维护和管理
 * @author 29108
 * @date 2025/7/21
 * @version 2.0 - 集成统一 Redis Repository 模式
 *
 * 功能特性:
 * - 用户会话创建和销毁
 * - 多设备登录支持
 * - 会话状态同步
 * - 会话安全验证
 * - 会话超时管理
 * - 并发会话控制
 *
 * 安全特性:
 * - 会话令牌防伪造
 * - IP地址验证
 * - 设备指纹验证
 * - 会话劫持检测
 * - 异常登录检测
 *
 * 与现有模块集成:
 * - 使用Redis进行会话存储
 * - 使用ThreadPool进行异步处理
 * - 使用ConfigManager进行配置管理
 * - 使用Logger进行日志记录
 * - 集成JWT管理器进行令牌管理
 * - 集成统一 Redis Repository 模式
 */

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "user_models.h"
#include "jwt_manager.h"
#include "session_redis_repository.h"
#include "common/database/redis_pool.h"
#include "common/thread_pool/thread_pool.h"
#include "common/config/config_manager.h"
#include "common/logger/logger.h"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <future>
#include <nlohmann/json.hpp>

namespace core_services {
    namespace auth_service {

        /**
         * @brief 会话验证结果
         */
        struct SessionValidationResult {
            bool valid = false;                                     ///< 是否有效
            std::string error_message;                              ///< 错误消息
            std::string error_code;                                 ///< 错误代码
            std::optional<UserSession> session;                     ///< 会话信息（有效时）
            bool expired = false;                                   ///< 是否过期
            bool ip_mismatch = false;                               ///< IP地址不匹配
            bool device_mismatch = false;                           ///< 设备不匹配
            bool concurrent_limit_exceeded = false;                 ///< 超出并发限制

            /**
             * @brief 默认构造函数
             */
            SessionValidationResult() = default;

            /**
             * @brief 成功结果构造函数
             * @param session 会话信息
             */
            explicit SessionValidationResult(const UserSession& session);

            /**
             * @brief 失败结果构造函数
             * @param error_message 错误消息
             * @param error_code 错误代码
             */
            SessionValidationResult(const std::string& error_message, const std::string& error_code);

            /**
             * @brief 转换为JSON对象
             * @return JSON对象
             */
            nlohmann::json toJson() const;
        };

        // DeviceInfo 已移动到 session_redis_repository.h 中定义

        /**
         * @brief 会话管理器类
         * @details 负责用户会话的完整生命周期管理
         */
        class SessionManager {
        public:
            /**
             * @brief 配置结构体
             */
            struct Config {
                std::chrono::seconds session_timeout{3600};         ///< 会话超时时间（1小时）
                std::chrono::seconds max_idle_time{1800};           ///< 最大空闲时间（30分钟）
                int max_concurrent_sessions = 5;                    ///< 最大并发会话数
                bool enable_multi_device = true;                    ///< 是否允许多设备登录
                bool enable_ip_validation = true;                   ///< 是否启用IP验证
                bool enable_device_validation = false;              ///< 是否启用设备验证
                bool enable_session_migration = true;               ///< 是否启用会话迁移
                std::chrono::seconds cleanup_interval{300};         ///< 清理间隔（5分钟）
                bool enable_session_encryption = true;              ///< 是否启用会话加密
                std::string encryption_key;                          ///< 加密密钥（必须配置，不再有默认值）
                bool enable_anomaly_detection = true;               ///< 是否启用异常检测
                double max_ip_change_frequency = 0.1;               ///< 最大IP变化频率（每小时）

                /**
                 * @brief 从配置管理器加载配置
                 * @return 会话管理器配置对象
                 */
                static Config fromConfigManager();

                /**
                 * @brief 验证配置有效性
                 * @return 验证结果，成功返回空字符串
                 */
                std::string validate() const;
            };

            /**
             * @brief 构造函数
             * @param config 配置
             * @param redis_pool Redis连接池
             * @param jwt_manager JWT管理器
             * @param thread_pool 线程池
             */
            SessionManager(const Config& config,
                          std::shared_ptr<common::database::RedisPool> redis_pool,
                          std::shared_ptr<JwtManager> jwt_manager,
                          std::shared_ptr<common::thread_pool::ThreadPool> thread_pool = nullptr);

            /**
             * @brief 析构函数
             */
            ~SessionManager();

            // ==================== 会话生命周期管理 ====================

            /**
             * @brief 创建用户会话
             * @param user_info 用户信息
             * @param client_ip 客户端IP
             * @param user_agent 用户代理字符串
             * @param device_id 设备ID（可选）
             * @return 会话信息，创建失败返回nullopt
             */
            std::optional<UserSession> createSession(const CoreUserInfo& user_info,
                                                    const std::string& client_ip,
                                                    const std::string& user_agent,
                                                    const std::string& device_id = "");

            /**
             * @brief 验证会话
             * @param session_id 会话ID
             * @param client_ip 客户端IP
             * @param user_agent 用户代理字符串
             * @return 验证结果
             */
            SessionValidationResult validateSession(const std::string& session_id,
                                                   const std::string& client_ip,
                                                   const std::string& user_agent = "");

            /**
             * @brief 更新会话活动时间
             * @param session_id 会话ID
             * @param client_ip 客户端IP
             * @return 更新成功返回true
             */
            bool updateSessionActivity(const std::string& session_id, const std::string& client_ip = "");

            /**
             * @brief 销毁会话
             * @param session_id 会话ID
             * @return 销毁成功返回true
             */
            bool destroySession(const std::string& session_id);

            /**
             * @brief 销毁用户的所有会话
             * @param user_id 用户ID
             * @return 销毁的会话数量
             */
            int destroyAllUserSessions(const std::string& user_id);

            /**
             * @brief 销毁用户在指定设备上的会话
             * @param user_id 用户ID
             * @param device_id 设备ID
             * @return 销毁的会话数量
             */
            int destroyUserDeviceSessions(const std::string& user_id, const std::string& device_id);

            // ==================== 会话查询和管理 ====================

            /**
             * @brief 获取会话信息
             * @param session_id 会话ID
             * @return 会话信息，未找到返回nullopt
             */
            std::optional<UserSession> getSession(const std::string& session_id);

            /**
             * @brief 获取用户的所有活跃会话
             * @param user_id 用户ID
             * @return 会话信息列表
             */
            std::vector<UserSession> getUserActiveSessions(const std::string& user_id);

            /**
             * @brief 获取用户在指定设备上的会话
             * @param user_id 用户ID
             * @param device_id 设备ID
             * @return 会话信息列表
             */
            std::vector<UserSession> getUserDeviceSessions(const std::string& user_id, const std::string& device_id);

            /**
             * @brief 检查用户是否在线
             * @param user_id 用户ID
             * @return 如果用户在线返回true
             */
            bool isUserOnline(const std::string& user_id);

            /**
             * @brief 获取在线用户数量
             * @return 在线用户数量
             */
            int64_t getOnlineUserCount();

            /**
             * @brief 获取活跃会话数量
             * @return 活跃会话数量
             */
            int64_t getActiveSessionCount() const;

            // ==================== 设备管理 ====================

            /**
             * @brief 注册设备
             * @param user_id 用户ID
             * @param device_info 设备信息
             * @return 注册成功返回true
             */
            bool registerDevice(const std::string& user_id, const DeviceInfo& device_info);

            /**
             * @brief 获取用户设备列表
             * @param user_id 用户ID
             * @return 设备信息列表
             */
            std::vector<DeviceInfo> getUserDevices(const std::string& user_id);

            /**
             * @brief 设置设备信任状态
             * @param user_id 用户ID
             * @param device_id 设备ID
             * @param trusted 是否信任
             * @return 设置成功返回true
             */
            bool setDeviceTrusted(const std::string& user_id, const std::string& device_id, bool trusted);

            /**
             * @brief 移除设备
             * @param user_id 用户ID
             * @param device_id 设备ID
             * @return 移除成功返回true
             */
            bool removeDevice(const std::string& user_id, const std::string& device_id);

            // ==================== 异步操作 ====================

            /**
             * @brief 异步创建会话
             * @param user_info 用户信息
             * @param client_ip 客户端IP
             * @param user_agent 用户代理字符串
             * @param device_id 设备ID
             * @return Future对象，包含会话信息
             */
            std::future<std::optional<UserSession>> createSessionAsync(const CoreUserInfo& user_info,
                                                                      const std::string& client_ip,
                                                                      const std::string& user_agent,
                                                                      const std::string& device_id = "");

            /**
             * @brief 异步验证会话
             * @param session_id 会话ID
             * @param client_ip 客户端IP
             * @param user_agent 用户代理字符串
             * @return Future对象，包含验证结果
             */
            std::future<SessionValidationResult> validateSessionAsync(const std::string& session_id,
                                                                     const std::string& client_ip,
                                                                     const std::string& user_agent = "");

            // ==================== 安全和监控 ====================

            /**
             * @brief 检测异常登录
             * @param user_id 用户ID
             * @param client_ip 客户端IP
             * @param device_info 设备信息
             * @return 如果检测到异常返回true
             */
            bool detectAnomalousLogin(const std::string& user_id, const std::string& client_ip, const DeviceInfo& device_info);

            /**
             * @brief 记录安全事件
             * @param user_id 用户ID
             * @param event_type 事件类型
             * @param description 事件描述
             * @param client_ip 客户端IP
             * @param additional_data 附加数据
             */
            void recordSecurityEvent(const std::string& user_id, const std::string& event_type, const std::string& description,
                                   const std::string& client_ip, const nlohmann::json& additional_data = {});

            /**
             * @brief 获取用户安全事件
             * @param user_id 用户ID
             * @param limit 限制数量
             * @return 安全事件列表
             */
            std::vector<nlohmann::json> getUserSecurityEvents(const std::string& user_id, int limit = 50);

            /**
             * @brief 清理过期会话
             * @return 清理的会话数量
             */
            int cleanupExpiredSessions();

            /**
             * @brief 强制清理用户会话（管理员操作）
             * @param user_id 用户ID
             * @param reason 清理原因
             * @return 清理的会话数量
             */
            int forceCleanupUserSessions(const std::string& user_id, const std::string& reason);

            // ==================== 配置和统计 ====================

            /**
             * @brief 更新配置
             * @param new_config 新配置
             * @return 更新成功返回true
             */
            bool updateConfig(const Config& new_config);

            /**
             * @brief 获取当前配置
             * @return 当前配置
             */
            const Config& getConfig() const { return config_; }

        /**
         * @brief 获取统计信息
         * @return 统计信息JSON对象
         */
        nlohmann::json getStatistics() const;

            /**
             * @brief 重置统计信息
             */
            void resetStatistics();

            /**
             * @brief 获取健康状态
             * @return 健康状态信息
             */
            nlohmann::json getHealthStatus() const;

        private:
            Config config_;                                                     ///< 配置
            std::shared_ptr<common::database::RedisPool> redis_pool_;          ///< Redis连接池
            std::shared_ptr<JwtManager> jwt_manager_;                          ///< JWT管理器
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;     ///< 线程池

            // ==================== 新架构：统一 Redis Repository ====================
            std::unique_ptr<SessionRedisRepository> session_redis_;            ///< 会话 Redis Repository
            std::unique_ptr<DeviceRedisRepository> device_redis_;              ///< 设备 Redis Repository
            std::unique_ptr<SecurityEventRedisRepository> security_event_redis_; ///< 安全事件 Redis Repository

            // 统计信息
            mutable std::atomic<uint64_t> sessions_created_{0};                ///< 创建的会话数量
            mutable std::atomic<uint64_t> sessions_validated_{0};              ///< 验证的会话数量
            mutable std::atomic<uint64_t> sessions_destroyed_{0};              ///< 销毁的会话数量
            mutable std::atomic<uint64_t> validation_failures_{0};             ///< 验证失败次数
            mutable std::atomic<uint64_t> anomalous_logins_{0};                ///< 异常登录次数
            mutable std::atomic<uint64_t> security_events_{0};                 ///< 安全事件数量

            // 清理任务
            std::atomic<bool> cleanup_running_{false};                         ///< 清理任务是否运行中
            std::thread cleanup_thread_;                                       ///< 清理线程

            // ==================== 新架构初始化方法 ====================
            /**
             * @brief 初始化 Redis Repository
             * @details 创建类型化的 Redis Repository 实例
             */
            void initializeRedisRepositories();

            // ==================== 内部方法 ====================

            /**
             * @brief 生成会话缓存键
             * @param session_id 会话ID
             * @return 缓存键
             */
            std::string generateSessionCacheKey(const std::string& session_id);

            /**
             * @brief 生成用户会话列表缓存键
             * @param user_id 用户ID
             * @return 缓存键
             */
            std::string generateUserSessionsKey(const std::string& user_id);

            /**
             * @brief 生成设备缓存键
             * @param user_id 用户ID
             * @param device_id 设备ID
             * @return 缓存键
             */
            std::string generateDeviceCacheKey(const std::string& user_id, const std::string& device_id);

            /**
             * @brief 生成安全事件缓存键
             * @param user_id 用户ID
             * @return 缓存键
             */
            std::string generateSecurityEventsKey(const std::string& user_id);

            /**
             * @brief 将会话存储到Redis
             * @param session 会话信息
             * @return 存储成功返回true
             */
            bool storeSessionToRedis(const UserSession& session);

            /**
             * @brief 从Redis获取会话
             * @param session_id 会话ID
             * @return 会话信息，未找到返回nullopt
             */
            std::optional<UserSession> getSessionFromRedis(const std::string& session_id);

            /**
             * @brief 从Redis删除会话
             * @param session_id 会话ID
             * @return 删除成功返回true
             */
            bool removeSessionFromRedis(const std::string& session_id);

            /**
             * @brief 更新用户会话列表
             * @param user_id 用户ID
             * @param session_id 会话ID
             * @param add 是否添加（false为移除）
             * @return 更新成功返回true
             */
            bool updateUserSessionsList(const std::string& user_id, const std::string& session_id, bool add);

            /**
             * @brief 检查并发会话限制
             * @param user_id 用户ID
             * @return 如果超出限制返回true
             */
            bool checkConcurrentSessionLimit(const std::string& user_id);

            /**
             * @brief 验证IP地址
             * @param session 会话信息
             * @param client_ip 客户端IP
             * @return 验证通过返回true
             */
            bool validateIPAddress(const UserSession& session, const std::string& client_ip);

            /**
             * @brief 验证设备信息
             * @param session 会话信息
             * @param user_agent 用户代理字符串
             * @return 验证通过返回true
             */
            bool validateDeviceInfo(const UserSession& session, const std::string& user_agent);

            /**
             * @brief 加密会话数据
             * @param data 原始数据
             * @return 加密后的数据
             */
            std::string encryptSessionData(const std::string& data);

            /**
             * @brief 解密会话数据
             * @param encrypted_data 加密的数据
             * @return 解密后的数据
             */
            std::string decryptSessionData(const std::string& encrypted_data);

            /**
             * @brief 清理任务工作函数
             */
            void cleanupWorker();

            /**
             * @brief 启动清理任务
             */
            void startCleanupTask();

            /**
             * @brief 停止清理任务
             */
            void stopCleanupTask();

            /**
             * @brief 分析用户登录模式
             * @param user_id 用户ID
             * @param client_ip 客户端IP
             * @return 异常评分（0-1，越高越异常）
             */
            double analyzeLoginPattern(const std::string& user_id, const std::string& client_ip);

            /**
             * @brief 计算IP地址距离
             * @param ip1 IP地址1
             * @param ip2 IP地址2
             * @return 距离评分（0-1）
             */
            double calculateIPDistance(const std::string& ip1, const std::string& ip2);

            /**
             * @brief 清理用户的过期会话
             * @param user_id 用户ID
             * @return 清理的会话数量
             */
            int cleanupExpiredUserSessions(const std::string& user_id);
        };

    } // namespace auth_service
} // namespace core_services

#endif //SESSION_MANAGER_H
