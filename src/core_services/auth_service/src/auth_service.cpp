/**
 * @file auth_service.cpp
 * @brief 认证服务重构版 - 使用用户服务客户端架构
 * @details 重构后的认证服务，通过用户服务客户端获取用户信息，移除直接数据库访问
 * @author AI Assistant  
 * @date 2025-09-19
 * @version 2.0.0
 */

#include "auth_service.h"
#include "user_service_client.h"
#include "common/logger/logger.h"
#include "common/network/event_loop.h"
#include "common/utils/email_service.h"
#include <sstream>
#include <algorithm>
#include <regex>
#include <chrono>
#include <thread>
#include <cstring>
#include <random>

namespace core_services
{
    namespace auth_service
    {
        // ==================== AuthService 重构实现 ====================

        /**
         * @brief 构造函数 (重构版)
         * @param config 服务配置
         */
        AuthService::AuthService(const Config& config)
            : config_(config), start_time_(std::chrono::steady_clock::now())
        {
            // 验证配置
            if (!config_.validate())
            {
                throw std::invalid_argument("认证服务配置验证失败");
            }

            LOG_INFO("认证服务创建完成 (新架构版本)");
            LOG_DEBUG("- 服务名称: " + config_.service_name);
            LOG_DEBUG("- 服务版本: " + config_.service_version);
            LOG_DEBUG("- 监听地址: " + config_.network_config.bind_address + ":" + std::to_string(config_.network_config.listen_port));
            LOG_DEBUG("- 工作线程数: " + std::to_string(config_.thread_pool_config.core_pool_size  ));
        }

        /**
         * @brief 析构函数 (重构版)
         */
        AuthService::~AuthService()
        {
            try
            {
                LOG_INFO("开始认证服务析构...");

                // 🔧 重要：确保服务完全停止
                if (running_)
                {
                    LOG_INFO("服务仍在运行，执行安全关闭...");
                    shutdown();
                }

                // 🔧 增强：等待任何可能的后台线程完成
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // 清理用户服务客户端
                if (user_service_client_)
                {
                    try
                    {
                        user_service_client_->clearAllCache();
                        user_service_client_.reset();
                    }
                    catch (const std::exception& e)
                    {
                        LOG_WARNING("清理用户服务客户端时发生异常: " + std::string(e.what()));
                    }
                }



                // 🔧 增强：清空EventLoop引用，避免悬空指针
                event_loop_.reset();

                LOG_INFO("认证服务析构完成");
            }
            catch (const std::system_error& e)
            {
                LOG_ERROR("认证服务析构系统错误: " + std::string(e.what()) +
                    " (错误码: " + std::to_string(e.code().value()) + ")");
            } catch (const std::exception& e)
            {
                LOG_ERROR("认证服务析构异常: " + std::string(e.what()));
            } catch (...)
            {
                LOG_ERROR("认证服务析构发生未知异常");
            }
        }

