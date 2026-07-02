#pragma once

#include <string>
#include <variant>
#include <optional>
#include <vector>
#include <stdexcept>

// C++17 compatible expected type (uses std::expected for C++23, custom for C++17)
#include "common/utils/expected.h"

namespace common {
namespace repository {

/**
 * 仓储操作错误码
 */
enum class RepositoryErrorCode {
    Success = 0,
    ConnectionFailed,      // 连接失败
    QueryFailed,           // 查询失败
    NotFound,              // 记录未找到
    DuplicateKey,          // 重复键
    ConstraintViolation,   // 约束违反
    Timeout,               // 超时
    CacheError,            // 缓存错误
    TransactionFailed,     // 事务失败
    InvalidInput,          // 无效输入
    SerializationError,    // 序列化错误
    Unknown                // 未知错误
};

/**
 * 仓储操作错误详情
 */
struct RepositoryError {
    RepositoryErrorCode code;
    std::string message;
    std::string query;      // 用于调试的 SQL 查询（可选）
    int sql_error_code{0};  // SQL 错误码（如果适用）

    /**
     * 创建错误
     */
    static RepositoryError connectionFailed(const std::string& msg) {
        return {RepositoryErrorCode::ConnectionFailed, msg};
    }

    static RepositoryError queryFailed(const std::string& msg, const std::string& query = "") {
        return {RepositoryErrorCode::QueryFailed, msg, query};
    }

    static RepositoryError notFound(const std::string& msg) {
        return {RepositoryErrorCode::NotFound, msg};
    }

    static RepositoryError duplicateKey(const std::string& msg) {
        return {RepositoryErrorCode::DuplicateKey, msg};
    }

    static RepositoryError constraintViolation(const std::string& msg) {
        return {RepositoryErrorCode::ConstraintViolation, msg};
    }

    static RepositoryError timeout(const std::string& msg) {
        return {RepositoryErrorCode::Timeout, msg};
    }

    static RepositoryError cacheError(const std::string& msg) {
        return {RepositoryErrorCode::CacheError, msg};
    }

    static RepositoryError transactionFailed(const std::string& msg) {
        return {RepositoryErrorCode::TransactionFailed, msg};
    }

    static RepositoryError invalidInput(const std::string& msg) {
        return {RepositoryErrorCode::InvalidInput, msg};
    }

    static RepositoryError serializationError(const std::string& msg) {
        return {RepositoryErrorCode::SerializationError, msg};
    }

    static RepositoryError unknown(const std::string& msg) {
        return {RepositoryErrorCode::Unknown, msg};
    }

    /**
     * 转换为字符串
     */
    std::string toString() const {
        std::string code_str;
        switch (code) {
            case RepositoryErrorCode::Success: code_str = "Success"; break;
            case RepositoryErrorCode::ConnectionFailed: code_str = "ConnectionFailed"; break;
            case RepositoryErrorCode::QueryFailed: code_str = "QueryFailed"; break;
            case RepositoryErrorCode::NotFound: code_str = "NotFound"; break;
            case RepositoryErrorCode::DuplicateKey: code_str = "DuplicateKey"; break;
            case RepositoryErrorCode::ConstraintViolation: code_str = "ConstraintViolation"; break;
            case RepositoryErrorCode::Timeout: code_str = "Timeout"; break;
            case RepositoryErrorCode::CacheError: code_str = "CacheError"; break;
            case RepositoryErrorCode::TransactionFailed: code_str = "TransactionFailed"; break;
            case RepositoryErrorCode::InvalidInput: code_str = "InvalidInput"; break;
            case RepositoryErrorCode::SerializationError: code_str = "SerializationError"; break;
            case RepositoryErrorCode::Unknown: code_str = "Unknown"; break;
        }

        std::string result = "[" + code_str + "] " + message;
        if (!query.empty()) {
            result += " (Query: " + query + ")";
        }
        if (sql_error_code != 0) {
            result += " (SQL Error: " + std::to_string(sql_error_code) + ")";
        }
        return result;
    }
};

/**
 * 仓储操作结果类型（C++23 std::expected）
 */
template<typename T>
using RepositoryResult = std::expected<T, RepositoryError>;

/**
 * 查询排序方向
 */
enum class SortDirection {
    Ascending,
    Descending
};

/**
 * 查询条件操作符
 */
enum class FilterOperator {
    Equal,          // =
    NotEqual,       // !=
    GreaterThan,    // >
    GreaterEqual,   // >=
    LessThan,       // <
    LessEqual,      // <=
    Like,           // LIKE
    In,             // IN
    NotIn,          // NOT IN
    Between,        // BETWEEN
    IsNull,         // IS NULL
    IsNotNull       // IS NOT NULL
};

/**
 * 查询条件值
 */
using FilterValue = std::variant<
    std::string,
    int,
    int64_t,
    double,
    bool,
    std::nullptr_t,
    std::vector<std::string>,
    std::vector<int>,
    std::pair<std::string, std::string>  // 用于 BETWEEN
>;

/**
 * 查询过滤条件
 */
struct FilterCondition {
    std::string field;
    FilterOperator op;
    FilterValue value;

