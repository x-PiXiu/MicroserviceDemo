#pragma once

#include "repository_error.h"
#include <string>
#include <vector>
#include <sstream>
#include <variant>
#include <optional>
#include <cstdint>

namespace common {
namespace repository {

/**
 * SQL 参数类型
 */
using SqlParam = std::variant<
    std::string,
    int,
    int64_t,
    uint64_t,
    double,
    bool,
    std::nullptr_t
>;

/**
 * 构建后的查询结果
 */
struct BuiltQuery {
    std::string sql;              // SQL 语句（使用 ? 占位符）
    std::vector<SqlParam> params; // 参数值列表

    bool isEmpty() const { return sql.empty(); }
    size_t paramCount() const { return params.size(); }
};

/**
 * JOIN 类型
 */
enum class JoinType {
    Inner,
    Left,
    Right,
    Cross
};

/**
 * 安全的 SQL 查询构建器
 *
 * 特性：
 * - 自动参数化防止 SQL 注入
 * - 流式 API
 * - 类型安全
 */
class QueryBuilder {
public:
    QueryBuilder() = default;

    // ========================================================================
    // SELECT 语句
    // ========================================================================

    /**
     * SELECT 指定列
     */
    QueryBuilder& select(const std::vector<std::string>& columns) {
        query_type_ = "SELECT";
        select_columns_ = columns;
        return *this;
    }

    /**
     * SELECT *
     */
    QueryBuilder& selectAll() {
        query_type_ = "SELECT";
        select_columns_ = {"*"};
        return *this;
    }

    /**
     * SELECT COUNT(*)
     */
    QueryBuilder& selectCount(const std::string& alias = "count") {
        query_type_ = "SELECT";
        select_columns_ = {"COUNT(*) AS " + alias};
        return *this;
    }

    /**
     * SELECT 聚合函数
     */
    QueryBuilder& selectAggregate(const std::string& func, const std::string& column, const std::string& alias = "") {
        query_type_ = "SELECT";
        std::string expr = func + "(" + column + ")";
        if (!alias.empty()) {
            expr += " AS " + alias;
        }
        select_columns_.push_back(expr);
        return *this;
    }

    // ========================================================================
    // FROM 子句
    // ========================================================================

    QueryBuilder& from(const std::string& table) {
        table_ = table;
        return *this;
    }

    QueryBuilder& from(const std::string& table, const std::string& alias) {
        table_ = table + " AS " + alias;
        return *this;
    }

    // ========================================================================
    // JOIN 子句
    // ========================================================================

    QueryBuilder& join(JoinType type, const std::string& table, const std::string& on) {
        std::string join_type;
        switch (type) {
            case JoinType::Inner: join_type = "INNER JOIN"; break;
            case JoinType::Left: join_type = "LEFT JOIN"; break;
            case JoinType::Right: join_type = "RIGHT JOIN"; break;
            case JoinType::Cross: join_type = "CROSS JOIN"; break;
        }
        join_clauses_.push_back(join_type + " " + table + " ON " + on);
        return *this;
    }

    QueryBuilder& innerJoin(const std::string& table, const std::string& on) {
        return join(JoinType::Inner, table, on);
    }

    QueryBuilder& leftJoin(const std::string& table, const std::string& on) {
        return join(JoinType::Left, table, on);
    }

    QueryBuilder& rightJoin(const std::string& table, const std::string& on) {
        return join(JoinType::Right, table, on);
    }

    // ========================================================================
    // INSERT 语句
    // ========================================================================

    QueryBuilder& insertInto(const std::string& table) {
        query_type_ = "INSERT";
        table_ = table;
        return *this;
    }

    /**
     * 设置插入值
     */
    QueryBuilder& values(const std::vector<std::pair<std::string, SqlParam>>& values) {
        insert_values_ = values;
        return *this;
    }

    /**
     * 添加单个字段值
     */
    QueryBuilder& setValue(const std::string& column, const SqlParam& value) {
        insert_values_.emplace_back(column, value);
        return *this;
    }

    // ========================================================================
    // UPDATE 语句
    // ========================================================================

    QueryBuilder& update(const std::string& table) {
        query_type_ = "UPDATE";
        table_ = table;
        return *this;
    }

