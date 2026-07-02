#include "common/repository/unit_of_work.h"
#include "common/logger/logger.h"
#include <stdexcept>

namespace common {
namespace repository {

// ============================================================================
// UnitOfWork Implementation
// ============================================================================

UnitOfWork::UnitOfWork(std::shared_ptr<database::MySQLPool> mysql_pool)
    : mysql_pool_(mysql_pool)
    , active_(false)
    , committed_(false) {
    if (!mysql_pool_) {
        throw std::invalid_argument("MySQLPool cannot be null");
    }
}

UnitOfWork::~UnitOfWork() {
    if (active_ && !committed_) {
        try {
            rollback();
            LOG_DEBUG("UnitOfWork automatically rolled back in destructor");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to rollback UnitOfWork in destructor: " + std::string(e.what()));
        }
    }
}

std::string UnitOfWork::getIsolationLevelSql(IsolationLevel level) {
    switch (level) {
        case IsolationLevel::ReadUncommitted:
            return "READ UNCOMMITTED";
        case IsolationLevel::ReadCommitted:
            return "READ COMMITTED";
        case IsolationLevel::RepeatableRead:
            return "REPEATABLE READ";
        case IsolationLevel::Serializable:
            return "SERIALIZABLE";
        default:
            return "REPEATABLE READ";
    }
}

std::expected<void, RepositoryError> UnitOfWork::begin(IsolationLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (active_) {
        LOG_WARNING("UnitOfWork is already active");
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "Transaction is already active"));
    }

    try {
        // 获取连接
        database::MySQLConnectionGuard guard(*mysql_pool_);
        connection_ = guard.get();

        if (!connection_) {
            LOG_ERROR("Failed to get database connection for transaction");
            return common::utils::unexpected(RepositoryError::connectionFailed(
                "Failed to get database connection"));
        }

        // 设置隔离级别
        std::string set_isolation = "SET TRANSACTION ISOLATION LEVEL " + getIsolationLevelSql(level);
        auto stmt = connection_->prepareStatement(set_isolation);
        if (stmt) {
            stmt->executeUpdate();
        }

        // 开始事务
        connection_->setAutoCommit(false);

        active_ = true;
        committed_ = false;

        LOG_DEBUG("Transaction started with isolation level: " + getIsolationLevelSql(level));
        return {};

    } catch (const sql::SQLException& e) {
        LOG_ERROR("Failed to begin transaction: " + std::string(e.what()));
        return common::utils::unexpected(RepositoryError::transactionFailed(
            std::string("Failed to begin transaction: ") + e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in begin transaction: " + std::string(e.what()));
        return common::utils::unexpected(RepositoryError::unknown(e.what()));
    }
}

std::expected<void, RepositoryError> UnitOfWork::commit() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!active_) {
        LOG_WARNING("No active transaction to commit");
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "No active transaction"));
    }

    if (committed_) {
        LOG_WARNING("Transaction already committed");
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "Transaction already committed"));
    }

    try {
        // 刷新所有注册的 Repository
        flushAll();

        // 提交事务
        if (connection_) {
            connection_->commit();
            connection_->setAutoCommit(true);
        }

        active_ = false;
        committed_ = true;

        // 清理
        repositories_.clear();
        connection_.reset();

        LOG_DEBUG("Transaction committed successfully");
        return {};

    } catch (const sql::SQLException& e) {
        LOG_ERROR("Failed to commit transaction: " + std::string(e.what()));

        // 尝试回滚
        try {
            if (connection_) {
                connection_->rollback();
                connection_->setAutoCommit(true);
            }
        } catch (...) {
            // 忽略回滚错误
        }

        active_ = false;
        return common::utils::unexpected(RepositoryError::transactionFailed(
            std::string("Failed to commit transaction: ") + e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in commit: " + std::string(e.what()));
        return common::utils::unexpected(RepositoryError::unknown(e.what()));
    }
}

std::expected<void, RepositoryError> UnitOfWork::rollback() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!active_) {
        LOG_DEBUG("No active transaction to rollback");
        return {};
    }

    try {
        if (connection_) {
            connection_->rollback();
            connection_->setAutoCommit(true);
        }

        active_ = false;
        committed_ = false;

        // 清理
        repositories_.clear();
        connection_.reset();

        LOG_DEBUG("Transaction rolled back successfully");
        return {};

    } catch (const sql::SQLException& e) {
        LOG_ERROR("Failed to rollback transaction: " + std::string(e.what()));
        active_ = false;
        return common::utils::unexpected(RepositoryError::transactionFailed(
            std::string("Failed to rollback transaction: ") + e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in rollback: " + std::string(e.what()));
        active_ = false;
        return common::utils::unexpected(RepositoryError::unknown(e.what()));
    }
}

bool UnitOfWork::isActive() const {
    return active_;
}

void UnitOfWork::registerRepository(std::shared_ptr<IRepositoryBase> repo) {
    std::lock_guard<std::mutex> lock(mutex_);
    repositories_.push_back(repo);
}

std::shared_ptr<database::MySQLConnection> UnitOfWork::getConnection() {
    return connection_;
}

void UnitOfWork::flushAll() {
    for (auto& repo : repositories_) {
        if (repo) {
            repo->flush();
        }
    }
}

// ============================================================================
// TransactionScope Implementation
// ============================================================================

TransactionScope::TransactionScope(
    std::shared_ptr<IUnitOfWork> uow,
    IsolationLevel level)
    : uow_(uow)
    , committed_(false)
    , moved_(false) {

    if (!uow_) {
        throw std::invalid_argument("UnitOfWork cannot be null");
    }

    auto result = uow_->begin(level);
    if (!result) {
        error_ = result.error();
        throw RepositoryException(result.error());
    }
}

TransactionScope::~TransactionScope() {
    if (!moved_ && !committed_ && uow_) {
        try {
            uow_->rollback();
        } catch (...) {
            // 忽略析构函数中的异常
        }
    }
}

TransactionScope::TransactionScope(TransactionScope&& other) noexcept
    : uow_(std::move(other.uow_))
    , committed_(other.committed_)
    , moved_(false)
    , error_(other.error_) {
    other.moved_ = true;
}

TransactionScope& TransactionScope::operator=(TransactionScope&& other) noexcept {
    if (this != &other) {
        if (!moved_ && !committed_ && uow_) {
            try {
                uow_->rollback();
            } catch (...) {
                // 忽略
            }
        }

        uow_ = std::move(other.uow_);
        committed_ = other.committed_;
        moved_ = false;
        error_ = other.error_;
        other.moved_ = true;
    }
    return *this;
}

std::expected<void, RepositoryError> TransactionScope::commit() {
    if (moved_) {
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "TransactionScope was moved"));
    }

    if (committed_) {
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "Transaction already committed"));
    }

    if (!uow_) {
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "UnitOfWork is null"));
    }

    auto result = uow_->commit();
    if (result) {
        committed_ = true;
    } else {
        error_ = result.error();
    }

    return result;
}

std::expected<void, RepositoryError> TransactionScope::rollback() {
    if (moved_) {
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "TransactionScope was moved"));
    }

    if (!uow_) {
        return common::utils::unexpected(RepositoryError::transactionFailed(
            "UnitOfWork is null"));
    }

    return uow_->rollback();
}

} // namespace repository
} // namespace common
