/**
 * @file service_registry.h
 * @brief 服务注册中心主服务接口
 * @details 协调所有组件，提供服务注册中心功能
 *
 * @author Claude Code
 * @date 2025-02-19
 */

#pragma once

#include "registry_config.h"
#include "redis_storage.h"
#include "index_manager.h"
#include "registry_manager.h"
#include "health_monitor.h"
#include "query_engine.h"
#include "event_publisher.h"
#include "registry_handler.h"
#include "common/http/http_server.h"
#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include "common/scheduler/task_scheduler.h"
#include <memory>
#include <atomic>
#include <thread>

namespace core_services {
namespace service_registry {

/**
 * @brief 服务注册中心
 * @details 主服务类，协调所有组件
 */
class ServiceRegistry {
public:
    /**
     * @brief 构造函数
     * @param config 服务配置
     */
    explicit ServiceRegistry(const ServiceRegistryConfig& config);

    /**
     * @brief 析构函数
     */
    ~ServiceRegistry();

    // 禁止拷贝
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    // ==================== 生命周期管理 ====================

    /**
     * @brief 启动服务
     * @return 是否启动成功
     */
    bool start();

    /**
     * @brief 停止服务
     */
    void stop();

    /**
     * @brief 请求停止服务（async-signal-safe）
     * @note 仅设置原子标志位，不执行任何非 signal-safe 操作
     *       适用于信号处理函数中调用
     */
    void requestStop() { running_.store(false); }

    /**
     * @brief 检查服务是否正在运行
     * @return 是否运行中
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief 等待服务停止
     */
    void wait();

    // ==================== 组件访问 ====================

    /**
     * @brief 获取注册管理器
     */
    std::shared_ptr<RegistryManager> getRegistryManager() { return registry_manager_; }

    /**
     * @brief 获取健康监控器
     */
    std::shared_ptr<HealthMonitor> getHealthMonitor() { return health_monitor_; }

    /**
     * @brief 获取查询引擎
     */
    std::shared_ptr<QueryEngine> getQueryEngine() { return query_engine_; }

    /**
     * @brief 获取配置
     */
    const ServiceRegistryConfig& getConfig() const { return config_; }

private:
    ServiceRegistryConfig config_;
    std::atomic<bool> running_{false};

    // 组件
    std::shared_ptr<common::database::RedisPool> redis_pool_;
    std::shared_ptr<RedisStorage> storage_;
    std::shared_ptr<IndexManager> index_manager_;
    std::shared_ptr<EventPublisher> event_publisher_;
    std::shared_ptr<RegistryManager> registry_manager_;
    std::shared_ptr<HealthMonitor> health_monitor_;
    std::shared_ptr<QueryEngine> query_engine_;
    std::shared_ptr<RegistryHandler> handler_;
    std::shared_ptr<common::http::HttpServer> http_server_;
    std::shared_ptr<common::scheduler::TaskScheduler> scheduler_;

    // 初始化组件
    bool initializeComponents();

    // 初始化定时任务
    void initializeScheduledTasks();

    // 设置 CORS 中间件
    void setupCorsMiddleware();

    // 清理过期服务任务
    void cleanupTask();

    // 更新统计信息任务
    void statsUpdateTask();
};

} // namespace service_registry
} // namespace core_services