    /**
     * 设置更新值
     */
    QueryBuilder& set(const std::vector<std::pair<std::string, SqlParam>>& values) {
        update_values_ = values;
        return *this;
    }

    /**
     * 添加单个更新字段
     */
    QueryBuilder& set(const std::string& column, const SqlParam& value) {
        update_values_.emplace_back(column, value);
        return *this;
    }

    // ========================================================================
    // DELETE 语句
    // ========================================================================

    QueryBuilder& deleteFrom(const std::string& table) {
        query_type_ = "DELETE";
        table_ = table;
        return *this;
    }

    // ========================================================================
    // WHERE 子句（自动参数化）
    // ========================================================================

    QueryBuilder& where(const std::string& column, const std::string& op, const SqlParam& value) {
        addWhereClause(column + " " + op + " ?", value);
        return *this;
    }

    QueryBuilder& whereEqual(const std::string& column, const SqlParam& value) {
        return where(column, "=", value);
    }

    QueryBuilder& whereNotEqual(const std::string& column, const SqlParam& value) {
        return where(column, "!=", value);
    }

    QueryBuilder& whereGreaterThan(const std::string& column, const SqlParam& value) {
        return where(column, ">", value);
    }

    QueryBuilder& whereGreaterEqual(const std::string& column, const SqlParam& value) {
        return where(column, ">=", value);
    }

    QueryBuilder& whereLessThan(const std::string& column, const SqlParam& value) {
        return where(column, "<", value);
    }

    QueryBuilder& whereLessEqual(const std::string& column, const SqlParam& value) {
        return where(column, "<=", value);
    }

    QueryBuilder& whereLike(const std::string& column, const std::string& pattern) {
        addWhereClause(column + " LIKE ?", pattern);
        return *this;
    }

    QueryBuilder& whereNotLike(const std::string& column, const std::string& pattern) {
        addWhereClause(column + " NOT LIKE ?", pattern);
        return *this;
    }

    QueryBuilder& whereIn(const std::string& column, const std::vector<SqlParam>& values) {
        if (values.empty()) {
            // 空列表，生成 FALSE 条件
            addWhereClauseRaw("1 = 0");
            return *this;
        }

        std::string placeholders;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) placeholders += ", ";
            placeholders += "?";
        }
        addWhereClauseRaw(column + " IN (" + placeholders + ")");
        for (const auto& v : values) {
            params_.push_back(v);
        }
        return *this;
    }

