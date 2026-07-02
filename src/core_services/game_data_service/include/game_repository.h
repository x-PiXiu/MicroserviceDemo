//
// Created by Microservice Team
// 游戏数据访问层 - 简洁稳定架构
// @version 2.0.0 - 集成统一 Redis Repository 模式
//

#ifndef GAME_REPOSITORY_H
#define GAME_REPOSITORY_H

#include "game_models.h"
#include "game_data_redis_repository.h"
#include <memory>
#include <vector>
#include <optional>
#include <future>

// 前向声明
namespace common {
namespace database {
class MySQLPool;
class RedisPool;
}
}

namespace core_services {
namespace game_service {

/**
 * 游戏数据访问层
 * 
 * 职责：
 * - 提供所有数据库操作接口
 * - 使用RAII管理数据库连接
 * - 提供缓存层支持
 * - 确保数据一致性
 */
class GameRepository {
public:
    /**
     * 构造函数
     * @param mysql_pool MySQL连接池
     * @param redis_pool Redis连接池  
     */
    GameRepository(
        std::shared_ptr<common::database::MySQLPool> mysql_pool,
        std::shared_ptr<common::database::RedisPool> redis_pool
    );
    
    ~GameRepository();

    // 禁用拷贝构造和赋值
    GameRepository(const GameRepository&) = delete;
    GameRepository& operator=(const GameRepository&) = delete;

    // === 用户游戏档案操作 ===
    
    /**
     * 创建用户游戏档案
     * @param profile 用户档案
     * @return 是否成功
     */
    bool createUserProfile(const UserGameProfile& profile);
    
    /**
     * 获取用户游戏档案
     * @param user_id 用户ID
     * @return 用户档案（可能为空）
     */
    std::optional<UserGameProfile> getUserProfile(const std::string& user_id);
    
    /**
     * 更新用户游戏档案
     * @param profile 用户档案
     * @return 是否成功
     */
    bool updateUserProfile(const UserGameProfile& profile);
    
    /**
     * 删除用户游戏档案
     * @param user_id 用户ID
     * @return 是否成功
     */
    bool deleteUserProfile(const std::string& user_id);
    
    /**
     * 分页获取用户档案列表
     * @param params 分页参数
     * @param filter 过滤条件
     * @return 分页结果
     */
    PaginatedResponse<UserGameProfile> getUserProfiles(
        const PaginationParams& params, 
        const QueryFilter& filter = {}
    );

    // === 游戏成就操作 ===

    /**
     * 解锁成就
     * @param achievement 成就
     * @return 是否成功
     */
    bool unlockAchievement(const GameAchievement& achievement);

    /**
     * 获取用户成就列表
     * @param user_id 用户ID
     * @param params 分页参数
     * @return 成就列表
     */
    PaginatedResponse<GameAchievement> getUserAchievements(
        const std::string& user_id,
        const PaginationParams& params
    );

    /**
     * 检查用户是否已解锁特定成就
     * @param user_id 用户ID
     * @param achievement_type 成就类型
     * @return 是否已解锁
     */
    bool hasUserUnlockedAchievement(const std::string& user_id, const std::string& achievement_type);

    /**
     * 确保成就定义存在于数据库
     * @param achievement_id 成就ID
     * @param name 成就名称
     * @param description 成就描述
     * @param points 成就点数
     * @return 是否成功
     */
    bool ensureAchievementDefinition(
        const std::string& achievement_id,
        const std::string& name,
        const std::string& description,
        int points
    );

    // === 用户库存操作 ===
    
    /**
     * 添加物品到用户库存
     * @param item 库存物品
     * @return 是否成功
     */
    bool addInventoryItem(const UserInventoryItem& item);
    
    /**
     * 获取用户库存
     * @param user_id 用户ID
     * @param params 分页参数
     * @return 库存物品列表
     */
    PaginatedResponse<UserInventoryItem> getUserInventory(
        const std::string& user_id, 
        const PaginationParams& params
    );
    
    /**
     * 使用/移除库存物品
     * @param user_id 用户ID
     * @param item_id 物品ID
     * @param quantity 数量（默认1）
     * @return 是否成功
     */
    bool useInventoryItem(const std::string& user_id, const std::string& item_id, int quantity = 1);
    
    /**
     * 获取特定物品数量
     * @param user_id 用户ID
     * @param item_type 物品类型
     * @return 物品数量
     */
    int getItemQuantity(const std::string& user_id, const std::string& item_type);

    // === 用户货币操作 ===
    
    /**
     * 获取用户货币
     * @param user_id 用户ID
     * @param currency_type 货币类型
     * @return 货币信息（可能为空）
     */
    std::optional<UserCurrency> getUserCurrency(const std::string& user_id, const std::string& currency_type);
    
    /**
     * 更新用户货币
     * @param currency 货币信息
     * @return 是否成功
     */
    bool updateUserCurrency(const UserCurrency& currency);
    
