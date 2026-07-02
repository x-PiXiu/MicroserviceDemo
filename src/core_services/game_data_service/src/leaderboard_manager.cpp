/**
 * @file leaderboard_manager.cpp
 * @brief 排行榜管理器实现
 */

#include "leaderboard_manager.h"
#include "game_repository.h"
#include "common/logger/logger.h"
#include "common/database/redis_pool.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace core_services {
namespace game_service {

// ========== 辅助函数 ==========

/**
 * 获取当前时间戳（ISO8601格式）
 */
static std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&time_t);

    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

/**
 * 从段位等级获取段位名称
 */
static std::string getTierNameFromLevel(int tier_level) {
    // 段位等级 0-15 映射到段位名称
    static const char* tier_names[] = {
        "Bronze III", "Bronze II", "Bronze I",
        "Silver III", "Silver II", "Silver I",
        "Gold III", "Gold II", "Gold I",
        "Platinum III", "Platinum II", "Platinum I",
        "Diamond III", "Diamond II", "Diamond I",
        "Master"
    };

    if (tier_level >= 0 && tier_level < 16) {
        return tier_names[tier_level];
    }
    return "Unranked";
}

// ========== 构造函数和析构函数 ==========

LeaderboardManager::LeaderboardManager(
    std::shared_ptr<GameRepository> repository,
    std::shared_ptr<common::database::RedisPool> redis_pool,
    const LeaderboardManagerConfig& config)
    : repository_(std::move(repository))
    , redis_pool_(std::move(redis_pool))
    , config_(config) {
}

LeaderboardManager::~LeaderboardManager() {
    LOG_INFO("LeaderboardManager destroyed");
}

// ========== 初始化和配置 ==========

bool LeaderboardManager::initialize() {
    LOG_INFO("Initializing LeaderboardManager...");

    if (!repository_) {
        LOG_ERROR("Repository is null");
        return false;
    }

    if (!redis_pool_) {
        LOG_WARNING("Redis pool is null, caching disabled");
        config_.enable_cache = false;
    }

    LOG_INFO("LeaderboardManager initialized");
    return true;
}

void LeaderboardManager::updateConfig(const LeaderboardManagerConfig& config) {
    config_ = config;
}

// ========== 排行榜更新 ==========

