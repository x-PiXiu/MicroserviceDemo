#pragma once

#include "common/http/http_client.h"
#include "circuit_breaker.h"
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>

/**
 * @file service_client.h
 * @brief 服务间通信客户端定义
 * @details 提供用户服务和游戏数据服务的客户端接口，支持缓存和重试
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

namespace game_services {
namespace gomoku {

/**
 * @brief 服务客户端配置
 */
struct ServiceClientConfig {
    std::string service_name;                   // 服务名称
    std::string base_url;                       // 服务基础URL
    int timeout_seconds = 5;                    // 请求超时时间
    int max_retries = 3;                        // 最大重试次数
    int cache_ttl_seconds = 300;                // 缓存TTL (5分钟)
    bool enable_cache = true;                   // 是否启用缓存
    bool enable_circuit_breaker = true;         // 是否启用熔断器
    std::string user_agent = "GomokuService/2.0"; // User-Agent
    
    // 熔断器配置
    CircuitBreakerConfig circuit_breaker_config;
    
    /**
     * @brief 验证配置有效性
     */
    std::string validate() const;
};

/**
 * @brief 用户信息结构体 (简化版)
 */
struct UserInfo {
    std::string user_id;
    std::string username;
    std::string nickname;
    std::string avatar_url;
    std::string status;
    std::string online_status;
    
    /**
     * @brief 从JSON构造
     */
    static UserInfo fromJson(const nlohmann::json& json);
    
    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const;
};

/**
 * @brief 用户游戏档案结构体 (简化版)
 */
struct UserGameProfile {
    std::string user_id;
    int game_type_id = 1;            // 默认五子棋
    int level = 1;                   // 默认等级 1
    int64_t experience = 0;          // 默认经验 0
    int total_games = 0;             // 默认总场次 0
    int wins = 0;
    int losses = 0;
    int draws = 0;
    int current_rating = 1200;       // 默认 ELO 评分 1200
    int peak_rating = 1200;          // 默认最高评分 1200
    int current_win_streak = 0;
    int best_win_streak = 0;
    
    /**
     * @brief 从JSON构造
     */
    static UserGameProfile fromJson(const nlohmann::json& json);
    
    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const;
    
    /**
     * @brief 计算胜率
     */
    double getWinRate() const;
};

/**
 * @brief 用户货币信息结构体
 */
struct UserCurrency {
    std::string user_id;
    int currency_type_id;
    int64_t balance;
    int64_t total_earned;
    int64_t total_spent;
    
    /**
     * @brief 从JSON构造
     */
    static UserCurrency fromJson(const nlohmann::json& json);
    
    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const;
};

/**
 * @brief 游戏结果数据
 */
struct GameResultData {
    std::string user_id;
    int game_type_id = 1;                       // 五子棋游戏类型ID
    std::string opponent_user_id;               // 对手用户ID
    std::string session_id;
    std::string result;                         // "win", "loss", "draw"
    int64_t score;
    int duration_seconds;
    int moves_count;
    int rating_change = 0;
    nlohmann::json game_data = nlohmann::json::object();
    
    /**
     * @brief 从JSON构造
     */
    static GameResultData fromJson(const nlohmann::json& json);
    
    /**
     * @brief 转换为JSON
     */
    nlohmann::json toJson() const;
    
    /**
     * @brief 验证数据有效性
     */
    std::string validate() const;
};

/**
 * @brief 用户服务客户端
 * @details 提供与用户服务通信的接口
 */
