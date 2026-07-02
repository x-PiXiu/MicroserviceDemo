//
// Created by Microservice Team
// 游戏数据服务主类 - 简洁稳定架构
//

#ifndef GAME_SERVICE_H
#define GAME_SERVICE_H

#include "game_repository.h"
#include "game_models.h"
#include "game_timer_manager.h"
#include "game_end_processor.h"
#include "achievement_manager.h"
#include "leaderboard_manager.h"
#include <common/config/config_manager.h>
#include <nlohmann/json.hpp>
#include <common/database/mysql_pool.h>
#include <common/database/redis_pool.h>
#include <common/http/http_server.h>
#include <common/http/http_request.h>
#include <common/http/http_response.h>
#include <common/http/http_client.h>
#include <common/thread_pool/thread_pool.h>  // 🔧 统一线程池支持
#include <memory>
#include <atomic>
#include <string>
#include <mutex>
#include <unordered_map>

namespace core_services {
namespace game_service {

/**
 * 游戏数据服务配置
 */
struct GameServiceConfig {
    // === 服务基本信息 ===
    std::string service_name = "game_data_service";
    std::string service_version = "1.0.0";
    std::string host = "0.0.0.0";
    int port = 8084;
    int worker_threads = 4;
    
    // 使用预定义的配置结构体
    common::config::NetworkConfig network_config;
    common::config::DatabaseConfig database_config;
    common::config::RedisConfig redis_config;
    common::config::ThreadPoolConfig thread_pool_config;
    
    // === 功能开关 ===
    bool enable_performance_monitoring = true;
    bool enable_cache_cleanup = true;
    bool enable_data_archival = true;
    bool enable_achievement_system = true;
    bool enable_ranking_system = true;
    bool enable_inventory_system = true;
    bool enable_currency_system = true;
    bool enable_status_logging = true;

    // === 定时任务配置 ===
    int cache_cleanup_interval_minutes = 30;        // 缓存清理间隔（分钟）
    int data_archival_interval_hours = 24;          // 数据归档间隔（小时）
    int achievement_update_interval_minutes = 15;   // 成就更新间隔（分钟）
    int ranking_update_interval_minutes = 10;       // 排行榜更新间隔（分钟）
    int inventory_cleanup_interval_hours = 6;       // 库存清理间隔（小时）
    int currency_audit_interval_hours = 12;         // 货币审计间隔（小时）
    int status_log_interval_minutes = 5;            // 状态日志间隔（分钟）
    
    // === 性能配置 ===
    int max_concurrent_players = 1000;              // 最大并发玩家数
    int cache_expire_seconds = 3600;                // 缓存过期时间（秒）
    int database_connection_timeout_seconds = 10;   // 数据库连接超时（秒）
    int redis_connection_timeout_seconds = 5;       // Redis连接超时（秒）

    // === 业务规则配置 ===
    int max_achievements_per_player = 500;          // 每个玩家最大成就数
    int max_inventory_items = 200;                  // 库存最大物品数
    double currency_transfer_daily_limit = 10000.0; // 每日货币转账限额
    int ranking_top_players_count = 100;            // 排行榜显示前N名玩家

    /**
     * 从配置管理器加载配置
     */
    static GameServiceConfig fromConfigManager();
    
    /**
     * 从配置文件加载配置
     */
    static GameServiceConfig fromConfigFile(const std::string& config_file_path);
    
    /**
     * 验证配置
     */
    bool validate() const;
};

/**
 * 游戏数据服务主类
 * 
 * 设计原则：
 * - 极简架构，避免复杂组件
 * - 无定时器，无后台任务
 * - 纯HTTP服务，响应式处理
 * - RAII资源管理
 * - 单一职责，专注数据服务
 */
class GameService {
public:
    /**
     * 构造函数
     * @param config 服务配置
     */
    explicit GameService(const GameServiceConfig& config);
    
    ~GameService();

    // 禁用拷贝构造和赋值
    GameService(const GameService&) = delete;
    GameService& operator=(const GameService&) = delete;

    /**
     * 初始化服务
     * @return 是否成功
     */
    bool initialize();
    
    /**
     * 启动服务
     * @return 是否成功
     */
    bool start();
    
    /**
     * 停止服务
     */
    void stop();
    
    /**
     * 设置业务定时任务
     */
    void setupBusinessTimers();

    /**
     * 优雅关闭服务
     */
    void shutdown();

    /**
     * 检查服务是否正在运行
     * @return 是否运行中
     */
    bool isRunning() const { return running_.load(); }

private:
    // === 定时任务实现 ===
    void performCacheCleanup();      // 缓存清理
    void performDataArchival();      // 数据归档  
    void updateAchievements();       // 成就更新
    void updateRankings();           // 排行榜更新
    void cleanupInventories();       // 库存清理
    void auditCurrencySystem();      // 货币审计
    void logServiceStatus();         // 状态日志

    /**
     * 获取服务状态信息
     * @return 状态JSON
     */
    std::string getStatusInfo() const;

private:
    std::shared_ptr<common::thread_pool::ThreadPool> shared_thread_pool_;
    
    // === 核心组件 ===
    GameServiceConfig config_;
    std::shared_ptr<common::database::MySQLPool> mysql_pool_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    std::shared_ptr<GameRepository> repository_;
    std::shared_ptr<common::http::HttpServer> http_server_;

    // === 游戏结算处理器 ===
    std::unique_ptr<GameEndProcessor> game_end_processor_;

    // === 成就和排行榜管理器 ===
    std::shared_ptr<AchievementManager> achievement_manager_;
    std::shared_ptr<LeaderboardManager> leaderboard_manager_;

    // === 独立定时器管理器（避免EventLoop timerfd问题） ===
    std::unique_ptr<GameTimerManager> timer_manager_;

