#pragma once

#include "repository_error.h"
#include <memory>
#include <functional>

namespace common {
namespace repository {

/**
 * 仓储接口基类
 * 用于 UnitOfWork 模式的参与
 */
class IRepositoryBase {
public:
    virtual ~IRepositoryBase() = default;

    /**
     * 设置事务上下文
     */
    virtual void setUnitOfWork(void* uow) = 0;

    /**
     * 刷新挂起的更改
     */
    virtual void flush() {}

    /**
     * 获取实体类型名称
     */
    virtual std::string getEntityTypeName() const = 0;
};

/**
 * 通用仓储接口
 * @tparam Entity 实体类型
 * @tparam IdType ID 类型，默认为 std::string
 */
template<typename Entity, typename IdType = std::string>
class IRepository : public IRepositoryBase {
public:
    ~IRepository() override = default;

    // ========================================================================
    // 基础 CRUD 操作
    // ========================================================================

    /**
     * 根据 ID 查找实体
     * @param id 实体 ID
     * @return 实体或错误
     */
    virtual RepositoryResult<Entity> findById(const IdType& id) = 0;

    /**
     * 查找所有实体
     * @param spec 查询规约（可选）
     * @return 实体列表或错误
     */
    virtual RepositoryResult<std::vector<Entity>> findAll(
        const QuerySpecification& spec = {}) = 0;

    /**
     * 查找一个实体（根据条件）
     * @param spec 查询规约
     * @return 实体或错误
     */
    virtual RepositoryResult<Entity> findOne(const QuerySpecification& spec) = 0;

    /**
     * 保存实体（插入或更新）
     * @param entity 实体
     * @return 保存后的实体或错误
     */
    virtual RepositoryResult<Entity> save(const Entity& entity) = 0;

    /**
     * 更新实体
     * @param entity 实体
     * @return 是否成功
     */
    virtual RepositoryResult<bool> update(const Entity& entity) = 0;

    /**
     * 部分更新实体
     * @param id 实体 ID
     * @param updates 更新字段（字段名 -> 值）
     * @return 是否成功
     */
    virtual RepositoryResult<bool> updatePartial(
        const IdType& id,
        const std::unordered_map<std::string, std::string>& updates) = 0;

    /**
     * 删除实体
     * @param id 实体 ID
     * @return 是否成功
     */
    virtual RepositoryResult<bool> remove(const IdType& id) = 0;

    // ========================================================================
    // 批量操作
    // ========================================================================

    /**
     * 批量保存
     * @param entities 实体列表
     * @return 保存后的实体列表或错误
     */
    virtual RepositoryResult<std::vector<Entity>> saveAll(
        const std::vector<Entity>& entities) = 0;

    /**
     * 批量删除
     * @param ids ID 列表
     * @return 是否成功
     */
    virtual RepositoryResult<bool> removeAll(const std::vector<IdType>& ids) = 0;

    // ========================================================================
    // 查询操作
    // ========================================================================

    /**
     * 检查实体是否存在
     * @param id 实体 ID
     * @return 是否存在
     */
    virtual RepositoryResult<bool> exists(const IdType& id) = 0;

    /**
     * 统计实体数量
     * @param spec 查询规约（可选）
     * @return 数量或错误
     */
    virtual RepositoryResult<size_t> count(const QuerySpecification& spec = {}) = 0;

    /**
     * 分页查询
     * @param spec 查询规约
     * @param pagination 分页参数
     * @return 分页结果或错误
     */
    virtual RepositoryResult<PagedResult<Entity>> findPaged(
        const QuerySpecification& spec,
        const Pagination& pagination) = 0;

    // ========================================================================
    // 实用方法
    // ========================================================================

    /**
     * 获取表名
     */
    virtual std::string getTableName() const = 0;

    /**
     * 获取 ID 列名
     */
    virtual std::string getIdColumnName() const = 0;

    // IRepositoryBase 实现
    std::string getEntityTypeName() const override {
        return typeid(Entity).name();
    }
};

/**
 * 软删除仓储接口
 * @tparam Entity 实体类型
 * @tparam IdType ID 类型
 */
template<typename Entity, typename IdType = std::string>
class ISoftDeleteRepository : public IRepository<Entity, IdType> {
public:
    ~ISoftDeleteRepository() override = default;

    /**
     * 软删除实体
     * @param id 实体 ID
     * @return 是否成功
     */
    virtual RepositoryResult<bool> softDelete(const IdType& id) = 0;

    /**
     * 恢复软删除的实体
     * @param id 实体 ID
     * @return 是否成功
     */
    virtual RepositoryResult<bool> restore(const IdType& id) = 0;

    /**
     * 查找所有（包括已删除）
     * @param spec 查询规约
     * @return 实体列表或错误
     */
    virtual RepositoryResult<std::vector<Entity>> findAllIncludingDeleted(
        const QuerySpecification& spec = {}) = 0;

    /**
     * 查找已删除的实体
     * @param spec 查询规约
     * @return 实体列表或错误
     */
    virtual RepositoryResult<std::vector<Entity>> findDeleted(
        const QuerySpecification& spec = {}) = 0;

    /**
     * 永久删除（硬删除）
     * @param id 实体 ID
     * @return 是否成功
     */
    virtual RepositoryResult<bool> permanentDelete(const IdType& id) = 0;
};

/**
 * 可缓存仓储接口
 * @tparam Entity 实体类型
 * @tparam IdType ID 类型
 */
template<typename Entity, typename IdType = std::string>
class ICachedRepository : public IRepository<Entity, IdType> {
public:
    ~ICachedRepository() override = default;

    /**
     * 使缓存失效
     * @param id 实体 ID
     */
    virtual void invalidateCache(const IdType& id) = 0;

    /**
     * 使所有缓存失效
     */
    virtual void invalidateAllCache() = 0;

    /**
     * 预热缓存
     * @param ids 要预热的 ID 列表
     */
    virtual void warmCache(const std::vector<IdType>& ids) = 0;

    /**
     * 获取缓存命中率
     */
    virtual double getCacheHitRate() const = 0;
};

/**
 * 仓储工厂接口
 */
template<typename Entity, typename IdType = std::string>
class IRepositoryFactory {
public:
    virtual ~IRepositoryFactory() = default;

    /**
     * 创建仓储实例
     */
    virtual std::shared_ptr<IRepository<Entity, IdType>> create() = 0;
};

} // namespace repository
} // namespace common
