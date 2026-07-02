/**
 * @file alert_manager.cpp
 * @brief 告警管理器实现 - 基于现有性能监控器的告警系统
 */

#include "alert_manager.h"
#include "common/logger/logger.h"
#include "common/http/http_client.h"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <regex>

namespace game_services {
    namespace game_base {

        AlertManager::AlertManager(const AlertConfig& config,
                                 common::monitoring::PerformanceMonitor& perf_monitor)
            : config_(config)
            , perf_monitor_(perf_monitor)
            , last_minute_start_(std::chrono::system_clock::now()) {
            
            LOG_INFO("🚨 AlertManager initializing with configuration:");
            LOG_INFO("  - Alerts enabled: " + std::string(config_.enable_alerts ? "Yes" : "No"));
            LOG_INFO("  - Check interval: " + std::to_string(config_.check_interval_seconds) + "s");
            LOG_INFO("  - History retention: " + std::to_string(config_.history_retention_hours) + "h");
            LOG_INFO("  - Notifications enabled: " + std::string(config_.enable_notifications ? "Yes" : "No"));
            LOG_INFO("  - Max alerts per minute: " + std::to_string(config_.max_alerts_per_minute));
            
            // 创建默认告警规则
            createDefaultRules();
        }

        AlertManager::~AlertManager() {
            stop();
            LOG_INFO("AlertManager destroyed");
        }

        bool AlertManager::start() {
            if (!config_.enable_alerts) {
                LOG_INFO("Alerts disabled by configuration");
                return true;
            }

            if (running_.exchange(true)) {
                LOG_WARNING("AlertManager already running");
                return true;
            }

            LOG_INFO("🚀 Starting AlertManager...");

            try {
                // 启动检查线程
                check_thread_ = std::thread(&AlertManager::checkThread, this);
                
                LOG_INFO("✅ AlertManager started successfully");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to start AlertManager: " + std::string(e.what()));
                running_.store(false);
                return false;
            }
        }

        void AlertManager::stop() {
            if (!running_.exchange(false)) {
                return;
            }

            stopping_.store(true);
            LOG_INFO("🛑 Stopping AlertManager...");

            // 等待检查线程结束
            if (check_thread_.joinable()) {
                check_thread_.join();
            }

            LOG_INFO("AlertManager stopped");
            stopping_.store(false);
        }

        bool AlertManager::addRule(const AlertRule& rule) {
            if (rule.rule_id.empty()) {
                LOG_ERROR("Alert rule ID cannot be empty");
                return false;
            }

            std::unique_lock<std::shared_mutex> lock(rules_mutex_);
            
            if (rules_.find(rule.rule_id) != rules_.end()) {
                LOG_WARNING("Alert rule already exists: " + rule.rule_id);
                return false;
            }

            rules_[rule.rule_id] = rule;
            
            LOG_INFO("✅ Alert rule added: " + rule.rule_id + " - " + rule.message_template);
            return true;
        }

        bool AlertManager::removeRule(const std::string& rule_id) {
            std::unique_lock<std::shared_mutex> lock(rules_mutex_);
            
            auto it = rules_.find(rule_id);
            if (it == rules_.end()) {
                return false;
            }

            rules_.erase(it);
            
            // 解决相关的活跃告警
            std::unique_lock<std::shared_mutex> alerts_lock(alerts_mutex_);
            for (auto alert_it = active_alerts_.begin(); alert_it != active_alerts_.end();) {
                if (alert_it->second.rule_id == rule_id) {
                    resolveAlertInternal(alert_it->first);
                    alert_it = active_alerts_.erase(alert_it);
                } else {
                    ++alert_it;
                }
            }

            LOG_INFO("🗑️ Alert rule removed: " + rule_id);
            return true;
        }

