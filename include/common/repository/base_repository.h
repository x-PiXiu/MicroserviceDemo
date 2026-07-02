#pragma once

#include "i_repository.h"
#include "query_builder.h"
#include "cache_manager.h"
#include "unit_of_work.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <shared_mutex>

namespace common {
namespace repository {

/**
 * 仓储基类实现
 * 提供通用的 CRUD 操作实现，子类只需实现特定的映射方法
 *
 * @tparam Entity 实体类型
 * @tparam IdType ID 类型
 */
template<typename Entity, typename IdType = std::string>
class BaseRepository : public IRepository<Entity, IdType> {
public:
    /**
     * 构造函数
     */
    BaseRepository(
        std::shared_ptr<database::MySQLPool> mysql_pool,
        std::shared_ptr<database::RedisPool> redis_pool = nullptr,
        std::shared_ptr<ICacheManager> cache_manager = nullptr
    ) : mysql_pool_(mysql_pool)
      , redis_pool_(redis_pool)
      , cache_manager_(cache_manager) {
    }

    virtual ~BaseRepository() = default;

    // ========================================================================
    // IRepository 实现
    // ========================================================================

    RepositoryResult<Entity> findById(const IdType& id) override {
        // 构建缓存键
        std::string cache_key = buildCacheKey(id);

        // 尝试从缓存获取
        if (cache_manager_) {
            auto cached = cache_manager_->get(cache_key);
            if (cached.has_value()) {
                auto entity = deserializeEntity(*cached);
                if (entity.has_value()) {
                    return entity.value();
                }
            }
        }

        // 从数据库加载
        QueryBuilder qb;
        auto query = qb.select(getSelectColumns())
                        .from(getTableName())
                        .whereEqual(getIdColumnName(), convertIdToParam(id))
                        .build();

        auto result = executeQuery(query);
        if (!result) {
            return common::utils::unexpected(result.error());
        }

        auto& results = result.value();
        if (results.empty()) {
            return common::utils::unexpected(RepositoryError::notFound(
                "Entity not found with id: " + convertIdToString(id)));
        }

        Entity entity = results[0];

        // 存入缓存
        if (cache_manager_) {
            cache_manager_->set(cache_key, serializeEntity(entity), getCacheTTL());
        }

        return entity;
    }

    RepositoryResult<std::vector<Entity>> findAll(
        const QuerySpecification& spec = {}) override {

        QueryBuilder qb;
        qb.select(getSelectColumns()).from(getTableName());

        // 应用查询规约
        applySpecification(qb, spec);

        auto query = qb.build();
        return executeQuery(query);
    }

    RepositoryResult<Entity> findOne(const QuerySpecification& spec) override {
        QuerySpecification limited_spec = spec;
        limited_spec.limit_value = 1;

        auto result = findAll(limited_spec);
        if (!result) {
            return common::utils::unexpected(result.error());
        }

        auto& entities = result.value();
        if (entities.empty()) {
            return common::utils::unexpected(RepositoryError::notFound(
                "No entity found matching criteria"));
        }

        return entities[0];
    }

    RepositoryResult<Entity> save(const Entity& entity) override {
        // 检查是否已存在
        auto id = getEntityId(entity);
        auto existing = findById(id);

        if (existing.has_value()) {
            // 更新
            auto update_result = update(entity);
            if (!update_result) {
                return common::utils::unexpected(update_result.error());
            }
            return findById(id);
        } else {
            // 插入
            return insert(entity);
        }
    }

    RepositoryResult<bool> update(const Entity& entity) override {
        auto query = buildUpdateQuery(entity);
        auto result = executeUpdate(query);

        if (result && *result > 0) {
            // 使缓存失效
            invalidateCache(getEntityId(entity));
            return true;
        }

        return result.has_value() ? false :
            common::utils::unexpected(result.error());
    }

    RepositoryResult<bool> updatePartial(
        const IdType& id,
        const std::unordered_map<std::string, std::string>& updates) override {

        if (updates.empty()) {
            return common::utils::unexpected(RepositoryError::invalidInput(
                "No fields to update"));
        }

        QueryBuilder qb;
        qb.update(getTableName());

        for (const auto& [field, value] : updates) {
            qb.set(field, value);
        }

        qb.whereEqual(getIdColumnName(), convertIdToParam(id));

        auto query = qb.build();
        auto result = executeUpdate(query);

        if (result && *result > 0) {
            invalidateCache(id);
            return true;
        }

        return result.has_value() ? false :
            common::utils::unexpected(result.error());
    }

    RepositoryResult<bool> remove(const IdType& id) override {
        QueryBuilder qb;
        auto query = qb.deleteFrom(getTableName())
                       .whereEqual(getIdColumnName(), convertIdToParam(id))
                       .build();

        auto result = executeUpdate(query);

        if (result && *result > 0) {
            invalidateCache(id);
            return true;
        }

        return result.has_value() ? false :
            common::utils::unexpected(result.error());
    }

