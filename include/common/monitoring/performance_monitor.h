/**
 * @file performance_monitor.h
 * @brief 性能监控组件 - 收集和分析系统性能指标
 * @author 29108
 * @date 2025/8/6
 * @version 1.0
 *
 * 功能特性:
 * - 响应时间监控
 * - 吞吐量统计
 * - 错误率分析
 * - 资源使用监控
 * - 实时指标收集
 * - 性能报告生成
 *
 * 监控指标:
 * - HTTP请求响应时间
 * - WebSocket连接数
 * - 数据库查询性能
 * - 内存和CPU使用率
 * - 错误和异常统计
 */

#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include "common/config/config_manager.h"
#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <shared_mutex>
#include <queue>
#include <nlohmann/json.hpp>

namespace common {
    namespace monitoring {

        /**
         * @brief 性能指标类型
         */
        enum class MetricType {
            COUNTER,        ///< 计数器（累计值）
            GAUGE,          ///< 仪表（瞬时值）
            HISTOGRAM,      ///< 直方图（分布统计）
            TIMER          ///< 计时器（时间统计）
        };

        /**
         * @brief 性能指标数据
         */
        struct MetricData {
            std::string name;                                          ///< 指标名称
            MetricType type;                                           ///< 指标类型
            double value;                                              ///< 指标值
            std::unordered_map<std::string, std::string> labels;       ///< 标签
            std::chrono::system_clock::time_point timestamp;           ///< 时间戳
            
            /**
             * @brief 转换为JSON格式
             */
            nlohmann::json toJson() const;
        };

        /**
         * @brief 直方图统计数据
         */
        struct HistogramStats {
            std::vector<double> buckets;                               ///< 分桶边界
            std::vector<uint64_t> counts;                              ///< 各桶计数
            uint64_t total_count = 0;                                  ///< 总计数
            double sum = 0.0;                                          ///< 总和
            double min_value = std::numeric_limits<double>::max();     ///< 最小值
            double max_value = std::numeric_limits<double>::lowest();  ///< 最大值
            
            /**
             * @brief 添加观测值
             */
            void observe(double value);
            
            /**
             * @brief 计算百分位数
             */
            double percentile(double p) const;
            
            /**
             * @brief 计算平均值
             */
            double average() const;
            
            /**
             * @brief 转换为JSON格式
             */
            nlohmann::json toJson() const;
        };

        /**
         * @brief 性能监控器
         */
        class PerformanceMonitor {
        public:
            /**
             * @brief 配置结构体
             */
            struct Config {
                bool enable_monitoring = true;                         ///< 是否启用监控
                int collection_interval_seconds = 60;                  ///< 收集间隔（秒）
                int retention_hours = 24;                              ///< 数据保留时间（小时）
                bool enable_system_metrics = true;                     ///< 是否启用系统指标
                bool enable_application_metrics = true;                ///< 是否启用应用指标
                bool enable_export = false;                            ///< 是否启用指标导出
                std::string export_endpoint = "/metrics";              ///< 导出端点
                std::string export_format = "json";                    ///< 导出格式（json/prometheus）
                
                /**
                 * @brief 从配置管理器加载配置
                 */
                static Config fromConfigManager();
                
                /**
                 * @brief 验证配置有效性
                 */
                bool validate() const;
            };

            /**
             * @brief 构造函数
             * @param config 配置
             * @param thread_pool 线程池（可选）
             */
            explicit PerformanceMonitor(const Config& config,
                                      std::shared_ptr<common::thread_pool::ThreadPool> thread_pool = nullptr);

            /**
             * @brief 析构函数
             */
            ~PerformanceMonitor();

            /**
             * @brief 启动监控
             */
            bool start();

            /**
             * @brief 停止监控
             */
            void stop();

            // ==================== 指标记录接口 ====================

            /**
             * @brief 增加计数器
             * @param name 指标名称
             * @param value 增加值（默认1）
             * @param labels 标签（可选）
             */
            void incrementCounter(const std::string& name, double value = 1.0,
                                const std::unordered_map<std::string, std::string>& labels = {});

            /**
             * @brief 设置仪表值
             * @param name 指标名称
             * @param value 指标值
             * @param labels 标签（可选）
             */
            void setGauge(const std::string& name, double value,
                         const std::unordered_map<std::string, std::string>& labels = {});

            /**
             * @brief 记录直方图观测值
             * @param name 指标名称
             * @param value 观测值
             * @param labels 标签（可选）
             */
            void observeHistogram(const std::string& name, double value,
                                const std::unordered_map<std::string, std::string>& labels = {});

            /**
             * @brief 记录计时器（自动计算耗时）
             * @param name 指标名称
             * @param start_time 开始时间
             * @param labels 标签（可选）
             */
            void recordTimer(const std::string& name, 
                           const std::chrono::high_resolution_clock::time_point& start_time,
                           const std::unordered_map<std::string, std::string>& labels = {});

