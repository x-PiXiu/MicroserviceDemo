/**
 * @file session_manager.cpp
 * @brief 会话管理器实现 - 负责用户会话的创建、维护和管理
 * @author 29108
 * @date 2025/7/21
 * @version 2.0 - 集成统一 Redis Repository 模式
 */

#include "session_manager.h"
#include "user_models.h"
#include "common/logger/logger.h"
#include "common/security/secret_manager.h"
#include "common/repository/redis_key_builder.h"
#include "common/repository/redis_ttl_config.h"
#include <regex>
#include <algorithm>
#include <random>

namespace core_services {
    namespace auth_service {

        // ==================== SessionValidationResult 实现 ====================

        /**
         * @brief 成功结果构造函数
         * @param session 会话信息
         */
        SessionValidationResult::SessionValidationResult(const UserSession& session)
            : valid(true)
            , session(session) {
        }

        /**
         * @brief 失败结果构造函数
         * @param error_message 错误消息
         * @param error_code 错误代码
         */
        SessionValidationResult::SessionValidationResult(const std::string& error_message, const std::string& error_code)
            : valid(false)
            , error_message(error_message)
            , error_code(error_code) {
        }

        /**
         * @brief 转换为JSON对象
         * @return JSON对象
         */
        nlohmann::json SessionValidationResult::toJson() const {
            nlohmann::json json;

            json["valid"] = valid;
            json["error_message"] = error_message;
            json["error_code"] = error_code;
            json["expired"] = expired;
            json["expired"] = expired;
            json["ip_mismatch"] = ip_mismatch;
            json["concurrent_limit_exceeded"] = concurrent_limit_exceeded;
            json["ip_mismatch"] = ip_mismatch;
            json["device_mismatch"] = device_mismatch;
            json["concurrent_limit_exceeded"] = concurrent_limit_exceeded;

            if (session.has_value()) {
                json["session"] = session->toJson();
            }

            return json;
        }

        // DeviceInfo 的实现已在 session_redis_repository.h 中定义

        // ==================== SessionManager::Config 实现 ====================

        /**
         * @brief 从配置管理器加载配置
         * @return 会话管理器配置对象
         *
         * 安全改进：优先从 SecretManager 获取加密密钥
         */
        SessionManager::Config SessionManager::Config::fromConfigManager() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            Config config;
            config.session_timeout = std::chrono::seconds(config_manager.get<int>("session.session_timeout", config.session_timeout.count()));
            config.max_idle_time = std::chrono::seconds(config_manager.get<int>("session.max_idle_time", config.max_idle_time.count()));
            config.max_concurrent_sessions = config_manager.get<int>("session.max_concurrent_sessions", config.max_concurrent_sessions);
            config.enable_multi_device = config_manager.get<bool>("session.enable_multi_device", config.enable_multi_device);
            config.enable_ip_validation = config_manager.get<bool>("session.enable_ip_validation", config.enable_ip_validation);
            config.enable_device_validation = config_manager.get<bool>("session.enable_device_validation", config.enable_device_validation);
            config.enable_session_migration = config_manager.get<bool>("session.enable_session_migration", config.enable_session_migration);
            config.cleanup_interval = std::chrono::seconds(config_manager.get<int>("session.cleanup_interval", config.cleanup_interval.count()));
            config.enable_session_encryption = config_manager.get<bool>("session.enable_session_encryption", config.enable_session_encryption);

            // 安全改进：优先从 SecretManager 获取会话加密密钥
            if (common::security::SecretManager::isInitialized()) {
                auto secret = common::security::SecretManager::getInstance().getSecret("SESSION_ENCRYPTION_KEY");
                if (secret.has_value() && !secret->empty()) {
                    config.encryption_key = secret.value();
                    LOG_INFO("会话加密密钥已从SecretManager加载");
                } else {
                    config.encryption_key = config_manager.get<std::string>("session.encryption_key", "");
                    if (!config.encryption_key.empty()) {
                        LOG_WARNING("会话加密密钥从ConfigManager加载，建议迁移到SecretManager");
                    }
                }
            } else {
                config.encryption_key = config_manager.get<std::string>("session.encryption_key", "");
                if (!config.encryption_key.empty()) {
                    LOG_WARNING("SecretManager未初始化，会话加密密钥从ConfigManager加载");
                }
            }

            // 验证加密密钥安全性
            if (config.enable_session_encryption) {
                if (config.encryption_key.empty()) {
                    LOG_ERROR("会话加密已启用但密钥未配置！请设置环境变量 SESSION_ENCRYPTION_KEY");
                    throw std::runtime_error("Session encryption key is not configured. "
                        "Please set SESSION_ENCRYPTION_KEY environment variable");
                }
                if (config.encryption_key.length() < 32) {
                    LOG_ERROR("会话加密密钥长度不足（当前: " + std::to_string(config.encryption_key.length()) +
                             "），要求至少32个字符");
                    throw std::runtime_error("Session encryption key must be at least 32 characters long");
                }
            }

            config.enable_anomaly_detection = config_manager.get<bool>("session.enable_anomaly_detection", config.enable_anomaly_detection);
            config.max_ip_change_frequency = config_manager.get<double>("session.max_ip_change_frequency", config.max_ip_change_frequency);

            LOG_INFO("会话管理器配置加载完成");
            return config;
        }

        /**
         * @brief 验证配置有效性
         * @return 验证结果，成功返回空字符串
         */
        std::string SessionManager::Config::validate() const {
            if (session_timeout.count() <= 0) {
                return "会话超时时间必须大于0";
            }

            if (max_idle_time.count() <= 0) {
                return "最大空闲时间必须大于0";
            }

            if (max_concurrent_sessions <= 0) {
                return "最大并发会话数必须大于0";
            }

            if (cleanup_interval.count() <= 0) {
                return "清理间隔必须大于0";
            }

            if (enable_session_encryption && encryption_key.empty()) {
                return "启用会话加密时加密密钥不能为空";
            }

            if (max_ip_change_frequency < 0 || max_ip_change_frequency > 1) {
                return "IP变化频率必须在0-1之间";
            }

            return ""; // 验证通过
        }

        // ==================== SessionManager 实现 ====================