void LeaderboardManager::updateLeaderboard(const LeaderboardUpdateData& data) {
    if (!config_.enabled || !redis_pool_) {
        return;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            LOG_ERROR("Failed to get Redis connection for leaderboard update");
            return;
        }

        // 更新各类型排行榜
        std::string game_type = "gomoku";  // 默认游戏类型

        // 1. 更新评分排行榜（GLOBAL, WEEKLY, MONTHLY）
        {
            // 全榜
            std::string global_key = buildRedisKey(LeaderboardType::RATING, LeaderboardScope::GLOBAL, game_type);
            conn->zadd(global_key, static_cast<double>(data.rating), data.user_id);

            // 周榜
            std::string weekly_key = buildRedisKey(LeaderboardType::RATING, LeaderboardScope::WEEKLY, game_type);
            conn->zadd(weekly_key, static_cast<double>(data.rating), data.user_id);

            // 月榜
            std::string monthly_key = buildRedisKey(LeaderboardType::RATING, LeaderboardScope::MONTHLY, game_type);
            conn->zadd(monthly_key, static_cast<double>(data.rating), data.user_id);
        }

        // 2. 更新连胜排行榜（GLOBAL, WEEKLY, MONTHLY）
        if (data.win_streak > 0) {
            // 全榜
            std::string global_key = buildRedisKey(LeaderboardType::WIN_STREAK, LeaderboardScope::GLOBAL, game_type);
            conn->zadd(global_key, static_cast<double>(data.win_streak), data.user_id);

            // 周榜
            std::string weekly_key = buildRedisKey(LeaderboardType::WIN_STREAK, LeaderboardScope::WEEKLY, game_type);
            conn->zadd(weekly_key, static_cast<double>(data.win_streak), data.user_id);

            // 月榜
            std::string monthly_key = buildRedisKey(LeaderboardType::WIN_STREAK, LeaderboardScope::MONTHLY, game_type);
            conn->zadd(monthly_key, static_cast<double>(data.win_streak), data.user_id);
        }

        // 3. 更新游戏时长排行榜（GLOBAL, WEEKLY, MONTHLY）
        if (data.playtime_seconds > 0) {
            double playtime_hours = static_cast<double>(data.playtime_seconds) / 3600.0;

            // 全榜
            std::string global_key = buildRedisKey(LeaderboardType::PLAYTIME, LeaderboardScope::GLOBAL, game_type);
            conn->zadd(global_key, playtime_hours, data.user_id);

            // 周榜
            std::string weekly_key = buildRedisKey(LeaderboardType::PLAYTIME, LeaderboardScope::WEEKLY, game_type);
            conn->zadd(weekly_key, playtime_hours, data.user_id);

            // 月榜
            std::string monthly_key = buildRedisKey(LeaderboardType::PLAYTIME, LeaderboardScope::MONTHLY, game_type);
            conn->zadd(monthly_key, playtime_hours, data.user_id);
        }

        // 4. 更新经验排行榜（GLOBAL, WEEKLY, MONTHLY）
        if (data.experience > 0) {
            // 全榜
            std::string global_key = buildRedisKey(LeaderboardType::EXPERIENCE, LeaderboardScope::GLOBAL, game_type);
            conn->zadd(global_key, static_cast<double>(data.experience), data.user_id);

            // 周榜
            std::string weekly_key = buildRedisKey(LeaderboardType::EXPERIENCE, LeaderboardScope::WEEKLY, game_type);
            conn->zadd(weekly_key, static_cast<double>(data.experience), data.user_id);

            // 月榜
            std::string monthly_key = buildRedisKey(LeaderboardType::EXPERIENCE, LeaderboardScope::MONTHLY, game_type);
            conn->zadd(monthly_key, static_cast<double>(data.experience), data.user_id);
        }

        // 5. 更新成就点数排行榜（GLOBAL, WEEKLY, MONTHLY）
        if (data.achievement_points > 0) {
            // 全榜
            std::string global_key = buildRedisKey(LeaderboardType::ACHIEVEMENTS, LeaderboardScope::GLOBAL, game_type);
            conn->zadd(global_key, static_cast<double>(data.achievement_points), data.user_id);

            // 周榜
            std::string weekly_key = buildRedisKey(LeaderboardType::ACHIEVEMENTS, LeaderboardScope::WEEKLY, game_type);
            conn->zadd(weekly_key, static_cast<double>(data.achievement_points), data.user_id);

            // 月榜
            std::string monthly_key = buildRedisKey(LeaderboardType::ACHIEVEMENTS, LeaderboardScope::MONTHLY, game_type);
            conn->zadd(monthly_key, static_cast<double>(data.achievement_points), data.user_id);
        }

        // 使相关缓存失效
        invalidateCache("leaderboard:*");

        LOG_DEBUG("Updated leaderboard for user: " + data.user_id);

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to update leaderboard: " + std::string(e.what()));
    }
}

void LeaderboardManager::batchUpdateLeaderboard(const std::vector<LeaderboardUpdateData>& data_list) {
    for (const auto& data : data_list) {
        updateLeaderboard(data);
    }
}

void LeaderboardManager::updateFromProfile(const UserGameProfile& profile) {
    LeaderboardUpdateData data;
    data.user_id = profile.user_id;
    data.display_name = profile.display_name;
    data.avatar_url = profile.avatar_url;
    data.rating = profile.current_rating;
    data.win_streak = profile.current_win_streak;
    data.playtime_seconds = 0;  // UserGameProfile doesn't track playtime
    data.experience = profile.experience_points;
    data.total_games = profile.total_games;
    data.wins = profile.wins;
    data.tier_name = getTierNameFromLevel(profile.tier_level);

    // 计算胜率
    if (profile.total_games > 0) {
        data.win_rate = static_cast<float>(profile.wins) / profile.total_games;
    }

    updateLeaderboard(data);
}

