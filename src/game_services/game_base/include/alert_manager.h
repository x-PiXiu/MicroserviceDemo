#pragma once

#include "common/monitoring/performance_monitor.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

/**
 * @file alert_manager.h
 * @brief 告警管理器 - 基于现有性能监控器的告警系统扩展
 * @details 提供阈值监控、告警触发、通知发送等功能
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 告警级别
         */
        enum class AlertLevel {
            INFO = 0,
            WARNING = 1,
            ERROR = 2,
            CRITICAL = 3
        };

        /**
         * @brief 告警规则
         */
        struct AlertRule {
            std::string rule_id;                                    ///< 规则ID
            std::string metric_name;                                ///< 监控指标名称
            std::string condition;                                  ///< 条件（>, <, >=, <=, ==, !=）
            double threshold;                                       ///< 阈值
            AlertLevel level;                                       ///< 告警级别
            int duration_seconds = 60;                              ///< 持续时间（秒）
            std::string message_template = "Metric {metric} {condition} {threshold}"; ///< 消息模板
            bool enabled = true;                                    ///< 是否启用
            std::chrono::system_clock::time_point created_at;       ///< 创建时间
            
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"rule_id", rule_id},
                    {"metric_name", metric_name},
                    {"condition", condition},
                    {"threshold", threshold},
                    {"level", static_cast<int>(level)},
                    {"duration_seconds", duration_seconds},
                    {"message_template", message_template},
                    {"enabled", enabled},
                    {"created_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                        created_at.time_since_epoch()).count()}
                };
            }
        };

        /**
         * @brief 告警事件
         */
        struct Alert {
            std::string alert_id;                                   ///< 告警ID
            std::string rule_id;                                    ///< 规则ID
            std::string metric_name;                                ///< 指标名称
            AlertLevel level;                                       ///< 告警级别
            std::string message;                                    ///< 告警消息
            double current_value;                                   ///< 当前值
            double threshold_value;                                 ///< 阈值
            std::chrono::system_clock::time_point triggered_at;     ///< 触发时间
            std::chrono::system_clock::time_point resolved_at;      ///< 解决时间
            bool resolved = false;                                  ///< 是否已解决
            int fire_count = 0;                                     ///< 触发次数
            
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"alert_id", alert_id},
                    {"rule_id", rule_id},
                    {"metric_name", metric_name},
                    {"level", static_cast<int>(level)},
                    {"message", message},
                    {"current_value", current_value},
                    {"threshold_value", threshold_value},
                    {"triggered_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                        triggered_at.time_since_epoch()).count()},
                    {"resolved_at", resolved ? std::chrono::duration_cast<std::chrono::milliseconds>(
                        resolved_at.time_since_epoch()).count() : 0},
                    {"resolved", resolved},
                    {"fire_count", fire_count}
                };
            }
        };

        /**
         * @brief 告警配置
         */
        struct AlertConfig {
            bool enable_alerts = true;                              ///< 是否启用告警
            int check_interval_seconds = 60;                       ///< 检查间隔（秒）
            int history_retention_hours = 168;                     ///< 历史保留时间（小时）
            bool enable_notifications = true;                      ///< 是否启用通知
            std::string notification_webhook = "";                 ///< 通知Webhook地址
            int max_alerts_per_minute = 10;                        ///< 每分钟最大告警数
            
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"enable_alerts", enable_alerts},
                    {"check_interval_seconds", check_interval_seconds},
                    {"history_retention_hours", history_retention_hours},
                    {"enable_notifications", enable_notifications},
                    {"notification_webhook", notification_webhook},
                    {"max_alerts_per_minute", max_alerts_per_minute}
                };
            }
        };

        /**
         * @brief 告警管理器
         * @details 基于现有性能监控器的告警系统
         */
        class AlertManager {
        public:
            using AlertCallback = std::function<void(const Alert&)>;

            /**
             * @brief 构造函数
             * @param config 告警配置
             * @param perf_monitor 性能监控器引用
             */
            explicit AlertManager(const AlertConfig& config,
                                common::monitoring::PerformanceMonitor& perf_monitor);

            /**
             * @brief 析构函数
             */
            ~AlertManager();

            // 禁用拷贝构造和赋值
            AlertManager(const AlertManager&) = delete;
            AlertManager& operator=(const AlertManager&) = delete;

            /**
             * @brief 启动告警监控
             * @return 是否启动成功
             */
            bool start();

            /**
             * @brief 停止告警监控
             */
            void stop();

            /**
             * @brief 添加告警规则
             * @param rule 告警规则
             * @return 是否添加成功
             */
            bool addRule(const AlertRule& rule);

            /**
             * @brief 移除告警规则
             * @param rule_id 规则ID
             * @return 是否移除成功
             */
            bool removeRule(const std::string& rule_id);

            /**
             * @brief 更新告警规则
             * @param rule 告警规则
             * @return 是否更新成功
             */
            bool updateRule(const AlertRule& rule);

            /**
             * @brief 启用/禁用告警规则
             * @param rule_id 规则ID
             * @param enabled 是否启用
             */
            void enableRule(const std::string& rule_id, bool enabled);

            /**
             * @brief 获取所有告警规则
             * @return 规则列表
             */
            std::vector<AlertRule> getAllRules() const;

            /**
             * @brief 获取活跃告警
             * @return 活跃告警列表
             */
            std::vector<Alert> getActiveAlerts() const;

            /**
             * @brief 获取告警历史
             * @param limit 限制数量
             * @return 历史告警列表
             */
            std::vector<Alert> getAlertHistory(int limit = 100) const;

            /**
             * @brief 手动触发告警检查
             */
            void checkAlerts();

            /**
             * @brief 解决告警
             * @param alert_id 告警ID
             * @param resolved_by 解决者
             */
            void resolveAlert(const std::string& alert_id, const std::string& resolved_by = "manual");

            /**
             * @brief 设置告警回调
             * @param callback 回调函数
             */
            void setAlertCallback(AlertCallback callback) { alert_callback_ = callback; }

            /**
             * @brief 获取告警统计信息
             * @return 统计信息
             */
            nlohmann::json getAlertStats() const;

            /**
             * @brief 更新配置
             * @param config 新配置
             */
            void updateConfig(const AlertConfig& config) { config_ = config; }

            /**
             * @brief 创建默认告警规则
             */
            void createDefaultRules();

        private:
            AlertConfig config_;
            common::monitoring::PerformanceMonitor& perf_monitor_;
            
            mutable std::shared_mutex rules_mutex_;
            std::unordered_map<std::string, AlertRule> rules_;
            
            mutable std::shared_mutex alerts_mutex_;
            std::unordered_map<std::string, Alert> active_alerts_;
            std::vector<Alert> alert_history_;
            
            std::atomic<bool> running_{false};
            std::atomic<bool> stopping_{false};
            std::thread check_thread_;
            
            AlertCallback alert_callback_;
            
            // 速率限制
            std::chrono::system_clock::time_point last_minute_start_;
            std::atomic<int> alerts_this_minute_{0};

            /**
             * @brief 检查线程函数
             */
            void checkThread();

            /**
             * @brief 评估告警规则
             * @param rule 告警规则
             */
            void evaluateRule(const AlertRule& rule);

            /**
             * @brief 检查条件是否满足
             * @param current_value 当前值
             * @param condition 条件
             * @param threshold 阈值
             * @return 是否满足条件
             */
            bool checkCondition(double current_value, const std::string& condition, double threshold) const;

            /**
             * @brief 触发告警
             * @param rule 告警规则
             * @param current_value 当前值
             */
            void fireAlert(const AlertRule& rule, double current_value);

            /**
             * @brief 解决告警
             * @param alert_id 告警ID
             */
            void resolveAlertInternal(const std::string& alert_id);

            /**
             * @brief 生成告警消息
             * @param rule 告警规则
             * @param current_value 当前值
             * @return 告警消息
             */
            std::string generateMessage(const AlertRule& rule, double current_value) const;

            /**
             * @brief 发送通知
             * @param alert 告警事件
             */
            void sendNotification(const Alert& alert);

            /**
             * @brief 清理过期告警历史
             */
            void cleanupHistory();

            /**
             * @brief 检查速率限制
             * @return 是否在限制内
             */
            bool checkRateLimit();
        };

    } // namespace game_base
} // namespace game_services