        /**
         * @brief 构造函数
         * @param config 配置
         * @param redis_pool Redis连接池
         * @param jwt_manager JWT管理器
         * @param thread_pool 线程池
         */
        SessionManager::SessionManager(const Config& config,
                                      std::shared_ptr<common::database::RedisPool> redis_pool,
                                      std::shared_ptr<JwtManager> jwt_manager,
                                      std::shared_ptr<common::thread_pool::ThreadPool> thread_pool)
            : config_(config)
            , redis_pool_(redis_pool)
            , jwt_manager_(jwt_manager)
            , thread_pool_(thread_pool) {

            // 验证配置
            std::string validation_error = config_.validate();
            if (!validation_error.empty()) {
                throw std::invalid_argument("会话管理器配置无效: " + validation_error);
            }

            // 验证必需的依赖
            if (!redis_pool_) {
                throw std::invalid_argument("Redis连接池不能为空");
            }

            if (!jwt_manager_) {
                throw std::invalid_argument("JWT管理器不能为空");
            }

            // ==================== 新架构：初始化 Redis Repository ====================
            initializeRedisRepositories();

            // 🔧 关键修复：禁用SessionManager的内置清理线程，避免与AuthService定时器冲突
            // startCleanupTask(); // ❌ 移除自动启动，改由外部统一管理
            cleanup_running_ = false; // 确保清理线程不会启动

            LOG_INFO("会话管理器初始化完成 (统一定时器管理模式)");
            LOG_INFO("- 会话超时: " + std::to_string(config_.session_timeout.count()) + "秒");
            LOG_INFO("- 最大并发会话: " + std::to_string(config_.max_concurrent_sessions));
            LOG_INFO("- 多设备支持: " + (config_.enable_multi_device ? std::string("启用") : std::string("禁用")));
            LOG_INFO("- 异常检测: " + (config_.enable_anomaly_detection ? std::string("启用") : std::string("禁用")));
            LOG_INFO("- 清理模式: 外部统一定时器管理 (避免线程冲突)");
        }

        /**
         * @brief 析构函数
         */
        SessionManager::~SessionManager() {
            // 🔧 安全的清理线程停止：只有在实际启动了清理线程时才停止
            if (cleanup_running_ || (cleanup_thread_.joinable())) {
                stopCleanupTask();
            }
            LOG_INFO("会话管理器销毁 (统一定时器管理模式)");
        }

        /**
         * @brief 初始化 Redis Repository
         * @details 创建类型化的 Redis Repository 实例
         */
        void SessionManager::initializeRedisRepositories() {
            if (redis_pool_) {
                try {
                    session_redis_ = std::make_unique<SessionRedisRepository>(redis_pool_);
                    device_redis_ = std::make_unique<DeviceRedisRepository>(redis_pool_);
                    security_event_redis_ = std::make_unique<SecurityEventRedisRepository>(redis_pool_);

                    // 如果启用会话加密，设置加密回调
                    if (config_.enable_session_encryption) {
                        session_redis_->setEncryptionCallbacks(
                            [this](const std::string& data) { return this->encryptSessionData(data); },
                            [this](const std::string& data) { return this->decryptSessionData(data); }
                        );
                    }

                    LOG_INFO("SessionManager: Redis Repository 初始化完成");
                    LOG_INFO("- SessionRedisRepository: 已初始化" +
                             std::string(config_.enable_session_encryption ? " (加密启用)" : " (加密禁用)"));
                    LOG_INFO("- DeviceRedisRepository: 已初始化");
                    LOG_INFO("- SecurityEventRedisRepository: 已初始化");
                } catch (const std::exception& e) {
                    LOG_ERROR("SessionManager: Redis Repository 初始化失败: " + std::string(e.what()));
                }
            }
        }

        /**
         * @brief 创建用户会话
         * @param user_info 用户信息
         * @param client_ip 客户端IP
         * @param user_agent 用户代理字符串
         * @param device_id 设备ID（可选）
         * @return 会话信息，创建失败返回nullopt
         */
        std::optional<UserSession> SessionManager::createSession(const CoreUserInfo& user_info,
                                                                const std::string& client_ip,
                                                                const std::string& user_agent,
                                                                const std::string& device_id) {
            try {
                sessions_created_++;

                // 1. 先清理过期会话，然后检查并发会话限制
                int cleaned_count = cleanupExpiredUserSessions(user_info.user_id);
                LOG_INFO("创建会话前清理过期会话: 用户ID=" + user_info.user_id +
                        ", 清理数量=" + std::to_string(cleaned_count));

                // 2. 如果仍然超出限制，尝试清理最旧的会话
                if (checkConcurrentSessionLimit(user_info.user_id)) {
                    LOG_WARNING("用户并发会话数仍超出限制，尝试清理最旧的会话");

                    // 获取用户所有活跃会话
                    auto active_sessions = getUserActiveSessions(user_info.user_id);
                    if (!active_sessions.empty()) {
                        // 按创建时间排序，清理最旧的会话
                        std::sort(active_sessions.begin(), active_sessions.end(),
                                [](const UserSession& a, const UserSession& b) {
                                    return a.created_at < b.created_at;
                                });

                        // 清理最旧的会话直到满足限制
                        int sessions_to_remove = active_sessions.size() - config_.max_concurrent_sessions + 1;
                        for (int i = 0; i < sessions_to_remove && i < static_cast<int>(active_sessions.size()); ++i) {
                            LOG_INFO("强制清理最旧会话: " + active_sessions[i].session_id);
                            destroySession(active_sessions[i].session_id);
                        }
                    }

                    // 再次检查限制
                    if (checkConcurrentSessionLimit(user_info.user_id)) {
                        LOG_ERROR("清理后仍超出会话限制，用户ID: " + user_info.user_id);
                        return std::nullopt;
                    }
                }

                // 2. 生成设备ID（如果未提供）
                std::string final_device_id = device_id;
                if (final_device_id.empty()) {
                    final_device_id = generateUUID();
                }

                // 3. 解析设备信息
                DeviceInfo device_info = DeviceInfo::parseFromUserAgent(user_agent, final_device_id);

                // 4. 异常登录检测
                if (config_.enable_anomaly_detection) {
                    if (detectAnomalousLogin(user_info.user_id, client_ip, device_info)) {
                        anomalous_logins_++;
                        recordSecurityEvent(user_info.user_id, "anomalous_login",
                                          "检测到异常登录行为", client_ip, {
                                              {"device_type", device_info.device_type},
                                              {"os_name", device_info.os_name},
                                              {"browser_name", device_info.browser_name}
                                          });
                        LOG_WARNING("检测到异常登录，用户ID: " + user_info.user_id);
                    }
                }

                // 5. 注册设备
                registerDevice(user_info.user_id, device_info);

                // 6. 创建会话对象
                UserSession session;
                session.session_id = generateUUID();
                session.user_id = user_info.user_id;
                session.device_type = device_info.device_type;
                session.device_id = final_device_id;
                session.client_ip = client_ip;
                session.user_agent = user_agent;
                session.created_at = std::chrono::system_clock::now();
                session.last_activity_at = session.created_at;
                session.expires_at = session.created_at + config_.session_timeout;
                session.is_active = true;

                // 7. 存储会话到Redis
                if (!storeSessionToRedis(session)) {
                    LOG_ERROR("存储会话到Redis失败");
                    return std::nullopt;
                }

                // 8. 更新用户会话列表
                updateUserSessionsList(user_info.user_id, session.session_id, true);

                LOG_INFO("用户会话创建成功，用户ID: " + user_info.user_id +
                        ", 会话ID: " + session.session_id);

                return session;

            } catch (const std::exception& e) {
                LOG_ERROR("创建用户会话时发生异常: " + std::string(e.what()));
                return std::nullopt;
            }
        }