    // 便捷构造方法
    static FilterCondition equal(const std::string& field, const std::string& value) {
        return {field, FilterOperator::Equal, value};
    }

    static FilterCondition equal(const std::string& field, int value) {
        return {field, FilterOperator::Equal, value};
    }

    static FilterCondition equal(const std::string& field, int64_t value) {
        return {field, FilterOperator::Equal, value};
    }

    static FilterCondition notEqual(const std::string& field, const std::string& value) {
        return {field, FilterOperator::NotEqual, value};
    }

    static FilterCondition greaterThan(const std::string& field, int64_t value) {
        return {field, FilterOperator::GreaterThan, value};
    }

    static FilterCondition greaterEqual(const std::string& field, int64_t value) {
        return {field, FilterOperator::GreaterEqual, value};
    }

    static FilterCondition lessThan(const std::string& field, int64_t value) {
        return {field, FilterOperator::LessThan, value};
    }

    static FilterCondition lessEqual(const std::string& field, int64_t value) {
        return {field, FilterOperator::LessEqual, value};
    }

    static FilterCondition like(const std::string& field, const std::string& pattern) {
        return {field, FilterOperator::Like, pattern};
    }

    static FilterCondition in(const std::string& field, const std::vector<std::string>& values) {
        return {field, FilterOperator::In, values};
    }

    static FilterCondition in(const std::string& field, const std::vector<int>& values) {
        return {field, FilterOperator::In, values};
    }

    static FilterCondition between(const std::string& field, const std::string& low, const std::string& high) {
        return {field, FilterOperator::Between, std::make_pair(low, high)};
    }

    static FilterCondition isNull(const std::string& field) {
        return {field, FilterOperator::IsNull, nullptr};
    }

    static FilterCondition isNotNull(const std::string& field) {
        return {field, FilterOperator::IsNotNull, nullptr};
    }
};

/**
 * 排序条件
 */
struct SortCondition {
    std::string field;
    SortDirection direction;

    static SortCondition asc(const std::string& field) {
        return {field, SortDirection::Ascending};
    }

    static SortCondition desc(const std::string& field) {
        return {field, SortDirection::Descending};
    }
};

/**
 * 分页参数
 */
struct Pagination {
    int page{1};          // 页码（从 1 开始）
    int page_size{20};    // 每页大小
    int offset() const { return (page - 1) * page_size; }
    int limit() const { return page_size; }
};

/**
 * 查询规约（用于灵活的查询构建）
 */
struct QuerySpecification {
    std::vector<FilterCondition> filters;
    std::vector<SortCondition> sort_by;
    std::optional<int> limit_value;
    std::optional<int> offset_value;