            // ==================== 便捷接口 ====================

            /**
             * @brief 记录HTTP请求
             * @param method HTTP方法
             * @param path 请求路径
             * @param status_code 状态码
             * @param duration 耗时（毫秒）
             */
            void recordHttpRequest(const std::string& method, const std::string& path,
                                 int status_code, std::chrono::milliseconds duration);

            /**
             * @brief 记录数据库查询
             * @param operation 操作类型（SELECT/INSERT/UPDATE/DELETE）
             * @param table 表名
             * @param duration 耗时（毫秒）
             * @param success 是否成功
             */
            void recordDatabaseQuery(const std::string& operation, const std::string& table,
                                   std::chrono::milliseconds duration, bool success = true);

            /**
             * @brief 记录WebSocket连接
             * @param event 事件类型（connect/disconnect）
             * @param current_connections 当前连接数
             */
            void recordWebSocketEvent(const std::string& event, int current_connections);

            /**
             * @brief 记录错误
             * @param error_type 错误类型
             * @param error_code 错误代码
             * @param service 服务名称
             */
            void recordError(const std::string& error_type, const std::string& error_code,
                           const std::string& service = "");

            // ==================== 查询接口 ====================

            /**
             * @brief 获取所有指标
             * @return 指标数据
             */
            nlohmann::json getAllMetrics() const;

            /**
             * @brief 获取特定指标
             * @param name 指标名称
             * @return 指标数据
             */
            nlohmann::json getMetric(const std::string& name) const;

            /**
             * @brief 获取系统统计信息
             * @return 统计信息
             */
            nlohmann::json getSystemStats() const;

            /**
             * @brief 获取性能报告
             * @param duration 时间范围（小时）
             * @return 性能报告
             */
            nlohmann::json getPerformanceReport(int duration_hours = 1) const;

            /**
             * @brief 导出Prometheus格式指标
             * @return Prometheus格式字符串
             */
            std::string exportPrometheusMetrics() const;

        private:
            Config config_;
            std::atomic<bool> running_{false};
            std::atomic<bool> stopping_{false};

            // 线程池
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;

            // 指标存储
            mutable std::shared_mutex metrics_mutex_;
            std::unordered_map<std::string, std::atomic<double>> counters_;
            std::unordered_map<std::string, std::atomic<double>> gauges_;
            std::unordered_map<std::string, HistogramStats> histograms_;
            
            // 历史数据
            mutable std::shared_mutex history_mutex_;
            std::queue<MetricData> metric_history_;

            /**
             * @brief 启动收集任务
             */
            void startCollectionTask();

            /**
             * @brief 收集系统指标
             */
            void collectSystemMetrics();

            /**
             * @brief 收集应用指标
             */
            void collectApplicationMetrics();

            /**
             * @brief 清理过期数据
             */
            void cleanupExpiredData();

            /**
             * @brief 获取系统CPU使用率
             */
            double getSystemCpuUsage() const;

            /**
             * @brief 获取系统内存使用率
             */
            double getSystemMemoryUsage() const;

            /**
             * @brief 获取当前时间戳
             */
            std::string getCurrentTimestamp() const;

            /**
             * @brief 生成指标键
             */
            std::string generateMetricKey(const std::string& name,
                                        const std::unordered_map<std::string, std::string>& labels) const;
        };

        /**
         * @brief 性能监控器单例
         */
        class PerformanceMonitorSingleton {
        public:
            static PerformanceMonitor& getInstance();
            static void initialize(const PerformanceMonitor::Config& config,
                                 std::shared_ptr<common::thread_pool::ThreadPool> thread_pool = nullptr);

        private:
            static std::unique_ptr<PerformanceMonitor> instance_;
            static std::once_flag initialized_;
        };

        // 便捷宏定义
        #define PERF_COUNTER(name, value, ...) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().incrementCounter(name, value, ##__VA_ARGS__)

        #define PERF_GAUGE(name, value, ...) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().setGauge(name, value, ##__VA_ARGS__)

        #define PERF_HISTOGRAM(name, value, ...) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().observeHistogram(name, value, ##__VA_ARGS__)

        #define PERF_TIMER(name, start_time, ...) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().recordTimer(name, start_time, ##__VA_ARGS__)

        #define PERF_HTTP_REQUEST(method, path, status, duration) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().recordHttpRequest(method, path, status, duration)

        #define PERF_DB_QUERY(op, table, duration, success) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().recordDatabaseQuery(op, table, duration, success)

        #define PERF_ERROR(type, code, service) \
            ::common::monitoring::PerformanceMonitorSingleton::getInstance().recordError(type, code, service)

    } // namespace monitoring
} // namespace common

#endif // PERFORMANCE_MONITOR_H