        bool AlertManager::updateRule(const AlertRule& rule) {
            std::unique_lock<std::shared_mutex> lock(rules_mutex_);
            
            auto it = rules_.find(rule.rule_id);
            if (it == rules_.end()) {
                return false;
            }

            it->second = rule;
            
            LOG_INFO("🔄 Alert rule updated: " + rule.rule_id);
            return true;
        }

        void AlertManager::enableRule(const std::string& rule_id, bool enabled) {
            std::unique_lock<std::shared_mutex> lock(rules_mutex_);
            
            auto it = rules_.find(rule_id);
            if (it != rules_.end()) {
                it->second.enabled = enabled;
                LOG_INFO("🔧 Alert rule " + rule_id + " " + (enabled ? "enabled" : "disabled"));
            }
        }

        std::vector<AlertRule> AlertManager::getAllRules() const {
            std::shared_lock<std::shared_mutex> lock(rules_mutex_);
            
            std::vector<AlertRule> result;
            for (const auto& [rule_id, rule] : rules_) {
                result.push_back(rule);
            }
            
            return result;
        }

        std::vector<Alert> AlertManager::getActiveAlerts() const {
            std::shared_lock<std::shared_mutex> lock(alerts_mutex_);
            
            std::vector<Alert> result;
            for (const auto& [alert_id, alert] : active_alerts_) {
                if (!alert.resolved) {
                    result.push_back(alert);
                }
            }
            
            // 按告警级别排序
            std::sort(result.begin(), result.end(), [](const Alert& a, const Alert& b) {
                return static_cast<int>(a.level) > static_cast<int>(b.level);
            });
            
            return result;
        }

        std::vector<Alert> AlertManager::getAlertHistory(int limit) const {
            std::shared_lock<std::shared_mutex> lock(alerts_mutex_);
            
            std::vector<Alert> result = alert_history_;
            
            // 按时间倒序排序
            std::sort(result.begin(), result.end(), [](const Alert& a, const Alert& b) {
                return a.triggered_at > b.triggered_at;
            });
            
            if (limit > 0 && result.size() > static_cast<size_t>(limit)) {
                result.resize(limit);
            }
            
            return result;
        }

        void AlertManager::checkAlerts() {
            if (!config_.enable_alerts) {
                return;
            }

            std::shared_lock<std::shared_mutex> rules_lock(rules_mutex_);
            
            for (const auto& [rule_id, rule] : rules_) {
                if (rule.enabled) {
                    evaluateRule(rule);
                }
            }
        }

        void AlertManager::resolveAlert(const std::string& alert_id, const std::string& resolved_by) {
            std::unique_lock<std::shared_mutex> lock(alerts_mutex_);
            resolveAlertInternal(alert_id);
            LOG_INFO("✅ Alert resolved manually: " + alert_id + " by " + resolved_by);
        }