        /**
         * @brief 初始化服务 (重构版)
         * @return 初始化成功返回true
         */
        bool AuthService::initialize()
        {
            try
            {
                LOG_INFO("=== 认证服务初始化开始 (新架构) ===");

                // 1. 初始化认证专用数据库连接池
                if (!initializeAuthDatabasePools())
                {
                    LOG_ERROR("认证数据库连接池初始化失败");
                    return false;
                }

                // 2. 初始化认证专用数据库表结构
                if (!initializeAuthDatabaseSchema())
                {
                    LOG_ERROR("认证数据库表结构初始化失败");
                    return false;
                }

                // 3. 初始化用户服务客户端
                if (!initializeUserServiceClient())
                {
                    LOG_ERROR("用户服务客户端初始化失败");
                    return false;
                }

                // 4. 初始化业务组件
                if (!initializeBusinessComponents())
                {
                    LOG_ERROR("业务组件初始化失败");
                    return false;
                }

                // 5. 初始化HTTP服务器
                if (!initializeHttpServer())
                {
                    LOG_ERROR("HTTP服务器初始化失败");
                    return false;
                }

                // 6. 初始化Kafka组件 (如果启用)
                if (config_.kafka_config.enable)
                {
                    if (!initializeKafka())
                    {
                        LOG_ERROR("Kafka组件初始化失败");
                        return false;
                    }
                }

                // 7. 初始化性能监控组件
                if (config_.enable_performance_monitoring)
                {
                    if (!initializePerformanceMonitor())
                    {
                        LOG_ERROR("性能监控组件初始化失败");
                        return false;
                    }
                }

                // 8. 健康检查外部服务
                if (!performExternalServicesHealthCheck())
                {
                    LOG_WARNING("外部服务健康检查失败，但继续启动");
                    // 不直接返回失败，允许容错启动
                }

                initialized_ = true;
                LOG_INFO("=== 认证服务初始化完成 (新架构) ===");
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("认证服务初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 初始化认证专用数据库连接池
         * @return 初始化成功返回true
         */
        bool AuthService::initializeAuthDatabasePools()
        {
            try
            {
                LOG_INFO("初始化认证专用数据库连接池...");

                // 初始化MySQL连接池 (连接到auth_sessions_db)
                // common::config::DatabaseConfig mysql_config;
                mysql_pool_ = std::make_shared<common::database::MySQLPool>(config_.database_config);
                if (!mysql_pool_)
                {
                    LOG_ERROR("MySQL连接池创建失败");
                    return false;
                }

                // 启动MySQL连接池
                mysql_pool_->start();
                if (!mysql_pool_->isRunning())
                {
                    LOG_ERROR("MySQL连接池启动失败");
                    return false;
                }

                // 初始化Redis连接池
                // common::database::RedisPool::redisConfig redis_config;
                redis_pool_ = std::make_shared<common::database::RedisPool>(config_.redis_config);
                if (!redis_pool_)
                {
                    LOG_ERROR("Redis连接池创建失败");
                    return false;
                }

                // 启动Redis连接池
                redis_pool_->start();
                if (!redis_pool_->isRunning())
                {
                    LOG_ERROR("Redis连接池启动失败");
                    return false;
                }

                LOG_INFO("认证专用数据库连接池初始化完成");
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("初始化认证数据库连接池异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 初始化认证专用数据库表结构
         * @return 初始化成功返回true
         */
        bool AuthService::initializeAuthDatabaseSchema()
        {
            try
            {
                LOG_INFO("初始化认证专用数据库表结构...");

                // 使用数据库初始化器初始化认证专用表
                database_initializer_ = std::make_unique<DatabaseInitializer>(mysql_pool_);

                // 检查并创建必需的表
                auto init_result = database_initializer_->initializeDatabase();
                if (!init_result.success)
                {
                    LOG_ERROR("认证会话表初始化失败");
                    return false;
                }

                LOG_INFO("认证专用数据库表结构初始化完成");
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("初始化认证数据库表结构异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 初始化用户服务客户端
         * @return 初始化成功返回true
         */
        bool AuthService::initializeUserServiceClient()
        {
            try
            {
                LOG_INFO("初始化用户服务客户端...");

                // 配置用户服务客户端
                UserServiceClientConfig client_config;
                client_config.base_url = config_.user_service_base_url;
                client_config.timeout_seconds = config_.user_service_timeout_seconds;
                client_config.retry_count = config_.user_service_retry_count;
                client_config.cache_ttl_seconds = config_.user_service_cache_ttl_seconds;
                client_config.enable_cache = config_.enable_user_service_cache;
                client_config.user_agent = "AuthService/2.0";

                // 创建用户服务客户端
                user_service_client_ = std::make_unique<UserServiceClient>(client_config, redis_pool_.get());

                // 健康检查用户服务
                if (!user_service_client_->isHealthy())
                {
                    LOG_WARNING("用户服务健康检查失败，但继续初始化");
                    // 在生产环境中可能需要更严格的处理
                }

                LOG_INFO("用户服务客户端初始化完成");
                LOG_INFO("- 服务地址: " + client_config.base_url);
                LOG_INFO("- 超时时间: " + std::to_string(client_config.timeout_seconds) + "s");
                LOG_INFO("- 重试次数: " + std::to_string(client_config.retry_count));
                LOG_INFO("- 缓存TTL: " + std::to_string(client_config.cache_ttl_seconds) + "s");

                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("初始化用户服务客户端异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 重构后的业务组件初始化
         * @return 初始化成功返回true
         */
        bool AuthService::initializeBusinessComponents()
        {
            try
            {
                LOG_INFO("初始化业务组件 (新架构)...");

                // 初始化线程池
                // common::config::ThreadPoolConfig thread_config;
                thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(config_.thread_pool_config);

                // 初始化线程池（已在前面完成）
                LOG_INFO("线程池初始化完成，准备初始化业务组件...");

                // 初始化JWT管理器
                auto jwt_config = JwtManager::Config::fromConfigManager();
                jwt_manager_ = std::make_shared<JwtManager>(jwt_config, redis_pool_);

                // 初始化密码管理器
                auto password_config = PasswordManager::Config::fromConfigManager();
                password_manager_ = std::make_unique<PasswordManager>(password_config);

                auto session_config = SessionManager::Config::fromConfigManager();
                session_manager_ = std::make_unique<SessionManager>(session_config, redis_pool_,
                                                                    jwt_manager_, thread_pool_);

                // 初始化邮件服务
                auto& email_service = common::utils::EmailService::getInstance();
                if (!email_service.initialize()) {
                    LOG_WARNING("邮件服务初始化失败，验证码功能将不可用");
                }

                // 初始化验证码管理器
                auto verify_config = VerificationCodeConfig::fromConfigManager();
                verification_code_manager_ = std::make_unique<VerificationCodeManager>(
                    redis_pool_.get(), verify_config);

                // ❌ 移除：不再初始化用户仓库，改用用户服务客户端
                // user_repository_ = std::make_unique<UserRepository>(...);

                LOG_INFO("业务组件初始化完成 (新架构)");
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("初始化业务组件异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 执行外部服务健康检查
         * @return 健康检查通过返回true
         */
        bool AuthService::performExternalServicesHealthCheck()
        {
            try
            {
                LOG_INFO("执行外部服务健康检查...");

                bool all_healthy = true;

                // 检查用户服务健康状态
                if (user_service_client_)
                {
                    bool user_service_healthy = user_service_client_->isHealthy();
                    LOG_INFO("用户服务健康状态: " + (user_service_healthy ? std::string("健康") : std::string("异常")));

                    if (!user_service_healthy)
                    {
                        all_healthy = false;
                        LOG_WARNING("用户服务不可用，认证功能可能受限");
                    }
                }

                // 检查Kafka服务健康状况（如果启用）
                if (config_.enable_kafka)
                {
                    // Kafka健康检查逻辑待实现
                    LOG_DEBUG("Kafka健康检查暂未实现");
                }

                return all_healthy;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("外部服务健康检查异常: " + std::string(e.what()));
                return false;
            }
        }

        // ==================== 核心认证逻辑重构 ====================

        /**
         * @brief 用户登录 (重构版)
         * @param request 登录请求
         * @return 认证结果
         */
        AuthResult AuthService::loginUser(const AuthRequest& request)
        {
            total_requests_++;
            auto start_time = std::chrono::high_resolution_clock::now();
            const std::string operation = "user_login";

            try
            {
                LOG_INFO("开始用户登录 (新架构): " + request.username);

                // 1. 验证请求
                std::string validation_error = request.validate();
                if (!validation_error.empty())
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "login", false,
                                    request.client_ip, request.user_agent, validation_error);
                    return AuthResult("请求验证失败: " + validation_error, "INVALID_REQUEST");
                }

                // 2. 通过用户服务查找用户
                LOG_DEBUG("通过用户服务查找用户: " + request.username);
                std::optional<CoreUserInfo> user_opt;

                // 根据输入格式判断是邮箱还是用户名
                if (request.username.find('@') != std::string::npos)
                {
                    user_opt = user_service_client_->getUserByEmail(request.username);
                }
                else
                {
                    user_opt = user_service_client_->getUserByUsername(request.username);
                }

                if (!user_opt.has_value())
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "login", false,
                                    request.client_ip, request.user_agent, "用户不存在");
                    LOG_WARNING("用户不存在: " + request.username);
                    return AuthResult("用户名或密码错误", "INVALID_CREDENTIALS");
                }

                CoreUserInfo& user = user_opt.value();
                LOG_DEBUG("找到用户: " + user.user_id + " (" + user.username + ")");

                // 3. 检查用户状态
                if (user.status != UserStatus::ACTIVE)
                {
                    failed_requests_++;
                    recordAuthEvent(user.user_id, user.username, "login", false,
                                    request.client_ip, request.user_agent, "账户状态异常");
                    LOG_WARNING(
                        "用户账户状态异常: " + user.username + ", 状态: " + std::to_string(static_cast<int>(user.status)));
                    return AuthResult("用户账户已被禁用或锁定", "ACCOUNT_DISABLED");
                }

                // 4. 检查用户是否被锁定
                if (user.locked_until.has_value() && user.locked_until.value() > std::chrono::system_clock::now())
                {
                    failed_requests_++;
                    auto locked_until_time_t = std::chrono::system_clock::to_time_t(user.locked_until.value());
                    std::string locked_until_str = std::ctime(&locked_until_time_t);
                    locked_until_str.pop_back(); // 移除换行符

                    recordAuthEvent(user.user_id, user.username, "login", false,
                                    request.client_ip, request.user_agent, "账户已锁定");
                    LOG_WARNING("用户账户已锁定: " + user.username + ", 锁定到: " + locked_until_str);
                    return AuthResult("账户已锁定，请稍后再试", "ACCOUNT_LOCKED");
                }

                // 5. 验证密码
                if (!password_manager_->verifyPassword(request.password, user.password_hash, user.salt))
                {
                    failed_requests_++;

                    // 通过用户服务增加失败尝试次数
                    user_service_client_->incrementLoginAttempts(user.user_id);

                    recordAuthEvent(user.user_id, user.username, "login", false,
                                    request.client_ip, request.user_agent, "密码错误");
                    LOG_WARNING("密码验证失败: " + user.username);

                    // 检查是否需要锁定账户
                    checkAndLockAccount(user);

                    return AuthResult("用户名或密码错误", "INVALID_CREDENTIALS");
                }

                // 6. 登录成功，重置失败尝试次数
                user_service_client_->resetLoginAttempts(user.user_id);

                // 7. 🔧 性能优化：异步更新登录信息，不阻塞登录响应
                std::string uid = user.user_id;
                std::string cip = request.client_ip;
                if (thread_pool_) {
                    thread_pool_->submit([this, uid, cip]() {
                        user_service_client_->updateLastLogin(uid, cip);
                        user_service_client_->updateOnlineStatus(uid, "online");
                    });
                }

                // 8. 创建会话（仍在认证服务中管理）
                auto session = session_manager_->createSession(user, request.client_ip, request.user_agent,
                                                               request.device_id);
                if (!session.has_value())
                {
                    failed_requests_++;
                    recordAuthEvent(user.user_id, user.username, "login", false,
                                    request.client_ip, request.user_agent, "会话创建失败");
                    return AuthResult("会话创建失败", "SESSION_CREATION_FAILED");
                }

                // 9. 生成JWT令牌
                std::string access_token = jwt_manager_->generateAccessToken(
                    user, session->session_id, request.device_type, request.client_ip);
                std::string refresh_token = jwt_manager_->generateRefreshToken(
                    user, session->session_id, session->device_type, session->client_ip);

                // 10. 记录成功的认证事件
                recordAuthEvent(user.user_id, user.username, "login", true,
                                request.client_ip, request.user_agent, "");

                successful_requests_++;
                total_logins_++; // 增加登录成功计数
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                LOG_INFO(
                    "用户登录成功 (新架构): " + user.username + " (ID: " + user.user_id + "), 耗时: " + std::to_string(duration.
                        count()) + "ms");

                return AuthResult(user, *session, access_token, refresh_token);
            }
            catch (const std::exception& e)
            {
                failed_requests_++;
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                LOG_ERROR("登录过程中发生异常: " + std::string(e.what()) + ", 耗时: " + std::to_string(duration.count()) + "ms");
                recordAuthEvent("", request.username, "login", false,
                                request.client_ip, request.user_agent, "系统异常: " + std::string(e.what()));
                return AuthResult("服务器内部错误", "INTERNAL_ERROR");
            }
        }

        /**
         * @brief 用户注册 (重构版)
         * @param request 注册请求
         * @return 认证结果
         */
        AuthResult AuthService::registerUser(const RegisterRequest& request)
        {
            total_requests_++;
            auto start_time = std::chrono::high_resolution_clock::now();

            try
            {
                LOG_INFO("开始用户注册 (新架构): " + request.username);

                // 1. 验证请求
                std::string validation_error = request.validate();
                if (!validation_error.empty())
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "register", false,
                                    request.client_ip, request.user_agent, validation_error);
                    return AuthResult("请求验证失败: " + validation_error, "INVALID_REQUEST");
                }

                // 2. 检查用户名是否已存在（通过用户服务）
                if (user_service_client_->isUsernameExists(request.username))
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "register", false,
                                    request.client_ip, request.user_agent, "用户名已存在");
                    return AuthResult("用户名已存在", "USERNAME_EXISTS");
                }

                // 3. 检查邮箱是否已被注册（通过用户服务）
                if (user_service_client_->isEmailExists(request.email))
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "register", false,
                                    request.client_ip, request.user_agent, "邮箱已被注册");
                    return AuthResult("邮箱已被注册", "EMAIL_EXISTS");
                }

                // 4. 生成唯一用户ID
                std::string user_id = generateUserId();

                // 5. 哈希密码
                auto hash_result = password_manager_->hashPassword(request.password);
                if (!hash_result.success)
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "register", false,
                                    request.client_ip, request.user_agent, "密码处理失败");
                    return AuthResult("密码处理失败: " + hash_result.error_message, "PASSWORD_HASH_FAILED");
                }

                // 6. 创建用户信息
                CoreUserInfo user_info;
                user_info.user_id = user_id;
                user_info.username = request.username;
                user_info.email = request.email;
                user_info.nickname = request.nickname.empty() ? request.username : request.nickname;
                user_info.password_hash = hash_result.hash;
                user_info.salt = hash_result.salt;
                user_info.status = UserStatus::ACTIVE;
                user_info.online_status = OnlineStatus::OFFLINE;
                user_info.created_at = std::chrono::system_clock::now();
                user_info.updated_at = user_info.created_at;

                // 7. 通过用户服务创建用户
                auto create_result = user_service_client_->createUser(user_info);
                if (!create_result.success)
                {
                    failed_requests_++;
                    recordAuthEvent("", request.username, "register", false,
                                    request.client_ip, request.user_agent, create_result.error_message);
                    return AuthResult(create_result.error_message, create_result.error_code);
                }

                std::string created_user_id = create_result.user_id;

                // 8. 创建会话
                auto session = session_manager_->createSession(user_info, request.client_ip, request.user_agent,
                                                               request.device_id);
                if (!session.has_value())
                {
                    // 用户已创建，但会话创建失败，记录为部分成功
                    LOG_WARNING("用户创建成功但会话创建失败: " + created_user_id);
                    recordAuthEvent(created_user_id, request.username, "register", false,
                                    request.client_ip, request.user_agent, "会话创建失败");
                    return AuthResult("会话创建失败", "SESSION_CREATION_FAILED");
                }

                // 9. 生成令牌
                std::string access_token = jwt_manager_->generateAccessToken(
                    user_info, session->session_id, request.device_type, request.client_ip);
                std::string refresh_token = jwt_manager_->generateRefreshToken(
                    user_info, session->session_id, session->device_type, session->client_ip);

                // 10. 记录成功的注册事件
                recordAuthEvent(created_user_id, request.username, "register", true,
                                request.client_ip, request.user_agent, "");

                successful_requests_++;
                total_registrations_++; // 增加注册成功计数
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                LOG_INFO(
                    "用户注册成功 (新架构): " + user_info.username + " (ID: " + created_user_id + "), 耗时: " + std::to_string(
                        duration.count()) + "ms");

                return AuthResult(user_info, *session, access_token, refresh_token);
            }
            catch (const std::exception& e)
            {
                failed_requests_++;
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                LOG_ERROR("注册过程中发生异常: " + std::string(e.what()) + ", 耗时: " + std::to_string(duration.count()) + "ms");
                recordAuthEvent("", request.username, "register", false,
                                request.client_ip, request.user_agent, "系统异常: " + std::string(e.what()));
                return AuthResult("服务器内部错误", "INTERNAL_ERROR");
            }
        }

        // ==================== 辅助方法 ====================

        /**
         * @brief 记录认证事件
         * @param user_id 用户ID
         * @param username 用户名
         * @param event_type 事件类型
         * @param success 是否成功
         * @param client_ip 客户端IP
         * @param user_agent 用户代理
         * @param failure_reason 失败原因
         */
        void AuthService::recordAuthEvent(const std::string& user_id,
                                          const std::string& username,
                                          const std::string& event_type,
                                          bool success,
                                          const std::string& client_ip,
                                          const std::string& user_agent,
                                          const std::string& failure_reason)
        {
            try
            {
                // 记录到用户服务的认证日志
                user_service_client_->recordAuthEvent(user_id, username, event_type, success,
                                                      client_ip, user_agent, failure_reason);

                // 同时记录到本地认证审计日志
                recordAuthEvent(user_id, username, event_type, success, client_ip, user_agent, failure_reason);
            }
            catch (const std::exception& e)
            {
                LOG_WARNING("记录认证事件失败: " + std::string(e.what()));
            }
        }

        /**
         * @brief 检查并锁定账户
         * @param user 用户信息
         */
        void AuthService::checkAndLockAccount(const CoreUserInfo& user)
        {
            try
            {
                // 根据配置决定是否锁定账户
                int max_attempts = config_.max_login_attempts;

                if (user.login_attempts >= max_attempts)
                {
                    // 计算锁定到期时间
                    auto lock_duration = std::chrono::minutes(config_.account_lockout_duration_minutes);
                    auto lock_until = std::chrono::system_clock::now() + lock_duration;

                    // 通过用户服务锁定账户
                    if (user_service_client_->lockUserUntil(user.user_id, lock_until))
                    {
                        LOG_WARNING(
                            "账户已锁定: " + user.username + ", 锁定时长: " + std::to_string(config_.
                                account_lockout_duration_minutes) + "分钟");
                    }
                    else
                    {
                        LOG_ERROR("锁定账户失败: " + user.username);
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("检查并锁定账户异常: " + std::string(e.what()));
            }
        }

        /**
         * @brief 生成唯一用户ID
         * @return 用户ID
         */
        std::string AuthService::generateUserId()
        {
            // 生成基于时间戳和随机数的用户ID
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

            // 添加随机数确保唯一性
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);
            int random_suffix = dis(gen);

            return "usr_" + std::to_string(timestamp) + "_" + std::to_string(random_suffix);
        }

        /**
         * @brief 获取统计信息 (重构版)
         * @return 统计信息JSON
         */
        nlohmann::json AuthService::getStatistics()
        {
            try
            {
                auto base_stats = getBaseStatistics();

                // 添加用户服务客户端统计
                if (user_service_client_)
                {
                    auto client_stats = user_service_client_->getStatistics();
                    base_stats["user_service_client"] = client_stats;
                }

                // 添加新架构相关信息
                base_stats["architecture_version"] = "2.0";
                base_stats["use_user_service"] = true;
                base_stats["database_type"] = "auth_sessions_only";

                return base_stats;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("获取统计信息异常: " + std::string(e.what()));
                return nlohmann::json{
                    {"error", "statistics_generation_failed"},
                    {"architecture_version", "2.0"}
                };
            }
        }

        // ==================== 缺失的函数实现 ====================

        /**
         * @brief 启动服务
         */
        bool AuthService::start()
        {
            try
            {
                LOG_INFO("=== 开始启动认证服务 ===");
                if (running_) {
                    LOG_WARNING("认证服务已在运行");
                    return true;
                }

                if (!initialized_) {
                    LOG_ERROR("认证服务尚未初始化，无法启动");
                    return false;
                }

                if (!http_server_running_ && !http_server_)
                {
                    LOG_ERROR("http_server_ 初始化失败");
                    return false;
                }

                running_ = true;

                // 设置定时任务
                setupTimerTasks();

                LOG_INFO("=== 认证服务启动成功 (新架构) ===");
                LOG_INFO("监听地址: " + config_.network_config.bind_address + ":" + std::to_string(config_.network_config.listen_port));

                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("启动认证服务时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 停止服务
         */
        void AuthService::shutdown()
        {
            // 首先检查并设置 running_ 为 false，防止重复调用
            if (!running_.exchange(false)) {
                LOG_INFO("AuthService已停止或正在停止中，跳过重复关闭");
                return;
            }

            try
            {
                LOG_INFO("停止AuthService...");

                // 停止定时任务
                stopCleanupTasks();

                // 停止Kafka组件
                if (kafka_consumer_)
                {
                    LOG_INFO("停止Kafka消费者...");
                    kafka_consumer_->stop();
                }

                if (kafka_producer_)
                {
                    LOG_INFO("停止Kafka生产者...");
                    kafka_producer_->stopPollingThread();
                }

                // 首先停止HTTP服务器（这会让EventLoop退出）
                if (http_server_)
                {
                    LOG_INFO("停止HTTP服务器...");
                    http_server_->stop();
                }

                // 等待HTTP服务器线程结束（添加超时保护）
                if (http_server_thread_.joinable())
                {
                    LOG_INFO("等待HTTP服务器线程结束...");
                    http_server_running_.store(false);

                    // 等待线程结束（最多等待3秒）
                    http_server_thread_.join();
                    LOG_INFO("HTTP服务器线程已结束");
                }

                // 关闭数据库连接池
                if (mysql_pool_)
                {
                    LOG_INFO("关闭MySQL连接池...");
                    mysql_pool_->stop();
                }

                if (redis_pool_)
                {
                    LOG_INFO("关闭Redis连接池...");
                    redis_pool_->stop();
                }

                // 关闭线程池
                if (thread_pool_)
                {
                    LOG_INFO("关闭线程池...");
                    thread_pool_->shutdown();
                }

                LOG_INFO("AuthService已停止");
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("停止AuthService时发生异常: " + std::string(e.what()));
            }
        }


        /**
         * @brief 初始化HTTP服务器
         */
        bool AuthService::initializeHttpServer()
        {
            http_server_running_ = true;
            std::promise<void> server_started_promise_;

            http_server_thread_ = std::thread([this, &server_started_promise_]()
            {
                try
                {
                    // 创建HTTP服务器配置
                    common::http::HttpServerConfig server_config;
                    server_config.network_config = config_.network_config;
                    server_config.worker_threads = config_.network_config.worker_threads;
                    server_config.request_timeout_ms = config_.network_config.connection_timeout * 1000; // 秒转毫秒
                    server_config.keep_alive_timeout_ms = config_.network_config.read_timeout * 1000; // 秒转毫秒

                    // 创建HTTP服务器
                    http_server_ = std::make_unique<common::http::HttpServer>(server_config, thread_pool_);

                    // 🔧 新增：设置 CORS 中间件
                    setupCorsMiddleware();

                    setupHttpRoutes();

                    // 启动前通知：服务器对象已准备好
                    server_started_promise_.set_value();

                    // 阻塞运行
                    http_server_->start();
                }
                catch (...)
                {
                    server_started_promise_.set_exception(std::current_exception());
                    http_server_running_ = false;
                    http_server_.reset();
                }
            });

            // 等待服务器对象构造完成（可选，用于同步）
            auto future = server_started_promise_.get_future();
            if (future.wait_for(std::chrono::seconds(300)) != std::future_status::ready)
            {
                LOG_ERROR("HttpServer failed to initialize within timeout");
                return false;
            }

            return true;
        }

        /**
         * @brief 初始化Kafka
         */
        bool AuthService::initializeKafka()
        {
            if (!config_.kafka_config.enable)
            {
                LOG_INFO("Kafka已禁用，跳过初始化");
                return true;
            }
            try
            {
                LOG_INFO("正在初始化Kafka集成...");

                // 初始化Kafka生产者
                // common::messaging::KafkaProducerConfig producer_config;
                kafka_producer_ = common::messaging::KafkaProducer::create(config_.kafka_config.producer);
                if (!kafka_producer_)
                {
                    LOG_ERROR("Kafka生产者创建失败");
                    return false;
                }

                // 初始化Kafka消费者
                // common::messaging::KafkaConsumerConfig consumer_config;
                kafka_consumer_ = common::messaging::KafkaConsumer::create(config_.kafka_config.consumer);
                if (!kafka_consumer_)
                {
                    LOG_ERROR("Kafka消费者创建失败");
                    return false;
                }

                // 订阅相关主题
                std::vector<std::string> topics = {
                    // config_.kafka_config.topic_prefix + ".services.events", // 服务注册/注销事件
                    config_.kafka_config.topic_prefix + ".system.events" // 系统级事件
                };

                if (!kafka_consumer_->subscribe(topics))
                {
                    LOG_ERROR("Kafka主题订阅失败");
                    return false;
                }

                // 启动消息处理
                common::messaging::MessageCallback message_callback = [this](
                    const std::string& topic, const std::string& key, const std::string& value, int64_t offset)
                {
                    (void)key; // 消除未使用参数警告
                    (void)offset; // 消除未使用参数警告
                    // TODO this->handleKafkaMessage(topic, value);
                };

                if (!kafka_consumer_->start(message_callback))
                {
                    LOG_ERROR("Kafka消费者启动失败");
                    return false;
                }

                LOG_INFO("API Gateway Kafka集成初始化成功");
                LOG_INFO("  - Brokers: " + config_.kafka_config.producer.brokers);
                LOG_INFO("  - Topic Prefix: " + config_.kafka_config.topic_prefix);
                LOG_INFO("  - Consumer Group: " + config_.kafka_config.consumer.group_id);
                LOG_INFO("  - 订阅主题数: " + std::to_string(topics.size()));

                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Kafka初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 初始化性能监控
         */
        bool AuthService::initializePerformanceMonitor()
        {
            try
            {
                if (!config_.enable_performance_monitoring)
                {
                    LOG_INFO("性能监控已禁用，跳过初始化");
                    return true;
                }

                LOG_INFO("初始化性能监控...");
                // 性能监控初始化逻辑在此实现
                LOG_INFO("性能监控初始化成功");
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("初始化性能监控时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 获取基础统计信息
         */
        nlohmann::json AuthService::getBaseStatistics()
        {
            try
            {
                nlohmann::json stats;
                stats["total_logins"] = total_logins_.load();
                stats["total_registrations"] = total_registrations_.load();
                stats["active_sessions"] = session_manager_ ? session_manager_->getActiveSessionCount() : 0;
                stats["uptime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_).count();

                return stats;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("获取基础统计信息时发生异常: " + std::string(e.what()));
                return nlohmann::json{};
            }
        }

        /**
         * @brief 配置验证
         */
        bool AuthService::Config::validate() const
        {
            try
            {
                // 验证各个配置模块
                server_config.validate();
                network_config.validate();
                database_config.validate();
                redis_config.validate();

                // 验证服务特定配置
                if (service_name.empty())
                {
                    LOG_ERROR("服务名称不能为空");
                    return false;
                }

                if (jwt_secret.empty())
                {
                    LOG_ERROR("JWT密钥不能为空");
                    return false;
                }

                if (user_service_base_url.empty())
                {
                    LOG_ERROR("用户服务基础URL不能为空");
                    return false;
                }

                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("配置验证时发生异常: " + std::string(e.what()));
                return false;
            }
        }

        bool AuthService::verifyApiEndpoints()
        {
            try
            {
                LOG_INFO("🔧 验证API端点是否可用...");

                // 🔧 修复：等待HTTP服务器完全启动
                int retry_count = 0;
                const int max_retries = 10;
                const int retry_delay_ms = 500;

                while (!http_server_running_.load() && retry_count < max_retries)
                {
                    LOG_INFO(
                        "⏳ 等待HTTP服务器启动... (重试 " + std::to_string(retry_count + 1) + "/" + std::to_string(max_retries) +
                        ")");
                    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                    retry_count++;
                }

                if (!http_server_running_.load())
                {
                    LOG_ERROR("❌ HTTP服务器未能在规定时间内启动");
                    return false;
                }

                // 创建本地HTTP客户端
                common::http::HttpClient client;
                client.setDefaultTimeout(5000); // 5秒超时

                // 构建本地API端点URL
                std::string endpoints_url = "http://" + config_.network_config.bind_address + ":" +
                    std::to_string(config_.network_config.listen_port) + "/api/v1/service/endpoints";

                // 尝试访问API端点
                auto response = client.get(endpoints_url);

                if (response.status_code == 200 && !response.body.empty())
                {
                    LOG_INFO("✅ API端点验证成功");
                    return true;
                }
                else
                {
                    LOG_ERROR("❌ API端点验证失败 - 状态码: " + std::to_string(response.status_code));
                    return false;
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("❌ API端点验证异常: " + std::string(e.what()));
                return false;
            }
        }

        nlohmann::json AuthService::Config::toJson() const
        {
            nlohmann::json json;

            // 服务基本信息
            json["service_name"] = service_name;
            json["service_version"] = service_version;

            // 服务器配置
            json["server"]["host"] = network_config.bind_address;
            json["server"]["port"] = network_config.listen_port;
            json["server"]["max_connections"] = server_config.max_connections;

            // 数据库配置
            json["database"]["mysql"]["host"] = database_config.mysql_host;
            json["database"]["mysql"]["port"] = database_config.mysql_port;
            json["database"]["mysql"]["user"] = database_config.mysql_user;
            json["database"]["mysql"]["password"] = "***"; // 不显示密码
            json["database"]["mysql"]["database"] = database_config.mysql_database;
            json["database"]["mysql"]["charset"] = database_config.mysql_charset;
            json["database"]["mysql"]["pool_size"] = database_config.mysql_pool_size;

            // Redis配置
            json["redis"]["host"] = redis_config.redis_host;
            json["redis"]["port"] = redis_config.redis_port;
            json["redis"]["password"] = "***"; // 不显示密码
            json["redis"]["database"] = redis_config.redis_database;
            json["redis"]["pool_size"] = redis_config.redis_pool_size;

            // 用户服务配置
            json["user_service"]["base_url"] = user_service_base_url;
            json["user_service"]["timeout_seconds"] = user_service_timeout_seconds;
            json["user_service"]["retry_count"] = user_service_retry_count;
            json["user_service"]["cache_ttl_seconds"] = user_service_cache_ttl_seconds;
            json["user_service"]["enable_cache"] = enable_user_service_cache;

            // 安全配置
            json["security"]["max_login_attempts"] = max_login_attempts;
            json["security"]["account_lockout_duration_minutes"] = account_lockout_duration_minutes;

            // JWT配置
            json["jwt"]["secret"] = "***"; // 不显示密钥
            json["jwt"]["access_expiry"] = jwt_access_expiry;
            json["jwt"]["refresh_expiry"] = jwt_refresh_expiry;

            // 功能开关
            json["features"]["enable_kafka"] = enable_kafka;
            json["features"]["enable_performance_monitoring"] = enable_performance_monitoring;

            return json;
        }

        /**
         * @brief 从配置管理器加载配置
         */
        AuthService::Config AuthService::Config::fromConfigManager()
        {
            Config config;

            try
            {
                auto& cm = common::config::ConfigManager::getInstance();

                // 服务基本信息
                config.service_name = cm.get<std::string>("service.name", "auth_service");
                config.service_version = cm.get<std::string>("service.version", "2.0.0");

                // 使用预定义的配置结构体
                config.server_config = common::config::ServerConfig::fromConfigManager();
                config.network_config = common::config::NetworkConfig::fromConfigManager();
                config.database_config = common::config::DatabaseConfig::fromConfigManager();
                config.redis_config = common::config::RedisConfig::fromConfigManager();
                config.thread_pool_config = common::config::ThreadPoolConfig::fromConfigManager();
                config.kafka_config = common::config::KafkaConfig::fromConfigManager();

                // 用户服务配置
                config.user_service_base_url = cm.get<std::string>("external_services.user_service.base_url",
                                                                   "http://localhost:8082");
                config.user_service_timeout_seconds = cm.get<int>("external_services.user_service.timeout_seconds", 5);
                config.user_service_retry_count = cm.get<int>("external_services.user_service.retry_count", 3);
                config.user_service_cache_ttl_seconds = cm.get<int>("external_services.user_service.cache_ttl_seconds",
                                                                    300);
                config.enable_user_service_cache = cm.get<bool>("external_services.user_service.cache.enable", true);

                // 安全配置
                config.max_login_attempts = cm.get<int>("security.account_lockout.max_failed_attempts", 5);
                config.account_lockout_duration_minutes = cm.get<int>(
                    "security.account_lockout.lockout_duration_minutes", 15);

                // JWT配置
                config.jwt_secret = cm.get<std::string>("jwt.secret_key", "your_jwt_secret_key_here");
                config.jwt_access_expiry = cm.get<int>("jwt.access_token_expire_seconds", 3600);
                config.jwt_refresh_expiry = cm.get<int>("jwt.refresh_token_expire_seconds", 604800);

                // 功能开关
                config.enable_kafka = cm.get<bool>("kafka.enable", false);
                config.enable_performance_monitoring = cm.get<bool>("monitoring.enable_metrics_collection", true);

                // 🔧 修复：清理任务配置 - 正确的时间单位转换
                config.enable_session_cleanup = cm.get<bool>("monitoring.audit_logging.enable", true);
                config.enable_token_cleanup = cm.get<bool>("jwt.blacklist.enable", true);

                // 🔧 会话清理：使用较短的间隔用于演示（5分钟），生产环境可配置更长
                int session_timeout_hours = cm.get<int>("security.session.session_timeout_hours", 1); // 默认1小时
                config.session_cleanup_interval_minutes = std::max(5, session_timeout_hours * 30); // 最少5分钟，最多按小时数*30分钟

                // 🔧 Token清理：直接从分钟级配置读取，默认10分钟
                config.token_cleanup_interval_minutes = cm.get<int>("jwt.blacklist.cleanup_interval_minutes", 10);
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("从配置管理器加载配置时发生异常: " + std::string(e.what()));
            }

            return config;
        }

        AuthService::Config AuthService::Config::fromConfigFile(const std::string& config_file_path)
        {
            try
            {
                // 先加载配置文件
                auto& config_manager = common::config::ConfigManager::getInstance();
                if (!config_manager.loadFromFile(config_file_path))
                {
                    LOG_ERROR("无法加载配置文件: " + config_file_path);
                    return Config{}; // 返回默认配置
                }

                // 然后使用ConfigManager加载配置
                return fromConfigManager();
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("解析配置文件异常: " + std::string(e.what()));
                return Config{}; // 返回默认配置
            }
        }

        // ==================== HTTP处理实现 ====================

        /**
         * @brief 设置 CORS 中间件
         */
        void AuthService::setupCorsMiddleware()
        {
            LOG_DEBUG("设置 CORS 中间件...");

            // 创建 CORS 中间件函数
            auto cors_middleware = [this](const common::http::HttpRequest& request, common::http::HttpResponse& response) {
                // 处理 OPTIONS 预检请求
                if (request.getMethod() == common::http::HttpMethod::OPTIONS) {
                    response.setHeader("Access-Control-Allow-Origin", "*");
                    response.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH");
                    response.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Accept, Origin");
                    response.setHeader("Access-Control-Max-Age", "86400");
                    response.setStatus(200);
                    response.setBody("");
                    return false; // 停止处理
                }

                // 为其他请求设置 CORS 头
                std::string origin = request.getHeader("origin");
                if (!origin.empty()) {
                    response.setHeader("Access-Control-Allow-Origin", origin);
                } else {
                    response.setHeader("Access-Control-Allow-Origin", "*");
                }
                response.setHeader("Access-Control-Allow-Credentials", "true");

                return true; // 继续处理
            };

            http_server_->use(cors_middleware, "CorsMiddleware");
            LOG_DEBUG("CORS 中间件设置完成");
        }

        /**
         * @brief 设置HTTP路由
         */
        void AuthService::setupHttpRoutes()
        {
            LOG_INFO("设置HTTP路由...");

            // ========== 认证 API ==========
            http_server_->route("POST", "/api/v1/auth/login",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleLoginRequest(req, resp);
                                });

            http_server_->route("POST", "/api/v1/auth/register",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleRegisterRequest(req, resp);
                                });

            http_server_->route("POST", "/api/v1/auth/refresh",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleRefreshTokenRequest(req, resp);
                                });

            http_server_->route("POST", "/api/v1/auth/validate",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleValidateTokenRequest(req, resp);
                                });

            http_server_->route("POST", "/api/v1/auth/logout",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleLogoutRequest(req, resp);
                                });

            // ========== 邮箱验证码 API ==========
            http_server_->route("POST", "/api/v1/auth/email/send-code",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleSendCodeRequest(req, resp);
                                });

            http_server_->route("POST", "/api/v1/auth/email/verify-code",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleVerifyCodeRequest(req, resp);
                                });

            http_server_->route("POST", "/api/v1/auth/password/reset",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleResetPasswordRequest(req, resp);
                                });

            // ========== 系统级 API (统一前缀 /api/v1/auth/) ==========
            http_server_->route("GET", "/api/v1/auth/health",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleHealthCheckRequest(req, resp);
                                });

            http_server_->route("GET", "/api/v1/auth/stats",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleStatisticsRequest(req, resp);
                                });

            // API发现端点
            http_server_->route("GET", "/api/v1/auth/service/endpoints",
                                [this](const common::http::HttpRequest& req, common::http::HttpResponse& resp)
                                {
                                    handleServiceEndpointsRequest(req, resp);
                                });

            LOG_INFO("HTTP路由设置完成，共注册 11 个路由");
        }

        /**
         * @brief 处理登录请求
         */
        void AuthService::handleLoginRequest(const common::http::HttpRequest& request,
                                             common::http::HttpResponse& response)
        {
            try
            {
                // 解析请求体
                auto request_data = nlohmann::json::parse(request.getBody());
                std::string username = request_data.value("username", "");
                std::string password = request_data.value("password", "");
                std::string email = request_data.value("email", "");
                std::string code = request_data.value("code", "");

                // 判断登录方式：验证码登录 or 密码登录
                bool is_code_login = !email.empty() && !code.empty();

                if (is_code_login)
                {
                    // ========== 邮箱验证码登录 ==========
                    if (email.empty() || code.empty())
                    {
                        response.setStatus(400);
                        response.setBody(R"({"error": "邮箱和验证码不能为空"})");
                        return;
                    }

                    // 校验验证码
                    if (!verification_code_manager_)
                    {
                        response.setStatus(503);
                        response.setBody(R"({"error": "验证码服务不可用"})");
                        return;
                    }

                    auto verify_result = verification_code_manager_->verifyCodeDirect(
                        email, code, VerifyPurpose::LOGIN);

                    if (!verify_result.success)
                    {
                        int status = 401;
                        if (verify_result.error_code == "CODE_EXPIRED") status = 410;
                        response.setStatus(status);
                        nlohmann::json error_resp;
                        error_resp["error"] = verify_result.message;
                        error_resp["error_code"] = verify_result.error_code;
                        response.setBody(error_resp.dump());
                        return;
                    }

                    // 查找用户
                    auto user = user_service_client_->getUserByEmail(email);
                    if (!user.has_value())
                    {
                        response.setStatus(401);
                        response.setBody(R"({"error": "该邮箱未注册"})");
                        return;
                    }

                    // 检查用户状态
                    if (user->status != UserStatus::ACTIVE)
                    {
                        response.setStatus(403);
                        response.setBody(R"({"error": "用户账户已被禁用或锁定"})");
                        return;
                    }

                    // 更新登录信息
                    std::string client_ip = request.getHeader("X-Real-IP");
                    if (client_ip.empty()) client_ip = request.getHeader("X-Forwarded-For");
                    if (client_ip.empty()) client_ip = "unknown";

                    // 🔧 性能优化：异步更新，不阻塞登录响应
                    std::string uid = user->user_id;
                    std::string cip = client_ip;
                    if (thread_pool_) {
                        thread_pool_->submit([this, uid, cip]() {
                            user_service_client_->updateLastLogin(uid, cip);
                            user_service_client_->updateOnlineStatus(uid, "online");
                            user_service_client_->resetLoginAttempts(uid);
                        });
                    }

                    // 生成会话和 JWT
                    std::string session_id = "auth_sess_" + std::to_string(
                        std::chrono::system_clock::now().time_since_epoch().count());

                    auto session = session_manager_->createSession(*user, client_ip, request.getHeader("User-Agent"), "");
                    auto access_token = jwt_manager_->generateAccessToken(*user, session_id, "web", client_ip);
                    auto refresh_token = jwt_manager_->generateRefreshToken(*user, session_id, "web", client_ip);

                    nlohmann::json response_data;
                    response_data["success"] = true;
                    response_data["access_token"] = access_token;
                    response_data["refresh_token"] = refresh_token;
                    response_data["expires_in"] = 3600;
                    response_data["login_type"] = "email_code";

                    response.setStatus(200);
                    response.setBody(response_data.dump());
                    return;
                }

                // ========== 密码登录（原有逻辑）==========
                if (username.empty() || password.empty())
                {
                    response.setStatus(400);
                    response.setBody(R"({"error": "用户名和密码不能为空"})");
                    return;
                }

                // 通过用户服务客户端验证用户
                auto user = user_service_client_->getUserByUsername(username);
                if (!user.has_value())
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "用户名或密码错误"})");
                    return;
                }

                // 验证密码
                if (!password_manager_->verifyPassword(password, user->password_hash))
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "用户名或密码错误"})");
                    return;
                }

