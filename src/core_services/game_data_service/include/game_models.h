//
// Created by Microservice Team
// 游戏数据模型定义 - 简洁稳定架构
// @version 2.1.0 - 添加游戏结算相关模型
//

#ifndef GAME_MODELS_H
#define GAME_MODELS_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <optional>

namespace core_services {
namespace game_service {

// 时间格式化辅助函数
std::string timeToString(const std::chrono::system_clock::time_point& tp);
std::chrono::system_clock::time_point stringToTime(const std::string& str);

// 用户游戏档案
struct UserGameProfile {
    std::string user_id;
    std::string display_name;
    int level = 1;
    long experience_points = 0;
    std::string avatar_url;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_login_at;

    // 游戏统计
    int total_games = 0;
    int wins = 0;
    int losses = 0;
    int draws = 0;

    // 评分相关
    int current_rating = 1200;
    int peak_rating = 1200;
    int tier_level = 5;          // 段位等级 (0-15)
    int current_win_streak = 0;
    int best_win_streak = 0;

    // 序列化到JSON
    std::string toJson() const;
    // 从JSON反序列化
    static UserGameProfile fromJson(const std::string& json);
};

// 游戏成就
struct GameAchievement {
    std::string achievement_id;
    std::string user_id;
    std::string achievement_type; // "first_win", "streak_master", "level_up", etc.
    std::string title;
    std::string description;
    int points = 0;
    std::chrono::system_clock::time_point unlocked_at;

    std::string toJson() const;
    static GameAchievement fromJson(const std::string& json);
};

// 用户库存物品
struct UserInventoryItem {
    std::string inventory_id;  // 库存记录ID (数据库主键)
    std::string item_id;       // 物品定义ID
    std::string user_id;
    std::string item_type; // "powerup", "skin", "booster", etc.
    std::string item_name;
    int quantity = 1;
    std::map<std::string, std::string> metadata; // 物品属性
    std::chrono::system_clock::time_point acquired_at;

    std::string toJson() const;
    static UserInventoryItem fromJson(const std::string& json);
};

// 用户货币
struct UserCurrency {
    std::string user_id;
    std::string currency_type; // "coins", "gems", "tokens", etc.
    long amount = 0;
    std::chrono::system_clock::time_point last_updated_at;

    std::string toJson() const;
    static UserCurrency fromJson(const std::string& json);
};

// 排行榜条目
struct LeaderboardEntry {
    std::string entry_id;
    std::string user_id;
    std::string leaderboard_type; // "global", "weekly", "monthly", etc.
    std::string game_type; // "gomoku", "snake", etc.
    long score = 0;
    int rank_position = 0;
    std::string username;   // 用户名（来自数据库）
    std::string nickname;   // 昵称（来自数据库）
    std::string avatar_url; // 头像URL（来自数据库）
    std::map<std::string, std::string> extra_data; // 额外数据
    std::chrono::system_clock::time_point recorded_at;

    std::string toJson() const;
    static LeaderboardEntry fromJson(const std::string& json);
};

// ==================== 游戏结算相关模型 ====================

/**
 * 用户每日统计
 */
struct UserDailyStats {
    std::string user_id;
    int game_type_id = 1;        // 游戏类型ID
    std::string stat_date;        // 统计日期 (YYYY-MM-DD)
    int games_played = 0;         // 当日游戏场次
    int games_won = 0;            // 当日胜场
    int games_lost = 0;           // 当日败场
    int games_drawn = 0;          // 当日平局
    bool first_win_claimed = false; // 首胜奖励是否已领取
    int total_playtime_seconds = 0; // 当日总游戏时长
    int total_gold_earned = 0;    // 当日获得金币
    int total_honor_earned = 0;   // 当日获得荣誉
    int total_exp_earned = 0;     // 当日获得经验
    int rating_change = 0;        // 当日评分变化
    int peak_streak = 0;          // 当日最高连胜
    std::chrono::system_clock::time_point first_win_at; // 首胜时间
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;