    // === 用户信息缓存（从 user_service 获取） ===
    struct CachedUserInfo {
        std::string display_name;
        std::string avatar_url;
        std::chrono::steady_clock::time_point cached_at;
    };
    std::mutex user_info_cache_mutex_;
    std::unordered_map<std::string, CachedUserInfo> user_info_cache_;
    static constexpr int USER_INFO_CACHE_TTL_SECONDS = 300;
    std::string user_service_url_;
    common::http::HttpClient http_client_;
    
    // === 状态管理 ===
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::string service_id_;
    std::chrono::system_clock::time_point start_time_;

    // HTTP服务器线程管理
    std::thread http_server_thread_;                    // HTTP服务器线程
    std::atomic<bool> http_server_running_{false};      // HTTP服务器运行状态
    
    // === 初始化方法 ===
    
    /**
     * 初始化数据库连接池
     * @return 是否成功
     */
    bool initializeDatabasePools();
    
    /**
     * 初始化数据访问层
     * @return 是否成功
     */
    bool initializeRepository();
    
    /**
     * 初始化HTTP服务器
     * @return 是否成功
     */
    bool initializeHttpServer();

    /**
     * 设置 CORS 中间件
     */
    void setupCorsMiddleware();

    /**
     * 设置HTTP路由
     */
    void setupHttpRoutes();
    
    // === HTTP API 处理器 ===
    
    // 服务发现和健康检查
    void handleServiceEndpoints(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleHealthCheck(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleServiceStatus(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // 用户档案管理
    void handleCreateUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleDeleteUserProfile(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserProfiles(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // 成就系统
    void handleUnlockAchievement(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserAchievements(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // 库存管理
    void handleAddInventoryItem(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserInventory(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUseInventoryItem(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // 货币系统
    void handleGetUserCurrency(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleUpdateUserCurrency(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetAllUserCurrencies(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // 排行榜
    void handleUpdateLeaderboard(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetLeaderboard(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserRank(const common::http::HttpRequest& request, common::http::HttpResponse& response);

    // 游戏结算
    void handleGameEnd(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetGameSettlement(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    void handleGetUserDailyStats(const common::http::HttpRequest& request, common::http::HttpResponse& response);
    
    // === 辅助方法 ===
    
    /**
     * 解析请求参数
     * @param request HTTP请求
     * @return 分页参数
     */
    PaginationParams parsePaginationParams(const common::http::HttpRequest& request) const;
    
    /**
     * 解析查询过滤器
     * @param request HTTP请求
     * @return 查询过滤器
     */
    QueryFilter parseQueryFilter(const common::http::HttpRequest& request) const;
    
    /**
     * 发送成功响应
     * @param response HTTP响应
     * @param data 响应数据
     * @param message 响应消息
     */
    template<typename T>
    void sendSuccessResponse(common::http::HttpResponse& response, const T& data, const std::string& message = "Success") const;
    
    /**
     * 发送错误响应
     * @param response HTTP响应
     * @param code 错误代码
     * @param message 错误消息
     */
    void sendErrorResponse(common::http::HttpResponse& response, int code, const std::string& message) const;
    
    /**
     * 验证请求参数
     * @param required_fields 必需字段
     * @param request_body 请求体JSON
     * @return 验证结果和缺失字段
     */
    std::pair<bool, std::string> validateRequiredFields(
        const std::vector<std::string>& required_fields,
        const nlohmann::json& request_body
    ) const;
    
    /**
     * 提取路径参数
     * @param path HTTP路径
     * @param param_name 参数名称
     * @return 参数值（可能为空）
     */
    std::optional<std::string> extractPathParameter(const std::string& path, const std::string& param_name) const;
    
    /**
     * 记录API访问日志
     * @param method HTTP方法
     * @param path 请求路径
     * @param status_code 状态码
     * @param user_id 用户ID（可选）
     */
    void logApiAccess(const std::string& method, const std::string& path, int status_code, const std::string& user_id = "") const;
    
    /**
     * 生成服务ID
     * @return 唯一的服务ID
     */
    std::string generateServiceId() const;

    /**
     * 获取当前时间戳
     * @return ISO8601格式的时间戳字符串
     */
    std::string getCurrentTimestamp() const;

    /**
     * 从 user_service 批量获取用户显示名和头像
     * @param user_ids 用户ID列表
     * @return user_id -> {display_name, avatar_url} 映射
     */
    std::unordered_map<std::string, CachedUserInfo> resolveUserDisplayInfo(
        const std::vector<std::string>& user_ids);
};

// === 模板方法实现 ===

template<typename T>
void GameService::sendSuccessResponse(common::http::HttpResponse& response, const T& data, const std::string& message) const {
    nlohmann::json success_response;
    success_response["success"] = true;
    success_response["message"] = message;
    success_response["service"] = config_.service_name;
    success_response["timestamp"] = getCurrentTimestamp();
    
    // 简单处理：将数据转换为JSON字符串再解析
    // 这样可以处理有toJson()方法的自定义类型
    try {
        if constexpr (std::is_same_v<T, std::string>) {
            success_response["data"] = data;
        } else if constexpr (std::is_same_v<T, nlohmann::json>) {
            success_response["data"] = data;
        } else {
            // 默认情况：假设类型有toJson()方法
            success_response["data"] = nlohmann::json::parse(data.toJson());
        }
    } catch (const std::exception& e) {
        // 如果序列化失败，提供错误信息
        success_response["data"] = "Serialization error: " + std::string(e.what());
    }

    response.setStatus(200);
    response.setHeader("Content-Type", "application/json");
    response.setBody(success_response.dump(2));
}

} // namespace game_service
} // namespace core_services

#endif // GAME_SERVICE_H