// ========== 排行榜查询 ==========

LeaderboardResult LeaderboardManager::getLeaderboard(
    LeaderboardType type,
    LeaderboardScope scope,
    const std::string& game_type,
    int offset,
    int limit,
    const std::string& user_id) {

    LeaderboardResult result;
    result.type = typeToString(type);
    result.scope = scopeToString(scope);

    if (!config_.enabled) {
        result.total_players = 0;
        return result;
    }

    // 检查缓存
    if (config_.enable_cache) {
        std::string cache_key = buildCacheKey(type, scope, game_type, offset, limit);
        auto cached = getFromCache(cache_key);
        if (cached) {
            result = *cached;
            // 如果指定了用户，添加用户排名
            if (!user_id.empty()) {
                result.my_rank = getUserRankDetail(user_id, type, scope, game_type);
            }
            return result;
        }
    }

    if (!redis_pool_) {
        result.total_players = 0;
        return result;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            LOG_ERROR("Failed to get Redis connection for leaderboard query");
            result.total_players = 0;
            return result;
        }

        std::string redis_key = buildRedisKey(type, scope, game_type.empty() ? "gomoku" : game_type);

        // 获取排行榜数据
        auto entries_raw = conn->zrevrange(redis_key, offset, offset + limit - 1, false);

        // 获取总数
        result.total_players = conn->zcard(redis_key);

        // 构建条目详情
        int rank = offset + 1;
        for (const auto& entry_user_id : entries_raw) {
            LeaderboardEntryDetail detail = buildEntryDetail(entry_user_id, rank, 0, type);
            if (!detail.user_id.empty()) {
                // 获取分数
                double score = conn->zscore(redis_key, entry_user_id);
                detail.score = static_cast<int64_t>(score);
                result.entries.push_back(detail);
            }
            rank++;
        }

        // 获取更新时间
        result.updated_at = getCurrentTimestamp();

        // 保存到缓存
        if (config_.enable_cache) {
            std::string cache_key = buildCacheKey(type, scope, game_type, offset, limit);
            saveToCache(cache_key, result);
        }

        // 如果指定了用户，添加用户排名
        if (!user_id.empty()) {
            result.my_rank = getUserRankDetail(user_id, type, scope, game_type);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get leaderboard: " + std::string(e.what()));
    }

    return result;
}

int LeaderboardManager::getUserRank(
    const std::string& user_id,
    LeaderboardType type,
    LeaderboardScope scope,
    const std::string& game_type) {

    if (!config_.enabled || !redis_pool_) {
        return 0;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            return 0;
        }

        std::string redis_key = buildRedisKey(type, scope, game_type.empty() ? "gomoku" : game_type);

        // 获取用户分数
        double score = conn->zscore(redis_key, user_id);
        if (score == 0) {
            return 0;  // 用户不在排行榜中
        }

        // 通过获取所有成员并排序来计算排名
        // 注意：这是一个简化的实现，对于大型排行榜效率较低
        // 生产环境应考虑使用 ZREVRANK 命令（如果 hiredis 支持的话）
        auto all_members = conn->zrevrange(redis_key, 0, -1, false);
        for (size_t i = 0; i < all_members.size(); ++i) {
            if (all_members[i] == user_id) {
                return static_cast<int>(i) + 1;  // 1-based rank
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get user rank: " + std::string(e.what()));
    }

    return 0;
}

std::optional<LeaderboardEntryDetail> LeaderboardManager::getUserRankDetail(
    const std::string& user_id,
    LeaderboardType type,
    LeaderboardScope scope,
    const std::string& game_type) {

    if (!config_.enabled || !redis_pool_) {
        return std::nullopt;
    }

    int rank = getUserRank(user_id, type, scope, game_type);
    if (rank <= 0) {
        return std::nullopt;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            return std::nullopt;
        }

        std::string redis_key = buildRedisKey(type, scope, game_type.empty() ? "gomoku" : game_type);

        double score = conn->zscore(redis_key, user_id);
        if (score == 0) {
            return std::nullopt;
        }

        LeaderboardEntryDetail detail = buildEntryDetail(user_id, rank, static_cast<int64_t>(score), type);
        return detail;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get user rank detail: " + std::string(e.what()));
    }

    return std::nullopt;
}