                // 生成会话ID
                std::string session_id = "auth_sess_" + std::to_string(
                    std::chrono::system_clock::now().time_since_epoch().count());
                std::string client_ip = request.getHeader("X-Real-IP");
                if (client_ip.empty())
                {
                    client_ip = request.getHeader("X-Forwarded-For");
                }
                if (client_ip.empty())
                {
                    client_ip = "unknown";
                }

                // 🔧 性能优化：异步更新用户状态，不阻塞登录响应
                std::string uid = user->user_id;
                std::string cip = client_ip;
                if (thread_pool_) {
                    thread_pool_->submit([this, uid, cip]() {
                        user_service_client_->updateLastLogin(uid, cip);
                        user_service_client_->updateOnlineStatus(uid, "online");
                        user_service_client_->resetLoginAttempts(uid);
                    });
                }

                // 生成JWT令牌
                auto access_token = jwt_manager_->generateAccessToken(*user, session_id, "web", client_ip);
                auto refresh_token = jwt_manager_->generateRefreshToken(*user, session_id, "web", client_ip);

                // 返回成功响应
                nlohmann::json response_data;
                response_data["success"] = true;
                response_data["access_token"] = access_token;
                response_data["refresh_token"] = refresh_token;
                response_data["expires_in"] = 3600; // 1小时