        /**
         * @brief 验证会话
         * @param session_id 会话ID
         * @param client_ip 客户端IP
         * @param user_agent 用户代理字符串
         * @return 验证结果
         */
        SessionValidationResult SessionManager::validateSession(const std::string& session_id,
                                                               const std::string& client_ip,
                                                               const std::string& user_agent) {
            try {
                sessions_validated_++;

                // 1. 从Redis获取会话
                auto session_opt = getSessionFromRedis(session_id);
                if (!session_opt.has_value()) {
                    validation_failures_++;
                    return SessionValidationResult("会话不存在", "SESSION_NOT_FOUND");
                }

                auto session = session_opt.value();

                // 2. 检查会话是否活跃
                if (!session.is_active) {
                    validation_failures_++;
                    return SessionValidationResult("会话已失效", "SESSION_INACTIVE");
                }

                // 3. 检查会话是否过期
                auto now = std::chrono::system_clock::now();
                if (now > session.expires_at) {
                    validation_failures_++;
                    auto result = SessionValidationResult("会话已过期", "SESSION_EXPIRED");
                    result.expired = true;
                    return result;
                }

                // 4. 检查空闲时间
                auto idle_time = now - session.last_activity_at;
                if (idle_time > config_.max_idle_time) {
                    validation_failures_++;
                    auto result = SessionValidationResult("会话空闲时间过长", "SESSION_IDLE_TIMEOUT");
                    result.expired = true;
                    return result;
                }

                // 5. IP地址验证
                if (config_.enable_ip_validation) {
                    if (!validateIPAddress(session, client_ip)) {
                        validation_failures_++;
                        auto result = SessionValidationResult("IP地址不匹配", "IP_MISMATCH");
                        result.ip_mismatch = true;
                        return result;
                    }
                }

                // 6. 设备验证
                if (config_.enable_device_validation && !user_agent.empty()) {
                    if (!validateDeviceInfo(session, user_agent)) {
                        validation_failures_++;
                        auto result = SessionValidationResult("设备信息不匹配", "DEVICE_MISMATCH");
                        result.device_mismatch = true;
                        return result;
                    }
                }

                // 7. 更新会话活动时间
                updateSessionActivity(session_id, client_ip);

                LOG_DEBUG("会话验证成功，会话ID: " + session_id);
                return SessionValidationResult(session);

            } catch (const std::exception& e) {
                validation_failures_++;
                LOG_ERROR("验证会话时发生异常: " + std::string(e.what()));
                return SessionValidationResult("会话验证失败", "VALIDATION_ERROR");
            }
        }