    /**
     * 增加用户货币
     * @param user_id 用户ID
     * @param currency_type 货币类型
     * @param amount 增加数量
     * @return 是否成功
     */
    bool addUserCurrency(const std::string& user_id, const std::string& currency_type, long amount);
    
    /**
     * 扣减用户货币
     * @param user_id 用户ID
     * @param currency_type 货币类型
     * @param amount 扣减数量
     * @return 是否成功
     */
    bool deductUserCurrency(const std::string& user_id, const std::string& currency_type, long amount);
    
    /**
     * 获取用户所有货币
     * @param user_id 用户ID
     * @return 货币列表
     */
    std::vector<UserCurrency> getAllUserCurrencies(const std::string& user_id);

    // === 排行榜操作 ===

    /**
     * 更新排行榜条目
     * @param entry 排行榜条目
     * @return 是否成功
     */
    bool updateLeaderboardEntry(const LeaderboardEntry& entry);

    /**
     * 获取排行榜
     * @param leaderboard_type 排行榜类型
     * @param game_type 游戏类型
     * @param params 分页参数
     * @return 排行榜条目
     */
    PaginatedResponse<LeaderboardEntry> getLeaderboard(
        const std::string& leaderboard_type,
        const std::string& game_type,
        const PaginationParams& params
    );

    /**
     * 获取用户在排行榜中的排名
     * @param user_id 用户ID
     * @param leaderboard_type 排行榜类型
     * @param game_type 游戏类型
     * @return 排名（0表示未上榜）
     */
    int getUserRank(const std::string& user_id, const std::string& leaderboard_type, const std::string& game_type);

    // ==================== 游戏结算操作 ====================

    /**
     * 保存游戏结算记录
     * @param settlement 游戏结算记录
     * @return 是否成功
     */
    bool saveGameSettlement(const GameSettlement& settlement);

    /**
     * 保存游戏结算玩家记录
     * @param player 结算玩家记录
     * @return 是否成功
     */
    bool saveGameSettlementPlayer(const GameSettlementPlayer& player);

    /**
     * 批量保存游戏结算玩家记录
     * @param players 结算玩家记录列表
     * @return 是否成功
     */
    bool saveGameSettlementPlayers(const std::vector<GameSettlementPlayer>& players);

    /**
     * 获取游戏结算记录
     * @param game_id 游戏ID
     * @return 结算记录（可能为空）
     */
    std::optional<GameSettlement> getGameSettlement(const std::string& game_id);

    /**
     * 获取用户游戏结算历史
     * @param user_id 用户ID
     * @param params 分页参数
     * @return 结算记录列表
     */
    PaginatedResponse<GameSettlement> getUserGameSettlements(
        const std::string& user_id,
        const PaginationParams& params
    );

    // ==================== 每日统计操作 ====================

    /**
     * 获取用户每日统计
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @param date 日期 (YYYY-MM-DD)，空表示今天
     * @return 每日统计（可能为空）
     */
    std::optional<UserDailyStats> getUserDailyStats(
        const std::string& user_id,
        int game_type_id,
        const std::string& date = "");

    /**
     * 更新用户每日统计
     * @param stats 每日统计
     * @return 是否成功
     */
    bool updateUserDailyStats(const UserDailyStats& stats);

    /**
     * 增量更新用户每日统计
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @param games_played 增加的游戏场次
     * @param games_won 增加的胜场
     * @param games_lost 增加的败场
     * @param games_drawn 增加的平局
     * @param gold_earned 获得金币
     * @param honor_earned 获得荣誉
     * @param exp_earned 获得经验
     * @param rating_change 评分变化
     * @param is_win 是否胜利
     * @return 是否成功
     */
    bool incrementUserDailyStats(
        const std::string& user_id,
        int game_type_id,
        int games_played = 1,
        int games_won = 0,
        int games_lost = 0,
        int games_drawn = 0,
        int gold_earned = 0,
        int honor_earned = 0,
        int exp_earned = 0,
        int rating_change = 0,
        bool is_win = false
    );

    /**
     * 标记首胜已领取
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @return 是否成功
     */
    bool claimFirstWinReward(const std::string& user_id, int game_type_id);

    // ==================== 段位历史操作 ====================

    /**
     * 记录段位变化历史
     * @param history 段位历史
     * @return 是否成功
     */
    bool recordTierHistory(const TierHistory& history);

    /**
     * 获取用户段位变化历史
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @param params 分页参数
     * @return 段位历史列表
     */
    PaginatedResponse<TierHistory> getUserTierHistory(
        const std::string& user_id,
        int game_type_id,
        const PaginationParams& params
    );

    // ==================== 奖励日志操作 ====================

    /**
     * 记录奖励发放日志
     * @param log 奖励日志
     * @return 是否成功
     */
    bool recordRewardLog(const RewardLog& log);