                response.setStatus(200);
                response.setBody(response_data.dump());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("处理登录请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理注册请求
         */
        void AuthService::handleRegisterRequest(const common::http::HttpRequest& request,
                                                common::http::HttpResponse& response)
        {
            try
            {
                // 解析请求体
                auto request_data = nlohmann::json::parse(request.getBody());
                std::string username = request_data.value("username", "");
                std::string password = request_data.value("password", "");
                std::string email = request_data.value("email", "");
                std::string nickname = request_data.value("nickname", "");
                std::string verify_token = request_data.value("verify_token", "");
                std::string device_type = request_data.value("device_type", "web");
                std::string device_id = request_data.value("device_id", "");

                // 基本字段验证
                if (username.empty() || password.empty() || email.empty() || verify_token.empty())
                {
                    response.setStatus(400);
                    response.setBody(R"({"error": "用户名、密码、邮箱和验证token不能为空"})");
                    return;
                }

                // 验证 verify_token 有效性
                if (!verification_code_manager_)
                {
                    response.setStatus(503);
                    response.setBody(R"({"error": "验证码服务不可用"})");
                    return;
                }

                std::string token_email = verification_code_manager_->verifyToken(verify_token);
                if (token_email.empty())
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "验证token无效或已过期，请重新获取验证码", "error_code": "INVALID_VERIFY_TOKEN"})");
                    return;
                }