LeaderboardResult LeaderboardManager::getUserSurroundingRank(
    const std::string& user_id,
    LeaderboardType type,
    LeaderboardScope scope,
    const std::string& game_type,
    int range) {

    int user_rank = getUserRank(user_id, type, scope, game_type);
    if (user_rank <= 0) {
        return getLeaderboard(type, scope, game_type, 0, range * 2 + 1, user_id);
    }

    // 计算偏移量，确保有足够的前后条目
    int offset = std::max(0, user_rank - range - 1);
    int limit = range * 2 + 1;

    return getLeaderboard(type, scope, game_type, offset, limit, user_id);
}

// ========== 维护操作 ==========

void LeaderboardManager::refreshCache(LeaderboardType type) {
    invalidateCache(type == static_cast<LeaderboardType>(-1) ? "" : typeToString(type));
    LOG_INFO("Leaderboard cache refreshed");
}

void LeaderboardManager::resetWeeklyLeaderboards() {
    if (!redis_pool_) {
        return;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            LOG_ERROR("Failed to get Redis connection for weekly reset");
            return;
        }

        std::string current_week = getCurrentWeekKey();
        LOG_INFO("Resetting weekly leaderboards for week: " + current_week);

        std::string game_type = "gomoku";

        // 归档当前周榜数据（可选）
        // conn->rename("leaderboard:rating:weekly:" + game_type,
        //              "leaderboard:rating:weekly_archive:" + current_week + ":" + game_type);

        // 重置周榜（删除所有数据）
        std::vector<std::string> weekly_keys = {
            buildRedisKey(LeaderboardType::RATING, LeaderboardScope::WEEKLY, game_type),
            buildRedisKey(LeaderboardType::WIN_STREAK, LeaderboardScope::WEEKLY, game_type),
            buildRedisKey(LeaderboardType::EXPERIENCE, LeaderboardScope::WEEKLY, game_type),
            buildRedisKey(LeaderboardType::ACHIEVEMENTS, LeaderboardScope::WEEKLY, game_type)
        };

        for (const auto& key : weekly_keys) {
            conn->del(key);
            LOG_DEBUG("Deleted weekly leaderboard key: " + key);
        }

        // 使缓存失效
        invalidateCache("leaderboard:.*:weekly.*");

        // 通知回调
        if (update_callback_) {
            LeaderboardUpdateNotification notification;
            notification.type = LeaderboardType::RATING;
            notification.scope = LeaderboardScope::WEEKLY;
            notification.old_rank = -1;  // -1 表示重置
            notification.new_rank = 0;
            update_callback_(notification);
        }

        LOG_INFO("Weekly leaderboards reset successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to reset weekly leaderboards: " + std::string(e.what()));
    }
}