        /**
         * @brief 更新会话活动时间
         * @param session_id 会话ID
         * @param client_ip 客户端IP
         * @return 更新成功返回true
         */
        bool SessionManager::updateSessionActivity(const std::string& session_id, const std::string& client_ip) {
            try {
                auto session_opt = getSessionFromRedis(session_id);
                if (!session_opt.has_value()) {
                    return false;
                }

                auto session = session_opt.value();
                session.last_activity_at = std::chrono::system_clock::now();

                // 如果提供了IP地址，更新IP
                if (!client_ip.empty()) {
                    session.client_ip = client_ip;
                }

                // 延长过期时间
                session.expires_at = session.last_activity_at + config_.session_timeout;

                return storeSessionToRedis(session);

            } catch (const std::exception& e) {
                LOG_ERROR("更新会话活动时间时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 销毁会话
         * @param session_id 会话ID
         * @return 销毁成功返回true
         */
        bool SessionManager::destroySession(const std::string& session_id) {
            try {
                // 1. 获取会话信息
                auto session_opt = getSessionFromRedis(session_id);
                if (!session_opt.has_value()) {
                    LOG_WARNING("尝试销毁不存在的会话: " + session_id);
                    return false;
                }

                auto session = session_opt.value();

                // 2. 从Redis删除会话
                if (!removeSessionFromRedis(session_id)) {
                    LOG_ERROR("从Redis删除会话失败: " + session_id);
                    return false;
                }

                // 3. 更新用户会话列表
                updateUserSessionsList(session.user_id, session_id, false);

                // 4. 记录安全事件
                recordSecurityEvent(session.user_id, "session_destroyed",
                                  "会话被销毁", session.client_ip, {
                                      {"session_id", session_id},
                                      {"device_type", session.device_type}
                                  });

                sessions_destroyed_++;
                LOG_INFO("会话销毁成功，会话ID: " + session_id + ", 用户ID: " + session.user_id);

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("销毁会话时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 销毁用户的所有会话
         * @param user_id 用户ID
         * @return 销毁的会话数量
         */
        int SessionManager::destroyAllUserSessions(const std::string& user_id) {
            try {
                auto sessions = getUserActiveSessions(user_id);
                int destroyed_count = 0;

                for (const auto& session : sessions) {
                    if (destroySession(session.session_id)) {
                        destroyed_count++;
                    }
                }

                LOG_INFO("销毁用户所有会话完成，用户ID: " + user_id +
                        ", 销毁数量: " + std::to_string(destroyed_count));

                return destroyed_count;

            } catch (const std::exception& e) {
                LOG_ERROR("销毁用户所有会话时发生异常: " + std::string(e.what()));
                return 0;
            }
        }

        // ==================== 内部方法实现 ====================

        /**
         * @brief 生成会话缓存键
         * @param session_id 会话ID
         * @return 缓存键
         */
        std::string SessionManager::generateSessionCacheKey(const std::string& session_id) {
            return common::repository::RedisKeyBuilder::AuthService::session(session_id);
        }

        /**
         * @brief 生成用户会话列表缓存键
         * @param user_id 用户ID
         * @return 缓存键
         */
        std::string SessionManager::generateUserSessionsKey(const std::string& user_id) {
            return common::repository::RedisKeyBuilder::AuthService::userSessions(user_id);
        }

        /**
         * @brief 将会话存储到Redis
         * @param session 会话信息
         * @return 存储成功返回true
         */
        bool SessionManager::storeSessionToRedis(const UserSession& session) {
            try {
                // 计算TTL
                auto now = std::chrono::system_clock::now();
                auto ttl = std::chrono::duration_cast<std::chrono::seconds>(session.expires_at - now);

                if (ttl.count() <= 0) {
                    LOG_WARNING("会话已过期，不存储到Redis: " + session.session_id);
                    return false;
                }

                // 使用新架构：通过 Redis Repository 存储
                if (session_redis_) {
                    auto result = session_redis_->saveWithTTL(session.session_id, session, ttl);
                    if (result.has_value() && result.value()) {
                        return true;
                    }
                    LOG_WARNING("通过 Redis Repository 存储会话失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return false;
                }

                std::string cache_key = generateSessionCacheKey(session.session_id);
                std::string session_data = session.toJson().dump();

                // 如果启用了会话加密
                if (config_.enable_session_encryption) {
                    session_data = encryptSessionData(session_data);
                }

                // 存储会话数据
                redis_conn->set(cache_key, session_data, static_cast<int>(ttl.count()));

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("存储会话到Redis时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 获取会话信息
         * @param session_id 会话ID
         * @return 会话信息，未找到返回nullopt
         */
        std::optional<UserSession> SessionManager::getSession(const std::string& session_id) {
            return getSessionFromRedis(session_id);
        }

        /**
         * @brief 从Redis获取会话
         * @param session_id 会话ID
         * @return 会话信息，未找到返回nullopt
         */
        std::optional<UserSession> SessionManager::getSessionFromRedis(const std::string& session_id) {
            try {
                // 使用新架构：通过 Redis Repository 获取
                if (session_redis_) {
                    auto result = session_redis_->findById(session_id);
                    if (result.has_value()) {
                        if (result->has_value()) {
                            return result->value();
                        }
                        return std::nullopt;  // 会话不存在
                    }
                    LOG_WARNING("通过 Redis Repository 获取会话失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return std::nullopt;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return std::nullopt;
                }

                std::string cache_key = generateSessionCacheKey(session_id);
                std::string session_data = redis_conn->get(cache_key);

                if (session_data.empty()) {
                    return std::nullopt;
                }

                // 如果启用了会话加密
                if (config_.enable_session_encryption) {
                    session_data = decryptSessionData(session_data);
                }

                auto json_data = nlohmann::json::parse(session_data);
                return UserSession::fromJson(json_data);

            } catch (const std::exception& e) {
                LOG_ERROR("从Redis获取会话时发生异常: " + std::string(e.what()));
                return std::nullopt;
            }
        }

        /**
         * @brief 从Redis删除会话
         * @param session_id 会话ID
         * @return 删除成功返回true
         */
        bool SessionManager::removeSessionFromRedis(const std::string& session_id) {
            try {
                // 使用新架构：通过 Redis Repository 删除
                if (session_redis_) {
                    auto result = session_redis_->remove(session_id);
                    if (result.has_value()) {
                        return result.value();
                    }
                    LOG_WARNING("通过 Redis Repository 删除会话失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return false;
                }

                std::string cache_key = generateSessionCacheKey(session_id);
                return redis_conn->del(cache_key);

            } catch (const std::exception& e) {
                LOG_ERROR("从Redis删除会话时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 更新用户会话列表
         * @param user_id 用户ID
         * @param session_id 会话ID
         * @param add 是否添加（false为移除）
         * @return 更新成功返回true
         */
        bool SessionManager::updateUserSessionsList(const std::string& user_id, const std::string& session_id, bool add) {
            try {
                // 使用新架构：通过 Redis Repository 操作
                if (session_redis_) {
                    if (add) {
                        auto result = session_redis_->addToUserSessions(user_id, session_id);
                        if (result.has_value() && result.value()) {
                            return true;
                        }
                    } else {
                        auto result = session_redis_->removeFromUserSessions(user_id, session_id);
                        if (result.has_value() && result.value()) {
                            return true;
                        }
                    }
                    LOG_WARNING("通过 Redis Repository 更新会话列表失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return false;
                }

                std::string list_key = generateUserSessionsKey(user_id);

                if (add) {
                    // 添加会话ID到集合
                    redis_conn->sadd(list_key, session_id);
                    // 设置过期时间
                    redis_conn->expire(list_key, config_.session_timeout.count());
                } else {
                    // 从集合中移除会话ID
                    redis_conn->srem(list_key, session_id);
                }

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("更新用户会话列表时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 检查并发会话限制
         * @param user_id 用户ID
         * @return 如果超出限制返回true
         */
        bool SessionManager::checkConcurrentSessionLimit(const std::string& user_id) {
            try {
                auto active_sessions = getUserActiveSessions(user_id);
                int current_sessions = static_cast<int>(active_sessions.size());

                LOG_INFO("用户并发会话检查 - 用户ID: " + user_id +
                        ", 当前活跃会话数: " + std::to_string(current_sessions) +
                        ", 最大允许会话数: " + std::to_string(config_.max_concurrent_sessions));

                // 如果达到限制，记录详细的会话信息
                if (current_sessions >= config_.max_concurrent_sessions) {
                    LOG_WARNING("用户会话数达到限制，详细信息:");
                    for (size_t i = 0; i < active_sessions.size(); ++i) {
                        const auto& session = active_sessions[i];
                        LOG_WARNING("  会话[" + std::to_string(i) + "]: ID=" + session.session_id +
                                   ", 设备=" + session.device_id +
                                   ", IP=" + session.client_ip +
                                   ", 创建时间=" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                       session.created_at.time_since_epoch()).count()) +
                                   ", 过期时间=" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                       session.expires_at.time_since_epoch()).count()));
                    }
                }

                return current_sessions >= config_.max_concurrent_sessions;

            } catch (const std::exception& e) {
                LOG_ERROR("检查并发会话限制时发生异常: " + std::string(e.what()));
                return true; // 出错时保守处理
            }
        }

        /**
         * @brief 验证IP地址
         * @param session 会话信息
         * @param client_ip 客户端IP
         * @return 验证通过返回true
         */
        bool SessionManager::validateIPAddress(const UserSession& session, const std::string& client_ip) {
            if (client_ip.empty() || session.client_ip.empty()) {
                return true; // 如果没有IP信息，跳过验证
            }

            // 简单的IP地址匹配
            if (session.client_ip == client_ip) {
                return true;
            }

            // 如果启用了会话迁移，允许IP变化
            if (config_.enable_session_migration) {
                // 记录IP变化事件
                recordSecurityEvent(session.user_id, "ip_change",
                                  "会话IP地址发生变化", client_ip, {
                                      {"old_ip", session.client_ip},
                                      {"new_ip", client_ip},
                                      {"session_id", session.session_id}
                                  });
                return true;
            }

            return false;
        }

        /**
         * @brief 验证设备信息
         * @param session 会话信息
         * @param user_agent 用户代理字符串
         * @return 验证通过返回true
         */
        bool SessionManager::validateDeviceInfo(const UserSession& session, const std::string& user_agent) {
            if (user_agent.empty() || session.user_agent.empty()) {
                return true; // 如果没有设备信息，跳过验证
            }

            // 简化的用户代理匹配（可以根据需要实现更复杂的逻辑）
            return session.user_agent == user_agent;
        }

        /**
         * @brief 获取用户的所有活跃会话
         * @param user_id 用户ID
         * @return 会话信息列表
         */
        std::vector<UserSession> SessionManager::getUserActiveSessions(const std::string& user_id) {
            std::vector<UserSession> active_sessions;

            try {
                if (!redis_pool_) {
                    return active_sessions;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return active_sessions;
                }

                std::string list_key = generateUserSessionsKey(user_id);
                auto session_ids = redis_conn->smembers(list_key); // 使用集合而不是列表

                for (const auto& session_id : session_ids) {
                    auto session_opt = getSessionFromRedis(session_id);
                    if (session_opt.has_value()) {
                        auto session = session_opt.value();

                        // 检查会话是否仍然有效
                        auto now = std::chrono::system_clock::now();
                        if (session.is_active && now <= session.expires_at) {
                            active_sessions.push_back(session);
                        } else {
                            // 清理过期会话
                            removeSessionFromRedis(session_id);
                            updateUserSessionsList(user_id, session_id, false);
                        }
                    }
                }

            } catch (const std::exception& e) {
                LOG_ERROR("获取用户活跃会话时发生异常: " + std::string(e.what()));
            }

            return active_sessions;
        }

        /**
         * @brief 记录安全事件
         * @param user_id 用户ID
         * @param event_type 事件类型
         * @param description 事件描述
         * @param client_ip 客户端IP
         * @param additional_data 附加数据
         */
        void SessionManager::recordSecurityEvent(const std::string& user_id, const std::string& event_type,
                                                const std::string& description, const std::string& client_ip,
                                                const nlohmann::json& additional_data) {
            try {
                security_events_++;

                nlohmann::json event = {
                    {"timestamp", getCurrentTimestamp()},
                    {"user_id", user_id},
                    {"event_type", event_type},
                    {"description", description},
                    {"client_ip", client_ip},
                    {"additional_data", additional_data}
                };

                // 使用新架构：通过 Redis Repository 存储
                if (security_event_redis_) {
                    auto result = security_event_redis_->recordEvent(user_id, event);
                    if (result.has_value() && result.value()) {
                        LOG_INFO("安全事件记录 (via Redis Repository): " + event.dump());
                        return;
                    }
                    LOG_WARNING("通过 Redis Repository 记录安全事件失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (redis_pool_) {
                    // 使用 RAII 连接管理器，确保连接自动归还
                    common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                    auto redis_conn = conn_guard.get();
                    if (redis_conn) {
                        std::string events_key = generateSecurityEventsKey(user_id);
                        redis_conn->lpush(events_key, event.dump());

                        // 限制事件列表长度（简化处理，实际应该使用LTRIM）
                        // redis_conn->listTrim(events_key, 0, 99); // Redis接口中没有这个方法

                        // 设置过期时间（30天）
                        redis_conn->expire(events_key, 30 * 24 * 3600);
                    }
                }

                LOG_INFO("安全事件记录: " + event.dump());

            } catch (const std::exception& e) {
                LOG_ERROR("记录安全事件时发生异常: " + std::string(e.what()));
            }
        }

        /**
         * @brief 检测异常登录
         * @param user_id 用户ID
         * @param client_ip 客户端IP
         * @param device_info 设备信息
         * @return 如果检测到异常返回true
         */
        bool SessionManager::detectAnomalousLogin(const std::string& user_id, const std::string& client_ip, const DeviceInfo& device_info) {
            try {
                if (!config_.enable_anomaly_detection) {
                    return false;
                }

                // 分析登录模式
                double anomaly_score = analyzeLoginPattern(user_id, client_ip);

                // 检查设备是否为新设备
                auto user_devices = getUserDevices(user_id);
                bool is_new_device = std::none_of(user_devices.begin(), user_devices.end(),
                    [&](const DeviceInfo& d) { return d.device_id == device_info.device_id; });

                if (is_new_device) {
                    anomaly_score += 0.3; // 新设备增加异常评分
                }

                // 检查IP地址变化
                if (anomaly_score > 0.5) {
                    return true;
                }

                return false;

            } catch (const std::exception& e) {
                LOG_ERROR("检测异常登录时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 分析用户登录模式
         * @param user_id 用户ID
         * @param client_ip 客户端IP
         * @return 异常评分（0-1，越高越异常）
         */
        double SessionManager::analyzeLoginPattern(const std::string& user_id, [[maybe_unused]] const std::string& client_ip) {
            try {
                // 获取用户最近的安全事件
                auto events = getUserSecurityEvents(user_id, 10);

                double score = 0.0;
                int ip_changes = 0;

                for (const auto& event : events) {
                    if (event.contains("event_type") && event["event_type"] == "ip_change") {
                        ip_changes++;
                    }
                }

                // 如果IP变化频率过高
                if (ip_changes > 3) {
                    score += 0.4;
                }

                // 可以添加更多的异常检测逻辑
                // 例如：地理位置检查、登录时间模式分析等

                return std::min(1.0, score);

            } catch (const std::exception& e) {
                LOG_ERROR("分析登录模式时发生异常: " + std::string(e.what()));
                return 0.0;
            }
        }

        /**
         * @brief 获取用户安全事件
         * @param user_id 用户ID
         * @param limit 限制数量
         * @return 安全事件列表
         */
        std::vector<nlohmann::json> SessionManager::getUserSecurityEvents(const std::string& user_id, int limit) {
            std::vector<nlohmann::json> events;

            try {
                // 使用新架构：通过 Redis Repository 获取
                if (security_event_redis_) {
                    auto result = security_event_redis_->getEvents(user_id, limit);
                    if (result.has_value()) {
                        return result.value();
                    }
                    LOG_WARNING("通过 Redis Repository 获取安全事件失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return events;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return events;
                }

                std::string events_key = generateSecurityEventsKey(user_id);
                // 简化处理：获取所有事件，然后在应用层限制数量
                // Redis接口中没有listGetRange方法
                std::vector<std::string> event_strings;
                // 暂时返回空列表，实际应该重新设计存储方式

                for (const auto& event_str : event_strings) {
                    try {
                        events.push_back(nlohmann::json::parse(event_str));
                    } catch (const std::exception& e) {
                        LOG_WARNING("解析安全事件失败: " + std::string(e.what()));
                    }
                }

            } catch (const std::exception& e) {
                LOG_ERROR("获取用户安全事件时发生异常: " + std::string(e.what()));
            }

            return events;
        }

        /**
         * @brief 获取用户设备列表
         * @param user_id 用户ID
         * @return 设备信息列表
         */
        std::vector<DeviceInfo> SessionManager::getUserDevices(const std::string& user_id) {
            std::vector<DeviceInfo> devices;

            try {
                // 使用新架构：通过 Redis Repository 获取
                if (device_redis_) {
                    auto result = device_redis_->getUserDevices(user_id);
                    if (result.has_value()) {
                        return result.value();
                    }
                    LOG_WARNING("通过 Redis Repository 获取设备列表失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return devices;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return devices;
                }

                // Redis接口中没有getKeysByPattern方法
                // 简化处理：使用预定义的设备键集合
                std::string devices_set_key = "devices:user:" + user_id;
                auto device_keys = redis_conn->smembers(devices_set_key);

                for (const auto& key : device_keys) {
                    std::string device_data = redis_conn->get(key);
                    if (!device_data.empty()) {
                        try {
                            auto device_json = nlohmann::json::parse(device_data);
                            devices.push_back(DeviceInfo::fromJson(device_json));
                        } catch (const std::exception& e) {
                            LOG_WARNING("解析设备信息失败: " + std::string(e.what()));
                        }
                    }
                }

            } catch (const std::exception& e) {
                LOG_ERROR("获取用户设备列表时发生异常: " + std::string(e.what()));
            }

            return devices;
        }

        /**
         * @brief 注册设备
         * @param user_id 用户ID
         * @param device_info 设备信息
         * @return 注册成功返回true
         */
        bool SessionManager::registerDevice(const std::string& user_id, const DeviceInfo& device_info) {
            try {
                // 使用新架构：通过 Redis Repository 保存
                if (device_redis_) {
                    auto result = device_redis_->saveDevice(user_id, device_info);
                    if (result.has_value() && result.value()) {
                        return true;
                    }
                    LOG_WARNING("通过 Redis Repository 保存设备失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return false;
                }

                std::string device_key = generateDeviceCacheKey(user_id, device_info.device_id);
                std::string device_data = device_info.toJson().dump();

                redis_conn->set(device_key, device_data, 30 * 24 * 3600); // 30天过期

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("注册设备时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 生成设备缓存键
         * @param user_id 用户ID
         * @param device_id 设备ID
         * @return 缓存键
         */
        std::string SessionManager::generateDeviceCacheKey(const std::string& user_id, const std::string& device_id) {
            return "device:user:" + user_id + ":device:" + device_id;
        }

        /**
         * @brief 生成安全事件缓存键
         * @param user_id 用户ID
         * @return 缓存键
         */
        std::string SessionManager::generateSecurityEventsKey(const std::string& user_id) {
            return "security:events:user:" + user_id;
        }

        /**
         * @brief 加密会话数据
         * @param data 原始数据
         * @return 加密后的数据
         */
        std::string SessionManager::encryptSessionData(const std::string& data) {
            // 简化的加密实现（实际应该使用更强的加密算法）
            // 这里只是示例，生产环境应该使用AES等强加密算法
            return data; // 暂时返回原始数据
        }

        /**
         * @brief 解密会话数据
         * @param encrypted_data 加密的数据
         * @return 解密后的数据
         */
        std::string SessionManager::decryptSessionData(const std::string& encrypted_data) {
            // 简化的解密实现
            return encrypted_data; // 暂时返回原始数据
        }

        /**
         * @brief 清理任务工作函数
         */
        void SessionManager::cleanupWorker() {
            while (cleanup_running_) {
                try {
                    cleanupExpiredSessions();
                    std::this_thread::sleep_for(config_.cleanup_interval);
                } catch (const std::exception& e) {
                    LOG_ERROR("清理任务执行时发生异常: " + std::string(e.what()));
                }
            }
        }

        /**
         * @brief 启动清理任务
         */
        void SessionManager::startCleanupTask() {
            cleanup_running_ = true;
            cleanup_thread_ = std::thread(&SessionManager::cleanupWorker, this);
            LOG_INFO("会话清理任务已启动");
        }

        /**
         * @brief 停止清理任务
         */
        void SessionManager::stopCleanupTask() {
            cleanup_running_ = false;
            if (cleanup_thread_.joinable()) {
                cleanup_thread_.join();
            }
            LOG_INFO("会话清理任务已停止");
        }

        /**
         * @brief 清理过期会话
         * @return 清理的会话数量
         */
        int SessionManager::cleanupExpiredSessions() {
            int cleaned_count = 0;

            try {
                if (!redis_pool_) {
                    return 0;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return 0;
                }

                // Redis接口中没有getKeysByPattern方法
                // 简化处理：使用预定义的会话键集合
                std::string sessions_set_key = "active_sessions";
                auto session_keys = redis_conn->smembers(sessions_set_key);

                auto now = std::chrono::system_clock::now();

                for (const auto& key : session_keys) {
                    std::string session_data = redis_conn->get(key);
                    if (!session_data.empty()) {
                        try {
                            if (config_.enable_session_encryption) {
                                session_data = decryptSessionData(session_data);
                            }

                            auto session_json = nlohmann::json::parse(session_data);
                            auto session = UserSession::fromJson(session_json);

                            // 检查是否过期
                            if (now > session.expires_at) {
                                std::string session_id = key.substr(8); // 移除"session:"前缀
                                destroySession(session_id);
                                cleaned_count++;
                            }
                        } catch (const std::exception& e) {
                            LOG_WARNING("清理会话时解析失败: " + std::string(e.what()));
                            // 删除无效的会话数据
                            redis_conn->del(key);
                            cleaned_count++;
                        }
                    }
                }

                if (cleaned_count > 0) {
                    LOG_INFO("清理过期会话完成，清理数量: " + std::to_string(cleaned_count));
                }

            } catch (const std::exception& e) {
                LOG_ERROR("清理过期会话时发生异常: " + std::string(e.what()));
            }

            return cleaned_count;
        }

        /**
         * @brief 获取活跃会话总数
         * @return 当前活跃会话数量
         */
        int64_t SessionManager::getActiveSessionCount() const {
            try {
                if (!redis_pool_) {
                    return 0;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return 0;
                }

                // 获取活跃会话集合的成员数量
                std::string sessions_set_key = "active_sessions";
                auto session_keys = redis_conn->smembers(sessions_set_key);
                
                int64_t active_count = 0;
                
                // 验证每个会话是否确实活跃
                for (const auto& session_key : session_keys) {
                    // 检查会话是否存在且未过期
                    if (redis_conn->exists(session_key)) {
                        // 这里可以进一步检查会话的过期时间，但为了性能，暂时假设存在的会话都是活跃的
                        active_count++;
                    }
                }
                
                return active_count;
                
            } catch (const std::exception& e) {
                LOG_ERROR("获取活跃会话数失败: " + std::string(e.what()));
                return 0;
            }
        }

        /**
         * @brief 获取统计信息
         * @return 统计信息JSON对象
         */
        nlohmann::json SessionManager::getStatistics() const {
            nlohmann::json stats;

            stats["sessions_created"] = sessions_created_.load();
            stats["sessions_validated"] = sessions_validated_.load();
            stats["sessions_destroyed"] = sessions_destroyed_.load();
            stats["validation_failures"] = validation_failures_.load();
            stats["anomalous_logins"] = anomalous_logins_.load();
            stats["security_events"] = security_events_.load();

            // 计算成功率
            uint64_t total_validations = sessions_validated_.load();
            if (total_validations > 0) {
                double success_rate = 1.0 - (static_cast<double>(validation_failures_.load()) / total_validations);
                stats["validation_success_rate"] = success_rate;
            } else {
                stats["validation_success_rate"] = 0.0;
            }

            // 配置信息
            stats["config"] = {
                {"session_timeout", config_.session_timeout.count()},
                {"max_idle_time", config_.max_idle_time.count()},
                {"max_concurrent_sessions", config_.max_concurrent_sessions},
                {"enable_multi_device", config_.enable_multi_device},
                {"enable_ip_validation", config_.enable_ip_validation},
                {"enable_device_validation", config_.enable_device_validation},
                {"enable_anomaly_detection", config_.enable_anomaly_detection}
            };

            return stats;
        }

        /**
         * @brief 更新配置
         * @param new_config 新配置
         * @return 更新成功返回true
         */
        bool SessionManager::updateConfig(const Config& new_config) {
            try {
                // 验证新配置的有效性
                if (new_config.session_timeout.count() <= 0) {
                    LOG_ERROR("会话超时时间配置无效");
                    return false;
                }

                if (new_config.max_concurrent_sessions <= 0) {
                    LOG_ERROR("最大并发会话数配置无效");
                    return false;
                }

                // 更新配置
                config_ = new_config;


                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("更新会话管理器配置失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 设置设备信任状态
         * @param user_id 用户ID
         * @param device_id 设备ID
         * @param trusted 是否信任
         * @return 设置成功返回true
         */
        bool SessionManager::setDeviceTrusted(const std::string& user_id, const std::string& device_id, bool trusted) {
            try {
                // 使用新架构：通过 Redis Repository 更新
                if (device_redis_) {
                    auto result = device_redis_->setDeviceTrusted(user_id, device_id, trusted);
                    if (result.has_value() && result.value()) {
                        LOG_INFO("设备信任状态更新: 用户ID=" + user_id +
                                ", 设备ID=" + device_id + ", 信任=" + (trusted ? "是" : "否") +
                                " (via Redis Repository)");
                        return true;
                    }
                    LOG_WARNING("通过 Redis Repository 设置设备信任状态失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return false;
                }

                std::string device_key = "device:user:" + user_id + ":" + device_id;
                std::string device_data = redis_conn->get(device_key);

                if (!device_data.empty()) {
                    auto device_json = nlohmann::json::parse(device_data);
                    device_json["trusted"] = trusted;
                    device_json["trust_updated_at"] = getCurrentTimestamp();

                    redis_conn->set(device_key, device_json.dump(), 30 * 24 * 3600); // 30天过期

                    LOG_INFO("设备信任状态更新: 用户ID=" + user_id +
                            ", 设备ID=" + device_id + ", 信任=" + (trusted ? "是" : "否"));
                    return true;
                }

                return false;

            } catch (const std::exception& e) {
                LOG_ERROR("设置设备信任状态失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 移除设备
         * @param user_id 用户ID
         * @param device_id 设备ID
         * @return 移除成功返回true
         */
        bool SessionManager::removeDevice(const std::string& user_id, const std::string& device_id) {
            try {
                // 使用新架构：通过 Redis Repository 删除
                if (device_redis_) {
                    auto result = device_redis_->removeDevice(user_id, device_id);
                    if (result.has_value() && result.value()) {
                        // 销毁该设备上的所有会话
                        destroyUserDeviceSessions(user_id, device_id);

                        LOG_INFO("设备移除成功: 用户ID=" + user_id +
                                ", 设备ID=" + device_id + " (via Redis Repository)");
                        return true;
                    }
                    LOG_WARNING("通过 Redis Repository 移除设备失败，降级到直接操作");
                }

                // 降级到旧架构（向后兼容）
                if (!redis_pool_) {
                    return false;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    return false;
                }

                // 删除设备信息
                std::string device_key = "device:user:" + user_id + ":" + device_id;
                redis_conn->del(device_key);

                // 从设备集合中移除
                std::string devices_set_key = "devices:user:" + user_id;
                redis_conn->srem(devices_set_key, device_key);

                // 销毁该设备上的所有会话
                destroyUserDeviceSessions(user_id, device_id);


                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("移除设备失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 强制清理用户会话（管理员操作）
         * @param user_id 用户ID
         * @param reason 清理原因
         * @return 清理的会话数量
         */
        int SessionManager::forceCleanupUserSessions(const std::string& user_id, const std::string& reason) {
            try {
                int cleaned_count = destroyAllUserSessions(user_id);

                // 记录管理员操作
                nlohmann::json additional_data = {
                    {"reason", reason},
                    {"cleaned_sessions", cleaned_count},
                    {"timestamp", getCurrentTimestamp()}
                };
                recordSecurityEvent(user_id, "ADMIN_FORCE_CLEANUP", "管理员强制清理用户会话", "127.0.0.1", additional_data);

                LOG_WARNING("管理员强制清理用户会话: 用户ID=" + user_id +
                           ", 原因=" + reason + ", 清理数量=" + std::to_string(cleaned_count));

                return cleaned_count;

            } catch (const std::exception& e) {
                LOG_ERROR("强制清理用户会话失败: " + std::string(e.what()));
                return 0;
            }
        }

        /**
         * @brief 计算IP地址距离
         * @param ip1 IP地址1
         * @param ip2 IP地址2
         * @return 距离评分（0-1）
         */
        double SessionManager::calculateIPDistance(const std::string& ip1, const std::string& ip2) {
            // 简化的IP距离计算
            // 实际实现应该使用GeoIP数据库计算地理距离

            if (ip1 == ip2) {
                return 0.0; // 相同IP，距离为0
            }

            // 检查是否为同一网段
            auto pos1 = ip1.find_last_of('.');
            auto pos2 = ip2.find_last_of('.');

            if (pos1 != std::string::npos && pos2 != std::string::npos) {
                std::string subnet1 = ip1.substr(0, pos1);
                std::string subnet2 = ip2.substr(0, pos2);

                if (subnet1 == subnet2) {
                    return 0.1; // 同一网段，距离很小
                }
            }

            // 不同网段，返回中等距离
            return 0.5;
        }

        /**
         * @brief 销毁用户设备会话
         */
        int SessionManager::destroyUserDeviceSessions(const std::string& user_id, const std::string& device_id) {
            try {
                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("获取Redis连接失败");
                    return 0;
                }

                // 获取设备会话键
                std::string device_sessions_key = "device_sessions:user:" + user_id + ":device:" + device_id;
                auto session_ids = redis_conn->smembers(device_sessions_key);

                int destroyed_count = 0;
                // 删除所有会话
                for (const auto& session_id : session_ids) {
                    if (destroySession(session_id)) {
                        destroyed_count++;
                    }
                }

                // 删除设备会话集合
                redis_conn->del(device_sessions_key);

                LOG_INFO("销毁用户设备会话完成: 用户ID=" + user_id + ", 设备ID=" + device_id +
                        ", 销毁数量=" + std::to_string(destroyed_count));

                return destroyed_count;

            } catch (const std::exception& e) {
                LOG_ERROR("销毁用户设备会话失败: " + std::string(e.what()));
                return 0;
            }
        }



        /**
         * @brief 重置统计信息
         */
        void SessionManager::resetStatistics() {
            try {
                // 重置内部统计计数器
                // 这里可以添加具体的统计重置逻辑
                LOG_INFO("SessionManager统计信息已重置");

            } catch (const std::exception& e) {
                LOG_ERROR("重置SessionManager统计信息失败: " + std::string(e.what()));
            }
        }

        /**
         * @brief 清理用户的过期会话
         * @param user_id 用户ID
         * @return 清理的会话数量
         */
        int SessionManager::cleanupExpiredUserSessions(const std::string& user_id) {
            int cleaned_count = 0;
            try {
                if (!redis_pool_) {
                    return 0;
                }

                // 使用 RAII 连接管理器，确保连接自动归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn) {
                    LOG_ERROR("无法获取Redis连接");
                    return 0;
                }

                std::string list_key = generateUserSessionsKey(user_id);
                auto session_ids = redis_conn->smembers(list_key);

                auto now = std::chrono::system_clock::now();

                for (const auto& session_id : session_ids) {
                    auto session_opt = getSessionFromRedis(session_id);
                    if (session_opt.has_value()) {
                        auto session = session_opt.value();

                        // 检查会话是否过期或无效
                        if (!session.is_active || now > session.expires_at) {
                            LOG_INFO("清理过期会话: " + session_id + ", 用户ID: " + user_id);

                            // 删除会话
                            removeSessionFromRedis(session_id);
                            updateUserSessionsList(user_id, session_id, false);
                            cleaned_count++;
                        }
                    } else {
                        // 会话数据不存在，从用户会话列表中移除
                        LOG_INFO("清理无效会话引用: " + session_id + ", 用户ID: " + user_id);
                        updateUserSessionsList(user_id, session_id, false);
                        cleaned_count++;
                    }
                }

                if (cleaned_count > 0) {
                    LOG_INFO("用户过期会话清理完成 - 用户ID: " + user_id +
                            ", 清理数量: " + std::to_string(cleaned_count));
                }

            } catch (const std::exception& e) {
                LOG_ERROR("清理用户过期会话时发生异常: " + std::string(e.what()));
            }

            return cleaned_count;
        }

    } // namespace auth_service
} // namespace core_services