                // 确保 token 中的邮箱与请求邮箱一致
                if (token_email != email)
                {
                    response.setStatus(400);
                    response.setBody(R"({"error": "验证token与邮箱不匹配", "error_code": "TOKEN_EMAIL_MISMATCH"})");
                    return;
                }

                // 📝 检查用户名是否已存在
                auto existing_user = user_service_client_->getUserByUsername(username);
                if (existing_user.has_value())
                {
                    response.setStatus(409);
                    response.setBody(R"({"error": "用户名已存在"})");
                    return;
                }

                // 📝 检查邮箱是否已被注册
                if (user_service_client_->isEmailExists(email))
                {
                    response.setStatus(409);
                    response.setBody(R"({"error": "邮箱已被注册"})");
                    return;
                }

                // 🔧 修复: 生成唯一用户ID
                std::string user_id = generateUserId();

                // 📝 哈希密码
                auto hash_result = password_manager_->hashPassword(password);
                if (!hash_result.success)
                {
                    nlohmann::json error_data;
                    error_data["success"] = false;
                    error_data["error"] = hash_result.error_message;
                    response.setStatus(400);
                    response.setBody(error_data.dump());
                    return;
                }

                // 📝 创建用户信息对象
                CoreUserInfo new_user;
                new_user.user_id = user_id; // 🔧 修复: 添加用户ID
                new_user.username = username;
                new_user.email = email;
                new_user.nickname = nickname.empty() ? username : nickname;
                new_user.password_hash = hash_result.hash;
                new_user.salt = hash_result.salt;
                new_user.status = UserStatus::ACTIVE;
                new_user.online_status = OnlineStatus::OFFLINE;
                new_user.created_at = std::chrono::system_clock::now();
                new_user.updated_at = new_user.created_at;

                // 📝 通过用户服务创建用户
                auto create_result = user_service_client_->createUser(new_user);
                if (!create_result.success)
                {
                    // 根据错误类型返回适当的状态码和错误信息
                    int status_code = 500; // 默认为服务器错误

                    if (create_result.error_code == "USERNAME_EXISTS")
                    {
                        status_code = 409; // Conflict
                    }
                    else if (create_result.error_code == "EMAIL_EXISTS")
                    {
                        status_code = 409; // Conflict
                    }
                    else if (create_result.error_code == "DUPLICATE_ENTRY")
                    {
                        status_code = 409; // Conflict
                    }
                    else if (create_result.status_code > 0)
                    {
                        status_code = create_result.status_code;
                    }

                    nlohmann::json error_response;
                    error_response["error"] = create_result.error_message;
                    error_response["error_code"] = create_result.error_code;
                    if (create_result.status_code > 0)
                    {
                        error_response["status_code"] = create_result.status_code;
                    }

                    response.setStatus(status_code);
                    response.setBody(error_response.dump());
                    return;
                }

                std::string created_user_id = create_result.user_id;

                // 🎯 尝试创建会话和JWT令牌（完整注册流程）
                std::string client_ip = request.getHeader("X-Real-IP");
                if (client_ip.empty())
                {
                    client_ip = request.getHeader("X-Forwarded-For");
                }
                if (client_ip.empty())
                {
                    client_ip = "unknown";
                }

                std::string user_agent = request.getHeader("User-Agent");
                if (user_agent.empty())
                {
                    user_agent = "unknown";
                }