    std::string toJson() const;
    static UserDailyStats fromJson(const std::string& json);
};

/**
 * 段位历史记录
 */
struct TierHistory {
    int64_t id = 0;
    std::string user_id;
    int game_type_id = 1;
    int tier_before = 0;          // 变化前段位
    int tier_after = 0;           // 变化后段位
    int rating_before = 0;        // 变化前评分
    int rating_after = 0;         // 变化后评分
    bool is_promotion = false;    // 是否升级
    std::string game_id;          // 触发变化的游戏ID
    std::chrono::system_clock::time_point created_at;

    std::string toJson() const;
    static TierHistory fromJson(const std::string& json);
};

/**
 * 奖励发放日志
 */
struct RewardLog {
    int64_t id = 0;
    std::string log_id;           // 日志唯一ID
    std::string user_id;
    int game_type_id = 1;
    std::string game_id;          // 游戏ID
    std::string reward_type;      // 奖励类型 (game_end/daily_first_win/streak_bonus)
    std::string currency_type;    // 货币类型 (GOLD/GEM/HONOR)
    int currency_amount = 0;      // 货币数量
    int experience = 0;           // 经验值
    int rating_change = 0;        // 评分变化
    std::string reason;           // 奖励原因
    std::string metadata;         // 额外元数据 (JSON)
    std::chrono::system_clock::time_point created_at;

    std::string toJson() const;
    static RewardLog fromJson(const std::string& json);
};

/**
 * 游戏结算记录
 */
struct GameSettlement {
    int64_t id = 0;
    std::string settlement_id;    // 结算ID
    std::string game_id;          // 游戏ID
    std::string game_type;        // 游戏类型 (gomoku/chess/etc)
    int game_type_id = 1;         // 游戏类型ID
    std::string game_mode;        // 游戏模式 (casual/ranked/tournament)
    int duration_seconds = 0;     // 游戏时长(秒)
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point ended_at;
    int player_count = 0;         // 玩家数量
    std::string settlement_data;  // 完整结算数据JSON
    std::chrono::system_clock::time_point created_at;

    std::string toJson() const;
    static GameSettlement fromJson(const std::string& json);
};

/**
 * 游戏结算玩家记录
 */
struct GameSettlementPlayer {
    int64_t id = 0;
    std::string settlement_id;    // 结算ID
    std::string user_id;          // 用户ID
    std::string result;           // 游戏结果 (win/loss/draw/surrender/timeout)
    int rating_before = 0;        // 评分变化前
    int rating_after = 0;         // 评分变化后
    int rating_change = 0;        // 评分变化量
    int tier_before = 0;          // 段位变化前
    int tier_after = 0;           // 段位变化后
    bool tier_changed = false;    // 段位是否变化
    int gold_earned = 0;          // 获得金币
    int gem_earned = 0;           // 获得宝石
    int honor_earned = 0;         // 获得荣誉
    int experience_earned = 0;    // 获得经验
    int win_streak_before = 0;    // 连胜变化前
    int win_streak_after = 0;     // 连胜变化后
    bool is_first_win_today = false; // 是否今日首胜
    std::string achievements_unlocked; // 解锁的成就列表JSON
    std::string bonuses;          // 额外奖励信息JSON
    std::chrono::system_clock::time_point created_at;

    std::string toJson() const;
    static GameSettlementPlayer fromJson(const std::string& json);
};

// API响应包装器
template<typename T>
struct ApiResponse {
    bool success = false;
    std::string message;
    T data;
    int code = 200;

    std::string toJson() const;
};

// 分页查询参数
struct PaginationParams {
    int page = 1;
    int limit = 20;
    std::string sort_by = "created_at";
    std::string sort_order = "desc"; // "asc" or "desc"

    // 计算偏移量
    int getOffset() const { return (page - 1) * limit; }
};

// 分页响应
template<typename T>
struct PaginatedResponse {
    std::vector<T> items;
    int total_count = 0;
    int page = 1;
    int limit = 20;
    int total_pages = 0;

    std::string toJson() const;
};

// 查询过滤器
struct QueryFilter {
    std::map<std::string, std::string> filters;
    std::string search_term;
    std::chrono::system_clock::time_point date_from;
    std::chrono::system_clock::time_point date_to;

    // 构建SQL WHERE子句
    std::string buildWhereClause() const;
};

} // namespace game_service
} // namespace core_services

#endif // GAME_MODELS_H
