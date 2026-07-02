/**
 * @file currency_manager.h
 * @brief 货币管理器 - 处理用户货币余额和交易
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 *
 * 职责：
 * - 管理用户三种货币（金币、宝石、荣誉值）的余额
 * - 处理货币增加和扣除
 * - 提供事务安全的货币操作
 * - 支持货币上限和下限检查
 */

#pragma once

#include "reward_config.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>

// 前向声明
namespace core_services {
namespace game_service {
    class GameRepository;
}
}

namespace core_services {
namespace game_service {

/**
 * 货币余额
 */
struct CurrencyBalance {
    std::string user_id;
    int gold = 0;
    int gem = 0;
    int honor = 0;
    std::chrono::system_clock::time_point updated_at;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CurrencyBalance, user_id, gold, gem, honor)

    // 获取指定类型的余额
    int get(CurrencyType type) const {
        switch (type) {
            case CurrencyType::GOLD: return gold;
            case CurrencyType::GEM: return gem;
            case CurrencyType::HONOR: return honor;
            default: return 0;
        }
    }

    // 设置指定类型的余额
    void set(CurrencyType type, int amount) {
        switch (type) {
            case CurrencyType::GOLD: gold = amount; break;
            case CurrencyType::GEM: gem = amount; break;
            case CurrencyType::HONOR: honor = amount; break;
        }
    }
};

/**
 * 货币交易记录
 */
struct CurrencyTransaction {
    std::string transaction_id;
    std::string user_id;
    CurrencyType currency_type;
    int amount;                     // 正数为增加，负数为扣除
    int balance_before;
    int balance_after;
    std::string reason;             // 交易原因
    std::string source;             // 来源（游戏、任务、商城等）
    std::string reference_id;       // 关联ID（游戏ID、订单ID等）
    std::chrono::system_clock::time_point created_at;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CurrencyTransaction,
        transaction_id, user_id, currency_type, amount,
        balance_before, balance_after, reason, source, reference_id)
};

/**
 * 货币交易请求
 */
struct CurrencyTransactionRequest {
    std::string user_id;
    CurrencyType currency_type;
    int amount;                     // 正数为增加，负数为扣除
    std::string reason;
    std::string source;
    std::string reference_id;
    bool allow_negative = false;    // 是否允许余额为负

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CurrencyTransactionRequest,
        user_id, currency_type, amount, reason, source, reference_id, allow_negative)
};

/**
 * 货币交易结果
 */
struct CurrencyTransactionResult {
    bool success = false;
    std::string error_message;
    CurrencyTransaction transaction;
    CurrencyBalance new_balance;

    // 工厂方法：创建成功结果（重命名为 ok 避免与成员变量 success 冲突）
    static CurrencyTransactionResult ok(const CurrencyTransaction& tx, const CurrencyBalance& balance) {
        CurrencyTransactionResult result;
        result.success = true;
        result.transaction = tx;
        result.new_balance = balance;
        return result;
    }

    // 工厂方法：创建失败结果
    static CurrencyTransactionResult failure(const std::string& error) {
        CurrencyTransactionResult result;
        result.success = false;
        result.error_message = error;
        return result;
    }
};

/**
 * 批量货币操作请求
 */
struct BatchCurrencyRequest {
    std::string user_id;
    std::vector<CurrencyReward> rewards;
    std::string reason;
    std::string source;
    std::string reference_id;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(BatchCurrencyRequest,
        user_id, rewards, reason, source, reference_id)
};

/**
 * 货币配置
 */
struct CurrencyConfig {
    // 各货币上限
    int max_gold = 9999999;
    int max_gem = 99999;
    int max_honor = 999999;

    // 各货币初始值
    int initial_gold = 1000;
    int initial_gem = 0;
    int initial_honor = 0;

    // 是否启用事务日志
    bool enable_transaction_log = true;

    // 交易历史保留天数
    int transaction_history_days = 30;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CurrencyConfig,
        max_gold, max_gem, max_honor,
        initial_gold, initial_gem, initial_honor,
        enable_transaction_log, transaction_history_days)
};

/**
 * 货币管理器
 *
 * 线程安全的货币管理，支持：
 * - 单用户货币操作
 * - 批量货币奖励发放
 * - 事务日志记录
 * - 余额缓存
 */
class CurrencyManager {
public:
    /**
     * 构造函数
     * @param repository 游戏数据仓储（用于持久化）
     * @param config 货币配置
     */
    CurrencyManager(std::shared_ptr<GameRepository> repository,
                    const CurrencyConfig& config = CurrencyConfig());

    /**
     * 析构函数
     */
    ~CurrencyManager();

    // ========== 余额查询 ==========

    /**
     * 获取用户货币余额
     * @param user_id 用户ID
     * @return 货币余额
     */
    CurrencyBalance getBalance(const std::string& user_id);

    /**
     * 获取用户指定货币类型的余额
     * @param user_id 用户ID
     * @param type 货币类型
     * @return 余额
     */
    int getBalance(const std::string& user_id, CurrencyType type);