        nlohmann::json AlertManager::getAlertStats() const {
            std::shared_lock<std::shared_mutex> rules_lock(rules_mutex_);
            std::shared_lock<std::shared_mutex> alerts_lock(alerts_mutex_);
            
            nlohmann::json stats;
            stats["total_rules"] = rules_.size();
            stats["active_alerts"] = active_alerts_.size();
            stats["alert_history_count"] = alert_history_.size();
            stats["config"] = config_.toJson();
            
            // 按级别统计活跃告警
            int info_count = 0, warning_count = 0, error_count = 0, critical_count = 0;
            for (const auto& [alert_id, alert] : active_alerts_) {
                if (alert.resolved) continue;
                switch (alert.level) {
                    case AlertLevel::INFO: info_count++; break;
                    case AlertLevel::WARNING: warning_count++; break;
                    case AlertLevel::ERROR: error_count++; break;
                    case AlertLevel::CRITICAL: critical_count++; break;
                }
            }
            
            stats["active_alerts_by_level"] = {
                {"info", info_count},
                {"warning", warning_count},
                {"error", error_count},
                {"critical", critical_count}
            };
            
            // 启用/禁用规则统计
            int enabled_rules = 0, disabled_rules = 0;
            for (const auto& [rule_id, rule] : rules_) {
                if (rule.enabled) enabled_rules++;
                else disabled_rules++;
            }
            stats["enabled_rules"] = enabled_rules;
            stats["disabled_rules"] = disabled_rules;
            
            stats["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            return stats;
        }

        void AlertManager::createDefaultRules() {
            // CPU使用率告警
            AlertRule cpu_rule;
            cpu_rule.rule_id = "cpu_usage_high";
            cpu_rule.metric_name = "system_cpu_usage_percent";
            cpu_rule.condition = ">";
            cpu_rule.threshold = 80.0;
            cpu_rule.level = AlertLevel::WARNING;
            cpu_rule.duration_seconds = 300; // 5分钟
            cpu_rule.message_template = "CPU使用率过高: {current_value}% > {threshold}%";
            cpu_rule.created_at = std::chrono::system_clock::now();
            addRule(cpu_rule);

            // 内存使用率告警
            AlertRule memory_rule;
            memory_rule.rule_id = "memory_usage_high";
            memory_rule.metric_name = "system_memory_usage_percent";
            memory_rule.condition = ">";
            memory_rule.threshold = 85.0;
            memory_rule.level = AlertLevel::WARNING;
            memory_rule.duration_seconds = 300;
            memory_rule.message_template = "内存使用率过高: {current_value}% > {threshold}%";
            memory_rule.created_at = std::chrono::system_clock::now();
            addRule(memory_rule);

            // WebSocket连接数告警
            AlertRule websocket_rule;
            websocket_rule.rule_id = "websocket_connections_high";
            websocket_rule.metric_name = "websocket_connections_current";
            websocket_rule.condition = ">";
            websocket_rule.threshold = 1000.0;
            websocket_rule.level = AlertLevel::ERROR;
            websocket_rule.duration_seconds = 60;
            websocket_rule.message_template = "WebSocket连接数过多: {current_value} > {threshold}";
            websocket_rule.created_at = std::chrono::system_clock::now();
            addRule(websocket_rule);

            // HTTP错误率告警
            AlertRule http_error_rule;
            http_error_rule.rule_id = "http_error_rate_high";
            http_error_rule.metric_name = "http_errors_total";
            http_error_rule.condition = ">";
            http_error_rule.threshold = 50.0;
            http_error_rule.level = AlertLevel::ERROR;
            http_error_rule.duration_seconds = 120;
            http_error_rule.message_template = "HTTP错误数过多: {current_value} > {threshold}";
            http_error_rule.created_at = std::chrono::system_clock::now();
            addRule(http_error_rule);

            // 数据库查询错误告警
            AlertRule db_error_rule;
            db_error_rule.rule_id = "db_errors_high";
            db_error_rule.metric_name = "db_errors_total";
            db_error_rule.condition = ">";
            db_error_rule.threshold = 10.0;
            db_error_rule.level = AlertLevel::CRITICAL;
            db_error_rule.duration_seconds = 60;
            db_error_rule.message_template = "数据库错误过多: {current_value} > {threshold}";
            db_error_rule.created_at = std::chrono::system_clock::now();
            addRule(db_error_rule);

            LOG_INFO("✅ Default alert rules created");
        }

        void AlertManager::checkThread() {
            LOG_INFO("Alert checking thread started");

            while (running_.load() && !stopping_.load()) {
                try {
                    checkAlerts();
                    cleanupHistory();
                    
                    std::this_thread::sleep_for(std::chrono::seconds(config_.check_interval_seconds));
                    
                } catch (const std::exception& e) {
                    LOG_ERROR("Alert checking error: " + std::string(e.what()));
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                }
            }

            LOG_INFO("Alert checking thread stopped");
        }

        void AlertManager::evaluateRule(const AlertRule& rule) {
            try {
                // 从性能监控器获取指标数据
                nlohmann::json metric_data = perf_monitor_.getMetric(rule.metric_name);
                
                if (metric_data.empty()) {
                    return; // 指标不存在
                }

                double current_value = 0.0;
                bool metric_found = false;

                // 从不同类型的指标中提取当前值
                if (metric_data.contains("gauges")) {
                    for (const auto& [key, value] : metric_data["gauges"].items()) {
                        if (key.find(rule.metric_name) == 0) {
                            current_value = value.get<double>();
                            metric_found = true;
                            break;
                        }
                    }
                }
                
                if (!metric_found && metric_data.contains("counters")) {
                    for (const auto& [key, value] : metric_data["counters"].items()) {
                        if (key.find(rule.metric_name) == 0) {
                            current_value = value.get<double>();
                            metric_found = true;
                            break;
                        }
                    }
                }

                if (!metric_found) {
                    return; // 无法获取指标值
                }

                // 检查条件是否满足
                bool condition_met = checkCondition(current_value, rule.condition, rule.threshold);
                std::string alert_key = rule.rule_id + "_" + rule.metric_name;

                std::unique_lock<std::shared_mutex> alerts_lock(alerts_mutex_);
                auto active_it = active_alerts_.find(alert_key);

                if (condition_met) {
                    if (active_it == active_alerts_.end()) {
                        // 新告警
                        fireAlert(rule, current_value);
                    } else {
                        // 更新现有告警
                        active_it->second.current_value = current_value;
                        active_it->second.fire_count++;
                    }
                } else if (active_it != active_alerts_.end()) {
                    // 条件不满足但存在活跃告警，解决它
                    resolveAlertInternal(alert_key);
                    active_alerts_.erase(active_it);
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Error evaluating rule " + rule.rule_id + ": " + std::string(e.what()));
            }
        }

        bool AlertManager::checkCondition(double current_value, const std::string& condition, double threshold) const {
            if (condition == ">") {
                return current_value > threshold;
            } else if (condition == ">=") {
                return current_value >= threshold;
            } else if (condition == "<") {
                return current_value < threshold;
            } else if (condition == "<=") {
                return current_value <= threshold;
            } else if (condition == "==") {
                return std::abs(current_value - threshold) < 0.001; // 浮点数相等比较
            } else if (condition == "!=") {
                return std::abs(current_value - threshold) >= 0.001;
            }
            
            LOG_WARNING("Unknown condition: " + condition);
            return false;
        }

        void AlertManager::fireAlert(const AlertRule& rule, double current_value) {
            if (!checkRateLimit()) {
                LOG_WARNING("Alert rate limit exceeded, skipping alert: " + rule.rule_id);
                return;
            }

            std::string alert_key = rule.rule_id + "_" + rule.metric_name;
            
            Alert alert;
            alert.alert_id = alert_key + "_" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            alert.rule_id = rule.rule_id;
            alert.metric_name = rule.metric_name;
            alert.level = rule.level;
            alert.current_value = current_value;
            alert.threshold_value = rule.threshold;
            alert.triggered_at = std::chrono::system_clock::now();
            alert.message = generateMessage(rule, current_value);
            alert.fire_count = 1;

            // 添加到活跃告警
            active_alerts_[alert_key] = alert;
            
            // 添加到历史记录
            alert_history_.push_back(alert);

            // 发送通知
            if (config_.enable_notifications) {
                sendNotification(alert);
            }

            // 触发回调
            if (alert_callback_) {
                try {
                    alert_callback_(alert);
                } catch (const std::exception& e) {
                    LOG_ERROR("Alert callback error: " + std::string(e.what()));
                }
            }

            std::string level_str;
            switch (alert.level) {
                case AlertLevel::INFO: level_str = "INFO"; break;
                case AlertLevel::WARNING: level_str = "WARNING"; break;
                case AlertLevel::ERROR: level_str = "ERROR"; break;
                case AlertLevel::CRITICAL: level_str = "CRITICAL"; break;
            }

            LOG_WARNING("🚨 ALERT [" + level_str + "] " + alert.message);
        }

        void AlertManager::resolveAlertInternal(const std::string& alert_id) {
            auto it = active_alerts_.find(alert_id);
            if (it != active_alerts_.end()) {
                it->second.resolved = true;
                it->second.resolved_at = std::chrono::system_clock::now();
                
                // 更新历史记录中对应的告警
                for (auto& hist_alert : alert_history_) {
                    if (hist_alert.alert_id == it->second.alert_id) {
                        hist_alert.resolved = true;
                        hist_alert.resolved_at = it->second.resolved_at;
                        break;
                    }
                }
            }
        }

        std::string AlertManager::generateMessage(const AlertRule& rule, double current_value) const {
            std::string message = rule.message_template;
            
            // 替换占位符
            std::regex current_regex(R"(\{current_value\})");
            std::regex threshold_regex(R"(\{threshold\})");
            std::regex metric_regex(R"(\{metric\})");
            std::regex condition_regex(R"(\{condition\})");
            
            message = std::regex_replace(message, current_regex, std::to_string(current_value));
            message = std::regex_replace(message, threshold_regex, std::to_string(rule.threshold));
            message = std::regex_replace(message, metric_regex, rule.metric_name);
            message = std::regex_replace(message, condition_regex, rule.condition);
            
            return message;
        }

        void AlertManager::sendNotification(const Alert& alert) {
            if (config_.notification_webhook.empty()) {
                return;
            }

            try {
                nlohmann::json notification;
                notification["alert"] = alert.toJson();
                notification["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                notification["source"] = "GameServerBase";
                
                // 使用HTTP客户端发送通知（异步）
                std::thread notification_thread([this, notification]() {
                    try {
                        common::http::HttpClient client;
                        auto response = client.post(config_.notification_webhook, notification.dump(),
                            {{"Content-Type", "application/json"}});
                        
                        if (response.status_code == 200) {
                            LOG_DEBUG("Alert notification sent successfully");
                        } else {
                            LOG_WARNING("Alert notification failed: " + std::to_string(response.status_code));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Alert notification error: " + std::string(e.what()));
                    }
                });
                
                notification_thread.detach();
                
            } catch (const std::exception& e) {
                LOG_ERROR("Error preparing alert notification: " + std::string(e.what()));
            }
        }

        void AlertManager::cleanupHistory() {
            std::unique_lock<std::shared_mutex> lock(alerts_mutex_);
            
            auto now = std::chrono::system_clock::now();
            auto cutoff_time = now - std::chrono::hours(config_.history_retention_hours);
            
            // 清理过期的历史记录
            alert_history_.erase(
                std::remove_if(alert_history_.begin(), alert_history_.end(),
                    [cutoff_time](const Alert& alert) {
                        return alert.triggered_at < cutoff_time;
                    }),
                alert_history_.end());
            
            // 限制历史记录数量（最多保留1000条）
            if (alert_history_.size() > 1000) {
                // 按时间排序，保留最新的1000条
                std::sort(alert_history_.begin(), alert_history_.end(),
                    [](const Alert& a, const Alert& b) {
                        return a.triggered_at > b.triggered_at;
                    });
                alert_history_.resize(1000);
            }
        }

        bool AlertManager::checkRateLimit() {
            auto now = std::chrono::system_clock::now();
            
            // 重置分钟窗口
            if (now - last_minute_start_ >= std::chrono::minutes(1)) {
                last_minute_start_ = now;
                alerts_this_minute_.store(0);
            }
            
            // 检查是否超过限制
            if (alerts_this_minute_.load() >= config_.max_alerts_per_minute) {
                return false;
            }
            
            alerts_this_minute_++;
            return true;
        }

    } // namespace game_base
} // namespace game_services

