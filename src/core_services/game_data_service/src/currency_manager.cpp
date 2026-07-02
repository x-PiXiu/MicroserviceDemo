/**
 * @file currency_manager.cpp
 * @brief 货币管理器实现
 * @author AI Assistant
 * @date 2026-02-20
 * @version 1.0.0
 */

#include "currency_manager.h"
#include "game_repository.h"
#include <common/logger/logger.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>

namespace core_services {
namespace game_service {

// ==================== CurrencyManager ====================

CurrencyManager::CurrencyManager(std::shared_ptr<GameRepository> repository,
                                 const CurrencyConfig& config)
    : repository_(repository)
    , config_(config) {
    if (!repository_) {
        throw std::invalid_argument("GameRepository cannot be null");
    }
}

CurrencyManager::~CurrencyManager() {
    // 清理缓存
    clearCache();
}

// ==================== 余额查询 ====================

CurrencyBalance CurrencyManager::getBalance(const std::string& user_id) {
    // 先检查缓存
    auto cached = getFromCache(user_id);
    if (cached) {
        return *cached;
    }

    // 从持久化加载
    CurrencyBalance balance = loadFromRepository(user_id);
    updateCache(user_id, balance);

    return balance;
}

int CurrencyManager::getBalance(const std::string& user_id, CurrencyType type) {
    return getBalance(user_id).get(type);
}

std::unordered_map<std::string, CurrencyBalance> CurrencyManager::getBalances(
    const std::vector<std::string>& user_ids) {

    std::unordered_map<std::string, CurrencyBalance> result;

    for (const auto& user_id : user_ids) {
        result[user_id] = getBalance(user_id);
    }

    return result;
}

bool CurrencyManager::hasEnough(const std::string& user_id, CurrencyType type, int amount) {
    if (amount <= 0) {
        return true;  // 非正数总是"足够"
    }
    return getBalance(user_id, type) >= amount;
}

// ==================== 货币操作 ====================

CurrencyTransactionResult CurrencyManager::transact(const CurrencyTransactionRequest& request) {
    // 验证金额
    if (!validateAmount(request.amount, request.allow_negative)) {
        return CurrencyTransactionResult::failure("Invalid amount: " + std::to_string(request.amount));
    }

    // 获取当前余额
    CurrencyBalance balance = getBalance(request.user_id);
    int current_balance = balance.get(request.currency_type);
    int new_balance = current_balance + request.amount;

    // 检查是否允许负余额
    if (!request.allow_negative && new_balance < 0) {
        return CurrencyTransactionResult::failure(
            "Insufficient balance. Current: " + std::to_string(current_balance) +
            ", Required: " + std::to_string(-request.amount));
    }

    // 应用上限
    new_balance = applyLimit(new_balance, request.currency_type);

    // 创建交易记录
    CurrencyTransaction transaction;
    transaction.transaction_id = generateTransactionId();
    transaction.user_id = request.user_id;
    transaction.currency_type = request.currency_type;
    transaction.amount = request.amount;
    transaction.balance_before = current_balance;
    transaction.balance_after = new_balance;
    transaction.reason = request.reason;
    transaction.source = request.source;
    transaction.reference_id = request.reference_id;
    transaction.created_at = now();

    // 更新余额
    balance.set(request.currency_type, new_balance);
    balance.updated_at = now();

    // 持久化
    if (!saveToRepository(balance)) {
        return CurrencyTransactionResult::failure("Failed to save balance");
    }

    // 记录交易日志
    if (config_.enable_transaction_log) {
        logTransaction(transaction);
    }

    // 更新缓存
    updateCache(request.user_id, balance);

    return CurrencyTransactionResult::ok(transaction, balance);
}

CurrencyTransactionResult CurrencyManager::addCurrency(
    const std::string& user_id,
    CurrencyType type,
    int amount,
    const std::string& reason,
    const std::string& source,
    const std::string& reference_id) {

    if (amount < 0) {
        return CurrencyTransactionResult::failure("Amount must be positive for addCurrency");
    }

    CurrencyTransactionRequest request;
    request.user_id = user_id;
    request.currency_type = type;
    request.amount = amount;
    request.reason = reason;
    request.source = source;
    request.reference_id = reference_id;
    request.allow_negative = false;

    return transact(request);
}

CurrencyTransactionResult CurrencyManager::deductCurrency(
    const std::string& user_id,
    CurrencyType type,
    int amount,
    const std::string& reason,
    const std::string& source,
    const std::string& reference_id) {

    if (amount < 0) {
        return CurrencyTransactionResult::failure("Amount must be positive for deductCurrency");
    }

    CurrencyTransactionRequest request;
    request.user_id = user_id;
    request.currency_type = type;
    request.amount = -amount;  // 扣除为负数
    request.reason = reason;
    request.source = source;
    request.reference_id = reference_id;
    request.allow_negative = false;

    return transact(request);
}

std::vector<CurrencyTransactionResult> CurrencyManager::grantRewards(const BatchCurrencyRequest& request) {
    std::vector<CurrencyTransactionResult> results;
    results.reserve(request.rewards.size());

    for (const auto& reward : request.rewards) {
        CurrencyTransactionRequest tx_request;
        tx_request.user_id = request.user_id;
        tx_request.currency_type = reward.type;
        tx_request.amount = reward.amount;
        tx_request.reason = request.reason.empty() ? reward.reason : request.reason;
        tx_request.source = request.source;
        tx_request.reference_id = request.reference_id;
        tx_request.allow_negative = false;

        results.push_back(transact(tx_request));
    }

    return results;
}

std::vector<CurrencyTransactionResult> CurrencyManager::grantReward(
    const std::string& user_id,
    const Reward& reward,
    const std::string& source,
    const std::string& reference_id) {

    BatchCurrencyRequest batch_request;
    batch_request.user_id = user_id;
    batch_request.rewards = reward.currencies;
    batch_request.source = source;
    batch_request.reference_id = reference_id;

    // 构建原因描述
    std::stringstream ss;
    if (!source.empty()) {
        ss << source << ": ";
    }
    ss << "Reward granted";
    if (reward.rating_change != 0) {
        ss << ", Rating: " << (reward.rating_change > 0 ? "+" : "") << reward.rating_change;
    }
    ss << ", EXP: " << reward.experience;
    batch_request.reason = ss.str();

    return grantRewards(batch_request);
}

// ==================== 交易历史 ====================

std::vector<CurrencyTransaction> CurrencyManager::getTransactionHistory(
    const std::string& user_id,
    int limit,
    int offset) {

    std::vector<CurrencyTransaction> history;

    try {
        if (!repository_) {
            return history;
        }

        // 通过 reward_logs 表查询交易历史
        PaginationParams params;
        params.page = (offset / limit) + 1;
        params.limit = limit;

        auto reward_history = repository_->getUserRewardHistory(user_id, params, "currency");

        for (const auto& log : reward_history.items) {
            CurrencyTransaction tx;
            tx.transaction_id = log.log_id;
            tx.user_id = log.user_id;
            tx.amount = log.currency_amount;
            tx.balance_before = 0;  // 历史记录中没有存储
            tx.balance_after = 0;
            tx.reason = log.reason;
            tx.source = log.reward_type;
            tx.reference_id = log.game_id;
            tx.created_at = log.created_at;

            // 解析货币类型
            if (log.currency_type == "GOLD") tx.currency_type = CurrencyType::GOLD;
            else if (log.currency_type == "GEM") tx.currency_type = CurrencyType::GEM;
            else if (log.currency_type == "HONOR") tx.currency_type = CurrencyType::HONOR;
            else tx.currency_type = CurrencyType::GOLD;

            history.push_back(tx);
        }

    } catch (const std::exception& e) {
        // 记录错误但返回空列表
    }

    return history;
}

std::vector<CurrencyTransaction> CurrencyManager::getTransactionHistory(
    const std::string& user_id,
    CurrencyType type,
    int limit,
    int offset) {

    std::vector<CurrencyTransaction> history;

    try {
        if (!repository_) {
            return history;
        }

        // 转换货币类型为字符串
        std::string currency_type_str;
        switch (type) {
            case CurrencyType::GOLD: currency_type_str = "GOLD"; break;
            case CurrencyType::GEM: currency_type_str = "GEM"; break;
            case CurrencyType::HONOR: currency_type_str = "HONOR"; break;
            default: currency_type_str = "GOLD";
        }

        PaginationParams params;
        params.page = (offset / limit) + 1;
        params.limit = limit;

        auto reward_history = repository_->getUserRewardHistory(user_id, params, "currency");

        for (const auto& log : reward_history.items) {
            // 过滤特定货币类型
            if (log.currency_type != currency_type_str) {
                continue;
            }

            CurrencyTransaction tx;
            tx.transaction_id = log.log_id;
            tx.user_id = log.user_id;
            tx.currency_type = type;
            tx.amount = log.currency_amount;
            tx.balance_before = 0;
            tx.balance_after = 0;
            tx.reason = log.reason;
            tx.source = log.reward_type;
            tx.reference_id = log.game_id;
            tx.created_at = log.created_at;

            history.push_back(tx);
        }

    } catch (const std::exception& e) {
        // 记录错误但返回空列表
    }

    return history;
}

// ==================== 管理操作 ====================

CurrencyBalance CurrencyManager::initializeUser(const std::string& user_id) {
    CurrencyBalance balance;
    balance.user_id = user_id;
    balance.gold = config_.initial_gold;
    balance.gem = config_.initial_gem;
    balance.honor = config_.initial_honor;
    balance.updated_at = now();

    // 保存到持久化
    saveToRepository(balance);

    // 更新缓存
    updateCache(user_id, balance);

    return balance;
}

bool CurrencyManager::resetBalance(const std::string& user_id, const CurrencyBalance& balance) {
    CurrencyBalance new_balance = balance;
    new_balance.user_id = user_id;
    new_balance.updated_at = now();

    // 应用上限
    new_balance.gold = applyLimit(new_balance.gold, CurrencyType::GOLD);
    new_balance.gem = applyLimit(new_balance.gem, CurrencyType::GEM);
    new_balance.honor = applyLimit(new_balance.honor, CurrencyType::HONOR);

    if (!saveToRepository(new_balance)) {
        return false;
    }

    updateCache(user_id, new_balance);
    return true;
}

void CurrencyManager::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    balance_cache_.clear();
}

void CurrencyManager::refreshCache(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    balance_cache_.erase(user_id);
}

// ==================== 配置 ====================

void CurrencyManager::updateConfig(const CurrencyConfig& config) {
    config_ = config;
}

int CurrencyManager::getMaxBalance(CurrencyType type) const {
    switch (type) {
        case CurrencyType::GOLD: return config_.max_gold;
        case CurrencyType::GEM: return config_.max_gem;
        case CurrencyType::HONOR: return config_.max_honor;
        default: return 0;
    }
}

// ==================== 私有方法 ====================

std::string CurrencyManager::generateTransactionId() {
    // 生成格式: TXN-YYYYMMDDHHMMSS-RANDOM
    auto now_time = now();
    auto now_t = std::chrono::system_clock::to_time_t(now_time);
    std::tm tm = *std::localtime(&now_t);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::stringstream ss;
    ss << "TXN-"
       << std::put_time(&tm, "%Y%m%d%H%M%S")
       << "-" << dis(gen);

    return ss.str();
}

bool CurrencyManager::validateAmount(int amount, bool allow_negative) const {
    if (allow_negative) {
        return true;  // 允许任何金额
    }
    // 不允许负余额时，金额可以是任何整数（扣除会变成负的交易）
    return true;
}

int CurrencyManager::applyLimit(int value, CurrencyType type) const {
    int max = getMaxBalance(type);
    // 应用上下限：不允许负余额，不允许超过上限
    return std::max(0, std::min(value, max));
}

std::optional<CurrencyBalance> CurrencyManager::getFromCache(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = balance_cache_.find(user_id);
    if (it != balance_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void CurrencyManager::updateCache(const std::string& user_id, const CurrencyBalance& balance) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    balance_cache_[user_id] = balance;
}

CurrencyBalance CurrencyManager::loadFromRepository(const std::string& user_id) {
    CurrencyBalance balance;
    balance.user_id = user_id;
    balance.updated_at = now();

    try {
        if (!repository_) {
            // 没有仓储，返回初始值
            balance.gold = config_.initial_gold;
            balance.gem = config_.initial_gem;
            balance.honor = config_.initial_honor;
            return balance;
        }

        // 从仓储加载各货币类型
        auto gold_opt = repository_->getUserCurrency(user_id, "GOLD");
        if (gold_opt.has_value()) {
            balance.gold = static_cast<int>(gold_opt->amount);
        } else {
            balance.gold = config_.initial_gold;
        }

        auto gem_opt = repository_->getUserCurrency(user_id, "GEM");
        if (gem_opt.has_value()) {
            balance.gem = static_cast<int>(gem_opt->amount);
        } else {
            balance.gem = config_.initial_gem;
        }

        auto honor_opt = repository_->getUserCurrency(user_id, "HONOR");
        if (honor_opt.has_value()) {
            balance.honor = static_cast<int>(honor_opt->amount);
        } else {
            balance.honor = config_.initial_honor;
        }

    } catch (const std::exception& e) {
        // 加载失败，使用初始值
        balance.gold = config_.initial_gold;
        balance.gem = config_.initial_gem;
        balance.honor = config_.initial_honor;
    }

    return balance;
}

bool CurrencyManager::saveToRepository(const CurrencyBalance& balance) {
    try {
        if (!repository_) {
            return true;  // 没有仓储，直接返回成功
        }

        bool success = true;

        // 保存各货币类型
        UserCurrency gold;
        gold.user_id = balance.user_id;
        gold.currency_type = "GOLD";
        gold.amount = balance.gold;
        if (!repository_->updateUserCurrency(gold)) {
            success = false;
        }

        UserCurrency gem;
        gem.user_id = balance.user_id;
        gem.currency_type = "GEM";
        gem.amount = balance.gem;
        if (!repository_->updateUserCurrency(gem)) {
            success = false;
        }

        UserCurrency honor;
        honor.user_id = balance.user_id;
        honor.currency_type = "HONOR";
        honor.amount = balance.honor;
        if (!repository_->updateUserCurrency(honor)) {
            success = false;
        }

        return success;

    } catch (const std::exception& e) {
        return false;
    }
}

bool CurrencyManager::logTransaction(const CurrencyTransaction& transaction) {
    try {
        if (!repository_) {
            return true;  // 没有仓储，直接返回成功
        }

        // 转换货币类型为字符串
        std::string currency_type_str;
        switch (transaction.currency_type) {
            case CurrencyType::GOLD: currency_type_str = "GOLD"; break;
            case CurrencyType::GEM: currency_type_str = "GEM"; break;
            case CurrencyType::HONOR: currency_type_str = "HONOR"; break;
            default: currency_type_str = "GOLD";
        }

        // 创建奖励日志
        RewardLog log;
        log.log_id = transaction.transaction_id;
        log.user_id = transaction.user_id;
        log.game_type_id = 1;  // 默认游戏类型
        log.game_id = transaction.reference_id;
        log.reward_type = transaction.source.empty() ? "currency" : transaction.source;
        log.currency_type = currency_type_str;
        log.currency_amount = transaction.amount;
        log.experience = 0;
        log.rating_change = 0;
        log.reason = transaction.reason;

        return repository_->recordRewardLog(log);

    } catch (const std::exception& e) {
        return false;
    }
}

} // namespace game_service
} // namespace core_services