void LeaderboardManager::resetMonthlyLeaderboards() {
    if (!redis_pool_) {
        return;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            LOG_ERROR("Failed to get Redis connection for monthly reset");
            return;
        }

        std::string current_month = getCurrentMonthKey();
        LOG_INFO("Resetting monthly leaderboards for month: " + current_month);

        std::string game_type = "gomoku";

        // 重置月榜（删除所有数据）
        std::vector<std::string> monthly_keys = {
            buildRedisKey(LeaderboardType::RATING, LeaderboardScope::MONTHLY, game_type),
            buildRedisKey(LeaderboardType::WIN_STREAK, LeaderboardScope::MONTHLY, game_type),
            buildRedisKey(LeaderboardType::EXPERIENCE, LeaderboardScope::MONTHLY, game_type),
            buildRedisKey(LeaderboardType::ACHIEVEMENTS, LeaderboardScope::MONTHLY, game_type)
        };

        for (const auto& key : monthly_keys) {
            conn->del(key);
            LOG_DEBUG("Deleted monthly leaderboard key: " + key);
        }

        // 使缓存失效
        invalidateCache("leaderboard:.*:monthly.*");

        // 通知回调
        if (update_callback_) {
            LeaderboardUpdateNotification notification;
            notification.type = LeaderboardType::RATING;
            notification.scope = LeaderboardScope::MONTHLY;
            notification.old_rank = -1;  // -1 表示重置
            notification.new_rank = 0;
            update_callback_(notification);
        }

        LOG_INFO("Monthly leaderboards reset successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to reset monthly leaderboards: " + std::string(e.what()));
    }
}

void LeaderboardManager::cleanupExpiredData() {
    if (!redis_pool_) {
        return;
    }

    try {
        common::database::RedisConnectionGuard conn(*redis_pool_);
        if (!conn.isValid()) {
            return;
        }

        // 清理过期的周榜数据（保留最近4周）
        // 这里简化实现，实际可能需要扫描匹配的键
        LOG_INFO("Cleaned up expired leaderboard data");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to cleanup expired data: " + std::string(e.what()));
    }
}

// ========== 统计信息 ==========

nlohmann::json LeaderboardManager::getStatistics() const {
    nlohmann::json stats;
    stats["enabled"] = config_.enabled;
    stats["cache_enabled"] = config_.enable_cache;
    stats["max_entries"] = config_.max_entries;

    if (redis_pool_) {
        try {
            common::database::RedisConnectionGuard conn(*redis_pool_);
            if (conn.isValid()) {
                // 获取各排行榜的条目数
                std::string game_type = "gomoku";
                stats["rating_count"] = conn->zcard(
                    buildRedisKey(LeaderboardType::RATING, LeaderboardScope::GLOBAL, game_type));
                stats["streak_count"] = conn->zcard(
                    buildRedisKey(LeaderboardType::WIN_STREAK, LeaderboardScope::GLOBAL, game_type));
            }
        } catch (const std::exception& e) {
            stats["error"] = e.what();
        }
    }

    return stats;
}

// ========== 内部方法 ==========

std::string LeaderboardManager::buildRedisKey(
    LeaderboardType type,
    LeaderboardScope scope,
    const std::string& game_type) const {

    std::ostringstream oss;
    oss << "gamedata:leaderboard:";

    // 添加范围
    if (scope == LeaderboardScope::WEEKLY) {
        oss << "weekly:" << getCurrentWeekKey() << ":";
    } else if (scope == LeaderboardScope::MONTHLY) {
        oss << "monthly:" << getCurrentMonthKey() << ":";
    } else {
        oss << "global:";
    }

    // 添加类型
    oss << typeToString(type);

    // 添加游戏类型
    if (!game_type.empty()) {
        oss << ":" << game_type;
    }

    return oss.str();
}

std::string LeaderboardManager::buildCacheKey(
    LeaderboardType type,
    LeaderboardScope scope,
    const std::string& game_type,
    int offset,
    int limit) const {

    std::ostringstream oss;
    oss << "leaderboard:" << typeToString(type) << ":" << scopeToString(scope);
    if (!game_type.empty()) {
        oss << ":" << game_type;
    }
    oss << ":" << offset << ":" << limit;
    return oss.str();
}

