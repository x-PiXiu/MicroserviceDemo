/**
 * @file query_engine.h
 * @brief 查询引擎接口
 * @details 多维度服务查询和过滤
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "service_info.h"
#include "registry_config.h"
#include "redis_storage.h"
#include "index_manager.h"
#include "common/logger/logger.h"
#include <memory>
#include <vector>
#include <random>

namespace core_services {
namespace service_registry {

/**
 * @brief 推荐策略枚举
 */
enum class RecommendStrategy {
    LEAST_LOAD,     ///< 最低负载
    RANDOM,         ///< 随机
    ROUND_ROBIN,    ///< 轮询
    WEIGHTED        ///< 加权
};

/**
 * @brief 查询引擎
 * @details 提供多维度服务查询和推荐功能
 */
class QueryEngine {
public:
    /**
     * @brief 构造函数
     * @param storage Redis 存储层
     * @param index_manager 索引管理器
     * @param config 注册中心配置
     */
    QueryEngine(
        std::shared_ptr<RedisStorage> storage,
        std::shared_ptr<IndexManager> index_manager,
        const RegistryConfig& config
    );

    /**
     * @brief 析构函数
     */
    ~QueryEngine() = default;

    // ==================== 查询操作 ====================

    /**
     * @brief 根据过滤器查询服务
     * @param filter 查询过滤器
     * @return 符合条件的服务列表
     */
    std::vector<ServiceInfo> query(const ServiceFilter& filter);

    /**
     * @brief 按服务名称查询
     * @param service_name 服务名称
     * @param healthy_only 是否只返回健康服务
     * @return 服务实例列表
     */
    std::vector<ServiceInfo> queryByName(
        const std::string& service_name,
        bool healthy_only = false
    );

    /**
     * @brief 按游戏类型查询
     * @param game_type 游戏类型
     * @param healthy_only 是否只返回健康服务
     * @return 服务实例列表
     */
    std::vector<ServiceInfo> queryByGameType(
        const std::string& game_type,
        bool healthy_only = false
    );

    /**
     * @brief 按区域查询
     * @param region 区域
     * @param healthy_only 是否只返回健康服务
     * @return 服务实例列表
     */
    std::vector<ServiceInfo> queryByRegion(
        const std::string& region,
        bool healthy_only = false
    );

    // ==================== 推荐操作 ====================

    /**
     * @brief 推荐服务实例
     * @param filter 查询过滤器
     * @param strategy 推荐策略
     * @return 推荐结果
     */
    ServiceRecommendation recommend(
        const ServiceFilter& filter,
        RecommendStrategy strategy = RecommendStrategy::LEAST_LOAD
    );

    /**
     * @brief 推荐指定游戏的服务实例
     * @param game_type 游戏类型
     * @param strategy 推荐策略
     * @return 推荐结果
     */
    ServiceRecommendation recommendForGame(
        const std::string& game_type,
        RecommendStrategy strategy = RecommendStrategy::LEAST_LOAD
    );

    // ==================== 排序操作 ====================

    /**
     * @brief 对服务列表排序
     * @param services 服务列表（会被修改）
     * @param sort_by 排序方式
     */
    void sortServices(
        std::vector<ServiceInfo>& services,
        ServiceFilter::SortBy sort_by
    );

private:
    std::shared_ptr<RedisStorage> storage_;
    std::shared_ptr<IndexManager> index_manager_;
    RegistryConfig config_;

    // 随机数生成器
    std::mt19937 rng_;

    // 计算负载分数
    double calculateLoadScore(const ServiceInfo& service);

    // 应用过滤器
    std::vector<ServiceInfo> applyFilter(
        const std::vector<ServiceInfo>& services,
        const ServiceFilter& filter
    );

    // 应用分页
    std::vector<ServiceInfo> applyPagination(
        const std::vector<ServiceInfo>& services,
        int limit,
        int offset
    );
};

} // namespace service_registry
} // namespace core_services
