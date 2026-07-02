/**
 * @file leaderboard_manager.h
 * @brief 排行榜管理器 - 处理多类型排行榜的更新和查询
 * @author AI Assistant
 * @date 2026-02-21
 * @version 1.0.0
 *
 * 职责：
 * - 管理多类型排行榜（评分、连胜、时长等）
 * - 支持全球榜和周榜
 * - 实时更新排名
 * - 缓存优化
 */

#pragma once

#include "game_models.h"
#include <common/database/redis_pool.h>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

namespace core_services {
namespace game_service {

// 前向声明
class GameRepository;

/**
 * 排行榜类型
 */
enum class LeaderboardType {
    RATING,         // 评分排行榜
    WIN_STREAK,     // 连胜排行榜
    PLAYTIME,       // 游戏时长排行榜
    WIN_RATE,       // 胜率排行榜
    EXPERIENCE,     // 经验排行榜
    ACHIEVEMENTS    // 成就点数排行榜
};

/**
 * 排行榜范围
 */
enum class LeaderboardScope {
    GLOBAL,         // 全球榜
    WEEKLY,         // 周榜
    MONTHLY,        // 月榜
    SEASONAL        // 赛季榜
};

/**
 * 排行榜条目详情
 */
struct LeaderboardEntryDetail {
    int rank;                           // 排名
    std::string user_id;                // 用户ID
    std::string display_name;           // 显示名称
    int64_t score;                      // 分数
    std::string tier_name;              // 段位名称
    float win_rate;                     // 胜率
    int games_played;                   // 游戏场次
    std::string avatar_url;             // 头像URL

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LeaderboardEntryDetail,
        rank, user_id, display_name, score, tier_name,
        win_rate, games_played, avatar_url)
};

/**
 * 排行榜结果
 */
struct LeaderboardResult {
    std::string type;                           // 排行榜类型
    std::string scope;                          // 排行榜范围
    std::string updated_at;                     // 更新时间

    std::vector<LeaderboardEntryDetail> entries;  // 排行榜条目

    // 用户自己的排名（如果查询中指定了用户）
    std::optional<LeaderboardEntryDetail> my_rank;

    int64_t total_players;                      // 总玩家数

    // 自定义序列化以处理 std::optional
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = type;
        j["scope"] = scope;
        j["updated_at"] = updated_at;
        j["entries"] = entries;
        if (my_rank.has_value()) {
            j["my_rank"] = my_rank.value();
        }
        j["total_players"] = total_players;
        return j;
    }
};

/**
 * 排行榜更新数据
 */
struct LeaderboardUpdateData {
    std::string user_id;
    std::string display_name;
    std::string avatar_url;

    int64_t rating = 0;
    int64_t win_streak = 0;
    int64_t playtime_seconds = 0;
    float win_rate = 0.0f;
    int64_t experience = 0;
    int64_t achievement_points = 0;

    int total_games = 0;
    int wins = 0;
    std::string tier_name;
};

/**
 * 排行榜更新通知
 */
struct LeaderboardUpdateNotification {
    std::string user_id;
    LeaderboardType type;
    LeaderboardScope scope;
    int old_rank;
    int new_rank;
    int64_t score;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LeaderboardUpdateNotification,
        user_id, type, scope, old_rank, new_rank, score)
};

/**
 * 排行榜管理器配置
 */
struct LeaderboardManagerConfig {
    bool enabled = true;

    // 排行榜大小
    int max_entries = 1000;                    // 最大条目数
    int default_page_size = 50;                 // 默认分页大小

    // 缓存配置
    int cache_ttl_seconds = 300;                // 缓存过期时间（5分钟）
    bool enable_cache = true;

    // 更新配置
    bool enable_realtime_updates = true;        // 启用实时更新
    int batch_update_interval_ms = 1000;        // 批量更新间隔

    // 周榜/月榜配置
    bool enable_weekly_reset = true;            // 启用周榜重置
    bool enable_monthly_reset = true;           // 启用月榜重置

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LeaderboardManagerConfig,
        enabled, max_entries, default_page_size,
        cache_ttl_seconds, enable_cache, enable_realtime_updates)
};

/**
 * 排行榜更新回调
 */
using LeaderboardUpdateCallback = std::function<void(const LeaderboardUpdateNotification&)>;

/**
 * 排行榜管理器
 *
 * 管理游戏平台的排行榜系统，支持：
 * 1. 多类型排行榜（评分、连胜、时长等）
 * 2. 多范围排行榜（全球、周、月、赛季）
 * 3. Redis 缓存和实时更新
 * 4. 排名变化通知
 */
class LeaderboardManager {
public:
    /**
     * 构造函数
     */
    LeaderboardManager(
        std::shared_ptr<GameRepository> repository,
        std::shared_ptr<common::database::RedisPool> redis_pool,
        const LeaderboardManagerConfig& config = LeaderboardManagerConfig());

    ~LeaderboardManager();

    // ========== 初始化和配置 ==========

    /**
     * 初始化排行榜管理器
     */
    bool initialize();

    /**
     * 更新配置
     */
    void updateConfig(const LeaderboardManagerConfig& config);