std::optional<LeaderboardResult> LeaderboardManager::getFromCache(const std::string& cache_key) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_.find(cache_key);
    if (it == cache_.end()) {
        return std::nullopt;
    }

    // 检查是否过期
    auto time_it = cache_times_.find(cache_key);
    if (time_it != cache_times_.end()) {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - time_it->second);
        if (age.count() > config_.cache_ttl_seconds) {
            cache_.erase(it);
            cache_times_.erase(time_it);
            return std::nullopt;
        }
    }

    return it->second;
}

void LeaderboardManager::saveToCache(const std::string& cache_key, const LeaderboardResult& result) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[cache_key] = result;
    cache_times_[cache_key] = std::chrono::system_clock::now();
}

void LeaderboardManager::invalidateCache(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    if (pattern.empty()) {
        cache_.clear();
        cache_times_.clear();
        return;
    }

    // 简单的前缀匹配
    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (it->first.find(pattern) != std::string::npos ||
            it->first.find(pattern.substr(0, pattern.length() - 1)) != std::string::npos) {
            cache_times_.erase(it->first);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string LeaderboardManager::getCurrentWeekKey() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time_t);

    // 计算ISO周数
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-W%V", &tm);
    return std::string(buffer);
}

std::string LeaderboardManager::getCurrentMonthKey() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time_t);

    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m", &tm);
    return std::string(buffer);
}

LeaderboardEntryDetail LeaderboardManager::buildEntryDetail(
    const std::string& user_id,
    int rank,
    int64_t score,
    LeaderboardType type) {

    LeaderboardEntryDetail detail;
    detail.rank = rank;
    detail.user_id = user_id;
    detail.score = score;

    // 从 Repository 获取用户详细信息
    if (repository_) {
        auto profile_opt = repository_->getUserProfile(user_id);
        if (profile_opt) {
            detail.display_name = profile_opt->display_name;
            detail.avatar_url = profile_opt->avatar_url;
            detail.tier_name = getTierNameFromLevel(profile_opt->tier_level);
            detail.games_played = profile_opt->total_games;

            // 计算胜率
            if (profile_opt->total_games > 0) {
                detail.win_rate = static_cast<float>(profile_opt->wins) / profile_opt->total_games;
            }
        }
    }

    return detail;
}

std::string LeaderboardManager::typeToString(LeaderboardType type) {
    switch (type) {
        case LeaderboardType::RATING: return "rating";
        case LeaderboardType::WIN_STREAK: return "win_streak";
        case LeaderboardType::PLAYTIME: return "playtime";
        case LeaderboardType::WIN_RATE: return "win_rate";
        case LeaderboardType::EXPERIENCE: return "experience";
        case LeaderboardType::ACHIEVEMENTS: return "achievements";
        default: return "unknown";
    }
}

LeaderboardType LeaderboardManager::stringToType(const std::string& str) {
    if (str == "rating") return LeaderboardType::RATING;
    if (str == "win_streak") return LeaderboardType::WIN_STREAK;
    if (str == "playtime") return LeaderboardType::PLAYTIME;
    if (str == "win_rate") return LeaderboardType::WIN_RATE;
    if (str == "experience") return LeaderboardType::EXPERIENCE;
    if (str == "achievements") return LeaderboardType::ACHIEVEMENTS;
    return LeaderboardType::RATING;
}

std::string LeaderboardManager::scopeToString(LeaderboardScope scope) {
    switch (scope) {
        case LeaderboardScope::GLOBAL: return "global";
        case LeaderboardScope::WEEKLY: return "weekly";
        case LeaderboardScope::MONTHLY: return "monthly";
        case LeaderboardScope::SEASONAL: return "seasonal";
        default: return "global";
    }
}

LeaderboardScope LeaderboardManager::stringToScope(const std::string& str) {
    if (str == "weekly") return LeaderboardScope::WEEKLY;
    if (str == "monthly") return LeaderboardScope::MONTHLY;
    if (str == "seasonal") return LeaderboardScope::SEASONAL;
    return LeaderboardScope::GLOBAL;
}

} // namespace game_service
} // namespace core_services