    // 链式构建方法
    QuerySpecification& where(const std::string& field, FilterOperator op, const FilterValue& value) {
        filters.push_back({field, op, value});
        return *this;
    }

    QuerySpecification& whereEqual(const std::string& field, const std::string& value) {
        filters.push_back(FilterCondition::equal(field, value));
        return *this;
    }

    QuerySpecification& whereEqual(const std::string& field, int value) {
        filters.push_back(FilterCondition::equal(field, value));
        return *this;
    }

    QuerySpecification& whereEqual(const std::string& field, int64_t value) {
        filters.push_back(FilterCondition::equal(field, value));
        return *this;
    }

    QuerySpecification& whereLike(const std::string& field, const std::string& pattern) {
        filters.push_back(FilterCondition::like(field, pattern));
        return *this;
    }

    QuerySpecification& whereIn(const std::string& field, const std::vector<std::string>& values) {
        filters.push_back(FilterCondition::in(field, values));
        return *this;
    }

    QuerySpecification& whereIn(const std::string& field, const std::vector<int>& values) {
        filters.push_back(FilterCondition::in(field, values));
        return *this;
    }

    QuerySpecification& whereBetween(const std::string& field, const std::string& low, const std::string& high) {
        filters.push_back(FilterCondition::between(field, low, high));
        return *this;
    }

    QuerySpecification& whereIsNull(const std::string& field) {
        filters.push_back(FilterCondition::isNull(field));
        return *this;
    }

    QuerySpecification& whereIsNotNull(const std::string& field) {
        filters.push_back(FilterCondition::isNotNull(field));
        return *this;
    }

    QuerySpecification& orderBy(const std::string& field, SortDirection direction = SortDirection::Ascending) {
        sort_by.push_back({field, direction});
        return *this;
    }

    QuerySpecification& orderByAsc(const std::string& field) {
        return orderBy(field, SortDirection::Ascending);
    }

    QuerySpecification& orderByDesc(const std::string& field) {
        return orderBy(field, SortDirection::Descending);
    }

    QuerySpecification& take(int n) {
        limit_value = n;
        return *this;
    }

    QuerySpecification& skip(int n) {
        offset_value = n;
        return *this;
    }

    QuerySpecification& paginate(int page, int page_size) {
        offset_value = (page - 1) * page_size;
        limit_value = page_size;
        return *this;
    }

    QuerySpecification& paginate(const Pagination& pagination) {
        return paginate(pagination.page, pagination.page_size);
    }

    // 清除条件
    void clear() {
        filters.clear();
        sort_by.clear();
        limit_value.reset();
        offset_value.reset();
    }

    // 检查是否有过滤条件
    bool hasFilters() const { return !filters.empty(); }
    bool hasSorting() const { return !sort_by.empty(); }
    bool hasPagination() const { return limit_value.has_value() || offset_value.has_value(); }
};

/**
 * 分页结果
 */
template<typename T>
struct PagedResult {
    std::vector<T> items;
    int total_count{0};
    int page{1};
    int page_size{20};
    int total_pages{0};

    bool hasNextPage() const { return page < total_pages; }
    bool hasPrevPage() const { return page > 1; }
    bool isEmpty() const { return items.empty(); }
    size_t size() const { return items.size(); }
};

/**
 * 仓储异常
 */
class RepositoryException : public std::runtime_error {
public:
    explicit RepositoryException(const RepositoryError& error)
        : std::runtime_error(error.toString())
        , error_(error) {}

    explicit RepositoryException(RepositoryErrorCode code, const std::string& message)
        : std::runtime_error("[" + std::to_string(static_cast<int>(code)) + "] " + message)
        , error_{code, message} {}

    const RepositoryError& getError() const { return error_; }
    RepositoryErrorCode getCode() const { return error_.code; }

private:
    RepositoryError error_;
};

} // namespace repository
} // namespace common