class UserServiceClient {
public:
    /**
     * @brief 构造函数
     * @param config 客户端配置
     */
    explicit UserServiceClient(const ServiceClientConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~UserServiceClient();
    
    /**
     * @brief 获取用户基础信息
     * @param user_id 用户ID
     * @return 用户信息，不存在或出错返回空
     */
    std::optional<UserInfo> getUserInfo(const std::string& user_id);
    
    /**
     * @brief 批量获取用户信息
     * @param user_ids 用户ID列表
     * @return 用户信息列表
     */
    std::vector<UserInfo> getBatchUserInfo(const std::vector<std::string>& user_ids);
    
    /**
     * @brief 检查用户是否存在
     * @param user_id 用户ID
     * @return 存在返回true
     */
    bool userExists(const std::string& user_id);
    
    /**
     * @brief 更新用户在线状态
     * @param user_id 用户ID
     * @param online_status 在线状态
     * @return 操作是否成功
     */
    bool updateOnlineStatus(const std::string& user_id, const std::string& online_status);
    
    /**
     * @brief 获取用户好友列表
     * @param user_id 用户ID
     * @return 好友用户ID列表
     */
    std::vector<std::string> getUserFriends(const std::string& user_id);
    
    /**
     * @brief 搜索用户
     * @param search_term 搜索关键词
     * @param limit 结果数量限制
     * @return 用户信息列表
     */
    std::vector<UserInfo> searchUsers(const std::string& search_term, int limit = 20);
    
    /**
     * @brief 清除用户缓存
     * @param user_id 用户ID
     */
    void clearUserCache(const std::string& user_id);
    
    /**
     * @brief 获取客户端统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getClientStatistics();

private:
    ServiceClientConfig config_;
    std::unique_ptr<common::http::HttpClient> http_client_;
    
    // 缓存相关
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, std::pair<UserInfo, std::chrono::steady_clock::time_point>> user_cache_;
    
    // 熔断器
    std::unique_ptr<CircuitBreaker<nlohmann::json>> circuit_breaker_;
    
    // 辅助方法
    std::string buildUrl(const std::string& endpoint) const;
    std::optional<nlohmann::json> makeRequest(const std::string& method, 
                                             const std::string& endpoint,
                                             const nlohmann::json& body = nlohmann::json::object());
    std::optional<nlohmann::json> makeRequestWithCircuitBreaker(const std::string& method, 
                                                               const std::string& endpoint,
                                                               const nlohmann::json& body = nlohmann::json::object());
    bool isCacheValid(const std::chrono::steady_clock::time_point& cache_time) const;
    std::string getUserCacheKey(const std::string& user_id) const;
};

/**
 * @brief 玩家游戏统计数据（用于结算请求）
 */
struct PlayerGameStats {
    int current_rating = 1200;
    int games_played = 0;
    int win_streak = 0;
    int tier_level = 5;
    bool is_first_win_today = false;
    int games_today = 0;

    static PlayerGameStats fromJson(const nlohmann::json& json) {
        PlayerGameStats stats;
        stats.current_rating = json.value("rating", json.value("current_rating", 1200));
        stats.games_played = json.value("totalGames", json.value("total_games", 0));
        stats.win_streak = json.value("currentStreak", json.value("current_win_streak", 0));
        stats.tier_level = json.value("tierLevel", json.value("tier_level", 5));
        stats.is_first_win_today = json.value("isFirstWinToday", json.value("is_first_win_today", false));
        stats.games_today = json.value("gamesToday", json.value("games_today", 0));
        return stats;
    }
};

/**
 * @brief 结算响应结构体
 */
struct SettlementResponse {
    bool success = false;
    std::string error_message;
    std::string game_id;
    std::vector<nlohmann::json> settlements;

    static SettlementResponse fromJson(const nlohmann::json& json) {
        SettlementResponse resp;
        resp.success = json.value("success", false);
        resp.error_message = json.value("error_message", json.value("error", ""));
        resp.game_id = json.value("game_id", "");
        if (json.contains("settlements") && json["settlements"].is_array()) {
            resp.settlements = json["settlements"].get<std::vector<nlohmann::json>>();
        } else if (json.contains("data") && json["data"].contains("settlements")) {
            resp.settlements = json["data"]["settlements"].get<std::vector<nlohmann::json>>();
        }
        return resp;
    }
};

// 前向声明
struct GameEndContext;

/**
 * @brief 游戏数据服务客户端
 * @details 提供与游戏数据服务通信的接口
 */
class GameDataServiceClient {
public:
    /**
     * @brief 构造函数
     * @param config 客户端配置
     */
    explicit GameDataServiceClient(const ServiceClientConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~GameDataServiceClient();
    
    /**
     * @brief 获取用户游戏档案
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID (默认为五子棋)
     * @return 游戏档案，不存在或出错返回空
     */
    std::optional<UserGameProfile> getUserGameProfile(const std::string& user_id, int game_type_id = 1);
    
    /**
     * @brief 创建用户游戏档案
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @return 操作是否成功
     */
    bool createUserGameProfile(const std::string& user_id, int game_type_id = 1);
    
    /**
     * @brief 记录游戏结果
     * @param game_result 游戏结果数据
     * @return 操作是否成功
     */
    bool recordGameResult(const GameResultData& game_result);
    
    /**
     * @brief 获取用户货币信息
     * @param user_id 用户ID
     * @param currency_type_id 货币类型ID (1=金币, 2=宝石)
     * @return 货币信息，不存在或出错返回空
     */
    std::optional<UserCurrency> getUserCurrency(const std::string& user_id, int currency_type_id = 1);
    
    /**
     * @brief 添加货币奖励
     * @param user_id 用户ID
     * @param currency_type_id 货币类型ID
     * @param amount 数量
     * @param reason 原因
     * @return 操作是否成功
     */
    bool addCurrencyReward(const std::string& user_id, 
                          int currency_type_id, 
                          int64_t amount, 
                          const std::string& reason = "game_reward");
    
    /**
     * @brief 更新成就进度
     * @param user_id 用户ID
     * @param achievement_id 成就ID
     * @param progress_increment 进度增量
     * @return 操作是否成功
     */
    bool updateAchievementProgress(const std::string& user_id,
                                  const std::string& achievement_id,
                                  int progress_increment = 1);
    
    /**
     * @brief 获取用户统计信息
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @return 统计信息JSON
     */
    nlohmann::json getUserGameStats(const std::string& user_id, int game_type_id = 1);
    
    /**
     * @brief 清除用户游戏档案缓存
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     */
    void clearGameProfileCache(const std::string& user_id, int game_type_id = 1);

    /**
     * @brief 获取客户端统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getClientStatistics();

    // ==================== 游戏结算 API ====================

    /**
     * @brief 提交游戏结算
     * @param context 游戏结束上下文（包含所有结算所需数据）
     * @return 结算响应，失败返回空
     */
    std::optional<SettlementResponse> submitGameSettlement(const GameEndContext& context);

    /**
     * @brief 获取玩家游戏统计（用于构建结算请求）
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID（默认1=五子棋）
     * @return 玩家统计数据，失败返回空
     */
    std::optional<PlayerGameStats> getPlayerGameStats(const std::string& user_id, int game_type_id = 1);

private:
    ServiceClientConfig config_;
    std::unique_ptr<common::http::HttpClient> http_client_;

    // 缓存相关
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, std::pair<UserGameProfile, std::chrono::steady_clock::time_point>> profile_cache_;
    std::unordered_map<std::string, std::pair<UserCurrency, std::chrono::steady_clock::time_point>> currency_cache_;

    // 熔断器
    std::unique_ptr<CircuitBreaker<nlohmann::json>> circuit_breaker_;

    // 辅助方法
    std::string buildUrl(const std::string& endpoint) const;
    std::optional<nlohmann::json> makeRequest(const std::string& method,
                                             const std::string& endpoint,
                                             const nlohmann::json& body = nlohmann::json::object());
    std::optional<nlohmann::json> makeRequestWithCircuitBreaker(const std::string& method,
                                                               const std::string& endpoint,
                                                               const nlohmann::json& body = nlohmann::json::object());
    bool isCacheValid(const std::chrono::steady_clock::time_point& cache_time) const;
    std::string getProfileCacheKey(const std::string& user_id, int game_type_id) const;
    std::string getCurrencyCacheKey(const std::string& user_id, int currency_type_id) const;
};

/**
 * @brief 服务客户端管理器
 * @details 统一管理所有服务客户端实例
 */
class ServiceClientManager {
public:
    /**
     * @brief 构造函数
     */
    ServiceClientManager();
    
    /**
     * @brief 析构函数
     */
    ~ServiceClientManager();
    
    /**
     * @brief 初始化客户端
     * @param user_service_config 用户服务配置
     * @param game_data_service_config 游戏数据服务配置
     * @return 初始化是否成功
     */
    bool initialize(const ServiceClientConfig& user_service_config,
                   const ServiceClientConfig& game_data_service_config);
    
    /**
     * @brief 获取用户服务客户端
     * @return 用户服务客户端指针
     */
    UserServiceClient* getUserServiceClient();
    
    /**
     * @brief 获取游戏数据服务客户端
     * @return 游戏数据服务客户端指针
     */
    GameDataServiceClient* getGameDataServiceClient();
    
    /**
     * @brief 清除所有缓存
     */
    void clearAllCaches();
    
    /**
     * @brief 检查服务健康状态
     * @return 健康状态信息
     */
    nlohmann::json checkServicesHealth();
    
    /**
     * @brief 获取单例实例
     * @return 服务客户端管理器引用
     */
    static ServiceClientManager& getInstance();
    
    /**
     * @brief 获取统计信息
     * @return 统计信息JSON
     */
    nlohmann::json getStatistics();
    
    /**
     * @brief 获取健康状态
     * @return 健康状态JSON
     */
    nlohmann::json getHealthStatus();

private:
    std::unique_ptr<UserServiceClient> user_service_client_;
    std::unique_ptr<GameDataServiceClient> game_data_service_client_;
    bool initialized_ = false;
    
    // 单例模式相关
    static ServiceClientManager* instance_;
    static std::mutex instance_mutex_;
};

} // namespace gomoku
} // namespace game_services


