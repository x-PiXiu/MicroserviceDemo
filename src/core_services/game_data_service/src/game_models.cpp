//
// Created by Microservice Team  
// 游戏数据模型实现 - 简洁稳定架构
//

#include "../include/game_models.h"
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include <ctime>

using json = nlohmann::json;

namespace core_services {
namespace game_service {

// 时间格式化辅助函数
std::string timeToString(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::chrono::system_clock::time_point stringToTime(const std::string& str) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// 段位等级转段位名称
std::string getTierName(int tier_level) {
    static const char* tier_names[] = {
        "Bronze III", "Bronze II", "Bronze I",      // 0, 1, 2
        "Silver III", "Silver II", "Silver I",      // 3, 4, 5
        "Gold III", "Gold II", "Gold I",            // 6, 7, 8
        "Platinum III", "Platinum II", "Platinum I",// 9, 10, 11
        "Diamond III", "Diamond II", "Diamond I",   // 12, 13, 14
        "Master"                                     // 15
    };

    if (tier_level >= 0 && tier_level < 16) {
        return tier_names[tier_level];
    }
    return "Bronze III";  // 默认
}

// 计算胜率
float calculateWinRate(int wins, int total_games) {
    if (total_games <= 0) {
        return 0.0f;
    }
    return static_cast<float>(wins) / static_cast<float>(total_games);
}

// UserGameProfile 实现
std::string UserGameProfile::toJson() const {
    json j;
    // 基础信息
    j["user_id"] = user_id;
    j["display_name"] = display_name;
    j["level"] = level;
    j["experience_points"] = experience_points;
    j["avatar_url"] = avatar_url;
    j["created_at"] = timeToString(created_at);
    j["last_login_at"] = timeToString(last_login_at);

    // 游戏统计
    j["total_games"] = total_games;
    j["wins"] = wins;
    j["losses"] = losses;
    j["draws"] = draws;

    // 评分相关
    j["current_rating"] = current_rating;
    j["peak_rating"] = peak_rating;
    j["tier_level"] = tier_level;
    j["current_win_streak"] = current_win_streak;
    j["best_win_streak"] = best_win_streak;

    // 计算字段
    j["tier_name"] = getTierName(tier_level);
    j["win_rate"] = calculateWinRate(wins, total_games);

    return j.dump();
}

UserGameProfile UserGameProfile::fromJson(const std::string& json_str) {
    json j = json::parse(json_str);
    UserGameProfile profile;

    // 基础信息
    profile.user_id = j.value("user_id", "");
    profile.display_name = j.value("display_name", "");
    profile.level = j.value("level", 1);
    profile.experience_points = j.value("experience_points", 0L);
    profile.avatar_url = j.value("avatar_url", "");

    // 游戏统计
    profile.total_games = j.value("total_games", 0);
    profile.wins = j.value("wins", 0);
    profile.losses = j.value("losses", 0);
    profile.draws = j.value("draws", 0);

    // 评分相关
    profile.current_rating = j.value("current_rating", 1200);
    profile.peak_rating = j.value("peak_rating", 1200);
    profile.tier_level = j.value("tier_level", 5);
    profile.current_win_streak = j.value("current_win_streak", 0);
    profile.best_win_streak = j.value("best_win_streak", 0);

    if (j.contains("created_at")) {
        profile.created_at = stringToTime(j["created_at"]);
    }
    if (j.contains("last_login_at")) {
        profile.last_login_at = stringToTime(j["last_login_at"]);
    }

    return profile;
}

// GameAchievement 实现
std::string GameAchievement::toJson() const {
    json j;
    j["achievement_id"] = achievement_id;
    j["user_id"] = user_id;
    j["achievement_type"] = achievement_type;
    j["title"] = title;
    j["description"] = description;
    j["points"] = points;
    j["unlocked_at"] = timeToString(unlocked_at);
    return j.dump();
}

GameAchievement GameAchievement::fromJson(const std::string& json_str) {
    json j = json::parse(json_str);
    GameAchievement achievement;
    achievement.achievement_id = j.value("achievement_id", "");
    achievement.user_id = j.value("user_id", "");
    achievement.achievement_type = j.value("achievement_type", "");
    achievement.title = j.value("title", "");
    achievement.description = j.value("description", "");
    achievement.points = j.value("points", 0);
    
    if (j.contains("unlocked_at")) {
        achievement.unlocked_at = stringToTime(j["unlocked_at"]);
    }
    
    return achievement;
}

// UserInventoryItem 实现
std::string UserInventoryItem::toJson() const {
    json j;
    j["item_id"] = item_id;
    j["user_id"] = user_id;
    j["item_type"] = item_type;
    j["item_name"] = item_name;
    j["quantity"] = quantity;
    j["metadata"] = metadata;
    j["acquired_at"] = timeToString(acquired_at);
    return j.dump();
}

UserInventoryItem UserInventoryItem::fromJson(const std::string& json_str) {
    json j = json::parse(json_str);
    UserInventoryItem item;
    item.item_id = j.value("item_id", "");
    item.user_id = j.value("user_id", "");
    item.item_type = j.value("item_type", "");
    item.item_name = j.value("item_name", "");
    item.quantity = j.value("quantity", 1);
    
    if (j.contains("metadata")) {
        item.metadata = j["metadata"].get<std::map<std::string, std::string>>();
    }
    if (j.contains("acquired_at")) {
        item.acquired_at = stringToTime(j["acquired_at"]);
    }
    
    return item;
}

// UserCurrency 实现
std::string UserCurrency::toJson() const {
    json j;
    j["user_id"] = user_id;
    j["currency_type"] = currency_type;
    j["amount"] = amount;
    j["last_updated_at"] = timeToString(last_updated_at);
    return j.dump();
}

UserCurrency UserCurrency::fromJson(const std::string& json_str) {
    json j = json::parse(json_str);
    UserCurrency currency;
    currency.user_id = j.value("user_id", "");
    currency.currency_type = j.value("currency_type", "");
    currency.amount = j.value("amount", 0L);
    
    if (j.contains("last_updated_at")) {
        currency.last_updated_at = stringToTime(j["last_updated_at"]);
    }
    
    return currency;
}

// LeaderboardEntry 实现
std::string LeaderboardEntry::toJson() const {
    json j;
    j["entry_id"] = entry_id;
    j["user_id"] = user_id;
    j["leaderboard_type"] = leaderboard_type;
    j["game_type"] = game_type;
    j["score"] = score;
    j["rank_position"] = rank_position;
    j["username"] = username;
    j["nickname"] = nickname;
    j["avatar_url"] = avatar_url;
    j["extra_data"] = extra_data;
    j["recorded_at"] = timeToString(recorded_at);
    return j.dump();
}

LeaderboardEntry LeaderboardEntry::fromJson(const std::string& json_str) {
    json j = json::parse(json_str);
    LeaderboardEntry entry;
    entry.entry_id = j.value("entry_id", "");
    entry.user_id = j.value("user_id", "");
    entry.leaderboard_type = j.value("leaderboard_type", "");
    entry.game_type = j.value("game_type", "");
    entry.score = j.value("score", 0L);
    entry.rank_position = j.value("rank_position", 0);
    entry.username = j.value("username", "");
    entry.nickname = j.value("nickname", "");
    entry.avatar_url = j.value("avatar_url", "");

    if (j.contains("extra_data")) {
        entry.extra_data = j["extra_data"].get<std::map<std::string, std::string>>();
    }
    if (j.contains("recorded_at")) {
        entry.recorded_at = stringToTime(j["recorded_at"]);
    }

    return entry;
}

// QueryFilter 实现
std::string QueryFilter::buildWhereClause() const {
    std::vector<std::string> conditions;
    
    for (const auto& filter : filters) {
        if (!filter.second.empty()) {
            conditions.push_back(filter.first + " = '" + filter.second + "'");
        }
    }
    
    if (!search_term.empty()) {
        conditions.push_back("(display_name LIKE '%" + search_term + "%' OR description LIKE '%" + search_term + "%')");
    }
    
    // 时间范围过滤器可以在需要时添加
    
    if (conditions.empty()) {
        return "1=1"; // 无条件
    }
    
    std::string result = conditions[0];
    for (size_t i = 1; i < conditions.size(); ++i) {
        result += " AND " + conditions[i];
    }
    
    return result;
}

// ApiResponse 模板特化（为了避免模板在头文件中的复杂性，这里提供一些常用的特化）
template<>
std::string ApiResponse<UserGameProfile>::toJson() const {
    json j;
    j["success"] = success;
    j["message"] = message;
    j["code"] = code;
    
    if (success) {
        j["data"] = json::parse(data.toJson());
    }
    
    return j.dump();
}

template<>
std::string ApiResponse<std::vector<UserGameProfile>>::toJson() const {
    json j;
    j["success"] = success;
    j["message"] = message;
    j["code"] = code;
    
    if (success) {
        json dataArray = json::array();
        for (const auto& profile : data) {
            dataArray.push_back(json::parse(profile.toJson()));
        }
        j["data"] = dataArray;
    }
    
    return j.dump();
}

template<>
std::string ApiResponse<GameAchievement>::toJson() const {
    json j;
    j["success"] = success;
    j["message"] = message;
    j["code"] = code;
    
    if (success) {
        j["data"] = json::parse(data.toJson());
    }
    
    return j.dump();
}

template<>
std::string ApiResponse<std::vector<GameAchievement>>::toJson() const {
    json j;
    j["success"] = success;
    j["message"] = message;
    j["code"] = code;
    
    if (success) {
        json dataArray = json::array();
        for (const auto& achievement : data) {
            dataArray.push_back(json::parse(achievement.toJson()));
        }
        j["data"] = dataArray;
    }
    
    return j.dump();
}

} // namespace game_service
} // namespace core_services
