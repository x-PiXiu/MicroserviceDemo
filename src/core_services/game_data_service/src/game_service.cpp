//
// Created by Microservice Team  
// 游戏数据服务主类实现 - 简洁稳定架构
//

#include "../include/game_service.h"
#include <common/logger/logger.h>
#include <common/http/http_client.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <random>

using json = nlohmann::json;

namespace core_services {
namespace game_service {

// === GameServiceConfig 实现 ===

GameServiceConfig GameServiceConfig::fromConfigManager() {
    GameServiceConfig config;
    
    try {
        auto& cm = common::config::ConfigManager::getInstance();
        
        // 🔧 服务基本信息 (修正配置键路径)
        config.service_name = cm.get<std::string>("service.name", "game_data_service");
        config.service_version = cm.get<std::string>("service.version", "1.0.0");

        // 使用预定义的配置结构体
        config.network_config = common::config::NetworkConfig::fromConfigManager();
        config.database_config = common::config::DatabaseConfig::fromConfigManager();
        config.redis_config = common::config::RedisConfig::fromConfigManager();
        config.thread_pool_config = common::config::ThreadPoolConfig::fromConfigManager();

        // 🔧 同步 network_config 到顶层字段（确保端口一致性）
        config.host = config.network_config.bind_address;
        config.port = config.network_config.listen_port;
        config.worker_threads = config.network_config.worker_threads;

        // === 功能开关配置 ===
        config.enable_performance_monitoring = cm.get<bool>("monitoring.enable_metrics_collection", true);
        config.enable_cache_cleanup = cm.get<bool>("cache.enable_cleanup", true);
        config.enable_data_archival = cm.get<bool>("data_management.enable_archival", true);
        config.enable_achievement_system = cm.get<bool>("features.achievements.enable", true);
        config.enable_ranking_system = cm.get<bool>("features.ranking.enable", true);
        config.enable_inventory_system = cm.get<bool>("features.inventory.enable", true);
        config.enable_currency_system = cm.get<bool>("features.currency.enable", true);
        config.enable_status_logging = cm.get<bool>("monitoring.enable_status_logging", true);

        // === 定时任务配置 ===
        config.cache_cleanup_interval_minutes = cm.get<int>("timers.cache_cleanup_interval_minutes", 30);
        config.data_archival_interval_hours = cm.get<int>("timers.data_archival_interval_hours", 24);
        config.achievement_update_interval_minutes = cm.get<int>("timers.achievement_update_interval_minutes", 15);
        config.ranking_update_interval_minutes = cm.get<int>("timers.ranking_update_interval_minutes", 10);
        config.inventory_cleanup_interval_hours = cm.get<int>("timers.inventory_cleanup_interval_hours", 6);
        config.currency_audit_interval_hours = cm.get<int>("timers.currency_audit_interval_hours", 12);
        config.status_log_interval_minutes = cm.get<int>("timers.status_log_interval_minutes", 5);
        
        // === 性能配置 ===
        config.max_concurrent_players = cm.get<int>("performance.max_concurrent_players", 1000);
        config.cache_expire_seconds = cm.get<int>("cache.expire_seconds", 3600);
        config.database_connection_timeout_seconds = cm.get<int>("database.connection_timeout_seconds", 10);
        config.redis_connection_timeout_seconds = cm.get<int>("redis.connection_timeout_seconds", 5);

        // === 业务规则配置 ===
        config.max_achievements_per_player = cm.get<int>("business_rules.max_achievements_per_player", 500);
        config.max_inventory_items = cm.get<int>("business_rules.max_inventory_items", 200);
        config.currency_transfer_daily_limit = cm.get<double>("business_rules.currency_transfer_daily_limit", 10000.0);
        config.ranking_top_players_count = cm.get<int>("business_rules.ranking_top_players_count", 100);

        LOG_INFO("[GameServiceConfig] ✅ 游戏数据服务配置从ConfigManager加载成功");
        
    } catch (const std::exception& e) {
        // std::cerr << "[GameServiceConfig] ❌ 从ConfigManager加载配置失败: " << e.what() << ", 使用默认配置" << std::endl;
        // 返回默认配置
        LOG_ERROR("[GameServiceConfig] ❌ 从ConfigManager加载配置失败: " + std::string(e.what()) + ", 使用默认配置");
    }
    
    return config;
}

GameServiceConfig GameServiceConfig::fromConfigFile(const std::string& config_file_path) {
    try {
        auto& cm = common::config::ConfigManager::getInstance();
        
        // 🔧 加载配置文件到ConfigManager
        if (!cm.loadFromFile(config_file_path)) {
            throw std::runtime_error("无法加载配置文件: " + config_file_path);
        }
        
        std::cout << "[GameServiceConfig] ✅ 配置文件加载成功: " << config_file_path << std::endl;
        
        // 🔧 使用fromConfigManager()加载具体配置
        return fromConfigManager();
        
    } catch (const std::exception& e) {
        std::cerr << "[GameServiceConfig] ❌ 从配置文件加载失败: " << e.what() << std::endl;
        throw;  // 重新抛出异常，让调用者处理
    }
}

bool GameServiceConfig::validate() const {
    std::vector<std::string> errors;
    
    try {
        // 🔧 验证服务基础配置
        if (service_name.empty()) {
            errors.push_back("服务名称不能为空");
        }
        
        if (service_version.empty()) {
            errors.push_back("服务版本不能为空");
        }
        
        if (worker_threads < 1 || worker_threads > 1000) {
            errors.push_back("工作线程数必须在1-1000范围内，当前值: " + std::to_string(worker_threads));
        }
        
        // 验证各个配置模块
        network_config.validate();
        database_config.validate();
        redis_config.validate();
        thread_pool_config.validate();
        
        // 🔧 如果有验证错误，记录并返回失败
        if (!errors.empty()) {
            // std::cerr << "[GameServiceConfig] ❌ 配置验证失败，错误数量: " << errors.size() << std::endl;
            LOG_ERROR("[GameServiceConfig] ❌ 配置验证失败，错误数量: " + errors.size());
            for (const auto& error : errors) {
                // std::cerr << "[GameServiceConfig]   - " << error << std::endl;
                LOG_ERROR("[GameServiceConfig]   - " + error);
            }
            return false;
        }
        
        // std::cout << "[GameServiceConfig] ✅ 配置验证通过" << std::endl;
        LOG_INFO("[GameServiceConfig] ✅ 配置验证通过");
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[GameServiceConfig] ❌ 配置验证过程中发生异常: " << e.what() << std::endl;
        return false;
    }
}

// === GameService 实现 ===

// 构造函数
GameService::GameService(const GameServiceConfig& config) 
    : config_(config), service_id_(generateServiceId()) {

    try {
        common::config::ThreadPoolConfig pool_config;
        pool_config.core_pool_size = config_.worker_threads;
        pool_config.maximum_pool_size = config_.worker_threads * 2;
        pool_config.queue_capacity = 1000;
        pool_config.keep_alive_time_ms = 60000;
        pool_config.thread_name_prefix = "game-data-service-";
        pool_config.enable_monitoring = false;
        
        shared_thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(config_.thread_pool_config);
        
        // 🔧 移除构造函数中的LOG调用，避免Logger初始化竞争
        std::cout << "[GameService] 🔧 统一线程池创建成功: " << config_.worker_threads 
                  << " 核心线程，" << (config_.worker_threads * 2) << " 最大线程" << std::endl;
        std::cout << "[GameService] ✅ 统一线程池已提前传递给Logger（构造函数阶段）" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[GameService] 统一线程池创建失败: " << e.what() << std::endl;
        // 继续运行但不使用线程池
        shared_thread_pool_.reset();
    }
    
    // 初始化独立定时器管理器（改进版，支持时间轮定时器）
    timer_manager_ = std::make_unique<GameTimerManager>();
    
    std::cout << "[GameService] GameService created with ID: " << service_id_ << std::endl;
}

GameService::~GameService() {
    if (running_.load()) {
        shutdown();
    }
    //LOG_INFO("GameService destroyed: " + service_id_);
}

// === 生命周期管理 ===

bool GameService::initialize() {
    if (initialized_.load()) {
        LOG_WARNING("GameService already initialized");
        return true;
    }

    try {
        LOG_INFO("=== 开始初始化游戏数据服务 ===");

        // 🔧 统一线程池已在构造函数中传递给Logger，此处跳过
        if (shared_thread_pool_) {
            LOG_INFO("✅ 统一线程池使用确认（已在构造函数中设置）");
        }

        // 初始化数据库连接池
        if (!initializeDatabasePools()) {
            LOG_ERROR("Failed to initialize database pools");
            return false;
        }

        // 初始化数据访问层
        if (!initializeRepository()) {
            LOG_ERROR("Failed to initialize repository");
            return false;
        }

        // 初始化成就管理器
        if (config_.enable_achievement_system) {
            AchievementManagerConfig achievement_config;
            achievement_config.enabled = true;
            achievement_config.send_notifications = true;
            achievement_config.auto_claim_rewards = true;

            achievement_manager_ = std::make_shared<AchievementManager>(repository_, achievement_config);
            if (!achievement_manager_->initialize()) {
                LOG_WARNING("Failed to initialize AchievementManager, achievements disabled");
                achievement_manager_.reset();
            } else {
                LOG_INFO("✅ AchievementManager initialized successfully");
            }
        }

        // 初始化排行榜管理器
        if (config_.enable_ranking_system) {
            LeaderboardManagerConfig leaderboard_config;
            leaderboard_config.enabled = true;
            leaderboard_config.enable_cache = true;
            leaderboard_config.cache_ttl_seconds = config_.cache_expire_seconds;

            leaderboard_manager_ = std::make_shared<LeaderboardManager>(
                repository_, redis_pool_, leaderboard_config);
            if (!leaderboard_manager_->initialize()) {
                LOG_WARNING("Failed to initialize LeaderboardManager, rankings disabled");
                leaderboard_manager_.reset();
            } else {
                LOG_INFO("✅ LeaderboardManager initialized successfully");
            }
        }

        // 初始化游戏结束处理器
        GameEndProcessorConfig processor_config;
        processor_config.enable_achievements = (achievement_manager_ != nullptr);
        processor_config.enable_leaderboard = (leaderboard_manager_ != nullptr);

        game_end_processor_ = std::make_unique<GameEndProcessor>(repository_, processor_config);

        // 将管理器设置到处理器中
        if (achievement_manager_) {
            game_end_processor_->setAchievementManager(achievement_manager_);
        }
        if (leaderboard_manager_) {
            game_end_processor_->setLeaderboardManager(leaderboard_manager_);
        }

        // 初始化HTTP服务器
        if (!initializeHttpServer()) {
            LOG_ERROR("Failed to initialize HTTP server");
            return false;
        }

        // 加载 user_service URL
        {
            auto& cm = common::config::ConfigManager::getInstance();
            user_service_url_ = cm.get<std::string>(
                "external_services.user_service_url", "http://127.0.0.1:8082");
            LOG_INFO("user_service URL: " + user_service_url_);
        }

        initialized_.store(true);
        LOG_INFO("=== 游戏数据服务初始化完成 ===");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception during initialization: " + std::string(e.what()));
        return false;
    }
}

bool GameService::start() {
    if (!initialized_.load()) {
        LOG_ERROR("Service not initialized, cannot start");
        return false;
    }

    if (running_.load()) {
        LOG_WARNING("Service already running");
        return true;
    }

    try {
        LOG_INFO("=== 启动游戏数据服务 ===");

        // 启动独立定时器管理器
        if (timer_manager_) {
            timer_manager_->start();
            LOG_INFO("GameTimerManager started successfully");
            
            // 🔧 添加业务定时任务
            setupBusinessTimers();
        }

        if (!http_server_running_ && !http_server_)
        {
            LOG_ERROR("http_server_ 初始化失败");
            return false;
        }

        running_.store(true);
        start_time_ = std::chrono::system_clock::now();

        LOG_INFO("🚀 游戏数据服务启动成功");
        LOG_INFO("服务ID: " + service_id_);
        LOG_INFO("监听地址: " + config_.host + ":" + std::to_string(config_.port));
        LOG_INFO("工作线程数: " + std::to_string(config_.worker_threads));

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception during startup: " + std::string(e.what()));
        running_.store(false);
        return false;
    }
}

void GameService::stop() {
    // 原子操作：检查并设置 running_ 为 false，防止重复调用
    if (!running_.exchange(false)) {
        LOG_INFO("Service not running or already stopping");
        return;
    }

    LOG_INFO("=== 停止游戏数据服务 ===");

    // 停止定时器管理器
    if (timer_manager_) {
        timer_manager_->stop();
        LOG_INFO("GameTimerManager stopped");
    }

    // 首先停止HTTP服务器（这会让EventLoop退出）
    if (http_server_) {
        LOG_INFO("停止HTTP服务器...");
        http_server_->stop();
    }

    // 等待HTTP服务器线程结束
    if (http_server_thread_.joinable())
    {
        LOG_INFO("等待HTTP服务器线程结束...");
        http_server_running_.store(false);
        http_server_thread_.join();
        LOG_INFO("HTTP服务器线程已结束");
    }

    LOG_INFO("游戏数据服务已停止");
}

void GameService::shutdown() {
    LOG_INFO("=== 开始优雅关闭游戏数据服务 ===");

    // 停止服务
    stop();

    // 清理资源
    try {
        if (mysql_pool_) {
            mysql_pool_->stop();
            mysql_pool_.reset();
        }

        if (redis_pool_) {
            redis_pool_->stop();
            redis_pool_.reset();
        }

        repository_.reset();
        http_server_.reset();
        timer_manager_.reset();

        initialized_.store(false);
        
        LOG_INFO("=== 游戏数据服务优雅关闭完成 ===");

    } catch (const std::exception& e) {
        LOG_ERROR("Exception during shutdown: " + std::string(e.what()));
    }
}

std::string GameService::getStatusInfo() const {
    json status;
    status["service_id"] = service_id_;
    status["service_name"] = "game_data_service";
    status["version"] = "1.0.0";
    status["running"] = running_.load();
    status["initialized"] = initialized_.load();
    
    if (running_.load()) {
        auto now = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        status["uptime_seconds"] = uptime;
        
        auto time_t = std::chrono::system_clock::to_time_t(start_time_);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
        status["start_time"] = oss.str();
    }
    
    status["host"] = config_.host;
    status["port"] = config_.port;
    status["worker_threads"] = config_.worker_threads;
    
    return status.dump(2);
}

// === 初始化方法 ===

bool GameService::initializeDatabasePools() {
    try {
        LOG_INFO("初始化数据库连接池...");

        // MySQL连接池配置（🔧 禁用独立线程池，使用同步模式）
        // common::config::DatabaseConfig mysql_config;
        mysql_pool_ = std::make_shared<common::database::MySQLPool>(config_.database_config);

        try {
            mysql_pool_->start();
            LOG_INFO("MySQL connection pool started successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start MySQL connection pool: " + std::string(e.what()));
            return false;
        }

        // common::config::RedisConfig redis_config;
        redis_pool_ = std::make_shared<common::database::RedisPool>(config_.redis_config);

        try {
            redis_pool_->start();
            LOG_INFO("Redis connection pool started successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start Redis connection pool: " + std::string(e.what()));
            return false;
        }

        LOG_INFO("数据库连接池初始化完成");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in initializeDatabasePools: " + std::string(e.what()));
        return false;
    }
}

bool GameService::initializeRepository() {
    try {
        LOG_INFO("初始化数据访问层...");

        repository_ = std::make_shared<GameRepository>(mysql_pool_, redis_pool_);
        
        // 测试数据库连接
        if (!repository_->testDatabaseConnection()) {
            LOG_ERROR("Database connection test failed");
            return false;
        }

        LOG_INFO("数据访问层初始化完成");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in initializeRepository: " + std::string(e.what()));
        return false;
    }
}

bool GameService::initializeHttpServer() {
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
            http_server_ = std::make_unique<common::http::HttpServer>(server_config, shared_thread_pool_);

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
 * @brief 设置 CORS 中间件
 */
void GameService::setupCorsMiddleware() {
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

void GameService::setupHttpRoutes() {
    LOG_INFO("设置HTTP路由...");

    // ========== 系统级 API (统一前缀 /api/v1/gamedata/) ==========
    http_server_->route("GET", "/api/v1/gamedata/health",
        [this](const auto& req, auto& resp) { handleHealthCheck(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/stats",
        [this](const auto& req, auto& resp) { handleServiceStatus(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/service/endpoints",
        [this](const auto& req, auto& resp) { handleServiceEndpoints(req, resp); });

    // ========== 用户档案 API ==========
    http_server_->route("POST", "/api/v1/gamedata/profiles",
        [this](const auto& req, auto& resp) { handleCreateUserProfile(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/profiles/:user_id",
        [this](const auto& req, auto& resp) { handleGetUserProfile(req, resp); });

    http_server_->route("PUT", "/api/v1/gamedata/profiles/:user_id",
        [this](const auto& req, auto& resp) { handleUpdateUserProfile(req, resp); });

    http_server_->route("DELETE", "/api/v1/gamedata/profiles/:user_id",
        [this](const auto& req, auto& resp) { handleDeleteUserProfile(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/profiles",
        [this](const auto& req, auto& resp) { handleGetUserProfiles(req, resp); });

    // ========== 成就系统 API ==========
    http_server_->route("POST", "/api/v1/gamedata/achievements",
        [this](const auto& req, auto& resp) { handleUnlockAchievement(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/achievements/:user_id",
        [this](const auto& req, auto& resp) { handleGetUserAchievements(req, resp); });

    // ========== 库存管理 API ==========
    http_server_->route("POST", "/api/v1/gamedata/inventory",
        [this](const auto& req, auto& resp) { handleAddInventoryItem(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/inventory/:user_id",
        [this](const auto& req, auto& resp) { handleGetUserInventory(req, resp); });

    http_server_->route("POST", "/api/v1/gamedata/inventory/:user_id/use",
        [this](const auto& req, auto& resp) { handleUseInventoryItem(req, resp); });

    // ========== 货币系统 API ==========
    http_server_->route("GET", "/api/v1/gamedata/currency/:user_id/:currency_type",
        [this](const auto& req, auto& resp) { handleGetUserCurrency(req, resp); });

    http_server_->route("PUT", "/api/v1/gamedata/currency",
        [this](const auto& req, auto& resp) { handleUpdateUserCurrency(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/currency/:user_id",
        [this](const auto& req, auto& resp) { handleGetAllUserCurrencies(req, resp); });

    // ========== 排行榜 API ==========
    http_server_->route("POST", "/api/v1/gamedata/leaderboard",
        [this](const auto& req, auto& resp) { handleUpdateLeaderboard(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/leaderboard/:leaderboard_type/:game_type",
        [this](const auto& req, auto& resp) { handleGetLeaderboard(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/leaderboard/:leaderboard_type/:game_type/rank/:user_id",
        [this](const auto& req, auto& resp) { handleGetUserRank(req, resp); });

    // ========== 游戏结算 API ==========
    http_server_->route("POST", "/api/v1/gamedata/settlement",
        [this](const auto& req, auto& resp) { handleGameEnd(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/settlement/:game_id",
        [this](const auto& req, auto& resp) { handleGetGameSettlement(req, resp); });

    http_server_->route("GET", "/api/v1/gamedata/stats/daily/:user_id",
        [this](const auto& req, auto& resp) { handleGetUserDailyStats(req, resp); });

    LOG_INFO("HTTP路由设置完成，共注册 22 个端点");
}

// === HTTP API 处理器 ===

void GameService::handleServiceEndpoints(const common::http::HttpRequest& /*request*/, common::http::HttpResponse& response) {
    json endpoints = json::array();

    // 服务发现
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/service/endpoints"}, {"description", "服务端点发现"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/health"}, {"description", "健康检查"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/stats"}, {"description", "服务状态"}});

    // 用户档案
    endpoints.push_back({{"method", "POST"}, {"path", "/api/v1/gamedata/profiles"}, {"description", "创建用户档案"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/profiles/{user_id}"}, {"description", "获取用户档案"}});
    endpoints.push_back({{"method", "PUT"}, {"path", "/api/v1/gamedata/profiles/{user_id}"}, {"description", "更新用户档案"}});
    endpoints.push_back({{"method", "DELETE"}, {"path", "/api/v1/gamedata/profiles/{user_id}"}, {"description", "删除用户档案"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/profiles"}, {"description", "获取用户档案列表"}});

    // 成就系统
    endpoints.push_back({{"method", "POST"}, {"path", "/api/v1/gamedata/achievements"}, {"description", "解锁成就"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/achievements/{user_id}"}, {"description", "获取用户成就"}});

    // 库存管理
    endpoints.push_back({{"method", "POST"}, {"path", "/api/v1/gamedata/inventory"}, {"description", "添加库存物品"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/inventory/{user_id}"}, {"description", "获取用户库存"}});
    endpoints.push_back({{"method", "POST"}, {"path", "/api/v1/gamedata/inventory/{user_id}/use"}, {"description", "使用库存物品"}});

    // 货币系统
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/currency/{user_id}/{currency_type}"}, {"description", "获取用户货币"}});
    endpoints.push_back({{"method", "PUT"}, {"path", "/api/v1/gamedata/currency"}, {"description", "更新用户货币"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/currency/{user_id}"}, {"description", "获取用户所有货币"}});

    // 排行榜
    endpoints.push_back({{"method", "POST"}, {"path", "/api/v1/gamedata/leaderboard"}, {"description", "更新排行榜"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}"}, {"description", "获取排行榜"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}/rank/{user_id}"}, {"description", "获取用户排名"}});

    // 游戏结算
    endpoints.push_back({{"method", "POST"}, {"path", "/api/v1/gamedata/settlement"}, {"description", "游戏结算处理"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/settlement/{game_id}"}, {"description", "获取游戏结算记录"}});
    endpoints.push_back({{"method", "GET"}, {"path", "/api/v1/gamedata/stats/daily/{user_id}"}, {"description", "获取用户每日统计"}});

    json result;
    result["success"] = true;  // 网关期望的字段
    result["service"] = "game_data_service";
    result["version"] = "1.0.0";
    result["endpoints"] = endpoints;
    result["total_endpoints"] = endpoints.size();

    response.setStatus(200);
    response.setBody(result.dump(2));
    response.setHeader("Content-Type", "application/json");

    logApiAccess("GET", "/api/v1/gamedata/service/endpoints", 200);
    LOG_INFO("✅ API发现端点请求处理完成，返回 " + std::to_string(endpoints.size()) + " 个端点");
}

void GameService::handleHealthCheck(const common::http::HttpRequest& /*request*/, common::http::HttpResponse& response) {
    json health;
    health["status"] = "healthy";
    health["service"] = "game_data_service";
    health["timestamp"] = getCurrentTimestamp();
    
    // 检查数据库连接
    bool db_healthy = repository_ && repository_->testDatabaseConnection();
    health["database"] = db_healthy ? "healthy" : "unhealthy";
    
    // 检查服务状态
    health["running"] = running_.load();
    health["uptime_seconds"] = running_.load() ? 
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - start_time_
        ).count() : 0;
    
    int status_code = (running_.load() && db_healthy) ? 200 : 503;
    response.setStatus(status_code);
    response.setBody(health.dump(2));
    response.setHeader("Content-Type", "application/json");
    
    logApiAccess("GET", "/api/v1/health", status_code);
}

void GameService::handleServiceStatus(const common::http::HttpRequest& /*request*/, common::http::HttpResponse& response) {
    std::string status_info = getStatusInfo();
    
    response.setStatus(200);
    response.setBody(status_info);
    response.setHeader("Content-Type", "application/json");
    
    logApiAccess("GET", "/api/v1/service/status", 200);
}

void GameService::handleCreateUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        json request_body = json::parse(request.getBody());
        
        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"user_id", "display_name"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }
        
        // 构建用户档案
        UserGameProfile profile;
        profile.user_id = request_body["user_id"];
        profile.display_name = request_body["display_name"];
        profile.level = request_body.value("level", 1);
        profile.experience_points = request_body.value("experience_points", 0L);
        profile.avatar_url = request_body.value("avatar_url", "");
        profile.created_at = std::chrono::system_clock::now();
        profile.last_login_at = std::chrono::system_clock::now();
        
        // 创建档案
        if (repository_->createUserProfile(profile)) {
            sendSuccessResponse(response, profile, "User profile created successfully");
            logApiAccess("POST", "/api/v1/profiles", 201, profile.user_id);
        } else {
            sendErrorResponse(response, 500, "Failed to create user profile");
            logApiAccess("POST", "/api/v1/profiles", 500, profile.user_id);
        }
        
    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/profiles", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/profiles", 500);
    }
}

void GameService::handleGetUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }
        
        auto profile = repository_->getUserProfile(user_id.value());
        if (profile.has_value()) {
            sendSuccessResponse(response, profile.value(), "User profile retrieved successfully");
            logApiAccess("GET", "/api/v1/profiles/" + user_id.value(), 200, user_id.value());
        } else {
            sendErrorResponse(response, 404, "User profile not found");
            logApiAccess("GET", "/api/v1/profiles/" + user_id.value(), 404, user_id.value());
        }
        
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/profiles/*", 500);
    }
}

// === 辅助方法 ===

PaginationParams GameService::parsePaginationParams(const common::http::HttpRequest& request) const {
    PaginationParams params;
    
    auto query_params = request.getParams();
    
    if (query_params.count("page")) {
        try {
            params.page = std::max(1, std::stoi(query_params.at("page")));
        } catch (...) {
            params.page = 1;
        }
    }
    
    if (query_params.count("limit")) {
        try {
            int limit = std::stoi(query_params.at("limit"));
            params.limit = std::max(1, std::min(100, limit)); // 限制在1-100之间
        } catch (...) {
            params.limit = 20;
        }
    }
    
    if (query_params.count("sort_by")) {
        params.sort_by = query_params.at("sort_by");
    }
    
    if (query_params.count("sort_order")) {
        std::string order = query_params.at("sort_order");
        if (order == "asc" || order == "desc") {
            params.sort_order = order;
        }
    }
    
    return params;
}


std::optional<std::string> GameService::extractPathParameter(const std::string& path, const std::string& param_name) const {
    // 简单的路径参数提取，假设格式为 /api/v1/resource/{param}
    std::regex pattern("\\{" + param_name + "\\}");
    // 这里需要更复杂的实现来匹配实际路径，暂时简化处理
    
    // 假设我们有实际的路径解析逻辑
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos + 1 < path.length()) {
        return path.substr(pos + 1);
    }
    
    return std::nullopt;
}

void GameService::logApiAccess(const std::string& method, const std::string& path, int status_code, const std::string& user_id) const {
    std::string log_msg = "HTTP " + method + " " + path + " -> " + std::to_string(status_code);
    if (!user_id.empty()) {
        log_msg += " [user: " + user_id + "]";
    }
    
    if (status_code < 400) {
        LOG_INFO(log_msg);
    } else {
        LOG_WARNING(log_msg);
    }
}

std::string GameService::generateServiceId() const {
    // 简单的服务ID生成：使用时间戳和随机数
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    return config_.service_name + "_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

std::string GameService::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::pair<bool, std::string> GameService::validateRequiredFields(
    const std::vector<std::string>& required_fields,
    const nlohmann::json& request_body
) const {
    for (const auto& field : required_fields) {
        if (!request_body.contains(field) || request_body[field].is_null()) {
            return {false, field};
        }
    }
    return {true, ""};
}


// 其他用户档案管理方法
void GameService::handleUpdateUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        // 提取 user_id
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        // 解析请求体
        json request_body = json::parse(request.getBody());

        // 先获取现有档案
        auto existing_profile = repository_->getUserProfile(user_id.value());
        if (!existing_profile.has_value()) {
            sendErrorResponse(response, 404, "User profile not found");
            logApiAccess("PUT", "/api/v1/profiles/" + user_id.value(), 404, user_id.value());
            return;
        }

        // 更新档案字段
        UserGameProfile profile = existing_profile.value();
        if (request_body.contains("display_name")) {
            profile.display_name = request_body["display_name"];
        }
        if (request_body.contains("level")) {
            profile.level = request_body["level"];
        }
        if (request_body.contains("experience_points")) {
            profile.experience_points = request_body["experience_points"];
        }
        if (request_body.contains("avatar_url")) {
            profile.avatar_url = request_body["avatar_url"];
        }
        profile.last_login_at = std::chrono::system_clock::now();

        // 执行更新
        if (repository_->updateUserProfile(profile)) {
            sendSuccessResponse(response, profile, "User profile updated successfully");
            logApiAccess("PUT", "/api/v1/profiles/" + user_id.value(), 200, user_id.value());
        } else {
            sendErrorResponse(response, 500, "Failed to update user profile");
            logApiAccess("PUT", "/api/v1/profiles/" + user_id.value(), 500, user_id.value());
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("PUT", "/api/v1/profiles/*", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("PUT", "/api/v1/profiles/*", 500);
    }
}

void GameService::handleDeleteUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        if (repository_->deleteUserProfile(user_id.value())) {
            json result;
            result["user_id"] = user_id.value();
            result["deleted"] = true;
            sendSuccessResponse(response, result, "User profile deleted successfully");
            logApiAccess("DELETE", "/api/v1/profiles/" + user_id.value(), 200, user_id.value());
        } else {
            sendErrorResponse(response, 404, "User profile not found");
            logApiAccess("DELETE", "/api/v1/profiles/" + user_id.value(), 404, user_id.value());
        }

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("DELETE", "/api/v1/profiles/*", 500);
    }
}

void GameService::handleGetUserProfiles(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        PaginationParams params = parsePaginationParams(request);
        QueryFilter filter;

        auto paginated = repository_->getUserProfiles(params, filter);

        json result;
        result["items"] = json::array();
        for (const auto& profile : paginated.items) {
            result["items"].push_back({
                {"user_id", profile.user_id},
                {"display_name", profile.display_name},
                {"level", profile.level},
                {"experience_points", profile.experience_points},
                {"avatar_url", profile.avatar_url}
            });
        }
        result["total_count"] = paginated.total_count;
        result["page"] = paginated.page;
        result["limit"] = paginated.limit;
        result["total_pages"] = paginated.total_pages;

        sendSuccessResponse(response, result, "User profiles retrieved successfully");
        logApiAccess("GET", "/api/v1/profiles", 200);

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/profiles", 500);
    }
}

// 成就系统
void GameService::handleUnlockAchievement(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        json request_body = json::parse(request.getBody());

        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"user_id", "achievement_type"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }

        GameAchievement achievement;
        achievement.user_id = request_body["user_id"];
        achievement.achievement_type = request_body["achievement_type"];
        achievement.title = request_body.value("title", achievement.achievement_type);
        achievement.description = request_body.value("description", "");
        achievement.points = request_body.value("points", 0);

        if (repository_->unlockAchievement(achievement)) {
            json result;
            result["user_id"] = achievement.user_id;
            result["achievement_type"] = achievement.achievement_type;
            result["title"] = achievement.title;
            result["description"] = achievement.description;
            result["points"] = achievement.points;
            result["unlocked"] = true;
            sendSuccessResponse(response, result, "Achievement unlocked successfully");
            logApiAccess("POST", "/api/v1/achievements", 200, achievement.user_id);
        } else {
            sendErrorResponse(response, 500, "Failed to unlock achievement");
            logApiAccess("POST", "/api/v1/achievements", 500, achievement.user_id);
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/achievements", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/achievements", 500);
    }
}

void GameService::handleGetUserAchievements(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        PaginationParams params = parsePaginationParams(request);
        auto paginated = repository_->getUserAchievements(user_id.value(), params);

        json result;
        result["items"] = json::array();
        for (const auto& achievement : paginated.items) {
            result["items"].push_back({
                {"achievement_id", achievement.achievement_id},
                {"achievement_type", achievement.achievement_type},
                {"title", achievement.title},
                {"description", achievement.description},
                {"points", achievement.points}
            });
        }
        result["total_count"] = paginated.total_count;
        result["page"] = paginated.page;
        result["limit"] = paginated.limit;
        result["total_pages"] = paginated.total_pages;

        sendSuccessResponse(response, result, "User achievements retrieved successfully");
        logApiAccess("GET", "/api/v1/achievements/" + user_id.value(), 200, user_id.value());

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/achievements/*", 500);
    }
}

// 库存管理
void GameService::handleAddInventoryItem(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        json request_body = json::parse(request.getBody());

        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"user_id", "item_type", "item_name"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }

        UserInventoryItem item;
        item.user_id = request_body["user_id"];
        item.item_id = request_body.value("item_id", "");
        item.item_type = request_body["item_type"];
        item.item_name = request_body["item_name"];
        item.quantity = request_body.value("quantity", 1);

        if (repository_->addInventoryItem(item)) {
            json result;
            result["user_id"] = item.user_id;
            result["item_type"] = item.item_type;
            result["item_name"] = item.item_name;
            result["quantity"] = item.quantity;
            result["added"] = true;
            sendSuccessResponse(response, result, "Inventory item added successfully");
            logApiAccess("POST", "/api/v1/inventory", 200, item.user_id);
        } else {
            sendErrorResponse(response, 500, "Failed to add inventory item");
            logApiAccess("POST", "/api/v1/inventory", 500, item.user_id);
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/inventory", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/inventory", 500);
    }
}

void GameService::handleGetUserInventory(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        PaginationParams params = parsePaginationParams(request);
        auto paginated = repository_->getUserInventory(user_id.value(), params);

        json result;
        result["items"] = json::array();
        for (const auto& item : paginated.items) {
            result["items"].push_back({
                {"inventory_id", item.inventory_id},
                {"item_type", item.item_type},
                {"item_name", item.item_name},
                {"quantity", item.quantity}
            });
        }
        result["total_count"] = paginated.total_count;
        result["page"] = paginated.page;
        result["limit"] = paginated.limit;
        result["total_pages"] = paginated.total_pages;

        sendSuccessResponse(response, result, "User inventory retrieved successfully");
        logApiAccess("GET", "/api/v1/inventory/" + user_id.value(), 200, user_id.value());

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/inventory/*", 500);
    }
}

void GameService::handleUseInventoryItem(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        json request_body = json::parse(request.getBody());

        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"item_id"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }

        std::string item_id = request_body["item_id"];
        int quantity = request_body.value("quantity", 1);

        if (repository_->useInventoryItem(user_id.value(), item_id, quantity)) {
            json result;
            result["user_id"] = user_id.value();
            result["item_id"] = item_id;
            result["quantity_used"] = quantity;
            result["used"] = true;
            sendSuccessResponse(response, result, "Inventory item used successfully");
            logApiAccess("POST", "/api/v1/inventory/" + user_id.value() + "/use", 200, user_id.value());
        } else {
            sendErrorResponse(response, 400, "Failed to use inventory item (insufficient quantity?)");
            logApiAccess("POST", "/api/v1/inventory/" + user_id.value() + "/use", 400, user_id.value());
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/inventory/*/use", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/inventory/*/use", 500);
    }
}

// 货币系统
void GameService::handleGetUserCurrency(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        auto currency_type = extractPathParameter(request.getPath(), "currency_type");

        if (!user_id.has_value() || !currency_type.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id or currency_type parameter");
            return;
        }

        auto currency = repository_->getUserCurrency(user_id.value(), currency_type.value());
        if (currency.has_value()) {
            json result;
            result["user_id"] = currency->user_id;
            result["currency_type"] = currency->currency_type;
            result["amount"] = currency->amount;
            sendSuccessResponse(response, result, "Currency retrieved successfully");
            logApiAccess("GET", "/api/v1/currency/" + user_id.value() + "/" + currency_type.value(), 200, user_id.value());
        } else {
            // 如果不存在，返回初始值 0
            json result;
            result["user_id"] = user_id.value();
            result["currency_type"] = currency_type.value();
            result["amount"] = 0;
            sendSuccessResponse(response, result, "Currency not found, returning default");
            logApiAccess("GET", "/api/v1/currency/" + user_id.value() + "/" + currency_type.value(), 200, user_id.value());
        }

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/currency/*/*", 500);
    }
}

void GameService::handleUpdateUserCurrency(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        json request_body = json::parse(request.getBody());

        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"user_id", "currency_type", "amount"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }

        std::string user_id = request_body["user_id"];
        std::string currency_type = request_body["currency_type"];
        std::string operation = request_body.value("operation", "set");
        long amount = request_body["amount"];

        bool success = false;
        long new_amount = amount;

        if (operation == "add") {
            success = repository_->addUserCurrency(user_id, currency_type, amount);
            if (success) {
                auto updated = repository_->getUserCurrency(user_id, currency_type);
                if (updated.has_value()) {
                    new_amount = updated->amount;
                }
            }
        } else if (operation == "subtract" || operation == "deduct") {
            success = repository_->deductUserCurrency(user_id, currency_type, amount);
            if (success) {
                auto updated = repository_->getUserCurrency(user_id, currency_type);
                if (updated.has_value()) {
                    new_amount = updated->amount;
                }
            }
        } else {
            // 默认为 set 操作
            UserCurrency currency;
            currency.user_id = user_id;
            currency.currency_type = currency_type;
            currency.amount = amount;
            success = repository_->updateUserCurrency(currency);
            new_amount = amount;
        }

        if (success) {
            json result;
            result["user_id"] = user_id;
            result["currency_type"] = currency_type;
            result["amount"] = new_amount;
            result["operation"] = operation;
            sendSuccessResponse(response, result, "Currency updated successfully");
            logApiAccess("PUT", "/api/v1/currency", 200, user_id);
        } else {
            sendErrorResponse(response, 400, "Failed to update currency (insufficient balance?)");
            logApiAccess("PUT", "/api/v1/currency", 400, user_id);
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("PUT", "/api/v1/currency", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("PUT", "/api/v1/currency", 500);
    }
}

void GameService::handleGetAllUserCurrencies(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        auto currencies = repository_->getAllUserCurrencies(user_id.value());

        json result;
        result["user_id"] = user_id.value();
        result["currencies"] = json::array();
        for (const auto& currency : currencies) {
            result["currencies"].push_back({
                {"currency_type", currency.currency_type},
                {"amount", currency.amount}
            });
        }
        result["total_types"] = currencies.size();

        sendSuccessResponse(response, result, "All currencies retrieved successfully");
        logApiAccess("GET", "/api/v1/currency/" + user_id.value(), 200, user_id.value());

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/currency/*", 500);
    }
}

// 排行榜
void GameService::handleUpdateLeaderboard(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        json request_body = json::parse(request.getBody());

        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"user_id", "leaderboard_type", "game_type", "score"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }

        LeaderboardEntry entry;
        entry.user_id = request_body["user_id"];
        entry.leaderboard_type = request_body["leaderboard_type"];
        entry.game_type = request_body["game_type"];
        entry.score = request_body["score"];
        entry.rank_position = request_body.value("rank_position", 0);

        if (repository_->updateLeaderboardEntry(entry)) {
            // 获取更新后的排名
            int rank = repository_->getUserRank(entry.user_id, entry.leaderboard_type, entry.game_type);

            json result;
            result["user_id"] = entry.user_id;
            result["leaderboard_type"] = entry.leaderboard_type;
            result["game_type"] = entry.game_type;
            result["score"] = entry.score;
            result["rank"] = rank;
            sendSuccessResponse(response, result, "Leaderboard updated successfully");
            logApiAccess("POST", "/api/v1/leaderboard", 200, entry.user_id);
        } else {
            sendErrorResponse(response, 500, "Failed to update leaderboard");
            logApiAccess("POST", "/api/v1/leaderboard", 500, entry.user_id);
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/leaderboard", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/leaderboard", 500);
    }
}

void GameService::handleGetLeaderboard(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto leaderboard_type_str = extractPathParameter(request.getPath(), "leaderboard_type");
        auto game_type = extractPathParameter(request.getPath(), "game_type");

        if (!leaderboard_type_str.has_value() || !game_type.has_value()) {
            sendErrorResponse(response, 400, "Missing leaderboard_type or game_type parameter");
            return;
        }

        // 检查 LeaderboardManager 是否可用
        if (!leaderboard_manager_) {
            sendErrorResponse(response, 503, "Leaderboard service not available");
            return;
        }

        // 解析查询参数
        PaginationParams params = parsePaginationParams(request);
        std::string scope_str = request.getQueryParam("scope", "global");
        std::string user_id = request.getQueryParam("user_id", "");

        // 转换类型和范围
        LeaderboardType type;
        LeaderboardScope scope;
        try {
            type = LeaderboardManager::stringToType(leaderboard_type_str.value());
            scope = LeaderboardManager::stringToScope(scope_str);
        } catch (const std::exception& e) {
            sendErrorResponse(response, 400, std::string("Invalid leaderboard type or scope: ") + e.what());
            return;
        }

        // 使用 LeaderboardManager 获取完整排行榜数据
        LeaderboardResult leaderboard_result = leaderboard_manager_->getLeaderboard(
            type, scope, game_type.value(), params.getOffset(), params.limit, user_id);

        // 从 user_service 批量获取用户显示名
        std::vector<std::string> entry_user_ids;
        for (const auto& entry : leaderboard_result.entries) {
            entry_user_ids.push_back(entry.user_id);
        }
        if (leaderboard_result.my_rank.has_value()) {
            entry_user_ids.push_back(leaderboard_result.my_rank->user_id);
        }
        auto user_display_map = resolveUserDisplayInfo(entry_user_ids);

        // 构建响应（使用 LeaderboardResult::toJson() 获取完整字段）
        json result;
        result["type"] = LeaderboardManager::typeToString(type);
        result["game_type"] = game_type.value();
        result["scope"] = scope_str;

        // 完整的排行榜条目（使用 entries 与文档保持一致）
        result["entries"] = json::array();
        for (const auto& entry : leaderboard_result.entries) {
            std::string display_name = entry.display_name;
            std::string avatar_url = entry.avatar_url;
            auto it = user_display_map.find(entry.user_id);
            if (it != user_display_map.end()) {
                if (display_name.empty()) display_name = it->second.display_name;
                if (avatar_url.empty()) avatar_url = it->second.avatar_url;
            }
            result["entries"].push_back({
                {"rank", entry.rank},
                {"user_id", entry.user_id},
                {"display_name", display_name},
                {"score", entry.score},
                {"tier_name", entry.tier_name},
                {"win_rate", entry.win_rate},
                {"games_played", entry.games_played},
                {"avatar_url", avatar_url}
            });
        }

        // 总玩家数
        result["total_players"] = leaderboard_result.total_players;

        // 分页信息
        result["page"] = params.page;
        result["limit"] = params.limit;

        // 更新时间
        result["updated_at"] = leaderboard_result.updated_at;

        // 用户自己的排名（如果查询中指定了 user_id）
        if (leaderboard_result.my_rank.has_value()) {
            const auto& my_rank = leaderboard_result.my_rank.value();
            std::string display_name = my_rank.display_name;
            std::string avatar_url = my_rank.avatar_url;
            auto it = user_display_map.find(my_rank.user_id);
            if (it != user_display_map.end()) {
                if (display_name.empty()) display_name = it->second.display_name;
                if (avatar_url.empty()) avatar_url = it->second.avatar_url;
            }
            result["my_rank"] = {
                {"rank", my_rank.rank},
                {"user_id", my_rank.user_id},
                {"display_name", display_name},
                {"score", my_rank.score},
                {"tier_name", my_rank.tier_name},
                {"win_rate", my_rank.win_rate},
                {"games_played", my_rank.games_played},
                {"avatar_url", avatar_url}
            };
        }

        sendSuccessResponse(response, result, "Leaderboard retrieved successfully");
        logApiAccess("GET", "/api/v1/leaderboard/" + leaderboard_type_str.value() + "/" + game_type.value(), 200);

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/leaderboard/*/*", 500);
    }
}

void GameService::handleGetUserRank(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto leaderboard_type_str = extractPathParameter(request.getPath(), "leaderboard_type");
        auto game_type = extractPathParameter(request.getPath(), "game_type");
        auto user_id = extractPathParameter(request.getPath(), "user_id");

        if (!leaderboard_type_str.has_value() || !game_type.has_value() || !user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing leaderboard_type, game_type or user_id parameter");
            return;
        }

        // 检查 LeaderboardManager 是否可用
        if (!leaderboard_manager_) {
            sendErrorResponse(response, 503, "Leaderboard service not available");
            return;
        }

        // 解析查询参数
        std::string scope_str = request.getQueryParam("scope", "global");

        // 转换类型和范围
        LeaderboardType type;
        LeaderboardScope scope;
        try {
            type = LeaderboardManager::stringToType(leaderboard_type_str.value());
            scope = LeaderboardManager::stringToScope(scope_str);
        } catch (const std::exception& e) {
            sendErrorResponse(response, 400, std::string("Invalid leaderboard type or scope: ") + e.what());
            return;
        }

        // 使用 LeaderboardManager 获取用户排名详情
        auto rank_detail = leaderboard_manager_->getUserRankDetail(
            user_id.value(), type, scope, game_type.value());

        json result;
        result["user_id"] = user_id.value();
        result["leaderboard_type"] = leaderboard_type_str.value();
        result["game_type"] = game_type.value();
        result["scope"] = scope_str;

        if (rank_detail.has_value()) {
            result["rank"] = rank_detail->rank;
            result["on_leaderboard"] = true;
            result["display_name"] = rank_detail->display_name;
            result["score"] = rank_detail->score;
            result["tier_name"] = rank_detail->tier_name;
            result["win_rate"] = rank_detail->win_rate;
            result["games_played"] = rank_detail->games_played;
            result["avatar_url"] = rank_detail->avatar_url;
        } else {
            result["rank"] = 0;
            result["on_leaderboard"] = false;
            result["display_name"] = "";
            result["score"] = 0;
        }

        sendSuccessResponse(response, result, "User rank retrieved successfully");
        logApiAccess("GET", "/api/v1/leaderboard/rank/" + user_id.value(), 200, user_id.value());

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/leaderboard/*/*/*", 500);
    }
}

// === 游戏结算处理器 ===

void GameService::handleGameEnd(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        json request_body = json::parse(request.getBody());

        // 验证必需字段
        auto [valid, missing_field] = validateRequiredFields({"game_id", "game_type", "players"}, request_body);
        if (!valid) {
            sendErrorResponse(response, 400, "Missing required field: " + missing_field);
            return;
        }

        // 构建 GameEndRequest
        GameEndRequest game_end_request;
        game_end_request.game_id = request_body["game_id"];
        game_end_request.game_type = request_body["game_type"];
        game_end_request.mode = stringToGameMode(request_body.value("mode", "casual"));
        game_end_request.duration_seconds = request_body.value("duration_seconds", 0);
        game_end_request.started_at = std::chrono::system_clock::now() - std::chrono::seconds(game_end_request.duration_seconds);
        game_end_request.ended_at = std::chrono::system_clock::now();

        // 解析玩家数据
        if (!request_body["players"].is_array() || request_body["players"].empty()) {
            sendErrorResponse(response, 400, "players must be a non-empty array");
            return;
        }

        for (const auto& player_json : request_body["players"]) {
            GameEndRequest::PlayerResult player;
            player.user_id = player_json["user_id"];
            player.result = stringToGameResult(player_json.value("result", "loss"));

            // 解析玩家游戏数据
            auto game_data_json = player_json.value("game_data", json::object());
            player.game_data.user_id = player.user_id;
            player.game_data.rating_before = game_data_json.value("rating_before", 1200);
            player.game_data.games_played = game_data_json.value("games_played", 0);
            player.game_data.win_streak = game_data_json.value("win_streak", 0);
            player.game_data.is_first_win_today = game_data_json.value("is_first_win_today", false);
            player.game_data.games_today = game_data_json.value("games_today", 0);
            player.game_data.tier_level = game_data_json.value("tier_level", 5);

            game_end_request.players.push_back(player);
        }

        // 初始化 GameEndProcessor（如果尚未初始化）
        if (!game_end_processor_) {
            GameEndProcessorConfig processor_config;
            game_end_processor_ = std::make_unique<GameEndProcessor>(repository_, processor_config);
        }

        // 处理游戏结束
        GameEndResponse game_response = game_end_processor_->processGameEnd(game_end_request);

        // 构建响应
        if (game_response.success) {
            json result;
            result["game_id"] = game_response.game_id;
            result["game_type"] = game_response.game_type;
            result["settlements"] = json::array();

            for (const auto& settlement : game_response.settlements) {
                json settlement_json;
                settlement_json["user_id"] = settlement.user_id;
                settlement_json["result"] = gameResultToString(settlement.result);
                settlement_json["rating"] = {
                    {"before", settlement.rating_before},
                    {"after", settlement.rating_after},
                    {"change", settlement.rating_change}
                };
                settlement_json["tier"] = {
                    {"before", settlement.tier_before},
                    {"after", settlement.tier_after},
                    {"changed", settlement.tier_changed},
                    {"promoted", settlement.tier_promoted},
                    {"demoted", settlement.tier_demoted}
                };
                settlement_json["rewards"] = {
                    {"gold", settlement.reward.getTotalGold()},
                    {"gem", settlement.reward.getTotalGem()},
                    {"honor", settlement.reward.getTotalHonor()},
                    {"experience", settlement.reward.experience}
                };
                settlement_json["stats"] = {
                    {"new_win_streak", settlement.new_win_streak},
                    {"new_games_played", settlement.new_games_played}
                };
                if (!settlement.achievements_unlocked.empty()) {
                    settlement_json["achievements_unlocked"] = settlement.achievements_unlocked;
                }

                result["settlements"].push_back(settlement_json);
            }

            sendSuccessResponse(response, result, "Game settlement processed successfully");
            logApiAccess("POST", "/api/v1/settlement", 200, game_end_request.game_id);
        } else {
            sendErrorResponse(response, 400, game_response.error_message);
            logApiAccess("POST", "/api/v1/settlement", 400);
        }

    } catch (const json::exception& e) {
        sendErrorResponse(response, 400, "Invalid JSON: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/settlement", 400);
    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("POST", "/api/v1/settlement", 500);
    }
}

void GameService::handleGetGameSettlement(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto game_id = extractPathParameter(request.getPath(), "game_id");
        if (!game_id.has_value()) {
            sendErrorResponse(response, 400, "Missing game_id parameter");
            return;
        }

        auto settlement = repository_->getGameSettlement(game_id.value());
        if (settlement.has_value()) {
            json result;
            result["settlement_id"] = settlement->settlement_id;
            result["game_id"] = settlement->game_id;
            result["game_type"] = settlement->game_type;
            result["game_mode"] = settlement->game_mode;
            result["duration_seconds"] = settlement->duration_seconds;
            result["player_count"] = settlement->player_count;
            // settlement_data is a JSON string, parse it
            try {
                result["settlement_data"] = json::parse(settlement->settlement_data);
            } catch (...) {
                result["settlement_data"] = settlement->settlement_data;
            }

            sendSuccessResponse(response, result, "Game settlement retrieved successfully");
            logApiAccess("GET", "/api/v1/settlement/" + game_id.value(), 200);
        } else {
            sendErrorResponse(response, 404, "Game settlement not found");
            logApiAccess("GET", "/api/v1/settlement/" + game_id.value(), 404);
        }

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/settlement/*", 500);
    }
}

void GameService::handleGetUserDailyStats(const common::http::HttpRequest& request, common::http::HttpResponse& response) {
    try {
        auto user_id = extractPathParameter(request.getPath(), "user_id");
        if (!user_id.has_value()) {
            sendErrorResponse(response, 400, "Missing user_id parameter");
            return;
        }

        // 获取查询参数
        auto params = request.getParams();
        int game_type_id = 1;  // 默认五子棋
        if (params.count("game_type_id")) {
            try {
                game_type_id = std::stoi(params.at("game_type_id"));
            } catch (...) {}
        }

        std::string date = params.count("date") ? params.at("date") : "";

        auto stats = repository_->getUserDailyStats(user_id.value(), game_type_id, date);
        if (stats.has_value()) {
            json result;
            result["user_id"] = stats->user_id;
            result["game_type_id"] = stats->game_type_id;
            result["stat_date"] = stats->stat_date;
            result["games_played"] = stats->games_played;
            result["games_won"] = stats->games_won;
            result["games_lost"] = stats->games_lost;
            result["games_drawn"] = stats->games_drawn;
            result["first_win_claimed"] = stats->first_win_claimed;
            result["total_playtime_seconds"] = stats->total_playtime_seconds;
            result["total_gold_earned"] = stats->total_gold_earned;
            result["total_honor_earned"] = stats->total_honor_earned;
            result["total_exp_earned"] = stats->total_exp_earned;
            result["rating_change"] = stats->rating_change;
            result["peak_streak"] = stats->peak_streak;

            sendSuccessResponse(response, result, "User daily stats retrieved successfully");
            logApiAccess("GET", "/api/v1/stats/daily/" + user_id.value(), 200, user_id.value());
        } else {
            // 返回空统计
            json result;
            result["user_id"] = user_id.value();
            result["game_type_id"] = game_type_id;
            result["stat_date"] = date.empty() ? "today" : date;
            result["games_played"] = 0;
            result["games_won"] = 0;
            result["games_lost"] = 0;
            result["games_drawn"] = 0;
            result["message"] = "No stats found for this date";

            sendSuccessResponse(response, result, "No stats found");
            logApiAccess("GET", "/api/v1/stats/daily/" + user_id.value(), 200, user_id.value());
        }

    } catch (const std::exception& e) {
        sendErrorResponse(response, 500, "Internal error: " + std::string(e.what()));
        logApiAccess("GET", "/api/v1/stats/daily/*", 500);
    }
}

// 错误响应方法实现
void GameService::sendErrorResponse(common::http::HttpResponse& response, int code, const std::string& message) const {
    try {
        response.setStatus(code);
        response.setHeader("Content-Type", "application/json");
        
        nlohmann::json error_json = {
            {"success", false},
            {"error", {
                {"code", code},
                {"message", message}
            }},
            {"timestamp", getCurrentTimestamp()}
        };
        
        response.setBody(error_json.dump());
        LOG_DEBUG("发送错误响应: code=" + std::to_string(code) + ", message=" + message);
    } catch (const std::exception& e) {
        // 备用错误响应
        response.setStatus(500);
        response.setHeader("Content-Type", "text/plain");
        response.setBody("Internal Server Error");
        LOG_ERROR("Failed to send error response: " + std::string(e.what()));
    }
}


// === 定时任务管理 ===

void GameService::setupBusinessTimers() {
    if (!timer_manager_ || !timer_manager_->isRunning()) {
        LOG_ERROR("GameTimerManager is not available");
        return;
    }
    
    LOG_INFO("🔧 设置游戏数据服务业务定时任务");
    
    // 1. 缓存清理定时器
    if (config_.enable_cache_cleanup) {
        timer_manager_->addPeriodicTimer(
            config_.cache_cleanup_interval_minutes * 60 * 1000, // 转换为毫秒
            [this]() {
                performCacheCleanup();
            }
        );
        LOG_INFO("✅ 缓存清理定时器已启动，间隔: " + std::to_string(config_.cache_cleanup_interval_minutes) + "分钟");
    }
    
    // 2. 数据归档定时器
    if (config_.enable_data_archival) {
        timer_manager_->addPeriodicTimer(
            config_.data_archival_interval_hours * 60 * 60 * 1000, // 转换为毫秒
            [this]() {
                performDataArchival();
            }
        );
        LOG_INFO("✅ 数据归档定时器已启动，间隔: " + std::to_string(config_.data_archival_interval_hours) + "小时");
    }
    
    // 3. 成就系统更新定时器
    if (config_.enable_achievement_system) {
        timer_manager_->addPeriodicTimer(
            config_.achievement_update_interval_minutes * 60 * 1000,
            [this]() {
                updateAchievements();
            }
        );
        LOG_INFO("✅ 成就更新定时器已启动，间隔: " + std::to_string(config_.achievement_update_interval_minutes) + "分钟");
    }
    
    // 4. 排行榜更新定时器
    if (config_.enable_ranking_system) {
        timer_manager_->addPeriodicTimer(
            config_.ranking_update_interval_minutes * 60 * 1000,
            [this]() {
                updateRankings();
            }
        );
        LOG_INFO("✅ 排行榜更新定时器已启动，间隔: " + std::to_string(config_.ranking_update_interval_minutes) + "分钟");
    }
    
    // 5. 库存清理定时器
    if (config_.enable_inventory_system) {
        timer_manager_->addPeriodicTimer(
            config_.inventory_cleanup_interval_hours * 60 * 60 * 1000,
            [this]() {
                cleanupInventories();
            }
        );
        LOG_INFO("✅ 库存清理定时器已启动，间隔: " + std::to_string(config_.inventory_cleanup_interval_hours) + "小时");
    }
    
    // 6. 货币系统审计定时器
    if (config_.enable_currency_system) {
        timer_manager_->addPeriodicTimer(
            config_.currency_audit_interval_hours * 60 * 60 * 1000,
            [this]() {
                auditCurrencySystem();
            }
        );
        LOG_INFO("✅ 货币审计定时器已启动，间隔: " + std::to_string(config_.currency_audit_interval_hours) + "小时");
    }

    // 7. 状态日志定时任务 (原编号8)
    if (config_.enable_status_logging) {
        timer_manager_->addPeriodicTimer(
            config_.status_log_interval_minutes * 60 * 1000,
            [this]() {
                logServiceStatus();
            }
        );
        LOG_INFO("✅ 状态日志定时器已启动，间隔: " + std::to_string(config_.status_log_interval_minutes) + "分钟");
    }
    
    LOG_INFO("🎯 游戏数据服务定时任务设置完成，活跃定时器数: " + std::to_string(timer_manager_->getActiveTimerCount()));
}

// === 定时任务实现 ===

void GameService::performCacheCleanup() {
    try {
        LOG_INFO("🧹 开始执行缓存清理任务");

        if (repository_) {
            // 清理过期的缓存数据
            int cleaned = repository_->cleanupExpiredCache("game_service:*");
            LOG_DEBUG("清理了 " + std::to_string(cleaned) + " 个缓存类型");
        }

        LOG_INFO("✅ 缓存清理任务完成");

    } catch (const std::exception& e) {
        LOG_ERROR("🚨 缓存清理任务异常: " + std::string(e.what()));
    }
}

void GameService::performDataArchival() {
    try {
        LOG_INFO("📁 开始执行数据归档任务");

        if (repository_) {
            // 归档超过30天的游戏记录
            int archived = repository_->archiveOldGameRecords(30);
            LOG_DEBUG("归档了 " + std::to_string(archived) + " 条历史游戏记录");
        }

        LOG_INFO("✅ 数据归档任务完成");

    } catch (const std::exception& e) {
        LOG_ERROR("🚨 数据归档任务异常: " + std::string(e.what()));
    }
}

void GameService::updateAchievements() {
    try {
        LOG_INFO("🏆 开始执行成就系统更新任务");

        if (repository_) {
            // 成就系统更新：检查玩家数据并自动解锁符合条件的成就
            // 例如：根据游戏场次、胜率、积分等解锁对应成就
            // 这里主要依赖业务触发时的成就解锁，定时任务用于批量检查
            LOG_DEBUG("成就系统状态检查完成（成就主要在游戏结算时实时解锁）");
        }

        LOG_INFO("✅ 成就系统更新任务完成");

    } catch (const std::exception& e) {
        LOG_ERROR("🚨 成就系统更新任务异常: " + std::string(e.what()));
    }
}

void GameService::updateRankings() {
    try {
        LOG_INFO("📊 开始执行排行榜更新任务");

        if (repository_) {
            // 重新计算排行榜排名（同步 MySQL 和 Redis 数据）
            int updated = repository_->recalculateRankings();
            LOG_DEBUG("更新了 " + std::to_string(updated) + " 个排名条目");
        }

        LOG_INFO("✅ 排行榜更新任务完成");

    } catch (const std::exception& e) {
        LOG_ERROR("🚨 排行榜更新任务异常: " + std::string(e.what()));
    }
}

void GameService::cleanupInventories() {
    try {
        LOG_INFO("🎒 开始执行库存清理任务");

        if (repository_) {
            // 清理过期或无效的库存项目（数量为0的物品）
            int cleaned = repository_->cleanupExpiredInventoryItems(90);
            LOG_DEBUG("清理了 " + std::to_string(cleaned) + " 个无效库存物品");
        }

        LOG_INFO("✅ 库存清理任务完成");

    } catch (const std::exception& e) {
        LOG_ERROR("🚨 库存清理任务异常: " + std::string(e.what()));
    }
}

void GameService::auditCurrencySystem() {
    try {
        LOG_INFO("💰 开始执行货币系统审计任务");

        if (repository_) {
            // 审计货币系统的完整性和一致性
            int inconsistencies = repository_->auditCurrencyConsistency();
            if (inconsistencies > 0) {
                LOG_WARNING("发现 " + std::to_string(inconsistencies) + " 个货币不一致问题，已记录并修复缓存");
            } else {
                LOG_DEBUG("货币系统审计通过，未发现不一致问题");
            }
        }

        LOG_INFO("✅ 货币系统审计任务完成");

    } catch (const std::exception& e) {
        LOG_ERROR("🚨 货币系统审计任务异常: " + std::string(e.what()));
    }
}

void GameService::logServiceStatus() {
    try {
        LOG_INFO("📋 生成服务状态日志");
        
        // 创建详细的状态报告
        std::ostringstream status_report;
        
        status_report << "\n=== 游戏数据服务状态报告 ===\n";
        
        // 基本信息
        auto now = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        status_report << "🔹 服务ID: " << service_id_ << "\n";
        status_report << "🔹 运行时间: " << uptime << "秒 (" << uptime/60 << "分钟)\n";
        status_report << "🔹 监听地址: " << config_.host << ":" << config_.port << "\n";
        status_report << "🔹 工作线程数: " << config_.worker_threads << "\n";
        
        // 数据库状态
        if (mysql_pool_) {
            status_report << "🔹 MySQL连接池: " << mysql_pool_->getTotalConnections() << "个连接\n";
        }
        if (redis_pool_) {
            status_report << "🔹 Redis连接池: " << redis_pool_->getTotalConnections() << "个连接\n";
        }
        
        // 定时器状态
        if (timer_manager_) {
            status_report << "🔹 活跃定时器: " << timer_manager_->getActiveTimerCount() << "个\n";
            status_report << timer_manager_->getTimerStats() << "\n";
        }
        
        // 功能模块状态
        status_report << "🔹 功能模块状态:\n";
        status_report << "  - 成就系统: " << (config_.enable_achievement_system ? "启用" : "禁用") << "\n";
        status_report << "  - 排行榜系统: " << (config_.enable_ranking_system ? "启用" : "禁用") << "\n";
        status_report << "  - 库存系统: " << (config_.enable_inventory_system ? "启用" : "禁用") << "\n";
        status_report << "  - 货币系统: " << (config_.enable_currency_system ? "启用" : "禁用") << "\n";
        status_report << "  - 缓存清理: " << (config_.enable_cache_cleanup ? "启用" : "禁用") << "\n";
        status_report << "  - 数据归档: " << (config_.enable_data_archival ? "启用" : "禁用") << "\n";
        
        status_report << "=== 状态报告结束 ===";
        
        LOG_INFO(status_report.str());
        
    } catch (const std::exception& e) {
        LOG_ERROR("🚨 生成状态日志异常: " + std::string(e.what()));
    }
}

std::unordered_map<std::string, GameService::CachedUserInfo>
GameService::resolveUserDisplayInfo(const std::vector<std::string>& user_ids) {
    std::unordered_map<std::string, CachedUserInfo> result;
    if (user_ids.empty()) return result;
    if (user_service_url_.empty()) {
        LOG_WARNING("resolveUserDisplayInfo: user_service_url not configured, skipping");
        return result;
    }

    auto now = std::chrono::steady_clock::now();

    // 先从缓存中取
    std::vector<std::string> missing_ids;
    LOG_INFO("resolveUserDisplayInfo: resolving " + std::to_string(user_ids.size()) + " users");
    {
        std::lock_guard<std::mutex> lock(user_info_cache_mutex_);
        for (const auto& uid : user_ids) {
            auto it = user_info_cache_.find(uid);
            if (it != user_info_cache_.end()) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.cached_at).count();
                if (age < USER_INFO_CACHE_TTL_SECONDS) {
                    result[uid] = it->second;
                    continue;
                }
            }
            missing_ids.push_back(uid);
        }
    }

    // 对缓存未命中的，从 user_service 获取
    for (const auto& uid : missing_ids) {
        try {
            std::string url = user_service_url_ + "/api/v1/user/" + uid;
            auto resp = http_client_.get(url, 3000);
            if (resp.success && resp.status_code == 200) {
                auto json_resp = nlohmann::json::parse(resp.body);

                // user_service 返回格式可能是 {data: {...}} 或直接 {...}
                nlohmann::json user_data;
                if (json_resp.contains("data") && json_resp["data"].is_object()) {
                    user_data = json_resp["data"];
                } else {
                    user_data = json_resp;
                }

                CachedUserInfo info;
                info.display_name = user_data.value("username", "");
                info.avatar_url = user_data.value("avatar_url", "");
                info.cached_at = now;

                LOG_INFO("resolveUserDisplayInfo: user " + uid + " -> display_name=" + info.display_name);
                result[uid] = info;
                {
                    std::lock_guard<std::mutex> lock(user_info_cache_mutex_);
                    user_info_cache_[uid] = info;
                }
            } else {
                LOG_WARNING("resolveUserDisplayInfo: HTTP failed for " + uid +
                    ", status=" + std::to_string(resp.status_code));
            }
        } catch (const std::exception& e) {
            LOG_WARNING("resolveUserDisplayInfo: exception for " + uid + ": " + e.what());
        }
    }

    return result;
}

} // namespace game_service
} // namespace core_services