    /**
     * 获取用户奖励历史
     * @param user_id 用户ID
     * @param params 分页参数
     * @param reward_type 奖励类型过滤（可选）
     * @return 奖励日志列表
     */
    PaginatedResponse<RewardLog> getUserRewardHistory(
        const std::string& user_id,
        const PaginationParams& params,
        const std::string& reward_type = ""
    );

    // ==================== 用户档案扩展操作 ====================

    /**
     * 更新用户评分
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @param new_rating 新评分
     * @param rating_change 评分变化量
     * @return 是否成功
     */
    bool updateUserRating(const std::string& user_id, int game_type_id, int new_rating, int rating_change);

    /**
     * 更新用户游戏统计
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @param result 游戏结果 (win/loss/draw)
     * @param duration_seconds 游戏时长
     * @param new_streak 新连胜数
     * @return 是否成功
     */
    bool updateUserGameStats(
        const std::string& user_id,
        int game_type_id,
        const std::string& result,
        int duration_seconds,
        int new_streak
    );

    /**
     * 获取用户游戏档案（按游戏类型）
     * @param user_id 用户ID
     * @param game_type_id 游戏类型ID
     * @return 用户档案（可能为空）
     */
    std::optional<UserGameProfile> getUserGameProfileByType(const std::string& user_id, int game_type_id);

    // === 数据库管理 ===

    /**
     * 测试数据库连接
     * @return 是否连接成功
     */
    bool testDatabaseConnection();

    /**
     * 初始化数据库表结构
     * @return 是否成功
     */
    bool initializeTables();

    /**
     * 清理过期数据
     * @param days_old 保留天数
     * @return 清理的记录数
     */
    int cleanupExpiredData(int days_old = 90);

    // === 定时任务相关操作 ===

    /**
     * 清理过期的缓存数据
     * @param pattern 缓存键模式（如 "game_service:*"）
     * @return 清理的缓存键数量
     */
    int cleanupExpiredCache(const std::string& pattern = "game_service:*");

    /**
     * 归档旧的游戏数据
     * @param days_old 归档多少天前的数据
     * @return 归档的记录数
     */
    int archiveOldGameRecords(int days_old = 30);

    /**
     * 清理过期的库存物品
     * @param days_old 过期天数
     * @return 清理的物品数量
     */
    int cleanupExpiredInventoryItems(int days_old = 90);

    /**
     * 审计货币系统一致性
     * @return 发现的不一致数量
     */
    int auditCurrencyConsistency();

    /**
     * 重新计算排行榜排名
     * @param leaderboard_type 排行榜类型（空表示所有）
     * @param game_type 游戏类型（空表示所有）
     * @return 更新的排名数量
     */
    int recalculateRankings(const std::string& leaderboard_type = "", const std::string& game_type = "");

private:
    // 数据库连接池
    std::shared_ptr<common::database::MySQLPool> mysql_pool_;
    std::shared_ptr<common::database::RedisPool> redis_pool_;

    // ==================== 新架构：统一 Redis Repository ====================
    // 使用类型化的 Redis Repository 替代直接 Redis 操作
    std::unique_ptr<UserGameProfileRedisRepository> profile_redis_;        ///< 用户档案缓存
    std::unique_ptr<UserCurrencyRedisRepository> currency_redis_;          ///< 用户货币缓存
    std::unique_ptr<UserInventoryRedisRepository> inventory_redis_;        ///< 用户库存缓存
    std::unique_ptr<GameAchievementRedisRepository> achievement_redis_;    ///< 游戏成就缓存

    // ==================== 旧架构（向后兼容，逐步废弃） ====================
    // 缓存相关的模板方法（将逐步迁移到 Redis Repository）
    /**
     * 构建缓存键
     * @param prefix 前缀
     * @param key 键
     * @return 完整缓存键
     */
    std::string buildCacheKey(const std::string& prefix, const std::string& key) const;

    /**
     * 从缓存获取数据
     * @param key 缓存键
     * @return 缓存数据（可能为空）
     */
    std::optional<std::string> getFromCache(const std::string& key) const;

    /**
     * 设置缓存数据
     * @param key 缓存键
     * @param value 缓存值
     * @param expire_seconds 过期时间（秒）
     * @return 是否成功
     */
    bool setToCache(const std::string& key, const std::string& value, int expire_seconds = 3600) const;

    /**
     * 删除缓存数据
     * @param key 缓存键
     * @return 是否成功
     */
    bool deleteFromCache(const std::string& key) const;

    // ==================== 新架构初始化方法 ====================
    /**
     * @brief 初始化 Redis Repository
     * @details 创建类型化的 Redis Repository 实例
     */
    void initializeRedisRepositories();

    /**
     * 生成UUID
     * @return UUID字符串
     */
    std::string generateUUID() const;

    /**
     * 获取当前时间戳字符串
     * @return 时间戳字符串
     */
    std::string getCurrentTimestamp() const;
};

} // namespace game_service
} // namespace core_services

#endif // GAME_REPOSITORY_H
