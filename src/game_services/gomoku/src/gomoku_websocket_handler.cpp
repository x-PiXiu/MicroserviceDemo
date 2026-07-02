/**
 * @file gomoku_websocket_handler.cpp
 * @brief 五子棋WebSocket处理器实现 (增强版)
 * @details 增强后的WebSocket处理器，使用服务客户端获取用户信息，支持JWT验证
 * @author AI Assistant
 * @date 2025-09-19
 * @version 2.0.0
 */

#include "gomoku_websocket_handler.h"
#include "service_client.h"
// [FIX] 已移除JWT验证器，认证由API网关统一处理
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <unistd.h>  // for close()

namespace game_services {
namespace gomoku {

// ==================== GomokuWebSocketHandler 增强实现 ====================

/**
 * @brief 构造函数 (增强版)
 */
GomokuWebSocketHandler::GomokuWebSocketHandler(std::shared_ptr<common::network::EventLoop> event_loop)
    : game_base::WebSocketHandlerBase(event_loop) {
    // 初始化服务客户端
    initializeServiceClients();
    
    // [FIX] 已移除JWT验证器初始化，认证由API网关统一处理
    
    LOG_INFO("增强版五子棋WebSocket处理器初始化完成");
}

/**
 * @brief 析构函数 (增强版)
 */
GomokuWebSocketHandler::~GomokuWebSocketHandler() {
    cleanup();
}

/**
 * @brief 认证玩家 (P1 优化版本: 缓存 + 异步加载)
 * @param player_id 玩家ID (由API网关验证后传入)
 * @return 认证是否成功
 */
bool GomokuWebSocketHandler::authenticatePlayer(const std::string& player_id) {
    LOG_DEBUG("Authenticating player session: " + player_id);

    try {
        // P1 优化: 1. 基本玩家ID格式验证
        if (player_id.empty() || player_id.length() < 3) {
            LOG_WARNING("Invalid player ID format: " + player_id);
            return false;
        }

        // 2. 验证用户ID格式（应该以usr_开头）
        if (player_id.substr(0, 4) != "usr_") {
            LOG_WARNING("Player ID should start with usr_: " + player_id);
            return false;
        }

        // P1 优化: 3. 检查本地缓存 (快速路径)
        {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            auto it = authenticated_players_.find(player_id);
            if (it != authenticated_players_.end() && it->second.authenticated) {
                LOG_DEBUG("Player authentication cache hit: " + player_id);
                // 异步更新在线状态（不阻塞连接）
                asyncUpdateOnlineStatus(player_id);
                return true;
            }
        }

        // P1 优化: 4. 异步加载用户信息（不阻塞连接）
        // JWT Token 已在握手时由 API 网关验证，信任认证结果
        asyncLoadUserInfo(player_id);

        // 5. 创建临时玩家信息（允许连接，后续异步填充详细数据）
        game_services::game_base::PlayerInfo temp_info;
        temp_info.player_id = player_id;
        temp_info.username = player_id;
        temp_info.nickname = player_id;
        temp_info.authenticated = true;  // 信任 JWT 验证结果
        temp_info.session_id = player_id + "_session";
        // 显式初始化游戏数据字段（确保不是垃圾值）
        temp_info.level = 1;
        temp_info.rating = 1500;
        temp_info.total_games = 0;
        temp_info.wins = 0;
        temp_info.losses = 0;
        temp_info.win_streak = 0;

        {
            std::lock_guard<std::shared_mutex> lock(players_mutex_);
            authenticated_players_[player_id] = temp_info;
        }

        LOG_INFO("Player authenticated (async load): " + player_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception authenticating player: " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief 重写认证状态设置方法
 * @details 当基类认证成功后调用此方法，用于更新 authenticated_players_ 映射
 */
void GomokuWebSocketHandler::setPlayerAuthenticated(const std::string& player_id, bool authenticated) {
    LOG_INFO("🎮 设置玩家认证状态: " + player_id + " -> " + (authenticated ? "authenticated" : "not authenticated"));

    try {
        if (authenticated) {
            // 检查是否已存在
            {
                std::shared_lock<std::shared_mutex> lock(players_mutex_);
                auto it = authenticated_players_.find(player_id);
                if (it != authenticated_players_.end() && it->second.authenticated) {
                    LOG_DEBUG("玩家已认证，跳过重复设置: " + player_id);
                    return;
                }
            }

            // 创建玩家信息并添加到映射
            game_services::game_base::PlayerInfo player_info;
            player_info.player_id = player_id;
            player_info.username = player_id;
            player_info.nickname = player_id;
            player_info.authenticated = true;
            player_info.session_id = player_id + "_session";
            player_info.last_activity = std::chrono::system_clock::now();
            // 显式初始化游戏数据字段（确保不是垃圾值）
            player_info.level = 1;
            player_info.rating = 1500;
            player_info.total_games = 0;
            player_info.wins = 0;
            player_info.losses = 0;
            player_info.win_streak = 0;

            {
                std::lock_guard<std::shared_mutex> lock(players_mutex_);
                authenticated_players_[player_id] = player_info;
            }

            LOG_INFO("✅ 玩家认证状态已设置: " + player_id);

            // 异步加载用户详细信息
            asyncLoadUserInfo(player_id);
        } else {
            // 移除认证状态
            std::lock_guard<std::shared_mutex> lock(players_mutex_);
            authenticated_players_.erase(player_id);
            LOG_INFO("玩家认证状态已移除: " + player_id);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("设置玩家认证状态失败: " + std::string(e.what()));
    }
}

/**
 * @brief 异步加载用户信息 (P1 优化)
 * @param player_id 玩家ID
 */
void GomokuWebSocketHandler::asyncLoadUserInfo(const std::string& player_id) {
    // 提交到线程池异步执行
    std::thread([this, player_id]() {
        try {
            auto user_service_client = service_client_manager_->getUserServiceClient();
            auto game_data_client = service_client_manager_->getGameDataServiceClient();

            if (!user_service_client || !game_data_client) {
                LOG_WARNING("Service clients not initialized for async load, using defaults");
                // 即使服务不可用，也设置默认值
                {
                    std::lock_guard<std::shared_mutex> lock(players_mutex_);
                    auto it = authenticated_players_.find(player_id);
                    if (it != authenticated_players_.end()) {
                        it->second.username = player_id;
                        it->second.nickname = player_id;
                        it->second.level = 1;
                        it->second.rating = 1500;
                    }
                }
                return;
            }

            // 尝试获取用户信息和游戏档案（使用 try-catch 包装每个调用）
            std::optional<game_services::gomoku::UserInfo> user_info;
            std::optional<UserGameProfile> game_profile;

            try {
                user_info = user_service_client->getUserInfo(player_id);
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to get user info: " + std::string(e.what()));
            }

            try {
                game_profile = game_data_client->getUserGameProfile(player_id, 1);

                // 如果没有档案，尝试创建一个
                if (!game_profile.has_value()) {
                    LOG_INFO("Creating game profile for new player: " + player_id);
                    try {
                        game_data_client->createUserGameProfile(player_id, 1);
                        game_profile = game_data_client->getUserGameProfile(player_id, 1);
                    } catch (const std::exception& e) {
                        LOG_WARNING("Failed to create game profile: " + std::string(e.what()) + ", using defaults");
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to get game profile: " + std::string(e.what()));
            }

            // 更新缓存（即使部分数据不可用也继续）
            {
                std::lock_guard<std::shared_mutex> lock(players_mutex_);
                auto it = authenticated_players_.find(player_id);
                if (it != authenticated_players_.end()) {
                    // 设置用户基本信息
                    if (user_info.has_value()) {
                        it->second.username = user_info->username;
                        it->second.nickname = user_info->nickname;
                        it->second.avatar_url = user_info->avatar_url;
                    } else {
                        // 使用默认值
                        it->second.username = player_id;
                        it->second.nickname = player_id;
                    }

                    // 设置游戏数据
                    if (game_profile.has_value()) {
                        it->second.level = game_profile->level;
                        it->second.rating = game_profile->current_rating;
                        it->second.total_games = game_profile->total_games;
                        it->second.wins = game_profile->wins;
                        it->second.losses = game_profile->losses;
                        it->second.win_streak = game_profile->current_win_streak;
                    } else {
                        // 使用默认游戏数据
                        it->second.level = 1;
                        it->second.rating = 1500;
                        it->second.total_games = 0;
                        it->second.wins = 0;
                        it->second.losses = 0;
                        it->second.win_streak = 0;
                    }
                    LOG_DEBUG("Async loaded user info for: " + player_id +
                             " (level=" + std::to_string(it->second.level) +
                             ", rating=" + std::to_string(it->second.rating) + ")");
                }
            }

            // 尝试更新在线状态（非关键操作）
            try {
                user_service_client->updateOnlineStatus(player_id, "online");
            } catch (const std::exception& e) {
                LOG_DEBUG("Failed to update online status: " + std::string(e.what()));
            }

        } catch (const std::exception& e) {
            LOG_ERROR("Async load user info failed: " + std::string(e.what()));
        }
    }).detach();
}

/**
 * @brief 异步更新在线状态 (P1 优化)
 * @param player_id 玩家ID
 */
void GomokuWebSocketHandler::asyncUpdateOnlineStatus(const std::string& player_id) {
    std::thread([this, player_id]() {
        try {
            auto user_service_client = service_client_manager_->getUserServiceClient();
            if (user_service_client) {
                user_service_client->updateOnlineStatus(player_id, "online");
            }
        } catch (const std::exception& e) {
            LOG_DEBUG("Failed to update online status: " + std::string(e.what()));
        }
    }).detach();
}

/**
 * @brief 获取玩家信息 (增强版本)
 * @param player_id 玩家ID
 * @return 玩家信息，不存在返回空
 */
std::optional<game_services::game_base::PlayerInfo> GomokuWebSocketHandler::getPlayerInfo(const std::string& player_id) const {
    std::shared_lock<std::shared_mutex> lock(players_mutex_);

    auto it = authenticated_players_.find(player_id);
    if (it != authenticated_players_.end()) {
        return it->second;
    }

    return std::nullopt;
}

/**
 * @brief 记录游戏结果 (增强版本)
 * @param game_id 游戏ID
 * @param black_player_id 黑方玩家ID
 * @param white_player_id 白方玩家ID
 * @param result 游戏结果
 * @param game_data 游戏数据
 */
void GomokuWebSocketHandler::recordGameResult(const std::string& game_id,
                     const std::string& black_player_id,
                     const std::string& white_player_id,
                     const std::string& result,
                     const nlohmann::json& game_data) {
    try {
        auto game_data_client = service_client_manager_->getGameDataServiceClient();
        if (!game_data_client) {
            LOG_ERROR("游戏数据服务客户端未初始化");
            return;
        }
        
        // 获取游戏时长
        int duration_seconds = game_data.value("duration_seconds", 0);
        int moves_count = game_data.value("moves_count", 0);
        
        // 记录黑方结果
        GameResultData black_result;
        black_result.user_id = black_player_id;
        black_result.game_type_id = 1; // 五子棋
        black_result.session_id = game_id;
        black_result.duration_seconds = duration_seconds;
        black_result.moves_count = moves_count;
        black_result.game_data = game_data;
        
        // 记录白方结果
        GameResultData white_result;
        white_result.user_id = white_player_id;
        white_result.game_type_id = 1; // 五子棋
        white_result.session_id = game_id;
        white_result.duration_seconds = duration_seconds;
        white_result.moves_count = moves_count;
        white_result.game_data = game_data;
        
        // 设置游戏结果和得分
        if (result == "black_win") {
            black_result.result = "win";
            black_result.score = 1000;
            white_result.result = "loss";
            white_result.score = 0;
        } else if (result == "white_win") {
            black_result.result = "loss";
            black_result.score = 0;
            white_result.result = "win";
            white_result.score = 1000;
        } else {
            black_result.result = "draw";
            black_result.score = 500;
            white_result.result = "draw";
            white_result.score = 500;
        }
        
        // 计算评分变化 (简单的ELO算法)
        auto black_player = getPlayerInfo(black_player_id);
        auto white_player = getPlayerInfo(white_player_id);
        
        if (black_player.has_value() && white_player.has_value()) {
            int rating_change = calculateRatingChange(black_player->rating, white_player->rating, result);
            black_result.rating_change = rating_change;
            white_result.rating_change = -rating_change;
        }
        
        // 提交游戏结果
        bool black_success = game_data_client->recordGameResult(black_result);
        bool white_success = game_data_client->recordGameResult(white_result);
        
        if (black_success && white_success) {
            LOG_INFO("游戏结果记录成功: " + game_id);
            
            // 更新成就进度
            updateAchievements(black_player_id, white_player_id, result, game_data);
            
            // 发放奖励
            rewardPlayers(black_player_id, white_player_id, result);
            
        } else {
            LOG_ERROR("游戏结果记录失败: " + game_id);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("记录游戏结果时发生异常: " + std::string(e.what()));
    }
}

/**
 * @brief 玩家断开连接处理 (增强版本)
 * @param player_id 玩家ID
 */
void GomokuWebSocketHandler::onPlayerDisconnected(const std::string& player_id) {
    try {
        // [FIX] 关键修复：只为真实用户（已认证）更新在线状态，避免用临时ID更新
        bool is_authenticated = false;
        {
            std::shared_lock<std::shared_mutex> lock(players_mutex_);
            is_authenticated = authenticated_players_.find(player_id) != authenticated_players_.end();
        }
        
        if (is_authenticated) {
            // 只有认证用户才更新在线状态
            auto user_service_client = service_client_manager_->getUserServiceClient();
            if (user_service_client) {
                LOG_DEBUG("更新已认证用户离线状态: " + player_id);
                user_service_client->updateOnlineStatus(player_id, "offline");
            }
        } else {
            // 临时用户（未认证）不需要更新在线状态
            LOG_DEBUG("临时用户断开连接，跳过在线状态更新: " + player_id);
        }
        
        // 清除玩家信息缓存
        {
            std::lock_guard<std::shared_mutex> lock(players_mutex_);
            authenticated_players_.erase(player_id);
        }
        
        // 只为认证用户清除服务缓存
        if (is_authenticated && service_client_manager_) {
            auto user_client = service_client_manager_->getUserServiceClient();
            auto game_data_client = service_client_manager_->getGameDataServiceClient();
            
            if (user_client) {
                user_client->clearUserCache(player_id);
            }
            if (game_data_client) {
                game_data_client->clearGameProfileCache(player_id, 1);
            }
        }
        
        LOG_INFO("玩家断开连接处理完成: " + player_id + 
                (is_authenticated ? " (已认证用户)" : " (临时用户)"));
        
    } catch (const std::exception& e) {
        LOG_ERROR("处理玩家断开连接时发生异常: " + std::string(e.what()));
    }
}

// ==================== 私有方法实现 ====================

/**
 * @brief 初始化服务客户端
 */
void GomokuWebSocketHandler::initializeServiceClients() {
    try {
        service_client_manager_ = std::make_shared<ServiceClientManager>();

        // P0 修复: 从配置管理器读取服务地址，而非硬编码
        auto& config_manager = common::config::ConfigManager::getInstance();

        // 配置用户服务客户端
        ServiceClientConfig user_service_config;
        user_service_config.service_name = "user_service";
        user_service_config.base_url = config_manager.get<std::string>(
            "services.user_service.url", "http://localhost:8082");
        user_service_config.timeout_seconds = config_manager.get<int>(
            "services.user_service.timeout_seconds", 5);
        user_service_config.cache_ttl_seconds = config_manager.get<int>(
            "services.user_service.cache_ttl_seconds", 300);

        // 配置游戏数据服务客户端
        ServiceClientConfig game_data_service_config;
        game_data_service_config.service_name = "game_data_service";
        game_data_service_config.base_url = config_manager.get<std::string>(
            "services.game_data_service.url", "http://localhost:8083");
        game_data_service_config.timeout_seconds = config_manager.get<int>(
            "services.game_data_service.timeout_seconds", 5);
        game_data_service_config.cache_ttl_seconds = config_manager.get<int>(
            "services.game_data_service.cache_ttl_seconds", 180);

        LOG_INFO("Service clients configured - user_service: " + user_service_config.base_url +
                ", game_data_service: " + game_data_service_config.base_url);

        // 初始化客户端
        if (!service_client_manager_->initialize(user_service_config, game_data_service_config)) {
            LOG_ERROR("Failed to initialize service clients");
            service_client_manager_.reset();
        } else {
            LOG_INFO("Service clients initialized successfully");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception initializing service clients: " + std::string(e.what()));
    }
}

/**
 * @brief 初始化JWT验证器
 */
// [FIX] 已删除JWT验证器初始化方法，认证由API网关统一处理

/**
 * @brief 计算评分变化
 * @param black_rating 黑方评分
 * @param white_rating 白方评分
 * @param result 游戏结果
 * @return 黑方评分变化
 */
int GomokuWebSocketHandler::calculateRatingChange(int black_rating, int white_rating, const std::string& result) {
    const int K = 32; // ELO系数
    
    // 计算期望胜率
    double expected_black = 1.0 / (1.0 + std::pow(10.0, (white_rating - black_rating) / 400.0));
    
    // 实际得分
    double actual_black = 0.0;
    if (result == "black_win") {
        actual_black = 1.0;
    } else if (result == "white_win") {
        actual_black = 0.0;
    } else {
        actual_black = 0.5; // 平局
    }
    
    // 计算评分变化
    return static_cast<int>(K * (actual_black - expected_black));
}

/**
 * @brief 更新成就进度
 * @param black_player_id 黑方玩家ID
 * @param white_player_id 白方玩家ID
 * @param result 游戏结果
 * @param game_data 游戏数据
 */
void GomokuWebSocketHandler::updateAchievements(const std::string& black_player_id,
                       const std::string& white_player_id,
                       const std::string& result,
                       const nlohmann::json& game_data) {
    try {
        auto game_data_client = service_client_manager_->getGameDataServiceClient();
        if (!game_data_client) {
            return;
        }
        
        // 更新参与游戏成就
        game_data_client->updateAchievementProgress(black_player_id, "gomoku_total_games", 1);
        game_data_client->updateAchievementProgress(white_player_id, "gomoku_total_games", 1);
        
        // 更新胜利相关成就
        if (result == "black_win") {
            game_data_client->updateAchievementProgress(black_player_id, "gomoku_first_win", 1);
            game_data_client->updateAchievementProgress(black_player_id, "gomoku_total_wins", 1);
            game_data_client->updateAchievementProgress(black_player_id, "gomoku_win_streak", 1);
        } else if (result == "white_win") {
            game_data_client->updateAchievementProgress(white_player_id, "gomoku_first_win", 1);
            game_data_client->updateAchievementProgress(white_player_id, "gomoku_total_wins", 1);
            game_data_client->updateAchievementProgress(white_player_id, "gomoku_win_streak", 1);
        }
        
        // 更新游戏时长成就
        int duration_hours = game_data.value("duration_seconds", 0) / 3600;
        if (duration_hours > 0) {
            game_data_client->updateAchievementProgress(black_player_id, "gomoku_playtime_10h", duration_hours);
            game_data_client->updateAchievementProgress(white_player_id, "gomoku_playtime_10h", duration_hours);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("更新成就进度时发生异常: " + std::string(e.what()));
    }
}

/**
 * @brief 发放奖励
 * @param black_player_id 黑方玩家ID
 * @param white_player_id 白方玩家ID
 * @param result 游戏结果
 */
void GomokuWebSocketHandler::rewardPlayers(const std::string& black_player_id,
                  const std::string& white_player_id,
                  const std::string& result) {
    try {
        auto game_data_client = service_client_manager_->getGameDataServiceClient();
        if (!game_data_client) {
            return;
        }
        
        // 基础参与奖励
        game_data_client->addCurrencyReward(black_player_id, 1, 10, "game_participation"); // 10金币
        game_data_client->addCurrencyReward(white_player_id, 1, 10, "game_participation");
        
        // 胜利额外奖励
        if (result == "black_win") {
            game_data_client->addCurrencyReward(black_player_id, 1, 50, "game_victory"); // 50金币
            game_data_client->addCurrencyReward(black_player_id, 2, 1, "game_victory");  // 1宝石
        } else if (result == "white_win") {
            game_data_client->addCurrencyReward(white_player_id, 1, 50, "game_victory");
            game_data_client->addCurrencyReward(white_player_id, 2, 1, "game_victory");
        } else {
            // 平局奖励
            game_data_client->addCurrencyReward(black_player_id, 1, 25, "game_draw");
            game_data_client->addCurrencyReward(white_player_id, 1, 25, "game_draw");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("发放奖励时发生异常: " + std::string(e.what()));
    }
}

/**
 * @brief 清理资源
 */
void GomokuWebSocketHandler::cleanup() {
    if (service_client_manager_) {
        service_client_manager_->clearAllCaches();
        service_client_manager_.reset();
    }
    
    // [FIX] 已移除JWT验证器清理代码
    
    {
        std::lock_guard<std::shared_mutex> lock(players_mutex_);
        authenticated_players_.clear();
    }
}

// ==================== 虚函数实现 ====================

/**
 * @brief 处理WebSocket连接（优化版）
 */
void GomokuWebSocketHandler::handleWebSocketConnection(int client_fd, const std::string& client_ip) {
    LOG_DEBUG("[GOMOKU] 连接: " + client_ip);
    
    // 连接频率限制（防止恶意连接）
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_connect_time;
    static std::mutex connect_mutex;
    
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        auto now = std::chrono::steady_clock::now();
        auto it = last_connect_time.find(client_ip);
        
        if (it != last_connect_time.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
            if (elapsed.count() < 1) {
                LOG_WARNING("[BLOCK] 连接过快: " + client_ip);
                close(client_fd);
                return;
            }
        }
        
        last_connect_time[client_ip] = now;
    }
    
    // 调用基类处理WebSocket握手等核心逻辑
    WebSocketHandlerBase::handleWebSocketConnection(client_fd, client_ip);
}

/**
 * @brief 处理WebSocket消息（优化版）
 */
void GomokuWebSocketHandler::handleWebSocketMessage(const std::string& player_id, const std::string& message) {
    LOG_DEBUG("[GOMOKU] 消息 from " + player_id);
    
    try {
        nlohmann::json json_msg = nlohmann::json::parse(message);
        
        if (!json_msg.contains("type")) {
            LOG_WARNING("⚠️ 消息缺少type字段");
            return;
        }
        
        std::string msg_type = json_msg.value("type", "");
        
        // [FIX] 优化：统一的消息分发，减少if-else层级
        
        // Qt6特定消息
        if (msg_type == "qt6_message") {
            handleQt6SpecificMessage(player_id, json_msg);
            return;
        }

        // 游戏动作消息（直接转发到游戏逻辑）
        if (msg_type == "place_piece" || msg_type == "undo_move" ||
            msg_type == "surrender" || msg_type == "draw_offer" ||
            msg_type == "draw_response" || msg_type == "chat") {

            if (game_action_callback_) {
                bool result = game_action_callback_(player_id, json_msg);
                LOG_DEBUG(result ? "✅ 游戏动作成功" : "❌ 游戏动作失败");
            } else {
                LOG_WARNING("⚠️ game_action_callback_未设置");
            }
            return;
        }
        
        // 准备状态消息（优化版）
        if (msg_type == "ready") {
            handleReadyMessage(player_id, json_msg);
            return;
        }

        // 房间列表订阅消息
        if (msg_type == "subscribe_room_list") {
            handleSubscribeRoomList(player_id, json_msg);
            return;
        }

        // 取消房间列表订阅消息
        if (msg_type == "unsubscribe_room_list") {
            handleUnsubscribeRoomList(player_id, json_msg);
            return;
        }

        // 断线重连消息
        if (msg_type == "reconnect") {
            handleReconnect(player_id, json_msg);
            return;
        }

        // 匹配系统消息
        if (msg_type == "match_start" || msg_type == "match_cancel" ||
            msg_type == "get_match_status" || msg_type == "get_match_pool") {
            handleMatchMessage(player_id, json_msg);
            return;
        }

        // 其他标准消息（authenticate、房间操作等）委托基类处理
        WebSocketHandlerBase::handleWebSocketMessage(player_id, message);
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ 处理消息失败: " + std::string(e.what()));
    }
}

/**
 * @brief 处理准备状态消息
 */
void GomokuWebSocketHandler::handleReadyMessage(const std::string& player_id, const nlohmann::json& message) {
    auto base_session = getPlayerSession(player_id);
    if (!base_session) {
        LOG_WARNING("⚠️ 未找到会话: " + player_id);
        return;
    }
    
    auto session = std::dynamic_pointer_cast<GomokuPlayerSession>(base_session);
    if (!session) {
        LOG_WARNING("⚠️ 会话类型转换失败");
        return;
    }

    bool ready = message.value("data", nlohmann::json{}).value("ready", true);

    // 通过房间操作回调设置准备状态（触发广播）
    if (room_operation_callback_) {
        nlohmann::json ready_operation = {
            {"type", "set_ready"},
            {"ready", ready}
        };
        room_operation_callback_(player_id, ready_operation);
    } else {
        // 回退：直接设置会话状态（不触发广播）
        session->setReady(ready);
    }

    LOG_INFO("✅ 玩家准备: " + player_id + " = " + (ready ? "ready" : "not ready"));

    // 发送确认消息
    nlohmann::json confirm = {
        {"type", "ready_confirm"},
        {"data", {
            {"playerId", player_id},
            {"ready", ready}
        }}
    };
    sendToPlayer(player_id, confirm);
}

/**
 * @brief 处理房间列表订阅消息
 */
void GomokuWebSocketHandler::handleSubscribeRoomList(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("📥 玩家订阅房间列表: " + player_id);

    // 调用订阅回调
    if (subscription_callback_) {
        subscription_callback_(player_id, true);
    }

    // 生成订阅ID（符合文档 §4.1 格式）
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string subscription_id = "sub_" + std::to_string(timestamp) + "_" + player_id.substr(0, 8);

    // 发送当前房间列表（符合文档 §4.1 格式）
    nlohmann::json response = {
        {"type", "subscribe_confirm"},
        {"data", {
            {"rooms", room_list_data_callback_ ? room_list_data_callback_() : nlohmann::json::array()},
            {"subscriptionId", subscription_id}
        }}
    };
    sendToPlayer(player_id, response);

    LOG_DEBUG("✅ 房间列表订阅成功，已发送当前房间列表，subscriptionId=" + subscription_id);
}

/**
 * @brief 处理取消房间列表订阅消息
 */
void GomokuWebSocketHandler::handleUnsubscribeRoomList(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("📤 玩家取消订阅房间列表: " + player_id);

    // 调用订阅回调
    if (subscription_callback_) {
        subscription_callback_(player_id, false);
    }

    // 发送确认消息
    nlohmann::json response = {
        {"type", "unsubscribe_confirm"},
        {"data", {{"success", true}}}
    };
    sendToPlayer(player_id, response);

    LOG_DEBUG("✅ 取消房间列表订阅成功");
}

/**
 * @brief 处理断线重连消息
 */
void GomokuWebSocketHandler::handleReconnect(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🔄 玩家请求断线重连: " + player_id);

    // 通过房间操作回调处理重连逻辑
    if (room_operation_callback_) {
        nlohmann::json reconnect_action = {
            {"type", "reconnect"},
            {"data", message.value("data", nlohmann::json::object())}
        };
        bool success = room_operation_callback_(player_id, reconnect_action);

        if (success) {
            LOG_INFO("✅ 断线重连成功: " + player_id);
        } else {
            LOG_WARNING("⚠️ 断线重连失败: " + player_id);
            nlohmann::json error_response = {
                {"type", "reconnect_failed"},
                {"data", {{"reason", "无法恢复之前的游戏状态"}}}
            };
            sendToPlayer(player_id, error_response);
        }
    } else {
        LOG_WARNING("⚠️ room_operation_callback_未设置，无法处理重连");
        nlohmann::json error_response = {
            {"type", "reconnect_failed"},
            {"data", {{"reason", "服务器未配置重连支持"}}}
        };
        sendToPlayer(player_id, error_response);
    }
}

/**
 * @brief 处理WebSocket断开连接
 */
void GomokuWebSocketHandler::handleWebSocketDisconnection(const std::string& player_id) {
    // [FIX] 修复关键问题：调用基类的断开连接处理（包含重复调用防护）
    LOG_INFO("WebSocket连接断开: " + player_id);

    // 取消房间列表订阅
    if (subscription_callback_) {
        subscription_callback_(player_id, false);
    }

    // 先处理五子棋特定的断开连接逻辑
    onPlayerDisconnected(player_id);

    // 然后调用基类的断开连接处理（包含防重入保护）
    WebSocketHandlerBase::handleWebSocketDisconnection(player_id);
}

/**
 * @brief 处理Qt6特定消息
 */
void GomokuWebSocketHandler::handleQt6SpecificMessage(const std::string& player_id, const nlohmann::json& message) {
    LOG_DEBUG("处理Qt6特定消息: " + player_id);

    try {
        // [FIX] 实现Qt6特定的消息处理逻辑
        // 处理Qt6客户端的特殊格式消息（目前与标准消息处理一致）
        LOG_DEBUG("处理Qt6特定消息: " + player_id);

    } catch (const std::exception& e) {
        LOG_ERROR("处理Qt6消息失败: " + std::string(e.what()));
    }
}

// ==================== 房间操作方法实现 ====================

/**
 * @brief 处理创建房间请求
 */
void GomokuWebSocketHandler::handleCreateRoomWebSocket(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🏠 处理创建房间请求: player=" + player_id);

    try {
        if (!isPlayerAuthenticated(player_id)) {
            sendErrorMessage(player_id, "AUTHENTICATION_REQUIRED", "请先认证再创建房间");
            return;
        }

        // 获取房间配置
        nlohmann::json config = message.value("data", nlohmann::json::object()).value("config", nlohmann::json::object());

        // 构建房间操作消息
        nlohmann::json room_operation = {
            {"type", "create_room"},
            {"config", config}
        };

        // 调用房间操作回调
        if (room_operation_callback_) {
            bool success = room_operation_callback_(player_id, room_operation);
            if (success) {
                // 获取创建的房间ID（通过回调后的房间管理器获取）
                std::string room_id = getPlayerRoom(player_id);

                // 按照文档格式发送 room_created 消息
                nlohmann::json response = {
                    {"type", "room_created"},
                    {"data", {
                        {"roomId", room_id},
                        {"creatorId", player_id},
                        {"pieceType", "black"},  // 创建者默认执黑
                        {"config", config}
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };
                sendToPlayer(player_id, response);
                LOG_INFO("✅ 创建房间成功: player=" + player_id + ", room=" + room_id);
            } else {
                sendErrorMessage(player_id, "CREATE_ROOM_FAILED", "创建房间失败");
            }
        } else {
            LOG_WARNING("⚠️ room_operation_callback_ 未设置");
            sendErrorMessage(player_id, "SERVER_ERROR", "服务器未配置房间操作处理器");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 创建房间异常: " + std::string(e.what()));
        sendErrorMessage(player_id, "CREATE_ROOM_ERROR", "创建房间异常: " + std::string(e.what()));
    }
}

/**
 * @brief 处理加入房间请求
 */
void GomokuWebSocketHandler::handleJoinRoomWebSocket(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🚪 处理加入房间请求: player=" + player_id);

    try {
        if (!isPlayerAuthenticated(player_id)) {
            sendErrorMessage(player_id, "AUTHENTICATION_REQUIRED", "请先认证再加入房间");
            return;
        }

        // 获取房间ID
        std::string room_id;
        if (message.contains("data")) {
            room_id = message["data"].value("roomId", "");
        } else if (message.contains("roomId")) {
            room_id = message.value("roomId", "");
        }

        if (room_id.empty()) {
            sendErrorMessage(player_id, "INVALID_REQUEST", "缺少房间ID");
            return;
        }

        LOG_INFO("玩家 " + player_id + " 尝试加入房间: " + room_id);

        // 构建房间操作消息
        nlohmann::json room_operation = {
            {"type", "join_room"},
            {"roomId", room_id}
        };

        // 调用房间操作回调
        if (room_operation_callback_) {
            bool success = room_operation_callback_(player_id, room_operation);
            if (success) {
                // 更新玩家-房间映射
                setPlayerRoom(player_id, room_id);

                nlohmann::json response = {
                    {"type", "join_room_success"},
                    {"data", {
                        {"roomId", room_id},
                        {"playerId", player_id}
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };
                sendToPlayer(player_id, response);
                LOG_INFO("✅ 加入房间成功: player=" + player_id + ", room=" + room_id);
            } else {
                sendErrorMessage(player_id, "JOIN_ROOM_FAILED", "加入房间失败，房间可能已满或不存在");
            }
        } else {
            LOG_WARNING("⚠️ room_operation_callback_ 未设置");
            sendErrorMessage(player_id, "SERVER_ERROR", "服务器未配置房间操作处理器");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 加入房间异常: " + std::string(e.what()));
        sendErrorMessage(player_id, "JOIN_ROOM_ERROR", "加入房间异常: " + std::string(e.what()));
    }
}

/**
 * @brief 处理离开房间请求
 */
void GomokuWebSocketHandler::handleLeaveRoomWebSocket(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🚪 处理离开房间请求: player=" + player_id);

    try {
        if (!isPlayerAuthenticated(player_id)) {
            sendErrorMessage(player_id, "AUTHENTICATION_REQUIRED", "请先认证");
            return;
        }

        // 获取房间ID（可选，如果未提供则使用当前房间）
        std::string room_id = getPlayerRoom(player_id);
        if (room_id.empty()) {
            sendErrorMessage(player_id, "NOT_IN_ROOM", "您当前不在任何房间中");
            return;
        }

        // 构建房间操作消息
        nlohmann::json room_operation = {
            {"type", "leave_room"},
            {"roomId", room_id}
        };

        // 调用房间操作回调
        if (room_operation_callback_) {
            bool success = room_operation_callback_(player_id, room_operation);
            if (success) {
                // 清理玩家-房间映射
                removePlayerRoom(player_id);

                nlohmann::json response = {
                    {"type", "leave_room_success"},
                    {"data", {
                        {"roomId", room_id},
                        {"playerId", player_id}
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };
                sendToPlayer(player_id, response);
                LOG_INFO("✅ 离开房间成功: player=" + player_id + ", room=" + room_id);
            } else {
                sendErrorMessage(player_id, "LEAVE_ROOM_FAILED", "离开房间失败");
            }
        } else {
            LOG_WARNING("⚠️ room_operation_callback_ 未设置");
            sendErrorMessage(player_id, "SERVER_ERROR", "服务器未配置房间操作处理器");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 离开房间异常: " + std::string(e.what()));
        sendErrorMessage(player_id, "LEAVE_ROOM_ERROR", "离开房间异常: " + std::string(e.what()));
    }
}

/**
 * @brief 处理开始游戏请求
 */
void GomokuWebSocketHandler::handleStartGameWebSocket(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🎮 处理开始游戏请求: player=" + player_id);

    try {
        if (!isPlayerAuthenticated(player_id)) {
            sendErrorMessage(player_id, "AUTHENTICATION_REQUIRED", "请先认证");
            return;
        }

        // 获取房间ID
        std::string room_id = getPlayerRoom(player_id);
        if (room_id.empty()) {
            sendErrorMessage(player_id, "NOT_IN_ROOM", "您当前不在任何房间中");
            return;
        }

        // 构建房间操作消息
        nlohmann::json room_operation = {
            {"type", "start_game"},
            {"roomId", room_id}
        };

        // 调用房间操作回调
        if (room_operation_callback_) {
            bool success = room_operation_callback_(player_id, room_operation);

            // [FIX] 按照文档格式发送 operation_result 消息
            nlohmann::json response = {
                {"type", "operation_result"},
                {"data", {
                    {"operation", "start_game"},
                    {"success", success},
                    {"roomId", room_id}
                }}
            };

            if (!success) {
                response["data"]["error"] = "START_GAME_FAILED";
                response["data"]["message"] = "开始游戏失败，可能人数不足或您不是房主";
            }

            sendToPlayer(player_id, response);
            LOG_INFO(success ? "✅ 开始游戏成功: player=" + player_id + ", room=" + room_id
                             : "❌ 开始游戏失败: player=" + player_id + ", room=" + room_id);
        } else {
            LOG_WARNING("⚠️ room_operation_callback_ 未设置");
            nlohmann::json response = {
                {"type", "operation_result"},
                {"data", {
                    {"operation", "start_game"},
                    {"success", false},
                    {"error", "SERVER_ERROR"},
                    {"message", "服务器未配置房间操作处理器"}
                }}
            };
            sendToPlayer(player_id, response);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 开始游戏异常: " + std::string(e.what()));
        nlohmann::json response = {
            {"type", "operation_result"},
            {"data", {
                {"operation", "start_game"},
                {"success", false},
                {"error", "START_GAME_ERROR"},
                {"message", "开始游戏异常: " + std::string(e.what())}
            }}
        };
        sendToPlayer(player_id, response);
    }
}

// ==================== 房间操作方法实现结束 ====================

/**
 * @brief 验证游戏会话Token
 *
 * 注意：JWT验证由API网关统一处理，这里只进行基本格式检查
 */
bool GomokuWebSocketHandler::validateGameSessionToken(const std::string& token, int client_fd) {
    LOG_DEBUG("验证游戏会话Token: fd=" + std::to_string(client_fd));

    try {
        // 1. 基本格式检查
        if (token.empty()) {
            LOG_WARNING("Token为空");
            return false;
        }

        if (token.length() < 10) {
            LOG_WARNING("Token格式无效，长度过短: " + std::to_string(token.length()));
            return false;
        }

        // 2. JWT 格式检查（必须包含三个由点分隔的部分）
        size_t dot_count = std::count(token.begin(), token.end(), '.');
        if (dot_count != 2) {
            LOG_WARNING("Token格式无效，不是有效的JWT格式");
            return false;
        }

        // 3. JWT验证由API网关统一处理
        // 这里通过基本格式检查后即认为有效
        // 实际的JWT验证在API网关层完成
        LOG_DEBUG("Token格式检查通过，JWT验证由API网关处理");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("验证游戏会话Token异常: " + std::string(e.what()) +
                 ", fd=" + std::to_string(client_fd));
        return false;
    }
}

/**
 * @brief 创建玩家会话
 */
std::shared_ptr<game_base::PlayerSessionBase> GomokuWebSocketHandler::createPlayerSession(
    const std::string& player_id, int client_fd, const std::string& client_ip) {
    
    LOG_INFO("创建玩家会话: " + player_id + ", fd=" + std::to_string(client_fd));
    
    try {
        // [FIX] 修复：创建具体的五子棋玩家会话
        LOG_DEBUG("准备创建五子棋玩家会话 - 客户端信息: fd=" +
                 std::to_string(client_fd) + ", ip=" + client_ip);
        
        // 创建五子棋玩家会话实例
        auto session = std::make_shared<GomokuPlayerSession>(player_id, client_fd, client_ip);
        
        // [FIX] 修复：设置发送回调，使用WebSocketHandler的sendRawMessage
        session->setSendCallback([this](const std::string& player_id, const std::string& message) -> bool {
            return this->sendRawMessage(player_id, message);
        });
        
        LOG_INFO("五子棋玩家会话创建成功: " + player_id + " (发送回调已设置)");
        return session;
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建玩家会话失败: " + std::string(e.what()));
        return nullptr;
    }
}

/**
 * @brief 获取支持的协议列表
 */
std::vector<std::string> GomokuWebSocketHandler::getSupportedProtocols() {
    return {"gomoku-protocol-v1", "game-protocol-v1", "websocket"};
}

/**
 * @brief 获取服务客户端管理器
 */
std::shared_ptr<ServiceClientManager> GomokuWebSocketHandler::getServiceClientManager() const {
    return service_client_manager_;
}

/**
 * @brief 设置玩家房间
 */
void GomokuWebSocketHandler::setPlayerRoom(const std::string& player_id, const std::string& room_id) {
    LOG_DEBUG("设置玩家房间: " + player_id + " -> " + room_id);
    
    try {
        std::lock_guard<std::shared_mutex> lock(players_mutex_);
        
        auto it = authenticated_players_.find(player_id);
        if (it != authenticated_players_.end()) {
            it->second.room_id = room_id;
            LOG_INFO("玩家房间设置成功: " + player_id + " -> " + room_id);
        } else {
            LOG_WARNING("未找到已认证的玩家: " + player_id);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("设置玩家房间失败: " + std::string(e.what()));
    }
}

/**
 * @brief 移除玩家房间
 */
void GomokuWebSocketHandler::removePlayerRoom(const std::string& player_id) {
    LOG_DEBUG("移除玩家房间: " + player_id);

    try {
        std::lock_guard<std::shared_mutex> lock(players_mutex_);

        auto it = authenticated_players_.find(player_id);
        if (it != authenticated_players_.end()) {
            it->second.room_id.clear();
            LOG_INFO("玩家房间移除成功: " + player_id);
        } else {
            LOG_WARNING("未找到已认证的玩家: " + player_id);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("移除玩家房间失败: " + std::string(e.what()));
    }
}

/**
 * @brief 获取玩家所在房间ID
 */
std::string GomokuWebSocketHandler::getPlayerRoom(const std::string& player_id) const {
    try {
        std::shared_lock<std::shared_mutex> lock(players_mutex_);

        auto it = authenticated_players_.find(player_id);
        if (it != authenticated_players_.end()) {
            return it->second.room_id;
        }

        // 也检查 room_mapping_mutex_ 中的映射
        std::shared_lock<std::shared_mutex> room_lock(room_mapping_mutex_);
        auto room_it = player_rooms_.find(player_id);
        if (room_it != player_rooms_.end()) {
            return room_it->second;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("获取玩家房间失败: " + std::string(e.what()));
    }

    return "";
}

/**
 * @brief 获取处理器统计信息
 */
nlohmann::json GomokuWebSocketHandler::getHandlerStats() const {
    nlohmann::json stats;
    
    try {
        std::shared_lock<std::shared_mutex> lock(players_mutex_);
        
        stats["total_authenticated_players"] = authenticated_players_.size();
        stats["handler_type"] = "GomokuWebSocketHandler";
        stats["version"] = "2.0.0";
        
        // 统计房间中的玩家数
        int players_in_rooms = 0;
        for (const auto& pair : authenticated_players_) {
            if (!pair.second.room_id.empty()) {
                players_in_rooms++;
            }
        }
        stats["players_in_rooms"] = players_in_rooms;
        
    } catch (const std::exception& e) {
        LOG_ERROR("获取处理器统计信息失败: " + std::string(e.what()));
        stats["error"] = e.what();
    }
    
    return stats;
}

// ==================== 匹配系统消息处理实现 ====================

/**
 * @brief 处理匹配消息（分发）
 */
void GomokuWebSocketHandler::handleMatchMessage(const std::string& player_id, const nlohmann::json& message) {
    LOG_DEBUG("🎮 处理匹配消息: player=" + player_id);

    match_messages_processed_.fetch_add(1);

    std::string msg_type = message.value("type", "");

    if (msg_type == "match_start") {
        handleMatchStart(player_id, message);
    } else if (msg_type == "match_cancel") {
        handleMatchCancel(player_id, message);
    } else if (msg_type == "get_match_status" || msg_type == "get_match_pool") {
        handleGetMatchPoolStatus(player_id, message);
    } else {
        LOG_WARNING("未知的匹配消息类型: " + msg_type);
        sendMatchStartResponse(player_id, false, "未知的匹配消息类型");
    }
}

/**
 * @brief 处理开始匹配请求
 */
void GomokuWebSocketHandler::handleMatchStart(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🎯 玩家请求开始匹配: " + player_id);

    try {
        if (!isPlayerAuthenticated(player_id)) {
            sendMatchStartResponse(player_id, false, "请先认证");
            return;
        }

        // 解析匹配参数
        auto data = message.value("data", nlohmann::json::object());
        std::string mode_str = data.value("mode", "ranked");
        std::string game_mode_str = data.value("gameMode", "freestyle");

        MatchMode mode = (mode_str == "casual") ? MatchMode::CASUAL : MatchMode::RANKED;
        GameMode game_mode = GomokuConfig::stringToGameMode(game_mode_str);

        // 调用匹配开始回调
        if (match_start_callback_) {
            bool success = match_start_callback_(player_id, mode, game_mode);
            if (success) {
                sendMatchStartResponse(player_id, true, "已加入匹配队列");
                LOG_INFO("✅ 玩家加入匹配队列: " + player_id + ", 模式: " + mode_str);
            } else {
                sendMatchStartResponse(player_id, false, "加入匹配队列失败");
            }
        } else {
            LOG_WARNING("⚠️ match_start_callback_ 未设置");
            sendMatchStartResponse(player_id, false, "服务器未配置匹配服务");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 开始匹配异常: " + std::string(e.what()));
        sendMatchStartResponse(player_id, false, "服务器内部错误");
    }
}

/**
 * @brief 处理取消匹配请求
 */
void GomokuWebSocketHandler::handleMatchCancel(const std::string& player_id, const nlohmann::json& message) {
    LOG_INFO("🚫 玩家请求取消匹配: " + player_id);

    try {
        if (match_cancel_callback_) {
            bool success = match_cancel_callback_(player_id);
            if (success) {
                sendMatchCancelResponse(player_id, true, "已取消匹配");
                LOG_INFO("✅ 玩家取消匹配成功: " + player_id);
            } else {
                sendMatchCancelResponse(player_id, false, "取消匹配失败，可能不在匹配队列中");
            }
        } else {
            LOG_WARNING("⚠️ match_cancel_callback_ 未设置");
            sendMatchCancelResponse(player_id, false, "服务器未配置匹配服务");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 取消匹配异常: " + std::string(e.what()));
        sendMatchCancelResponse(player_id, false, "服务器内部错误");
    }
}

/**
 * @brief 处理获取匹配池状态请求
 */
void GomokuWebSocketHandler::handleGetMatchPoolStatus(const std::string& player_id, const nlohmann::json& message) {
    LOG_DEBUG("📊 玩家请求匹配池状态: " + player_id);

    try {
        if (match_pool_status_callback_) {
            MatchPoolStatus status = match_pool_status_callback_();
            sendMatchPoolStatus(player_id, status);
        } else {
            LOG_WARNING("⚠️ match_pool_status_callback_ 未设置");
            nlohmann::json response = {
                {"type", "match_pool_status"},
                {"data", {
                    {"success", false},
                    {"error", "服务器未配置匹配服务"}
                }}
            };
            sendToPlayer(player_id, response);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("❌ 获取匹配池状态异常: " + std::string(e.what()));
        nlohmann::json response = {
            {"type", "match_pool_status"},
            {"data", {
                {"success", false},
                {"error", "服务器内部错误"}
            }}
        };
        sendToPlayer(player_id, response);
    }
}

/**
 * @brief 发送匹配开始确认
 */
void GomokuWebSocketHandler::sendMatchStartResponse(const std::string& player_id, bool success, const std::string& message) {
    nlohmann::json response = {
        {"type", "match_start_response"},
        {"data", {
            {"success", success},
            {"message", message},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }}
    };
    sendToPlayer(player_id, response);
}

/**
 * @brief 发送匹配取消确认
 */
void GomokuWebSocketHandler::sendMatchCancelResponse(const std::string& player_id, bool success, const std::string& message) {
    nlohmann::json response = {
        {"type", "match_cancel_response"},
        {"data", {
            {"success", success},
            {"message", message},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }}
    };
    sendToPlayer(player_id, response);
}

/**
 * @brief 发送匹配成功通知
 */
void GomokuWebSocketHandler::sendMatchFound(const std::string& player_id, const MatchResult& result) {
    nlohmann::json response = {
        {"type", "match_found"},
        {"data", result.toJson()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };
    sendToPlayer(player_id, response);
    LOG_INFO("🎉 已通知玩家匹配成功: " + player_id + ", match_id=" + result.match_id);
}

/**
 * @brief 发送匹配状态更新
 */
void GomokuWebSocketHandler::sendMatchStatusUpdate(const std::string& player_id, int wait_seconds, int pool_size) {
    nlohmann::json response = {
        {"type", "match_status_update"},
        {"data", {
            {"waitSeconds", wait_seconds},
            {"poolSize", pool_size},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }}
    };
    sendToPlayer(player_id, response);
}

/**
 * @brief 发送匹配池状态
 */
void GomokuWebSocketHandler::sendMatchPoolStatus(const std::string& player_id, const MatchPoolStatus& status) {
    nlohmann::json response = {
        {"type", "match_pool_status"},
        {"data", {
            {"success", true},
            {"status", status.toJson()},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }}
    };
    sendToPlayer(player_id, response);
}

/**
 * @brief 发送匹配超时通知
 */
void GomokuWebSocketHandler::sendMatchTimeout(const std::string& player_id) {
    nlohmann::json response = {
        {"type", "match_timeout"},
        {"data", {
            {"message", "匹配超时，请重新尝试"},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        }}
    };
    sendToPlayer(player_id, response);
    LOG_INFO("⏰ 已通知玩家匹配超时: " + player_id);
}

} // namespace gomoku
} // namespace game_services