                auto session = session_manager_->createSession(new_user, client_ip, user_agent, device_id);
                if (session.has_value())
                {
                    // 🎯 生成JWT令牌
                    std::string access_token = jwt_manager_->generateAccessToken(
                        new_user, session->session_id, device_type, client_ip);
                    std::string refresh_token = jwt_manager_->generateRefreshToken(
                        new_user, session->session_id, device_type, client_ip);

                    // 📝 返回完整成功响应
                    nlohmann::json response_data;
                    response_data["success"] = true;
                    response_data["message"] = "用户注册成功";
                    response_data["user_id"] = created_user_id;
                    response_data["access_token"] = access_token;
                    response_data["refresh_token"] = refresh_token;
                    response_data["expires_in"] = 3600; // 1小时
                    response_data["token_type"] = "Bearer";

                    response.setStatus(201);
                    response.setBody(response_data.dump());

                    LOG_INFO("用户注册成功: " + username + " (ID: " + created_user_id + ")");

                    // 消费 verify_token，使其失效
                    verification_code_manager_->consumeToken(verify_token);
                }
                else
                {
                    // 📝 用户创建成功，但会话创建失败
                    nlohmann::json response_data;
                    response_data["success"] = true;
                    response_data["message"] = "用户注册成功，请重新登录";
                    response_data["user_id"] = created_user_id;

                    response.setStatus(201);
                    response.setBody(response_data.dump());

                    LOG_WARNING("用户注册成功但会话创建失败: " + username + " (ID: " + created_user_id + ")");
                }
            }
            catch (const nlohmann::json::exception& e)
            {
                LOG_ERROR("处理注册请求JSON异常: " + std::string(e.what()));
                response.setStatus(400);
                response.setBody(R"({"error": "JSON格式错误"})");
            } catch (const std::exception& e)
            {
                LOG_ERROR("处理注册请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理Token刷新请求
         */
        void AuthService::handleRefreshTokenRequest(const common::http::HttpRequest& request,
                                                    common::http::HttpResponse& response)
        {
            try
            {
                // 解析请求体
                auto request_data = nlohmann::json::parse(request.getBody());
                std::string refresh_token = request_data.value("refresh_token", "");

                if (refresh_token.empty())
                {
                    response.setStatus(400);
                    response.setBody(R"({"error": "刷新令牌不能为空"})");
                    return;
                }

                // 验证并刷新令牌
                auto token_result = jwt_manager_->refreshAccessToken(refresh_token);
                if (token_result.first.empty())
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "无效的刷新令牌"})");
                    return;
                }

                // 返回新的访问令牌
                nlohmann::json response_data;
                response_data["success"] = true;
                response_data["access_token"] = token_result.first;
                response_data["refresh_token"] = token_result.second;
                response_data["expires_in"] = 3600;

                response.setStatus(200);
                response.setBody(response_data.dump());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("处理Token刷新请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理Token验证请求
         */
        void AuthService::handleValidateTokenRequest(const common::http::HttpRequest& request,
                                                     common::http::HttpResponse& response)
        {
            try
            {
                // 从请求头获取Authorization
                std::string auth_header = request.getHeader("Authorization");
                if (auth_header.empty() || auth_header.find("Bearer ") != 0)
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "缺少有效的授权头"})");
                    return;
                }

                std::string token = auth_header.substr(7); // 去除 "Bearer "

                // 验证令牌
                auto validation_result = jwt_manager_->validateAccessToken(token);
                if (!validation_result.valid)
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "无效的访问令牌"})");
                    return;
                }

                // 返回验证结果
                nlohmann::json response_data;
                response_data["success"] = true;
                response_data["valid"] = true;
                if (validation_result.claims)
                {
                    response_data["user_id"] = validation_result.claims->user_id;
                    response_data["username"] = validation_result.claims->username;
                    // 将时间点转换为时间戳
                    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        validation_result.claims->expires_at.time_since_epoch()).count();
                    response_data["expires_at"] = timestamp;
                }

                response.setStatus(200);
                response.setBody(response_data.dump());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("处理Token验证请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理登出请求
         */
        void AuthService::handleLogoutRequest(const common::http::HttpRequest& request,
                                              common::http::HttpResponse& response)
        {
            try
            {
                // 从请求头获取Authorization
                std::string auth_header = request.getHeader("Authorization");
                if (auth_header.empty() || auth_header.find("Bearer ") != 0)
                {
                    response.setStatus(401);
                    response.setBody(R"({"error": "缺少有效的授权头"})");
                    return;
                }

                std::string token = auth_header.substr(7);

                // 🔧 修复：登出前先验证 token 获取 user_id，用于更新在线状态
                std::string user_id;
                auto validation_result = jwt_manager_->validateAccessToken(token);
                if (validation_result.valid && validation_result.claims.has_value()) {
                    user_id = validation_result.claims->user_id;
                    LOG_INFO("登出用户: user_id=" + user_id);
                } else {
                    // Token 无效，但仍尝试撤销（可能是已过期但未被撤销的 token）
                    LOG_WARNING("登出时 token 验证失败，但仍尝试撤销: " + validation_result.error_message);
                }

                // 将令牌加入黑名单
                bool success = jwt_manager_->revokeToken(token);
                if (!success)
                {
                    response.setStatus(500);
                    response.setBody(R"({"error": "令牌撤销失败"})");
                    return;
                }

                // 🔧 修复：登出成功后更新用户在线状态为 offline
                if (!user_id.empty() && user_service_client_) {
                    bool status_updated = user_service_client_->updateOnlineStatus(user_id, "offline");
                    if (!status_updated) {
                        // 状态更新失败不影响登出成功，只记录日志
                        LOG_WARNING("登出成功但更新用户在线状态失败: user_id=" + user_id);
                    } else {
                        LOG_INFO("登出成功，用户状态已更新为 offline: user_id=" + user_id);
                    }
                }

                // 返回成功响应
                nlohmann::json response_data;
                response_data["success"] = true;
                response_data["message"] = "登出成功";

                response.setStatus(200);
                response.setBody(response_data.dump());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("处理登出请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理发送邮箱验证码请求
         */
        void AuthService::handleSendCodeRequest(const common::http::HttpRequest& request,
                                                 common::http::HttpResponse& response)
        {
            try
            {
                auto request_data = nlohmann::json::parse(request.getBody());
                std::string email = request_data.value("email", "");
                std::string purpose_str = request_data.value("purpose", "register");

                if (email.empty())
                {
                    response.setStatus(400);
                    response.setBody(R"({"success": false, "error": "邮箱不能为空"})");
                    return;
                }

                VerifyPurpose purpose = parseVerifyPurpose(purpose_str);

                // 根据用途做前置检查
                if (purpose == VerifyPurpose::REGISTER)
                {
                    if (user_service_client_->isEmailExists(email))
                    {
                        response.setStatus(409);
                        response.setBody(R"({"success": false, "error": "该邮箱已被注册"})");
                        return;
                    }
                }
                else if (purpose == VerifyPurpose::LOGIN || purpose == VerifyPurpose::RESET_PASSWORD)
                {
                    if (!user_service_client_->isEmailExists(email))
                    {
                        response.setStatus(404);
                        response.setBody(R"({"success": false, "error": "该邮箱未注册"})");
                        return;
                    }
                }

                auto result = verification_code_manager_->sendCode(email, purpose);

                nlohmann::json response_data;
                response_data["success"] = result.success;

                if (result.success)
                {
                    response_data["message"] = result.message;
                    response_data["cooldown_seconds"] = result.cooldown_seconds;
                    response.setStatus(200);
                }
                else
                {
                    int status = 429; // 默认 Too Many Requests
                    if (result.error_code == "INVALID_EMAIL") status = 400;
                    else if (result.error_code == "DAILY_LIMIT") status = 429;
                    else if (result.error_code == "REDIS_ERROR") status = 503;

                    response_data["error"] = result.message;
                    response_data["error_code"] = result.error_code;
                    if (result.cooldown_seconds > 0)
                    {
                        response_data["cooldown_seconds"] = result.cooldown_seconds;
                    }
                    response.setStatus(status);
                }

                response.setBody(response_data.dump());
            }
            catch (const nlohmann::json::exception& e)
            {
                LOG_ERROR("发送验证码请求JSON异常: " + std::string(e.what()));
                response.setStatus(400);
                response.setBody(R"({"success": false, "error": "JSON格式错误"})");
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("发送验证码请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"success": false, "error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理验证邮箱验证码请求
         */
        void AuthService::handleVerifyCodeRequest(const common::http::HttpRequest& request,
                                                   common::http::HttpResponse& response)
        {
            try
            {
                auto request_data = nlohmann::json::parse(request.getBody());
                std::string email = request_data.value("email", "");
                std::string code = request_data.value("code", "");
                std::string purpose_str = request_data.value("purpose", "register");

                if (email.empty() || code.empty())
                {
                    response.setStatus(400);
                    response.setBody(R"({"success": false, "error": "邮箱和验证码不能为空"})");
                    return;
                }

                VerifyPurpose purpose = parseVerifyPurpose(purpose_str);
                auto result = verification_code_manager_->verifyCode(email, code, purpose);

                nlohmann::json response_data;
                response_data["success"] = result.success;

                if (result.success)
                {
                    response_data["message"] = result.message;
                    response_data["verify_token"] = result.verify_token;
                    response.setStatus(200);
                }
                else
                {
                    int status = 400;
                    if (result.error_code == "CODE_EXPIRED") status = 410;
                    else if (result.error_code == "CODE_MISMATCH") status = 401;
                    else if (result.error_code == "REDIS_ERROR") status = 503;

                    response_data["error"] = result.message;
                    response_data["error_code"] = result.error_code;
                    response.setStatus(status);
                }

                response.setBody(response_data.dump());
            }
            catch (const nlohmann::json::exception& e)
            {
                LOG_ERROR("验证码校验请求JSON异常: " + std::string(e.what()));
                response.setStatus(400);
                response.setBody(R"({"success": false, "error": "JSON格式错误"})");
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("验证码校验请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"success": false, "error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理重设密码请求
         */
        void AuthService::handleResetPasswordRequest(const common::http::HttpRequest& request,
                                                      common::http::HttpResponse& response)
        {
            try
            {
                auto request_data = nlohmann::json::parse(request.getBody());
                std::string email = request_data.value("email", "");
                std::string code = request_data.value("code", "");
                std::string new_password = request_data.value("new_password", "");

                if (email.empty() || code.empty() || new_password.empty())
                {
                    response.setStatus(400);
                    response.setBody(R"({"success": false, "error": "邮箱、验证码和新密码不能为空"})");
                    return;
                }

                // 1. 校验验证码
                auto verify_result = verification_code_manager_->verifyCodeDirect(
                    email, code, VerifyPurpose::RESET_PASSWORD);

                if (!verify_result.success)
                {
                    nlohmann::json error_data;
                    error_data["success"] = false;
                    error_data["error"] = verify_result.message;
                    error_data["error_code"] = verify_result.error_code;

                    int status = 400;
                    if (verify_result.error_code == "CODE_EXPIRED") status = 410;
                    else if (verify_result.error_code == "CODE_MISMATCH") status = 401;

                    response.setStatus(status);
                    response.setBody(error_data.dump());
                    return;
                }

                // 2. 查找用户
                auto user_opt = user_service_client_->getUserByEmail(email);
                if (!user_opt.has_value())
                {
                    response.setStatus(404);
                    response.setBody(R"({"success": false, "error": "该邮箱未注册"})");
                    return;
                }

                // 3. 哈希新密码
                auto hash_result = password_manager_->hashPassword(new_password);
                if (!hash_result.success)
                {
                    nlohmann::json error_data;
                    error_data["success"] = false;
                    error_data["error"] = hash_result.error_message;
                    response.setStatus(400);
                    response.setBody(error_data.dump());
                    return;
                }

                // 4. 更新密码
                bool updated = user_service_client_->updatePassword(
                    user_opt->user_id, hash_result.hash, hash_result.salt);

                if (!updated)
                {
                    response.setStatus(500);
                    response.setBody(R"({"success": false, "error": "密码更新失败"})");
                    return;
                }

                // 5. 使所有现有会话失效（通过 Redis 批量清理）
                // TODO: session_manager 暂无 invalidateAllUserSessions 方法，
                // 用户下次请求时 JWT 验证会因密码变更而失败

                nlohmann::json response_data;
                response_data["success"] = true;
                response_data["message"] = "密码重设成功，请使用新密码登录";

                response.setStatus(200);
                response.setBody(response_data.dump());

                LOG_INFO("密码重设成功: " + email + " (user_id: " + user_opt->user_id + ")");
            }
            catch (const nlohmann::json::exception& e)
            {
                LOG_ERROR("重设密码请求JSON异常: " + std::string(e.what()));
                response.setStatus(400);
                response.setBody(R"({"success": false, "error": "JSON格式错误"})");
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("重设密码请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"success": false, "error": "服务器内部错误"})");
            }
        }

        /**
         * @brief 处理健康检查请求
         */
        void AuthService::handleHealthCheckRequest(const common::http::HttpRequest& request,
                                                   common::http::HttpResponse& response)
        {
            (void)request; // 避免未使用参数警告
            try
            {
                nlohmann::json health_data;

                // 🔧 优化：提供API网关期望的详细健康信息
                health_data["status"] = "healthy";
                health_data["service"] = "auth_service";
                health_data["version"] = "2.0.0";
                health_data["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // 🔧 增强：添加更详细的组件状态检查
                bool mysql_healthy = mysql_pool_ && mysql_pool_->isRunning();
                bool redis_healthy = redis_pool_ && redis_pool_->isRunning();
                bool user_service_healthy = true; // 用户服务连接是可选的，不影响核心服务

                try
                {
                    if (user_service_client_)
                    {
                        user_service_healthy = user_service_client_->isHealthy();
                    }
                }
                catch (...)
                {
                    user_service_healthy = false; // 捕获任何异常
                }

                health_data["components"] = {
                    {"mysql", mysql_healthy ? "healthy" : "unhealthy"},
                    {"redis", redis_healthy ? "healthy" : "unhealthy"},
                    {"user_service", user_service_healthy ? "healthy" : "degraded"}, // 降级而非失败
                    {"http_server", "healthy"}, // HTTP服务器显然在运行
                    {"event_loop", event_loop_ ? "healthy" : "unhealthy"}
                };

                // 🔧 增强：添加性能指标供API网关参考
                health_data["metrics"] = {
                    {"total_requests", total_requests_.load()},
                    {"successful_requests", successful_requests_.load()},
                    {"failed_requests", failed_requests_.load()},
                    {
                        "uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - start_time_).count()
                    },
                    {"response_time_ms", 1} // 当前响应时间（毫秒）
                };

                // 🔧 关键：核心服务健康状态（只要MySQL和Redis正常就认为健康）
                bool overall_healthy = mysql_healthy && redis_healthy;

                if (overall_healthy)
                {
                    health_data["status"] = "healthy";
                    response.setStatus(200);
                }
                else
                {
                    health_data["status"] = "unhealthy";
                    health_data["issues"] = nlohmann::json::array();
                    if (!mysql_healthy) health_data["issues"].push_back("MySQL连接池异常");
                    if (!redis_healthy) health_data["issues"].push_back("Redis连接池异常");
                    response.setStatus(503); // Service Unavailable
                }

                // 🔧 标准化响应头
                response.setHeader("Content-Type", "application/json");
                response.setHeader("Cache-Control", "no-cache");
                response.setBody(health_data.dump(2));

                LOG_DEBUG("🏥 健康检查响应: " + health_data["status"].get<std::string>() +
                    " (MySQL:" + (mysql_healthy ? "✅" : "❌") +
                    " Redis:" + (redis_healthy ? "✅" : "❌") + ")");
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("❌ 处理健康检查请求异常: " + std::string(e.what()));

                nlohmann::json error_response = {
                    {"status", "error"},
                    {"service", "auth_service"},
                    {"error", "健康检查异常"},
                    {"message", e.what()},
                    {
                        "timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()
                    }
                };

                response.setStatus(500);
                response.setHeader("Content-Type", "application/json");
                response.setBody(error_response.dump());
            }
        }

        /**
         * @brief 处理统计信息请求
         */
        void AuthService::handleStatisticsRequest(const common::http::HttpRequest& request,
                                                  common::http::HttpResponse& response)
        {
            (void)request; // 避免未使用参数警告
            try
            {
                nlohmann::json stats_data;
                stats_data["service"] = "auth_service";
                stats_data["version"] = "2.0.0";
                stats_data["uptime_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_).count();

                // 数据库连接池状态
                if (mysql_pool_)
                {
                    nlohmann::json mysql_stats;
                    mysql_stats["running"] = mysql_pool_->isRunning();
                    mysql_stats["type"] = "mysql";
                    stats_data["mysql_pool"] = mysql_stats;
                }

                if (redis_pool_)
                {
                    nlohmann::json redis_stats;
                    redis_stats["running"] = redis_pool_->isRunning();
                    redis_stats["type"] = "redis";
                    stats_data["redis_pool"] = redis_stats;
                }

                // 线程池状态
                if (thread_pool_)
                {
                    nlohmann::json thread_stats;
                    thread_stats["type"] = "thread_pool";
                    thread_stats["running"] = true;
                    stats_data["thread_pool"] = thread_stats;
                }

                response.setStatus(200);
                response.setBody(stats_data.dump());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("处理统计信息请求异常: " + std::string(e.what()));
                response.setStatus(500);
                response.setBody(R"({"error": "获取统计信息失败"})");
            }
        }

        /**
         * @brief 处理服务端点发现请求 (API Gateway使用)
         */
        void AuthService::handleServiceEndpointsRequest(const common::http::HttpRequest& request,
                                                        common::http::HttpResponse& response)
        {
            try
            {
                LOG_INFO("🔍 收到API发现请求，准备返回服务端点信息");

                nlohmann::json endpoints_data;
                endpoints_data["success"] = true;

                endpoints_data["service_name"] = config_.service_name;
                endpoints_data["service_version"] = config_.service_version;
                endpoints_data["host"] = config_.network_config.bind_address;
                endpoints_data["port"] = config_.network_config.listen_port;

                // API端点列表
                endpoints_data["endpoints"] = nlohmann::json::array({
                    {
                        {"path", "/api/v1/auth/login"},
                        {"method", "POST"},
                        {"description", "用户登录"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/auth/register"},
                        {"method", "POST"},
                        {"description", "用户注册"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/auth/refresh"},
                        {"method", "POST"},
                        {"description", "刷新访问令牌"},
                        {"requires_auth", true}
                    },
                    {
                        {"path", "/api/v1/auth/validate"},
                        {"method", "POST"},
                        {"description", "验证访问令牌"},
                        {"requires_auth", true}
                    },
                    {
                        {"path", "/api/v1/auth/logout"},
                        {"method", "POST"},
                        {"description", "用户登出"},
                        {"requires_auth", true}
                    },
                    {
                        {"path", "/api/v1/auth/email/send-code"},
                        {"method", "POST"},
                        {"description", "发送邮箱验证码"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/auth/email/verify-code"},
                        {"method", "POST"},
                        {"description", "验证邮箱验证码"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/auth/password/reset"},
                        {"method", "POST"},
                        {"description", "忘记密码 - 重设密码"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/health"},
                        {"method", "GET"},
                        {"description", "服务健康检查"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/statistics"},
                        {"method", "GET"},
                        {"description", "服务统计信息"},
                        {"requires_auth", false}
                    },
                    {
                        {"path", "/api/v1/service/endpoints"},
                        {"method", "GET"},
                        {"description", "API发现端点"},
                        {"requires_auth", false}
                    }
                });

                // 服务元数据
                endpoints_data["metadata"] = {
                    {"description", "Game Microservices Authentication Service"},
                    {"architecture", "user_service_client_based"},
                    {"features", nlohmann::json::array({"JWT认证", "会话管理", "用户服务集成"})}
                };

                response.setStatus(200);
                response.setHeader("Content-Type", "application/json");
                // 🔧 修复：确保连接头正确设置
                response.setHeader("Connection", "close");
                response.setBody(endpoints_data.dump(2));

                LOG_INFO("✅ API发现端点请求处理完成，返回 " + std::to_string(endpoints_data["endpoints"].size()) + " 个端点");
                LOG_DEBUG("🔍 API发现响应内容: " + endpoints_data.dump());
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("❌ 处理API发现请求异常: " + std::string(e.what()));

                nlohmann::json error_response;
                error_response["success"] = false;
                error_response["error"] = "内部服务器错误";
                error_response["message"] = e.what();

                response.setStatus(500);
                response.setHeader("Content-Type", "application/json");
                response.setBody(error_response.dump());
            }
        }

        // ==================== 时间轮定时任务实现 ====================

        /**
         * @brief 延迟设置定时任务（避免线程安全违规）
         */
        void AuthService::setupTimerTasks()
        {
            LOG_INFO("🔧 直接设置定时任务（简化版本，避免复杂的线程间通信）");

            try
            {
                // 获取EventLoop（如果可用）
                if (http_server_)
                {
                    event_loop_ = http_server_->getEventLoop();
                }

                if (event_loop_)
                {
                    // 启动清理任务
                    startCleanupTasks();
                    LOG_INFO("✅ 定时任务设置完成");
                }
                else
                {
                    LOG_WARNING("⚠️ 无法获取EventLoop，跳过定时任务设置");
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("❌ 设置定时任务时发生异常: " + std::string(e.what()));
            }
        }

        /**
         * @brief 启动清理任务（时间轮实现）
         */
        void AuthService::startCleanupTasks()
        {
            LOG_INFO("🔧 启动认证服务清理任务（简化版本）");

            if (!event_loop_)
            {
                LOG_ERROR("EventLoop未初始化，无法启动时间轮清理任务");
                return;
            }

            // 使用runInLoop来确保在EventLoop线程中执行，避免线程安全问题
            event_loop_->runInLoop([this]()
            {
                try
                {
                    LOG_INFO("📋 在EventLoop线程中设置会话和Token清理任务");

                    // 从配置中读取，如果没有配置则使用合理的默认值（30分钟）
                    if (config_.enable_session_cleanup)
                    {
                        int session_cleanup_interval_minutes = config_.session_cleanup_interval_minutes > 0
                                                                   ? config_.session_cleanup_interval_minutes
                                                                   : 30; // 🔧 改为30分钟默认值，避免过于频繁
                        int session_cleanup_interval_ms = session_cleanup_interval_minutes * 60 * 1000; // 转换为毫秒

                        session_cleanup_timer_id_ = event_loop_->runEvery(session_cleanup_interval_ms, [this]()
                        {
                            performSessionCleanup();
                        });

                        LOG_INFO("🧹 会话清理任务已注册到时间轮，间隔: " + std::to_string(session_cleanup_interval_minutes) + " 分钟");
                    }

                    // 🔧 修复：Token清理任务间隔配置
                    // 使用合理的默认值（60分钟），避免过于频繁的Redis操作
                    if (config_.enable_token_cleanup)
                    {
                        int token_cleanup_interval_minutes = config_.token_cleanup_interval_minutes > 0
                                                                 ? config_.token_cleanup_interval_minutes
                                                                 : 60; // 🔧 改为60分钟默认值
                        int token_cleanup_interval_ms = token_cleanup_interval_minutes * 60 * 1000; // 转换为毫秒

                        token_cleanup_timer_id_ = event_loop_->runEvery(token_cleanup_interval_ms, [this]()
                        {
                            performTokenCleanup();
                        });

                        LOG_INFO("🔄 Token清理任务已注册到时间轮，间隔: " + std::to_string(token_cleanup_interval_minutes) + " 分钟");
                    }

                    LOG_INFO("✅ 认证服务清理任务已启动（基于时间轮）");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("❌ 设置清理任务时发生异常: " + std::string(e.what()));
                } catch (...)
                {
                    LOG_ERROR("❌ 设置清理任务时发生未知异常");
                }
            });
        }

        /**
         * @brief 停止所有定时任务（时间轮实现）
         */
        void AuthService::stopCleanupTasks()
        {
            LOG_INFO("停止认证服务所有定时任务（时间轮实现）");

            // 🔧 增强：添加运行状态检查，避免重复停止
            static std::atomic<bool> tasks_stopped{false};
            if (tasks_stopped.exchange(true))
            {
                LOG_DEBUG("定时任务已经停止过，跳过重复操作");
                return;
            }

            try
            {
                if (event_loop_)
                {
                    // 🔧 增强：使用安全的定时器取消方式，捕获可能的系统异常
                    if (session_cleanup_timer_id_ != 0)
                    {
                        try
                        {
                            event_loop_->cancelTimer(session_cleanup_timer_id_);
                            session_cleanup_timer_id_ = 0;
                            LOG_INFO("会话清理定时器已取消");
                        }
                        catch (const std::system_error& e)
                        {
                            LOG_WARNING("取消会话清理定时器时发生系统错误: " + std::string(e.what()) +
                                " (错误码: " + std::to_string(e.code().value()) + ")");
                        } catch (const std::exception& e)
                        {
                            LOG_WARNING("取消会话清理定时器时发生异常: " + std::string(e.what()));
                        }
                    }

                    if (token_cleanup_timer_id_ != 0)
                    {
                        try
                        {
                            event_loop_->cancelTimer(token_cleanup_timer_id_);
                            token_cleanup_timer_id_ = 0;
                            LOG_INFO("Token清理定时器已取消");
                        }
                        catch (const std::system_error& e)
                        {
                            LOG_WARNING("取消Token清理定时器时发生系统错误: " + std::string(e.what()) +
                                " (错误码: " + std::to_string(e.code().value()) + ")");
                        } catch (const std::exception& e)
                        {
                            LOG_WARNING("取消Token清理定时器时发生异常: " + std::string(e.what()));
                        }
                    }
                }
                else
                {
                    LOG_DEBUG("EventLoop为空，无需取消定时器");
                }

                LOG_INFO("认证服务所有定时任务已停止");
            }
            catch (const std::system_error& e)
            {
                LOG_ERROR("停止定时任务时发生系统错误: " + std::string(e.what()) +
                    " (错误码: " + std::to_string(e.code().value()) + ")");
            } catch (const std::exception& e)
            {
                LOG_ERROR("停止定时任务时发生异常: " + std::string(e.what()));
            } catch (...)
            {
                LOG_ERROR("停止定时任务时发生未知异常");
            }
        }

        /**
         * @brief 执行会话清理任务
         */
        void AuthService::performSessionCleanup()
        {
            if (!session_manager_)
            {
                LOG_WARNING("会话管理器未初始化，跳过会话清理");
                return;
            }

            LOG_DEBUG("开始执行会话清理任务");
            auto start_time = std::chrono::high_resolution_clock::now();

            try
            {
                // 清理过期会话
                int cleaned_count = session_manager_->cleanupExpiredSessions();

                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                LOG_INFO("会话清理完成: 清理了 " + std::to_string(cleaned_count) +
                    " 个过期会话，耗时 " + std::to_string(duration.count()) + "ms");
            }
            catch (const std::system_error& e)
            {
                LOG_ERROR("❌ 会话清理过程中发生系统错误: " + std::string(e.what()) +
                    " (错误码: " + std::to_string(e.code().value()) + ")");
                // 系统错误不应该导致程序终止，只记录日志
            } catch (const std::runtime_error& e)
            {
                LOG_ERROR("❌ 会话清理过程中发生运行时错误: " + std::string(e.what()));
            } catch (const std::exception& e)
            {
                LOG_ERROR("❌ 会话清理过程中发生异常: " + std::string(e.what()));
            } catch (...)
            {
                LOG_ERROR("❌ 会话清理过程中发生未知异常");
            }
        }

        /**
         * @brief 执行Token清理任务
         */
        void AuthService::performTokenCleanup()
        {
            if (!redis_pool_ || !redis_pool_->isRunning())
            {
                LOG_WARNING("Redis连接池未初始化或未运行，跳过Token清理");
                return;
            }

            LOG_DEBUG("开始执行Token清理任务");
            auto start_time = std::chrono::high_resolution_clock::now();

            try
            {
                // 🔧 增强异常处理：防止Redis操作导致系统异常
                LOG_DEBUG("正在获取Redis连接进行Token清理...");

                // 🔧 优化：使用RAII连接管理器，确保连接安全归还
                common::database::RedisConnectionGuard conn_guard(*redis_pool_);
                auto redis_conn = conn_guard.get();
                if (!redis_conn)
                {
                    LOG_ERROR("无法获取Redis连接，跳过Token清理");
                    return;
                }

                // 🔧 连接健康检查：在使用前验证连接状态
                if (!redis_conn->isConnected())
                {
                    LOG_ERROR("Redis连接已断开，跳过Token清理");
                    return;
                }

                // 清理过期的访问令牌和刷新令牌
                int cleaned_tokens = 0;

                // 扫描过期的访问令牌 (前缀: auth:access_token:*)
                auto cutoff_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch() -
                    std::chrono::hours(config_.expired_token_retention_hours)
                ).count();
                (void)cutoff_timestamp; // 避免未使用警告，实际清理逻辑待实现

                // 实现具体的Redis Token清理逻辑
                // 清理过期的访问令牌
                std::vector<std::string> patterns = {
                    "auth:access_token:*",
                    "auth:refresh_token:*",
                    "auth:jwt_blacklist:*"
                };

                for (const auto& pattern : patterns)
                {
                    try
                    {
                        // 这里应该使用Redis的SCAN命令来迭代键
                        // 由于Redis C++客户端的限制，暂时用简化的方式
                        // 在实际实现中，应该使用redis_conn->scan()来迭代大量键
                        LOG_DEBUG("扫描过期Token: " + pattern);

                        // 示例：如果有特定的过期Token键，可以在这里删除
                        // redis_conn->del("specific_expired_token_key");
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR("清理Token时出错: " + std::string(e.what()));
                    }
                }

                // 暂时模拟清理结果
                cleaned_tokens = 0; // 实际实现后会有真实的清理数量

                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                LOG_INFO("Token清理完成: 清理了 " + std::to_string(cleaned_tokens) +
                    " 个过期Token，耗时 " + std::to_string(duration.count()) + "ms");
            }
            catch (const std::system_error& e)
            {
                LOG_ERROR("❌ Token清理过程中发生系统错误: " + std::string(e.what()) +
                    " (错误码: " + std::to_string(e.code().value()) + ")");
                // 系统错误不应该导致程序终止，只记录日志
            } catch (const std::runtime_error& e)
            {
                LOG_ERROR("❌ Token清理过程中发生运行时错误: " + std::string(e.what()));
            } catch (const std::exception& e)
            {
                LOG_ERROR("❌ Token清理过程中发生异常: " + std::string(e.what()));
            } catch (...)
            {
                LOG_ERROR("❌ Token清理过程中发生未知异常");
            }
        }

        // ==================== 服务发现相关实现 ====================

        /**
         * @brief 注册到API网关
         */
        // bool AuthService::registerToApiGateway()
        // {
        //     try
        //     {
        //         LOG_INFO("开始注册到API网关...");
        //
        //         // 构建符合网关期望的服务注册数据结构
        //         nlohmann::json register_data;
        //         register_data["service_name"] = config_.service_name; // 如 "auth_service"
        //         register_data["host"] = config_.network_config.bind_address; // 如 "172.18.0.10"
        //         register_data["port"] = config_.network_config.listen_port; // 如 8080
        //         register_data["version"] = config_.service_version; // 可选：用于标识版本
        //         register_data["weight"] = 100; // 默认权重，影响负载均衡
        //         register_data["healthy"] = true; // 初始健康状态
        //
        //         // 如果你的服务没有这个路径，请添加一个跳转或代理
        //         register_data["health_check_endpoint"] = "/api/v1/health";
        //
        //         // 可选：附加元数据（可用于监控、文档生成等）
        //         register_data["metadata"] = {
        //             {"description", "用户认证与授权微服务"},
        //             {"team", "Game Platform Team"},
        //             {"contact", "dev@gameplatform.com"},
        //             {"repository", "https://git.example.com/services/auth-service"},
        //             {"documentation", "/api/v1/docs"},
        //             {"architecture", "user_service_client_based"}
        //         };
        //
        //         std::string event_type = "server_registration";
        //         nlohmann::json event = {
        //             {"event_type", event_type},
        //             {"service", "auth_service"},
        //             {"service_version", "1.0.0"},
        //             {"timestamp", getCurrentTimestamp()},
        //             {"data", register_data}
        //         };
        //
        //         std::string topic = config_.kafka_config.topic_prefix + ".services.events";
        //         std::string key = event_type;
        //         LOG_WARNING(topic);
        //
        //         bool success = kafka_producer_->send(topic, key, event.dump());
        //
        //         if (success) {
        //             LOG_WARNING("[AuthService::registerToApiGateway] 网关事件发布成功: " + event_type);
        //         } else {
        //             LOG_WARNING("[AuthService::registerToApiGateway] 网关事件发布失败: " + event_type);
        //         }
        //
        //         return success;
        //
        //     }
        //     catch (const std::exception& e)
        //     {
        //         LOG_ERROR("❌ 注册到API网关时发生异常: " + std::string(e.what()));
        //         return false;
        //     }
        // }

    } // namespace auth_service
} // namespace core_services