    /**
     * 获取配置
     */
    const LeaderboardManagerConfig& getConfig() const { return config_; }

    // ========== 排行榜更新 ==========

    /**
     * 更新用户排行榜数据
     * @param data 更新数据
     */
    void updateLeaderboard(const LeaderboardUpdateData& data);

    /**
     * 批量更新排行榜
     * @param data_list 更新数据列表
     */
    void batchUpdateLeaderboard(const std::vector<LeaderboardUpdateData>& data_list);

    /**
     * 从游戏档案更新排行榜
     * @param profile 用户游戏档案
     */
    void updateFromProfile(const UserGameProfile& profile);

    // ========== 排行榜查询 ==========

    /**
     * 获取排行榜
     * @param type 排行榜类型
     * @param scope 排行榜范围
     * @param game_type 游戏类型（可选）
     * @param offset 偏移量
     * @param limit 数量限制
     * @param user_id 查询用户（用于获取自己的排名）
     * @return 排行榜结果
     */
    LeaderboardResult getLeaderboard(
        LeaderboardType type,
        LeaderboardScope scope = LeaderboardScope::GLOBAL,
        const std::string& game_type = "",
        int offset = 0,
        int limit = 50,
        const std::string& user_id = "");

    /**
     * 获取用户排名
     * @param user_id 用户ID
     * @param type 排行榜类型
     * @param scope 排行榜范围
     * @param game_type 游戏类型
     * @return 排名（0表示未上榜）
     */
    int getUserRank(
        const std::string& user_id,
        LeaderboardType type,
        LeaderboardScope scope = LeaderboardScope::GLOBAL,
        const std::string& game_type = "");

    /**
     * 获取用户排名详情
     * @param user_id 用户ID
     * @param type 排行榜类型
     * @param scope 排行榜范围
     * @param game_type 游戏类型
     * @return 排名详情（可能为空）
     */
    std::optional<LeaderboardEntryDetail> getUserRankDetail(
        const std::string& user_id,
        LeaderboardType type,
        LeaderboardScope scope = LeaderboardScope::GLOBAL,
        const std::string& game_type = "");

    /**
     * 获取用户周围排名（用于展示用户在排行榜中的位置）
     * @param user_id 用户ID
     * @param type 排行榜类型
     * @param scope 排行榜范围
     * @param game_type 游戏类型
     * @param range 前后范围数量
     * @return 排行榜结果
     */
    LeaderboardResult getUserSurroundingRank(
        const std::string& user_id,
        LeaderboardType type,
        LeaderboardScope scope = LeaderboardScope::GLOBAL,
        const std::string& game_type = "",
        int range = 5);

    // ========== 回调设置 ==========

    /**
     * 设置排行榜更新回调
     */
    void setUpdateCallback(LeaderboardUpdateCallback callback) {
        update_callback_ = std::move(callback);
    }

    // ========== 维护操作 ==========

    /**
     * 刷新排行榜缓存
     * @param type 排行榜类型（空表示所有）
     */
    void refreshCache(LeaderboardType type = static_cast<LeaderboardType>(-1));

    /**
     * 重置周榜
     */
    void resetWeeklyLeaderboards();

    /**
     * 重置月榜
     */
    void resetMonthlyLeaderboards();

    /**
     * 清理过期数据
     */
    void cleanupExpiredData();

    // ========== 统计信息 ==========

    /**
     * 获取统计信息
     */
    nlohmann::json getStatistics() const;

    // ========== 工具方法（公开） ==========

    /**
     * 类型转字符串
     */
    static std::string typeToString(LeaderboardType type);
    static LeaderboardType stringToType(const std::string& str);

    /**
     * 范围转字符串
     */
    static std::string scopeToString(LeaderboardScope scope);
    static LeaderboardScope stringToScope(const std::string& str);

private:
    std::shared_ptr<GameRepository> repository_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    LeaderboardManagerConfig config_;

    // 更新回调
    LeaderboardUpdateCallback update_callback_;

    // 缓存
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, LeaderboardResult> cache_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> cache_times_;

    // ========== 内部方法 ==========

    /**
     * 构建 Redis 排行榜键
     */
    std::string buildRedisKey(
        LeaderboardType type,
        LeaderboardScope scope,
        const std::string& game_type = "") const;

    /**
     * 构建缓存键
     */
    std::string buildCacheKey(
        LeaderboardType type,
        LeaderboardScope scope,
        const std::string& game_type,
        int offset,
        int limit) const;

    /**
     * 从缓存获取
     */
    std::optional<LeaderboardResult> getFromCache(const std::string& cache_key);

    /**
     * 保存到缓存
     */
    void saveToCache(const std::string& cache_key, const LeaderboardResult& result);

    /**
     * 使缓存失效
     */
    void invalidateCache(const std::string& pattern = "");

    /**
     * 获取当前周键
     */
    std::string getCurrentWeekKey() const;

    /**
     * 获取当前月键
     */
    std::string getCurrentMonthKey() const;

    /**
     * 获取排行榜条目详情
     */
    LeaderboardEntryDetail buildEntryDetail(
        const std::string& user_id,
        int rank,
        int64_t score,
        LeaderboardType type);
};

} // namespace game_service
} // namespace core_services
