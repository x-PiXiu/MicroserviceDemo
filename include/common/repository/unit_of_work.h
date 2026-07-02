#pragma once

#include "repository_error.h"
#include "common/database/mysql_pool.h"
#include <memory>
#include <functional>
#include <vector>
#include <mutex>

namespace common {
namespace repository {

// 前向声明
class IRepositoryBase;

/**
 * 事务隔离级别
 */
enum class IsolationLevel {
    ReadUncommitted,  // 读未提交
    ReadCommitted,    // 读已提交
    RepeatableRead,   // 可重复读
    Serializable      // 串行化
};

/**
 * 工作单元接口
 * 用于管理跨多个 Repository 的事务
 */
class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    /**
     * 开始事务
     */
    virtual std::expected<void, RepositoryError> begin(
        IsolationLevel level = IsolationLevel::RepeatableRead) = 0;

    /**
     * 提交事务
     */
    virtual std::expected<void, RepositoryError> commit() = 0;

    /**
     * 回滚事务
     */
    virtual std::expected<void, RepositoryError> rollback() = 0;

    /**
     * 检查事务是否活跃
     */
    virtual bool isActive() const = 0;

    /**
     * 注册参与事务的 Repository
     */
    virtual void registerRepository(std::shared_ptr<IRepositoryBase> repo) = 0;

    /**
     * 获取底层数据库连接
     */
    virtual std::shared_ptr<database::MySQLConnection> getConnection() = 0;

    /**
     * 在事务中执行操作（RAII 包装）
     * 自动提交/回滚
     */
    template<typename Func>
    auto executeInTransaction(Func&& func)
        -> std::expected<decltype(func()), RepositoryError>;
};

/**
 * 工作单元实现
 */
class UnitOfWork : public IUnitOfWork {
public:
    /**
     * 构造函数
     * @param mysql_pool MySQL 连接池
     */
    explicit UnitOfWork(std::shared_ptr<database::MySQLPool> mysql_pool);

    /**
     * 析构函数（自动回滚未提交的事务）
     */
    ~UnitOfWork() override;

    // IUnitOfWork 实现
    std::expected<void, RepositoryError> begin(
        IsolationLevel level = IsolationLevel::RepeatableRead) override;
    std::expected<void, RepositoryError> commit() override;
    std::expected<void, RepositoryError> rollback() override;
    bool isActive() const override;
    void registerRepository(std::shared_ptr<IRepositoryBase> repo) override;
    std::shared_ptr<database::MySQLConnection> getConnection() override;

    /**
     * 获取隔离级别对应的 SQL 字符串
     */
    static std::string getIsolationLevelSql(IsolationLevel level);

private:
    std::shared_ptr<database::MySQLPool> mysql_pool_;
    std::shared_ptr<database::MySQLConnection> connection_;
    std::vector<std::shared_ptr<IRepositoryBase>> repositories_;
    bool active_ = false;
    bool committed_ = false;
    std::mutex mutex_;

    void flushAll();
};

/**
 * 事务作用域守卫（RAII）
 */
class TransactionScope {
public:
    explicit TransactionScope(std::shared_ptr<IUnitOfWork> uow,
                              IsolationLevel level = IsolationLevel::RepeatableRead);
    ~TransactionScope();

    // 禁止拷贝
    TransactionScope(const TransactionScope&) = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;

    // 允许移动
    TransactionScope(TransactionScope&&) noexcept;
    TransactionScope& operator=(TransactionScope&&) noexcept;

    /**
     * 提交事务
     */
    std::expected<void, RepositoryError> commit();

    /**
     * 回滚事务
     */
    std::expected<void, RepositoryError> rollback();

    /**
     * 检查是否已提交
     */
    bool isCommitted() const { return committed_; }

    /**
     * 检查是否有错误
     */
    bool hasError() const { return error_.has_value(); }

    /**
     * 获取错误
     */
    const std::optional<RepositoryError>& getError() const { return error_; }

private:
    std::shared_ptr<IUnitOfWork> uow_;
    bool committed_ = false;
    bool moved_ = false;
    std::optional<RepositoryError> error_;
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename Func>
auto IUnitOfWork::executeInTransaction(Func&& func)
    -> std::expected<decltype(func()), RepositoryError> {

    using ReturnType = decltype(func());

    auto begin_result = begin();
    if (!begin_result) {
        return common::utils::unexpected(begin_result.error());
    }

    try {
        if constexpr (std::is_void_v<ReturnType>) {
            func();
            auto commit_result = commit();
            if (!commit_result) {
                rollback();
                return common::utils::unexpected(commit_result.error());
            }
            return {};
        } else {
            auto result = func();
            auto commit_result = commit();
            if (!commit_result) {
                rollback();
                return common::utils::unexpected(commit_result.error());
            }
            return result;
        }
    } catch (const std::exception& e) {
        rollback();
        return common::utils::unexpected(RepositoryError::unknown(
            std::string("Exception in transaction: ") + e.what()));
    } catch (...) {
        rollback();
        return common::utils::unexpected(RepositoryError::unknown(
            "Unknown exception in transaction"));
    }
}

} // namespace repository
} // namespace common
