/**
 * @file service_client.cpp
 * @brief 服务间通信客户端实现
 * @details 提供用户服务和游戏数据服务的客户端接口，支持缓存和重试
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

#include "../include/service_client.h"
#include "../include/gomoku_types.h"
#include "common/logger/logger.h"
#include <regex>
#include <sstream>

namespace game_services {
namespace gomoku {

// ==================== ServiceClientConfig 实现 ====================

std::string ServiceClientConfig::validate() const {
    if (service_name.empty()) {
        return "服务名称不能为空";
    }
    
    if (base_url.empty()) {
        return "服务基础URL不能为空";
    }
    
    if (timeout_seconds <= 0) {
        return "超时时间必须大于0";
    }
    
    if (max_retries < 0) {
        return "最大重试次数不能为负数";
    }
    
    if (cache_ttl_seconds < 0) {
        return "缓存TTL不能为负数";
    }
    
    return ""; // 验证通过
}

// ==================== UserInfo 实现 ====================

UserInfo UserInfo::fromJson(const nlohmann::json& json) {
    UserInfo info;
    
    if (json.contains("user_id")) {
        info.user_id = json["user_id"].get<std::string>();
    }
    
    if (json.contains("username")) {
        info.username = json["username"].get<std::string>();
    }
    
    if (json.contains("nickname")) {
        info.nickname = json["nickname"].get<std::string>();
    }
    
    if (json.contains("avatar_url")) {
        info.avatar_url = json["avatar_url"].get<std::string>();
    }
    
    if (json.contains("status")) {
        info.status = json["status"].get<std::string>();
    }
    
    if (json.contains("online_status")) {
        info.online_status = json["online_status"].get<std::string>();
    }
    
    return info;
}

nlohmann::json UserInfo::toJson() const {
    nlohmann::json json;
    json["user_id"] = user_id;
    json["username"] = username;
    json["nickname"] = nickname;
    json["avatar_url"] = avatar_url;
    json["status"] = status;
    json["online_status"] = online_status;
    
    return json;
}

// ==================== UserGameProfile 实现 ====================

UserGameProfile UserGameProfile::fromJson(const nlohmann::json& json) {
    UserGameProfile profile;  // 使用结构体的默认值

    profile.user_id = json.value("user_id", "");
    profile.game_type_id = json.value("game_type_id", 1);
    profile.level = json.value("level", 1);
    profile.experience = json.value("experience", json.value("experience_points", (int64_t)0));
    profile.total_games = json.value("total_games", json.value("totalGames", 0));
    profile.wins = json.value("wins", 0);
    profile.losses = json.value("losses", 0);
    profile.draws = json.value("draws", 0);
    profile.current_rating = json.value("current_rating", json.value("rating", 1200));
    profile.peak_rating = json.value("peak_rating", json.value("peakRating", 1200));
    profile.current_win_streak = json.value("current_win_streak", json.value("currentWinStreak", 0));
    profile.best_win_streak = json.value("best_win_streak", json.value("bestWinStreak", 0));

    
    return profile;
}

nlohmann::json UserGameProfile::toJson() const {
    nlohmann::json json;
    json["user_id"] = user_id;
    json["game_type_id"] = game_type_id;
    json["level"] = level;
    json["experience"] = experience;
    json["total_games"] = total_games;
    json["wins"] = wins;
    json["losses"] = losses;
    json["draws"] = draws;
    json["current_rating"] = current_rating;
    json["peak_rating"] = peak_rating;
    json["current_win_streak"] = current_win_streak;
    json["best_win_streak"] = best_win_streak;
    
    return json;
}

double UserGameProfile::getWinRate() const {
    if (total_games == 0) {
        return 0.0;
    }
    return (double)wins / total_games * 100.0;
}

// ==================== GameResultData 实现 ====================

GameResultData GameResultData::fromJson(const nlohmann::json& json) {
    GameResultData data;
    
    if (json.contains("user_id")) {
        data.user_id = json["user_id"].get<std::string>();
    }
    
    if (json.contains("game_type_id")) {
        data.game_type_id = json["game_type_id"].get<int>();
    }
    
    if (json.contains("opponent_user_id")) {
        data.opponent_user_id = json["opponent_user_id"].get<std::string>();
    }
    
    if (json.contains("result")) {
        data.result = json["result"].get<std::string>();
    }
    
    if (json.contains("score")) {
        data.score = json["score"].get<int>();
    }
    
    if (json.contains("duration_seconds")) {
        data.duration_seconds = json["duration_seconds"].get<int>();
    }
    
    if (json.contains("game_data")) {
        data.game_data = json.value("game_data", nlohmann::json::object());
    }
    
    return data;
}

nlohmann::json GameResultData::toJson() const {
    nlohmann::json json;
    json["user_id"] = user_id;
    json["game_type_id"] = game_type_id;
    json["opponent_user_id"] = opponent_user_id;
    json["result"] = result;
    json["score"] = score;
    json["duration_seconds"] = duration_seconds;
    json["game_data"] = game_data;
    
    return json;
}

// ==================== UserServiceClient 实现 ====================

UserServiceClient::UserServiceClient(const ServiceClientConfig& config) : config_(config) {
    http_client_ = std::make_unique<common::http::HttpClient>();
    
    // 初始化熔断器
    if (config_.enable_circuit_breaker) {
        circuit_breaker_ = std::make_unique<CircuitBreaker<nlohmann::json>>(config_.circuit_breaker_config);
        LOG_INFO("用户服务客户端初始化完成，熔断器已启用: " + config_.service_name);
    } else {
        LOG_INFO("用户服务客户端初始化完成，熔断器已禁用: " + config_.service_name);
    }
}

UserServiceClient::~UserServiceClient() {
    LOG_INFO("用户服务客户端销毁: " + config_.service_name);
}

std::optional<UserInfo> UserServiceClient::getUserInfo(const std::string& user_id) {
    if (user_id.empty()) {
        LOG_WARNING("用户ID为空，无法获取用户信息");
        return std::nullopt;
    }
    
    try {
        // 实现实际的HTTP请求
        std::string endpoint = "/api/v1/user/" + user_id;
        
        LOG_DEBUG("获取用户信息: " + user_id + " from " + config_.base_url + endpoint);
        
        // 🔧 修复：实现真正的HTTP客户端调用
        std::string url = config_.base_url + endpoint;
        
        try {
            // 构造HTTP请求头
            std::unordered_map<std::string, std::string> headers = {
                {"Content-Type", "application/json"},
                {"User-Agent", "GomokuService/2.0"},
                {"X-Service-Name", "gomoku-service"}
            };
            
            // 执行HTTP GET请求
            auto response = http_client_->get(url, headers, config_.timeout_seconds * 1000);
            
            if (response.status_code == 200) {
                // 解析JSON响应
                auto json_response = nlohmann::json::parse(response.body);
                
                if (json_response.contains("data") && json_response["data"].is_object()) {
                    auto user_data = json_response["data"];
                    UserInfo info = UserInfo::fromJson(user_data);
                    LOG_INFO("用户信息获取成功: " + info.username);
                    return info;
                } else {
                    LOG_WARNING("用户服务响应格式异常");
                    return std::nullopt;
                }
            } else if (response.status_code == 404) {
                LOG_WARNING("用户不存在: " + user_id);
                return std::nullopt;
            } else {
                LOG_ERROR("用户服务调用失败，状态码: " + std::to_string(response.status_code));
                return std::nullopt;
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR("解析用户服务响应JSON失败: " + std::string(e.what()));
            return std::nullopt;
        } catch (const std::exception& e) {
            LOG_ERROR("HTTP请求异常: " + std::string(e.what()));
            // 🔧 降级策略：返回基本用户信息
            UserInfo fallback_info;
            fallback_info.user_id = user_id;
            fallback_info.username = "User_" + user_id.substr(4, 8); // 从usr_前缀后取8位
            fallback_info.status = "unknown";
            fallback_info.online_status = "offline";
            LOG_WARNING("降级返回基本用户信息: " + fallback_info.username);
            return fallback_info;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户信息失败: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<nlohmann::json> UserServiceClient::makeRequestWithCircuitBreaker(
    const std::string& method, 
    const std::string& endpoint,
    const nlohmann::json& body) {
    
    if (!circuit_breaker_) {
        // 熔断器未启用，直接调用原方法
        return makeRequest(method, endpoint, body);
    }
    
    // 使用熔断器执行请求
    auto result = circuit_breaker_->execute([this, &method, &endpoint, &body]() -> nlohmann::json {
        auto response = makeRequest(method, endpoint, body);
        if (!response) {
            throw std::runtime_error("Request failed");
        }
        return *response;
    });
    
    if (!result) {
        LOG_WARNING("熔断器拒绝请求或请求失败: " + method + " " + endpoint);
        return std::nullopt;
    }
    
    return *result;
}

bool UserServiceClient::updateOnlineStatus(const std::string& user_id, const std::string& status) {
    if (user_id.empty() || status.empty()) {
        LOG_WARNING("用户ID或状态为空，无法更新在线状态");
        return false;
    }
    
    // 🔧 修复：检查是否为临时ID，避免无效请求
    if (user_id.find("temp_") == 0) {
        LOG_DEBUG("跳过临时用户在线状态更新: " + user_id);
        return true; // 返回true避免错误日志
    }
    
    try {
        // 🔧 修复：实现真正的HTTP PUT请求
        std::string endpoint = "/api/v1/user/" + user_id + "/online-status";
        std::string url = config_.base_url + endpoint;
        
        // 构造请求体
        nlohmann::json request_body = {
            {"online_status", status},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "GomokuService/2.0"},
            {"X-Service-Name", "gomoku-service"}
        };
        
        auto response = http_client_->put(url, request_body.dump(), headers, config_.timeout_seconds * 1000);
        
        if (response.status_code == 200 || response.status_code == 204) {
            LOG_DEBUG("用户在线状态更新成功: " + user_id + " -> " + status);
            return true;
        } else if (response.status_code == 0) {
            // 🔧 修复：连接失败时降级处理，避免重复错误日志
            LOG_DEBUG("用户服务连接失败，跳过在线状态更新: " + user_id);
            return false;
        } else {
            LOG_WARNING("用户在线状态更新失败，状态码: " + std::to_string(response.status_code) + 
                       ", 用户: " + user_id);
            return false;
        }
        
    } catch (const std::exception& e) {
        // 🔧 修复：降级错误日志级别，避免日志轰炸
        LOG_DEBUG("更新用户在线状态失败: " + std::string(e.what()) + ", 用户: " + user_id);
        return false;
    }
}

void UserServiceClient::clearUserCache(const std::string& user_id) {
    // 实现用户缓存清理
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    if (user_id.empty()) {
        // 清除所有用户缓存
        user_cache_.clear();
        LOG_INFO("已清除所有用户缓存");
    } else {
        // 清除指定用户的缓存
        auto it = user_cache_.find(user_id);
        if (it != user_cache_.end()) {
            user_cache_.erase(it);
            LOG_DEBUG("已清除用户缓存: " + user_id);
        } else {
            LOG_DEBUG("用户缓存不存在: " + user_id);
        }
    }
}

nlohmann::json UserServiceClient::getClientStatistics() {
    nlohmann::json stats;
    stats["service_name"] = config_.service_name;
    stats["base_url"] = config_.base_url;
    stats["cache_enabled"] = config_.enable_cache;
    
    return stats;
}

std::optional<nlohmann::json> UserServiceClient::makeRequest(const std::string& method, 
                                                            const std::string& endpoint,
                                                            const nlohmann::json& body) {
    try {
        std::string url = buildUrl(endpoint);
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", config_.user_agent},
            {"X-Service-Name", "gomoku-service"}
        };
        
        common::http::HttpClientResponse response;
        if (method == "GET") {
            response = http_client_->get(url, headers, config_.timeout_seconds * 1000);
        } else if (method == "POST") {
            response = http_client_->post(url, body.dump(), headers, config_.timeout_seconds * 1000);
        } else if (method == "PUT") {
            response = http_client_->put(url, body.dump(), headers, config_.timeout_seconds * 1000);
        } else if (method == "DELETE") {
            response = http_client_->del(url, config_.timeout_seconds * 1000);
        } else {
            LOG_ERROR("不支持的HTTP方法: " + method);
            return std::nullopt;
        }
        
        if (response.success && (response.status_code >= 200 && response.status_code < 300)) {
            if (!response.body.empty()) {
                return nlohmann::json::parse(response.body);
            } else {
                return nlohmann::json::object();
            }
        } else {
            LOG_WARNING("HTTP请求失败: " + method + " " + url + 
                       ", 状态码: " + std::to_string(response.status_code) +
                       ", 错误: " + response.error_message);
            return std::nullopt;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("makeRequest异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::string UserServiceClient::buildUrl(const std::string& endpoint) const {
    if (endpoint.empty()) {
        return config_.base_url;
    }
    
    std::string url = config_.base_url;
    if (url.back() != '/' && endpoint.front() != '/') {
        url += "/";
    }
    url += endpoint;
    return url;
}

// ==================== GameDataServiceClient 实现 ====================

GameDataServiceClient::GameDataServiceClient(const ServiceClientConfig& config) : config_(config) {
    http_client_ = std::make_unique<common::http::HttpClient>();
    
    // 初始化熔断器
    if (config_.enable_circuit_breaker) {
        circuit_breaker_ = std::make_unique<CircuitBreaker<nlohmann::json>>(config_.circuit_breaker_config);
        LOG_INFO("游戏数据服务客户端初始化完成，熔断器已启用: " + config_.service_name);
    } else {
        LOG_INFO("游戏数据服务客户端初始化完成，熔断器已禁用: " + config_.service_name);
    }
}

GameDataServiceClient::~GameDataServiceClient() {
    LOG_INFO("游戏数据服务客户端销毁: " + config_.service_name);
}

std::optional<UserGameProfile> GameDataServiceClient::getUserGameProfile(const std::string& user_id, int game_type_id) {
    if (user_id.empty()) {
        LOG_WARNING("用户ID为空，无法获取游戏档案");
        return std::nullopt;
    }

    try {
        // 🔧 修复：使用正确的 game_data_service API 路径
        std::string endpoint = "/api/v1/gamedata/profiles/" + user_id;
        std::string url = config_.base_url + endpoint;
        
        LOG_DEBUG("获取用户游戏档案: " + user_id + " from " + url);
        
        try {
            // 构造HTTP请求头
            std::unordered_map<std::string, std::string> headers = {
                {"Content-Type", "application/json"},
                {"User-Agent", "GomokuService/2.0"},
                {"X-Service-Name", "gomoku-service"}
            };
            
            auto response = http_client_->get(url, headers, config_.timeout_seconds * 1000);
            
            if (response.status_code == 200) {
                auto json_response = nlohmann::json::parse(response.body);
                
                if (json_response.contains("data") && json_response["data"].is_object()) {
                    auto profile_data = json_response["data"];
                    UserGameProfile profile = UserGameProfile::fromJson(profile_data);
                    LOG_INFO("用户游戏档案获取成功: " + user_id);
                    return profile;
                } else {
                    LOG_WARNING("游戏数据服务响应格式异常");
                    return std::nullopt;
                }
            } else if (response.status_code == 404) {
                LOG_INFO("用户游戏档案不存在: " + user_id);
                return std::nullopt;
            } else {
                LOG_ERROR("游戏数据服务调用失败，状态码: " + std::to_string(response.status_code));
                return std::nullopt;
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR("解析游戏数据服务响应JSON失败: " + std::string(e.what()));
            return std::nullopt;
        } catch (const std::exception& e) {
            LOG_ERROR("HTTP请求异常: " + std::string(e.what()));
            // 🔧 降级策略：返回默认游戏档案
            UserGameProfile fallback_profile;
            fallback_profile.user_id = user_id;
            fallback_profile.game_type_id = game_type_id;
            fallback_profile.level = 1;
            fallback_profile.experience = 0;
            fallback_profile.total_games = 0;
            fallback_profile.wins = 0;
            fallback_profile.losses = 0;
            fallback_profile.draws = 0;
            fallback_profile.current_rating = 1500; // 默认积分
            fallback_profile.peak_rating = 1500;
            fallback_profile.current_win_streak = 0;
            fallback_profile.best_win_streak = 0;
            LOG_WARNING("降级返回默认游戏档案: " + user_id);
            return fallback_profile;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取用户游戏档案失败: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool GameDataServiceClient::createUserGameProfile(const std::string& user_id, int game_type_id) {
    if (user_id.empty()) {
        LOG_WARNING("用户ID为空，无法创建游戏档案");
        return false;
    }

    try {
        // 🔧 修复：使用正确的 game_data_service API 路径
        std::string endpoint = "/api/v1/gamedata/profiles";
        std::string url = config_.base_url + endpoint;

        // 🔧 修复：使用正确的请求体格式（game_data_service 要求 user_id 和 display_name）
        nlohmann::json request_body = {
            {"user_id", user_id},
            {"display_name", user_id},  // 使用 user_id 作为默认显示名称
            {"level", 1},
            {"experience_points", 0},
            {"avatar_url", ""}
        };
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "GomokuService/2.0"},
            {"X-Service-Name", "gomoku-service"}
        };
        
        auto response = http_client_->post(url, request_body.dump(), headers, config_.timeout_seconds * 1000);
        
        if (response.status_code == 201 || response.status_code == 200) {
            LOG_DEBUG("用户游戏档案创建成功: " + user_id);
            return true;
        } else if (response.status_code == 409) {
            LOG_INFO("用户游戏档案已存在: " + user_id);
            return true; // 档案已存在也算成功
        } else {
            LOG_ERROR("创建用户游戏档案失败，状态码: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建用户游戏档案失败: " + std::string(e.what()));
        return false;
    }
}

bool GameDataServiceClient::recordGameResult(const GameResultData& result) {
    if (result.user_id.empty()) {
        LOG_WARNING("用户ID为空，无法记录游戏结果");
        return false;
    }
    
    try {
        // 🔧 修复：实现真正的HTTP POST请求
        std::string endpoint = "/api/v1/game-results";
        std::string url = config_.base_url + endpoint;
        
        // 构造请求体
        nlohmann::json request_body = result.toJson();
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "GomokuService/2.0"},
            {"X-Service-Name", "gomoku-service"}
        };
        
        auto response = http_client_->post(url, request_body.dump(), headers, config_.timeout_seconds * 1000);
        
        if (response.status_code == 201 || response.status_code == 200) {
            LOG_DEBUG("游戏结果记录成功: " + result.user_id + " -> " + result.result);
            return true;
        } else {
            LOG_ERROR("记录游戏结果失败，状态码: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("记录游戏结果失败: " + std::string(e.what()));
        return false;
    }
}

bool GameDataServiceClient::updateAchievementProgress(const std::string& user_id, const std::string& achievement_id, int progress) {
    if (user_id.empty() || achievement_id.empty()) {
        LOG_WARNING("用户ID或成就ID为空，无法更新成就进度");
        return false;
    }
    
    try {
        // 🔧 修复：实现真正的HTTP PUT请求
        std::string endpoint = "/api/v1/user/" + user_id + "/achievements/" + achievement_id;
        std::string url = config_.base_url + endpoint;
        
        // 构造请求体
        nlohmann::json request_body = {
            {"progress", progress},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "GomokuService/2.0"},
            {"X-Service-Name", "gomoku-service"}
        };
        
        auto response = http_client_->put(url, request_body.dump(), headers, config_.timeout_seconds * 1000);
        
        if (response.status_code == 200 || response.status_code == 204) {
            LOG_DEBUG("成就进度更新成功: " + user_id + " -> " + achievement_id + " (" + std::to_string(progress) + ")");
            return true;
        } else {
            LOG_ERROR("更新成就进度失败，状态码: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新成就进度失败: " + std::string(e.what()));
        return false;
    }
}

bool GameDataServiceClient::addCurrencyReward(const std::string& user_id, int currency_type_id, int64_t amount, const std::string& reason) {
    if (user_id.empty()) {
        LOG_WARNING("用户ID为空，无法添加货币奖励");
        return false;
    }
    
    try {
        // 🔧 修复：实现真正的HTTP POST请求
        std::string endpoint = "/api/v1/user/" + user_id + "/currency";
        std::string url = config_.base_url + endpoint;
        
        // 构造请求体
        nlohmann::json request_body = {
            {"currency_type_id", currency_type_id},
            {"amount", amount},
            {"reason", reason},
            {"source", "gomoku_game"},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "GomokuService/2.0"},
            {"X-Service-Name", "gomoku-service"}
        };
        
        auto response = http_client_->post(url, request_body.dump(), headers, config_.timeout_seconds * 1000);
        
        if (response.status_code == 201 || response.status_code == 200) {
            LOG_DEBUG("货币奖励添加成功: " + user_id + " -> 类型" + std::to_string(currency_type_id) + " 数量" + std::to_string(amount));
            return true;
        } else {
            LOG_ERROR("添加货币奖励失败，状态码: " + std::to_string(response.status_code));
            return false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("添加货币奖励失败: " + std::string(e.what()));
        return false;
    }
}

void GameDataServiceClient::clearGameProfileCache(const std::string& user_id, int game_type_id) {
    // 实现游戏档案缓存清理
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    if (user_id.empty()) {
        // 清除所有游戏档案缓存
        profile_cache_.clear();
        currency_cache_.clear();
        LOG_INFO("已清除所有游戏档案缓存");
    } else {
        // 清除指定用户的游戏档案缓存
        std::string profile_key = user_id + "_" + std::to_string(game_type_id);
        
        auto profile_it = profile_cache_.find(profile_key);
        if (profile_it != profile_cache_.end()) {
            profile_cache_.erase(profile_it);
            LOG_DEBUG("已清除游戏档案缓存: " + profile_key);
        }
        
        // 清除用户的货币缓存
        for (auto it = currency_cache_.begin(); it != currency_cache_.end();) {
            if (it->first.find(user_id + "_") == 0) {
                it = currency_cache_.erase(it);
                LOG_DEBUG("已清除货币缓存: " + it->first);
            } else {
                ++it;
            }
        }
    }
}

nlohmann::json GameDataServiceClient::getClientStatistics() {
    nlohmann::json stats;
    stats["service_name"] = config_.service_name;
    stats["base_url"] = config_.base_url;
    stats["cache_enabled"] = config_.enable_cache;

    return stats;
}

// ==================== 游戏结算 API 实现 ====================

std::optional<SettlementResponse> GameDataServiceClient::submitGameSettlement(const GameEndContext& context) {
    try {
        // 1. 构建 JSON 请求体
        nlohmann::json request_body = {
            {"game_id", context.room_id},
            {"game_type", context.game_type},
            {"mode", context.game_mode},
            {"duration_seconds", context.duration_seconds},
            {"players", nlohmann::json::array()},
            {"metadata", context.metadata}
        };

        // 添加玩家信息
        for (const auto& player : context.players) {
            request_body["players"].push_back({
                {"user_id", player.user_id},
                {"result", player.result},
                {"game_data", {
                    {"user_id", player.user_id},
                    {"rating_before", player.rating_before},
                    {"games_played", player.games_played},
                    {"win_streak", player.win_streak},
                    {"tier_level", player.tier_level},
                    {"is_first_win_today", player.is_first_win_today},
                    {"games_today", player.games_today}
                }}
            });
        }

        // 2. POST 到结算 API
        std::string endpoint = "/api/v1/gamedata/settlement";
        std::string url = config_.base_url + endpoint;

        LOG_INFO("[SETTLEMENT] 提交游戏结算: " + context.room_id +
                 ", 玩家数: " + std::to_string(context.players.size()) +
                 ", 时长: " + std::to_string(context.duration_seconds) + "秒");

        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "GomokuService/2.0"},
            {"X-Service-Name", "gomoku-service"}
        };

        auto response = http_client_->post(url, request_body.dump(), headers, config_.timeout_seconds * 1000);

        // 3. 解析响应
        if (response.status_code == 200) {
            auto json_resp = nlohmann::json::parse(response.body);
            SettlementResponse result = SettlementResponse::fromJson(json_resp);

            if (result.success) {
                LOG_INFO("[SETTLEMENT] 游戏结算成功: " + context.room_id +
                         ", 结算玩家数: " + std::to_string(result.settlements.size()));
            } else {
                LOG_ERROR("[SETTLEMENT] 游戏结算失败: " + result.error_message);
            }
            return result;
        } else {
            LOG_ERROR("[SETTLEMENT] 结算API调用失败，状态码: " + std::to_string(response.status_code) +
                     ", 响应: " + response.body);
            return std::nullopt;
        }

    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("[SETTLEMENT] 解析结算响应JSON失败: " + std::string(e.what()));
        return std::nullopt;
    } catch (const std::exception& e) {
        LOG_ERROR("[SETTLEMENT] 提交游戏结算异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<PlayerGameStats> GameDataServiceClient::getPlayerGameStats(const std::string& user_id, int game_type_id) {
    if (user_id.empty()) {
        LOG_WARNING("[SETTLEMENT] 用户ID为空，无法获取游戏统计");
        return std::nullopt;
    }

    try {
        // 调用用户游戏档案 API 获取统计数据
        auto profile = getUserGameProfile(user_id, game_type_id);
        if (profile) {
            PlayerGameStats stats;
            // 确保 rating 在有效范围内 (100-3000)
            stats.current_rating = (profile->current_rating < 100 || profile->current_rating > 3000)
                                   ? 1200 : profile->current_rating;
            stats.games_played = profile->total_games;
            stats.win_streak = profile->current_win_streak;
            stats.tier_level = 5; // TODO: 从段位系统获取
            stats.is_first_win_today = false; // TODO: 从每日统计获取
            stats.games_today = 0; // TODO: 从每日统计获取

            LOG_DEBUG("[SETTLEMENT] 获取玩家统计成功: " + user_id +
                     ", rating=" + std::to_string(stats.current_rating) +
                     ", games=" + std::to_string(stats.games_played));
            return stats;
        }

        // 如果获取失败，返回默认值
        LOG_WARNING("[SETTLEMENT] 获取玩家统计失败，使用默认值: " + user_id);
        return PlayerGameStats{};

    } catch (const std::exception& e) {
        LOG_ERROR("[SETTLEMENT] 获取玩家统计异常: " + std::string(e.what()));
        return PlayerGameStats{};
    }
}

std::optional<nlohmann::json> GameDataServiceClient::makeRequest(const std::string& method,
                                                              const std::string& endpoint,
                                                              const nlohmann::json& body) {
    try {
        std::string url = buildUrl(endpoint);
        
        // 构造HTTP请求头
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", config_.user_agent},
            {"X-Service-Name", "gomoku-service"}
        };
        
        common::http::HttpClientResponse response;
        if (method == "GET") {
            response = http_client_->get(url, headers, config_.timeout_seconds * 1000);
        } else if (method == "POST") {
            response = http_client_->post(url, body.dump(), headers, config_.timeout_seconds * 1000);
        } else if (method == "PUT") {
            response = http_client_->put(url, body.dump(), headers, config_.timeout_seconds * 1000);
        } else if (method == "DELETE") {
            response = http_client_->del(url, config_.timeout_seconds * 1000);
        } else {
            LOG_ERROR("不支持的HTTP方法: " + method);
            return std::nullopt;
        }
        
        if (response.success && (response.status_code >= 200 && response.status_code < 300)) {
            if (!response.body.empty()) {
                return nlohmann::json::parse(response.body);
            } else {
                return nlohmann::json::object();
            }
        } else {
            LOG_WARNING("HTTP请求失败: " + method + " " + url + 
                       ", 状态码: " + std::to_string(response.status_code) +
                       ", 错误: " + response.error_message);
            return std::nullopt;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("makeRequest异常: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::string GameDataServiceClient::buildUrl(const std::string& endpoint) const {
    if (endpoint.empty()) {
        return config_.base_url;
    }
    
    std::string url = config_.base_url;
    if (url.back() != '/' && endpoint.front() != '/') {
        url += "/";
    }
    url += endpoint;
    return url;
}

std::optional<nlohmann::json> GameDataServiceClient::makeRequestWithCircuitBreaker(
    const std::string& method, 
    const std::string& endpoint,
    const nlohmann::json& body) {
    
    if (!circuit_breaker_) {
        // 熔断器未启用，直接调用原方法
        return makeRequest(method, endpoint, body);
    }
    
    // 使用熔断器执行请求
    auto result = circuit_breaker_->execute([this, &method, &endpoint, &body]() -> nlohmann::json {
        auto response = makeRequest(method, endpoint, body);
        if (!response) {
            throw std::runtime_error("Request failed");
        }
        return *response;
    });
    
    if (!result) {
        LOG_WARNING("熔断器拒绝请求或请求失败: " + method + " " + endpoint);
        return std::nullopt;
    }
    
    return *result;
}

// ==================== ServiceClientManager 实现 ====================

// 静态实例
ServiceClientManager* ServiceClientManager::instance_ = nullptr;
std::mutex ServiceClientManager::instance_mutex_;

ServiceClientManager::ServiceClientManager() {
    LOG_INFO("服务客户端管理器初始化");
}

ServiceClientManager::~ServiceClientManager() {
    LOG_INFO("服务客户端管理器销毁");
}

ServiceClientManager& ServiceClientManager::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = new ServiceClientManager();
    }
    return *instance_;
}

bool ServiceClientManager::initialize(const ServiceClientConfig& user_config, const ServiceClientConfig& game_data_config) {
    try {
        // 初始化用户服务客户端
        user_service_client_ = std::make_unique<UserServiceClient>(user_config);
        
        // 初始化游戏数据服务客户端
        game_data_service_client_ = std::make_unique<GameDataServiceClient>(game_data_config);
        
        LOG_INFO("服务客户端管理器初始化完成");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("服务客户端管理器初始化失败: " + std::string(e.what()));
        return false;
    }
}

UserServiceClient* ServiceClientManager::getUserServiceClient() {
    if (!user_service_client_) {
        LOG_ERROR("用户服务客户端未初始化");
        return nullptr;
    }
    return user_service_client_.get();
}

GameDataServiceClient* ServiceClientManager::getGameDataServiceClient() {
    if (!game_data_service_client_) {
        LOG_ERROR("游戏数据服务客户端未初始化");
        return nullptr;
    }
    return game_data_service_client_.get();
}

void ServiceClientManager::clearAllCaches() {
    LOG_INFO("清除所有服务客户端缓存");
    
    if (user_service_client_) {
        user_service_client_->clearUserCache("");
    }
    
    if (game_data_service_client_) {
        game_data_service_client_->clearGameProfileCache("", 0);
    }
}

nlohmann::json ServiceClientManager::getStatistics() {
    nlohmann::json stats;
    
    if (user_service_client_) {
        stats["user_service"] = user_service_client_->getClientStatistics();
    }
    
    if (game_data_service_client_) {
        stats["game_data_service"] = game_data_service_client_->getClientStatistics();
    }
    
    return stats;
}

nlohmann::json ServiceClientManager::getHealthStatus() {
    nlohmann::json health;
    health["user_service_available"] = (user_service_client_ != nullptr);
    health["game_data_service_available"] = (game_data_service_client_ != nullptr);
    health["overall_healthy"] = (user_service_client_ != nullptr && game_data_service_client_ != nullptr);
    
    return health;
}

} // namespace gomoku
} // namespace game_services