    QueryBuilder& whereNotIn(const std::string& column, const std::vector<SqlParam>& values) {
        if (values.empty()) {
            // 空列表，生成 TRUE 条件
            addWhereClauseRaw("1 = 1");
            return *this;
        }

        std::string placeholders;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) placeholders += ", ";
            placeholders += "?";
        }
        addWhereClauseRaw(column + " NOT IN (" + placeholders + ")");
        for (const auto& v : values) {
            params_.push_back(v);
        }
        return *this;
    }

    QueryBuilder& whereBetween(const std::string& column, const SqlParam& low, const SqlParam& high) {
        addWhereClauseRaw(column + " BETWEEN ? AND ?");
        params_.push_back(low);
        params_.push_back(high);
        return *this;
    }

    QueryBuilder& whereNotBetween(const std::string& column, const SqlParam& low, const SqlParam& high) {
        addWhereClauseRaw(column + " NOT BETWEEN ? AND ?");
        params_.push_back(low);
        params_.push_back(high);
        return *this;
    }

    QueryBuilder& whereNull(const std::string& column) {
        addWhereClauseRaw(column + " IS NULL");
        return *this;
    }

    QueryBuilder& whereNotNull(const std::string& column) {
        addWhereClauseRaw(column + " IS NOT NULL");
        return *this;
    }

    // ========================================================================
    // 逻辑操作符
    // ========================================================================

    QueryBuilder& And() {
        if (!where_clauses_.empty() && needs_logical_op_) {
            where_clauses_.push_back("AND");
        }
        return *this;
    }

    QueryBuilder& Or() {
        if (!where_clauses_.empty() && needs_logical_op_) {
            where_clauses_.push_back("OR");
        }
        return *this;
    }

    QueryBuilder& beginGroup() {
        if (!where_clauses_.empty() && needs_logical_op_) {
            where_clauses_.push_back("AND");
        }
        where_clauses_.push_back("(");
        needs_logical_op_ = false;
        group_depth_++;
        return *this;
    }

    QueryBuilder& endGroup() {
        where_clauses_.push_back(")");
        needs_logical_op_ = true;
        if (group_depth_ > 0) group_depth_--;
        return *this;
    }

    // ========================================================================
    // ORDER BY / LIMIT / OFFSET
    // ========================================================================

    QueryBuilder& orderBy(const std::string& column, bool ascending = true) {
        order_by_.push_back(column + (ascending ? " ASC" : " DESC"));
        return *this;
    }

    QueryBuilder& orderByAsc(const std::string& column) {
        return orderBy(column, true);
    }

    QueryBuilder& orderByDesc(const std::string& column) {
        return orderBy(column, false);
    }

    QueryBuilder& limit(int count) {
        limit_ = count;
        return *this;
    }

    QueryBuilder& offset(int offset) {
        offset_ = offset;
        return *this;
    }

    QueryBuilder& paginate(int page, int page_size) {
        limit_ = page_size;
        offset_ = (page - 1) * page_size;
        return *this;
    }

    // ========================================================================
    // GROUP BY / HAVING
    // ========================================================================

    QueryBuilder& groupBy(const std::string& column) {
        group_by_.push_back(column);
        return *this;
    }

    QueryBuilder& having(const std::string& condition) {
        having_ = condition;
        return *this;
    }

    // ========================================================================
    // 构建查询
    // ========================================================================

    /**
     * 构建 SQL 查询
     */
    BuiltQuery build() const {
        BuiltQuery result;
        std::ostringstream sql;

        if (query_type_ == "SELECT") {
            buildSelect(sql);
        } else if (query_type_ == "INSERT") {
            buildInsert(sql);
        } else if (query_type_ == "UPDATE") {
            buildUpdate(sql);
        } else if (query_type_ == "DELETE") {
            buildDelete(sql);
        }

        result.sql = sql.str();
        result.params = params_;

        return result;
    }

    /**
     * 重置构建器
     */
    void reset() {
        query_type_.clear();
        table_.clear();
        select_columns_.clear();
        insert_values_.clear();
        update_values_.clear();
        where_clauses_.clear();
        join_clauses_.clear();
        order_by_.clear();
        group_by_.clear();
        having_.clear();
        params_.clear();
        limit_.reset();
        offset_.reset();
        group_depth_ = 0;
        needs_logical_op_ = false;
    }

    /**
     * 获取查询类型
     */
    std::string getQueryType() const { return query_type_; }

    /**
     * 检查是否有 WHERE 条件
     */
    bool hasWhere() const { return !where_clauses_.empty(); }