    RepositoryResult<std::vector<Entity>> saveAll(
        const std::vector<Entity>& entities) override {

        std::vector<Entity> saved;
        saved.reserve(entities.size());

        for (const auto& entity : entities) {
            auto result = save(entity);
            if (!result) {
                return common::utils::unexpected(result.error());
            }
            saved.push_back(result.value());
        }

        return saved;
    }

    RepositoryResult<bool> removeAll(const std::vector<IdType>& ids) override {
        if (ids.empty()) {
            return true;
        }

        std::vector<SqlParam> id_params;
        id_params.reserve(ids.size());
        for (const auto& id : ids) {
            id_params.push_back(convertIdToParam(id));
        }

        QueryBuilder qb;
        auto query = qb.deleteFrom(getTableName())
                       .whereIn(getIdColumnName(), id_params)
                       .build();

        auto result = executeUpdate(query);

        // 使所有缓存失效
        for (const auto& id : ids) {
            invalidateCache(id);
        }

        return result.has_value();
    }

    RepositoryResult<bool> exists(const IdType& id) override {
        QueryBuilder qb;
        auto query = qb.selectCount()
                       .from(getTableName())
                       .whereEqual(getIdColumnName(), convertIdToParam(id))
                       .build();

        auto result = executeCount(query);
        if (!result) {
            return common::utils::unexpected(result.error());
        }

        return *result > 0;
    }

    RepositoryResult<size_t> count(const QuerySpecification& spec = {}) override {
        QueryBuilder qb;
        qb.selectCount().from(getTableName());

        // 只应用过滤条件，忽略排序和分页
        QuerySpecification count_spec;
        count_spec.filters = spec.filters;
        applySpecification(qb, count_spec);

        auto query = qb.build();
        return executeCount(query);
    }

    RepositoryResult<PagedResult<Entity>> findPaged(
        const QuerySpecification& spec,
        const Pagination& pagination) override {

        // 获取总数
        auto total_result = count(spec);
        if (!total_result) {
            return common::utils::unexpected(total_result.error());
        }

        // 获取分页数据
        QuerySpecification paged_spec = spec;
        paged_spec.offset_value = pagination.offset();
        paged_spec.limit_value = pagination.limit();

        auto items_result = findAll(paged_spec);
        if (!items_result) {
            return common::utils::unexpected(items_result.error());
        }

        PagedResult<Entity> result;
        result.items = items_result.value();
        result.total_count = static_cast<int>(*total_result);
        result.page = pagination.page;
        result.page_size = pagination.page_size;
        result.total_pages = (result.total_count + pagination.page_size - 1) / pagination.page_size;

        return result;
    }

    // ========================================================================
    // IRepositoryBase 实现
    // ========================================================================

    void setUnitOfWork(void* uow) override {
        unit_of_work_ = static_cast<UnitOfWork*>(uow);
    }

    void flush() override {
        // 默认实现为空，子类可覆盖
    }

    // ========================================================================
    // 子类必须实现的抽象方法
    // ========================================================================

    /**
     * 获取表名
     */
    std::string getTableName() const override = 0;

    /**
     * 获取 ID 列名
     */
    std::string getIdColumnName() const override = 0;

protected:
    // ========================================================================
    // 子类需要实现的辅助方法
    // ========================================================================

    /**
     * 获取 SELECT 列列表
     * 默认返回 "*"，子类可覆盖
     */
    virtual std::vector<std::string> getSelectColumns() const {
        return {"*"};
    }

    /**
     * 从 ResultSet 解析实体
     */
    virtual Entity fromResultSet(sql::ResultSet* rs) const = 0;

    /**
     * 获取实体 ID
     */
    virtual IdType getEntityId(const Entity& entity) const = 0;

    /**
     * 序列化实体到字符串（用于缓存）
     */
    virtual std::string serializeEntity(const Entity& entity) const {
        return nlohmann::json(entity).dump();
    }