    /**
     * 批量获取用户余额
     * @param user_ids 用户ID列表
     * @return 用户ID -> 余额映射
     */
    std::unordered_map<std::string, CurrencyBalance> getBalances(
        const std::vector<std::string>& user_ids);

    /**
     * 检查用户是否有足够的货币
     * @param user_id 用户ID
     * @param type 货币类型
     * @param amount 所需金额
     * @return 是否足够
     */
    bool hasEnough(const std::string& user_id, CurrencyType type, int amount);

    // ========== 货币操作 ==========

    /**
     * 执行货币交易
     * @param request 交易请求
     * @return 交易结果
     */
    CurrencyTransactionResult transact(const CurrencyTransactionRequest& request);

    /**
     * 增加货币
     * @param user_id 用户ID
     * @param type 货币类型
     * @param amount 增加金额（必须为正）
     * @param reason 原因
     * @param source 来源
     * @param reference_id 关联ID
     * @return 交易结果
     */
    CurrencyTransactionResult addCurrency(
        const std::string& user_id,
        CurrencyType type,
        int amount,
        const std::string& reason = "",
        const std::string& source = "",
        const std::string& reference_id = "");

    /**
     * 扣除货币
     * @param user_id 用户ID
     * @param type 货币类型
     * @param amount 扣除金额（必须为正）
     * @param reason 原因
     * @param source 来源
     * @param reference_id 关联ID
     * @return 交易结果
     */
    CurrencyTransactionResult deductCurrency(
        const std::string& user_id,
        CurrencyType type,
        int amount,
        const std::string& reason = "",
        const std::string& source = "",
        const std::string& reference_id = "");

    /**
     * 批量发放奖励
     * @param request 批量奖励请求
     * @return 各货币交易结果列表
     */
    std::vector<CurrencyTransactionResult> grantRewards(const BatchCurrencyRequest& request);

    /**
     * 从 Reward 对象发放奖励
     * @param user_id 用户ID
     * @param reward 奖励对象
     * @param source 来源
     * @param reference_id 关联ID
     * @return 交易结果列表
     */
    std::vector<CurrencyTransactionResult> grantReward(
        const std::string& user_id,
        const Reward& reward,
        const std::string& source = "",
        const std::string& reference_id = "");

    // ========== 交易历史 ==========

    /**
     * 获取用户交易历史
     * @param user_id 用户ID
     * @param limit 返回记录数
     * @param offset 偏移量
     * @return 交易记录列表
     */
    std::vector<CurrencyTransaction> getTransactionHistory(
        const std::string& user_id,
        int limit = 50,
        int offset = 0);

    /**
     * 获取用户指定货币类型的交易历史
     * @param user_id 用户ID
     * @param type 货币类型
     * @param limit 返回记录数
     * @param offset 偏移量
     * @return 交易记录列表
     */
    std::vector<CurrencyTransaction> getTransactionHistory(
        const std::string& user_id,
        CurrencyType type,
        int limit = 50,
        int offset = 0);

    // ========== 管理操作 ==========

    /**
     * 初始化用户货币（新用户注册时调用）
     * @param user_id 用户ID
     * @return 初始余额
     */
    CurrencyBalance initializeUser(const std::string& user_id);

    /**
     * 重置用户货币（仅用于测试或特殊管理操作）
     * @param user_id 用户ID
     * @param balance 新余额
     * @return 是否成功
     */
    bool resetBalance(const std::string& user_id, const CurrencyBalance& balance);

    /**
     * 清理缓存
     */
    void clearCache();

    /**
     * 刷新用户缓存
     * @param user_id 用户ID
     */
    void refreshCache(const std::string& user_id);

    // ========== 配置 ==========

    /**
     * 获取货币配置
     */
    const CurrencyConfig& getConfig() const { return config_; }

    /**
     * 更新货币配置
     */
    void updateConfig(const CurrencyConfig& config);

    /**
     * 获取货币上限
     */
    int getMaxBalance(CurrencyType type) const;

private:
    std::shared_ptr<GameRepository> repository_;
    CurrencyConfig config_;

    // 余额缓存
    std::unordered_map<std::string, CurrencyBalance> balance_cache_;
    mutable std::mutex cache_mutex_;

    // 生成交易ID
    std::string generateTransactionId();

    // 获取当前时间戳
    std::chrono::system_clock::time_point now() const {
        return std::chrono::system_clock::now();
    }

    // 验证金额
    bool validateAmount(int amount, bool allow_negative) const;

    // 应用上限
    int applyLimit(int value, CurrencyType type) const;

    // 从缓存获取余额
    std::optional<CurrencyBalance> getFromCache(const std::string& user_id) const;

    // 更新缓存
    void updateCache(const std::string& user_id, const CurrencyBalance& balance);

    // 从持久化加载余额
    CurrencyBalance loadFromRepository(const std::string& user_id);

    // 保存到持久化
    bool saveToRepository(const CurrencyBalance& balance);

    // 记录交易日志
    bool logTransaction(const CurrencyTransaction& transaction);
};

} // namespace game_service
} // namespace core_services