private:
    std::string query_type_;
    std::string table_;
    std::vector<std::string> select_columns_;
    std::vector<std::pair<std::string, SqlParam>> insert_values_;
    std::vector<std::pair<std::string, SqlParam>> update_values_;
    std::vector<std::string> where_clauses_;
    std::vector<std::string> join_clauses_;
    std::vector<std::string> order_by_;
    std::vector<std::string> group_by_;
    std::string having_;
    mutable std::vector<SqlParam> params_;
    std::optional<int> limit_;
    std::optional<int> offset_;
    int group_depth_ = 0;
    bool needs_logical_op_ = false;

    void addWhereClause(const std::string& clause, const SqlParam& value) {
        if (!where_clauses_.empty() && needs_logical_op_) {
            where_clauses_.push_back("AND");
        }
        where_clauses_.push_back(clause);
        params_.push_back(value);
        needs_logical_op_ = true;
    }

    void addWhereClauseRaw(const std::string& clause) {
        if (!where_clauses_.empty() && needs_logical_op_) {
            where_clauses_.push_back("AND");
        }
        where_clauses_.push_back(clause);
        needs_logical_op_ = true;
    }

    void buildSelect(std::ostringstream& sql) const {
        sql << "SELECT ";

        // 列
        for (size_t i = 0; i < select_columns_.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << select_columns_[i];
        }

        // FROM
        sql << " FROM " << table_;

        // JOIN
        for (const auto& join : join_clauses_) {
            sql << " " << join;
        }

        // WHERE
        if (!where_clauses_.empty()) {
            sql << " WHERE ";
            for (const auto& clause : where_clauses_) {
                sql << clause << " ";
            }
        }

        // GROUP BY
        if (!group_by_.empty()) {
            sql << " GROUP BY ";
            for (size_t i = 0; i < group_by_.size(); ++i) {
                if (i > 0) sql << ", ";
                sql << group_by_[i];
            }
        }

        // HAVING
        if (!having_.empty()) {
            sql << " HAVING " << having_;
        }

        // ORDER BY
        if (!order_by_.empty()) {
            sql << " ORDER BY ";
            for (size_t i = 0; i < order_by_.size(); ++i) {
                if (i > 0) sql << ", ";
                sql << order_by_[i];
            }
        }

        // LIMIT / OFFSET
        if (limit_.has_value()) {
            sql << " LIMIT " << *limit_;
        }
        if (offset_.has_value()) {
            sql << " OFFSET " << *offset_;
        }
    }

    void buildInsert(std::ostringstream& sql) const {
        sql << "INSERT INTO " << table_ << " (";

        // 列名
        for (size_t i = 0; i < insert_values_.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << insert_values_[i].first;
        }

        sql << ") VALUES (";

        // 占位符
        for (size_t i = 0; i < insert_values_.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "?";
        }

        sql << ")";

        // 收集参数
        for (const auto& [col, val] : insert_values_) {
            params_.push_back(val);
        }
    }

    void buildUpdate(std::ostringstream& sql) const {
        sql << "UPDATE " << table_ << " SET ";

        // SET 子句
        for (size_t i = 0; i < update_values_.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << update_values_[i].first << " = ?";
        }

        // 收集参数
        for (const auto& [col, val] : update_values_) {
            params_.push_back(val);
        }

        // WHERE
        if (!where_clauses_.empty()) {
            sql << " WHERE ";
            for (const auto& clause : where_clauses_) {
                sql << clause << " ";
            }
        }
    }

    void buildDelete(std::ostringstream& sql) const {
        sql << "DELETE FROM " << table_;

        // WHERE
        if (!where_clauses_.empty()) {
            sql << " WHERE ";
            for (const auto& clause : where_clauses_) {
                sql << clause << " ";
            }
        }
    }
};

/**
 * 查询规约转换为 QueryBuilder
 */
inline QueryBuilder& applySpecification(QueryBuilder& qb, const QuerySpecification& spec) {
    // 应用过滤条件
    for (const auto& filter : spec.filters) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string>) {
                switch (filter.op) {
                    case FilterOperator::Equal: qb.whereEqual(filter.field, value); break;
                    case FilterOperator::NotEqual: qb.whereNotEqual(filter.field, value); break;
                    case FilterOperator::Like: qb.whereLike(filter.field, value); break;
                    default: break;
                }
            } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t>) {
                switch (filter.op) {
                    case FilterOperator::Equal: qb.whereEqual(filter.field, static_cast<int64_t>(value)); break;
                    case FilterOperator::NotEqual: qb.whereNotEqual(filter.field, static_cast<int64_t>(value)); break;
                    case FilterOperator::GreaterThan: qb.whereGreaterThan(filter.field, static_cast<int64_t>(value)); break;
                    case FilterOperator::GreaterEqual: qb.whereGreaterEqual(filter.field, static_cast<int64_t>(value)); break;
                    case FilterOperator::LessThan: qb.whereLessThan(filter.field, static_cast<int64_t>(value)); break;
                    case FilterOperator::LessEqual: qb.whereLessEqual(filter.field, static_cast<int64_t>(value)); break;
                    default: break;
                }
            }
        }, filter.value);
    }

    // 应用排序
    for (const auto& sort : spec.sort_by) {
        qb.orderBy(sort.field, sort.direction == SortDirection::Ascending);
    }

    // 应用分页
    if (spec.limit_value.has_value()) {
        qb.limit(*spec.limit_value);
    }
    if (spec.offset_value.has_value()) {
        qb.offset(*spec.offset_value);
    }

    return qb;
}

} // namespace repository
} // namespace common