    /**
     * 从字符串反序列化实体
     */
    virtual std::optional<Entity> deserializeEntity(const std::string& data) const {
        try {
            auto j = nlohmann::json::parse(data);
            return j.get<Entity>();
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    /**
     * 构建插入查询
     */
    virtual BuiltQuery buildInsertQuery(const Entity& entity) const = 0;

    /**
     * 构建更新查询
     */
    virtual BuiltQuery buildUpdateQuery(const Entity& entity) const = 0;

    /**
     * 获取缓存 TTL
     */
    virtual std::chrono::seconds getCacheTTL() const {
        return std::chrono::seconds(3600);  // 1 小时
    }

    /**
     * 转换 ID 到 SQL 参数
     */
    virtual SqlParam convertIdToParam(const IdType& id) const {
        if constexpr (std::is_same_v<IdType, std::string>) {
            return id;
        } else if constexpr (std::is_integral_v<IdType>) {
            return static_cast<int64_t>(id);
        } else {
            return std::string(id);
        }
    }

    /**
     * 转换 ID 到字符串
     */
    virtual std::string convertIdToString(const IdType& id) const {
        if constexpr (std::is_same_v<IdType, std::string>) {
            return id;
        } else {
            return std::to_string(id);
        }
    }

    // ========================================================================
    // 数据库操作辅助方法
    // ========================================================================

    /**
     * 执行查询并返回实体列表
     */
    RepositoryResult<std::vector<Entity>> executeQuery(const BuiltQuery& query) {
        try {
            auto conn = getConnection();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get database connection"));
            }

            auto stmt = conn->prepareStatement(query.sql);
            if (!stmt) {
                return common::utils::unexpected(RepositoryError::queryFailed(
                    "Failed to prepare statement", query.sql));
            }

            // 绑定参数
            bindParameters(stmt.get(), query.params);

            auto rs = stmt->executeQuery();
            if (!rs) {
                return common::utils::unexpected(RepositoryError::queryFailed(
                    "Failed to execute query", query.sql));
            }

            std::vector<Entity> entities;
            while (rs->next()) {
                entities.push_back(fromResultSet(rs.get()));
            }

            return entities;

        } catch (const sql::SQLException& e) {
            return common::utils::unexpected(RepositoryError::queryFailed(
                e.what(), query.sql));
        } catch (const std::exception& e) {
            return common::utils::unexpected(RepositoryError::unknown(e.what()));
        }
    }

    /**
     * 执行更新操作并返回影响的行数
     */
    RepositoryResult<int> executeUpdate(const BuiltQuery& query) {
        try {
            auto conn = getConnection();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get database connection"));
            }

            auto stmt = conn->prepareStatement(query.sql);
            if (!stmt) {
                return common::utils::unexpected(RepositoryError::queryFailed(
                    "Failed to prepare statement", query.sql));
            }

            // 绑定参数
            bindParameters(stmt.get(), query.params);

            int affected = stmt->executeUpdate();
            return affected;

        } catch (const sql::SQLException& e) {
            return common::utils::unexpected(RepositoryError::queryFailed(
                e.what(), query.sql));
        } catch (const std::exception& e) {
            return common::utils::unexpected(RepositoryError::unknown(e.what()));
        }
    }

    /**
     * 执行计数查询
     */
    RepositoryResult<size_t> executeCount(const BuiltQuery& query) {
        try {
            auto conn = getConnection();
            if (!conn) {
                return common::utils::unexpected(RepositoryError::connectionFailed(
                    "Failed to get database connection"));
            }

            auto stmt = conn->prepareStatement(query.sql);
            if (!stmt) {
                return common::utils::unexpected(RepositoryError::queryFailed(
                    "Failed to prepare statement", query.sql));
            }

            // 绑定参数
            bindParameters(stmt.get(), query.params);

            auto rs = stmt->executeQuery();
            if (!rs || !rs->next()) {
                return 0;
            }

            return static_cast<size_t>(rs->getInt64(1));

        } catch (const sql::SQLException& e) {
            return common::utils::unexpected(RepositoryError::queryFailed(
                e.what(), query.sql));
        } catch (const std::exception& e) {
            return common::utils::unexpected(RepositoryError::unknown(e.what()));
        }
    }

    /**
     * 插入实体
     */
    RepositoryResult<Entity> insert(const Entity& entity) {
        auto query = buildInsertQuery(entity);
        auto result = executeUpdate(query);

        if (!result) {
            return common::utils::unexpected(result.error());
        }

        // 获取插入后的实体（可能包含自增 ID）
        return findById(getEntityId(entity));
    }

    /**
     * 获取数据库连接
     */
    std::shared_ptr<database::MySQLConnection> getConnection() {
        if (unit_of_work_ && unit_of_work_->isActive()) {
            return unit_of_work_->getConnection();
        }

        database::MySQLConnectionGuard guard(*mysql_pool_);
        return guard.get();
    }

    /**
     * 使缓存失效
     */
    void invalidateCache(const IdType& id) {
        if (cache_manager_) {
            std::string cache_key = buildCacheKey(id);
            cache_manager_->remove(cache_key);
        }
    }

    /**
     * 构建缓存键
     */
    std::string buildCacheKey(const IdType& id) const {
        return getTableName() + ":" + convertIdToString(id);
    }

protected:
    std::shared_ptr<database::MySQLPool> mysql_pool_;
    std::shared_ptr<database::RedisPool> redis_pool_;
    std::shared_ptr<ICacheManager> cache_manager_;
    UnitOfWork* unit_of_work_ = nullptr;

private:
    /**
     * 绑定参数到 PreparedStatement
     */
    void bindParameters(sql::PreparedStatement* stmt,
                        const std::vector<SqlParam>& params) {
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            std::visit([i, stmt](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    stmt->setString(i + 1, arg);
                } else if constexpr (std::is_same_v<T, int>) {
                    stmt->setInt(i + 1, arg);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    stmt->setInt64(i + 1, arg);
                } else if constexpr (std::is_same_v<T, uint64_t>) {
                    stmt->setUInt64(i + 1, arg);
                } else if constexpr (std::is_same_v<T, double>) {
                    stmt->setDouble(i + 1, arg);
                } else if constexpr (std::is_same_v<T, bool>) {
                    stmt->setBoolean(i + 1, arg);
                } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    stmt->setNull(i + 1, sql::DataType::VARCHAR);
                }
            }, param);
        }
    }
};

} // namespace repository
} // namespace common
