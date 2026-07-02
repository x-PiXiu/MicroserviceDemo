#include "common/monitoring/performance_monitor.h"
#include <thread>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace common {
    namespace monitoring {

        nlohmann::json MetricData::toJson() const {
            return {
                {"name", name},
                {"type", static_cast<int>(type)},
                {"value", value},
                {"labels", labels},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    timestamp.time_since_epoch()).count()}
            };
        }

        void HistogramStats::observe(double value) {
            total_count++;
            sum += value;
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
            
            // 找到对应的桶
            for (size_t i = 0; i < buckets.size(); ++i) {
                if (value <= buckets[i]) {
                    counts[i]++;
                    break;
                }
            }
        }

        double HistogramStats::percentile(double p) const {
            if (total_count == 0) return 0.0;
            
            uint64_t target_count = static_cast<uint64_t>(total_count * p / 100.0);
            uint64_t cumulative_count = 0;
            
            for (size_t i = 0; i < buckets.size(); ++i) {
                cumulative_count += counts[i];
                if (cumulative_count >= target_count) {
                    return buckets[i];
                }
            }
            
            return max_value;
        }

        double HistogramStats::average() const {
            return total_count > 0 ? sum / total_count : 0.0;
        }

        nlohmann::json HistogramStats::toJson() const {
            // 🔧 安全的数值验证函数
            auto safe_double = [](double val) -> double {
                if (std::isnan(val) || std::isinf(val)) {
                    return 0.0;
                }
                return val;
            };
            
            return {
                {"buckets", buckets},
                {"counts", counts},
                {"total_count", total_count},
                {"sum", safe_double(sum)},
                {"min", safe_double(min_value)},
                {"max", safe_double(max_value)},
                {"average", safe_double(average())},
                {"p50", safe_double(percentile(50))},
                {"p95", safe_double(percentile(95))},
                {"p99", safe_double(percentile(99))}
            };
        }

        PerformanceMonitor::Config PerformanceMonitor::Config::fromConfigManager() {
            auto& config_manager = common::config::ConfigManager::getInstance();
            
            Config config;
            
            // 🔧 修复JSON类型错误：添加异常处理和配置值验证
            try {
                config.enable_monitoring = config_manager.get<bool>("monitoring.enable", true);
                config.collection_interval_seconds = config_manager.get<int>("monitoring.collection_interval", 60);
                config.retention_hours = config_manager.get<int>("monitoring.retention_hours", 24);
                config.enable_system_metrics = config_manager.get<bool>("monitoring.enable_system_metrics", true);
                config.enable_application_metrics = config_manager.get<bool>("monitoring.enable_application_metrics", true);
                config.enable_export = config_manager.get<bool>("monitoring.enable_export", false);
                config.export_endpoint = config_manager.get<std::string>("monitoring.export_endpoint", "/metrics");
                config.export_format = config_manager.get<std::string>("monitoring.export_format", "json");
                
                // 🔧 验证关键数值配置
                if (config.collection_interval_seconds <= 0) {
                    LOG_WARNING("Invalid monitoring.collection_interval (" + 
                               std::to_string(config.collection_interval_seconds) + "), using default 60");
                    config.collection_interval_seconds = 60;
                }
                
                if (config.retention_hours <= 0) {
                    LOG_WARNING("Invalid monitoring.retention_hours (" + 
                               std::to_string(config.retention_hours) + "), using default 24");
                    config.retention_hours = 24;
                }
                
                LOG_DEBUG("PerformanceMonitor配置加载成功: interval=" + 
                         std::to_string(config.collection_interval_seconds) + "s");
                
            } catch (const std::exception& e) {
                LOG_ERROR("加载PerformanceMonitor配置失败: " + std::string(e.what()) + ", 使用默认配置");
                
                // 🔧 发生异常时使用完全默认的配置
                config.enable_monitoring = true;
                config.collection_interval_seconds = 60;
                config.retention_hours = 24;
                config.enable_system_metrics = true;
                config.enable_application_metrics = true;
                config.enable_export = false;
                config.export_endpoint = "/metrics";
                config.export_format = "json";
            }
            
            return config;
        }

        bool PerformanceMonitor::Config::validate() const {
            if (collection_interval_seconds <= 0 || retention_hours <= 0) {
                return false;
            }
            
            if (export_format != "json" && export_format != "prometheus") {
                return false;
            }
            
            return true;
        }

        PerformanceMonitor::PerformanceMonitor(const Config& config,
                                             std::shared_ptr<common::thread_pool::ThreadPool> thread_pool)
            : config_(config)
            , thread_pool_(thread_pool) {
            
            if (!config_.validate()) {
                throw std::invalid_argument("Invalid PerformanceMonitor configuration");
            }
            
            LOG_INFO("PerformanceMonitor created with monitoring " +
                std::string((config_.enable_monitoring ? "enabled" : "disabled")));
        }

        PerformanceMonitor::~PerformanceMonitor() {
            stop();
            LOG_INFO("PerformanceMonitor destroyed");
        }

        bool PerformanceMonitor::start() {
            if (!config_.enable_monitoring) {
                LOG_INFO("Performance monitoring is disabled");
                return true;
            }
            
            if (running_.exchange(true)) {
                LOG_WARNING("PerformanceMonitor already running");
                return true;
            }
            
            LOG_INFO("Starting PerformanceMonitor...");
            
            try {
                // 启动收集任务
                // startCollectionTask();
                
                 LOG_WARNING("PerformanceMonitor started False");
                return true;
                
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to start PerformanceMonitor: " + std::string(e.what()));
                running_.store(false);
                return false;
            }
        }

        void PerformanceMonitor::stop() {
            if (!running_.exchange(false)) {
                return;
            }
            
            stopping_.store(true);
            LOG_INFO("Stopping PerformanceMonitor...");
            
            // 等待任务完成
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            LOG_INFO("PerformanceMonitor stopped");
            stopping_.store(false);
        }

        void PerformanceMonitor::incrementCounter(const std::string& name, double value,
                                                const std::unordered_map<std::string, std::string>& labels) {
            if (!config_.enable_monitoring) return;
            
            // 🔧 安全验证输入值，防止NaN/Inf导致JSON序列化错误
            if (std::isnan(value) || std::isinf(value)) {
                LOG_WARNING("⚠️ Attempted to increment counter " + name + " with invalid value: " + std::to_string(value) + ", using 0.0");
                value = 0.0;
            }
            
            if (value < 0) {
                LOG_WARNING("⚠️ Attempted to increment counter " + name + " with negative value: " + std::to_string(value) + ", using 0.0");
                value = 0.0;
            }
            
            std::string key = generateMetricKey(name, labels);
            
            std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
            // 使用compare_and_swap循环来实现原子加法，兼容所有编译器
            double expected = counters_[key].load();
            double desired;
            do {
                desired = expected + value;
                // 🔧 验证计算结果有效性
                if (std::isnan(desired) || std::isinf(desired)) {
                    LOG_WARNING("⚠️ Counter calculation resulted in invalid value for " + name + ", resetting to expected value");
                    desired = expected;
                    break;
                }
            } while (!counters_[key].compare_exchange_weak(expected, desired));
            
            // 记录历史数据
            MetricData data;
            data.name = name;
            data.type = MetricType::COUNTER;
            data.value = value;  // 使用已验证的value
            data.labels = labels;
            data.timestamp = std::chrono::system_clock::now();
            
            std::unique_lock<std::shared_mutex> history_lock(history_mutex_);
            metric_history_.push(data);
        }

        void PerformanceMonitor::setGauge(const std::string& name, double value,
                                        const std::unordered_map<std::string, std::string>& labels) {
            if (!config_.enable_monitoring) return;
            
            // 🔧 安全验证输入值，防止NaN/Inf导致JSON序列化错误
            if (std::isnan(value) || std::isinf(value)) {
                LOG_WARNING("⚠️ Attempted to set invalid gauge value for " + name + ": " + std::to_string(value) + ", using 0.0");
                value = 0.0;
            }
            
            std::string key = generateMetricKey(name, labels);
            
            std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
            gauges_[key].store(value);
            
            // 记录历史数据
            MetricData data;
            data.name = name;
            data.type = MetricType::GAUGE;
            data.value = value;  // 使用已验证的value
            data.labels = labels;
            data.timestamp = std::chrono::system_clock::now();
            
            std::unique_lock<std::shared_mutex> history_lock(history_mutex_);
            metric_history_.push(data);
        }

        void PerformanceMonitor::observeHistogram(const std::string& name, double value,
                                                const std::unordered_map<std::string, std::string>& labels) {
            if (!config_.enable_monitoring) return;
            
            std::string key = generateMetricKey(name, labels);
            
            std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
            
            // 初始化直方图（如果不存在）
            if (histograms_.find(key) == histograms_.end()) {
                HistogramStats& hist = histograms_[key];
                // 默认桶边界（毫秒）
                hist.buckets = {1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
                hist.counts.resize(hist.buckets.size(), 0);
            }
            
            histograms_[key].observe(value);
            
            // 记录历史数据
            MetricData data;
            data.name = name;
            data.type = MetricType::HISTOGRAM;
            data.value = value;
            data.labels = labels;
            data.timestamp = std::chrono::system_clock::now();
            
            std::unique_lock<std::shared_mutex> history_lock(history_mutex_);
            metric_history_.push(data);
        }

        void PerformanceMonitor::recordTimer(const std::string& name,
                                           const std::chrono::high_resolution_clock::time_point& start_time,
                                           const std::unordered_map<std::string, std::string>& labels) {
            try {
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                
                // 🔧 安全处理duration，防止NaN/Inf导致JSON序列化错误
                auto duration_value = static_cast<double>(duration.count());
                if (std::isnan(duration_value) || std::isinf(duration_value) || duration_value < 0) {
                    LOG_WARNING("⚠️ Invalid timer duration: " + std::to_string(duration_value) + "ms for " + name + ", using 0");
                    duration_value = 0.0;
                }
                
                observeHistogram(name + "_duration_ms", duration_value, labels);
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to record timer metrics for " + name + ": " + std::string(e.what()));
            }
        }

        void PerformanceMonitor::recordHttpRequest(const std::string& method, const std::string& path,
                                                 int status_code, std::chrono::milliseconds duration) {
            try {
                std::unordered_map<std::string, std::string> labels = {
                    {"method", method},
                    {"path", path},
                    {"status", std::to_string(status_code)}
                };
                
                // 🔧 安全处理duration，防止NaN/Inf导致JSON序列化错误
                auto duration_value = static_cast<double>(duration.count());
                if (std::isnan(duration_value) || std::isinf(duration_value) || duration_value < 0) {
                    LOG_WARNING("⚠️ Invalid HTTP request duration: " + std::to_string(duration_value) + "ms for " + method + " " + path + ", using 0");
                    duration_value = 0.0;
                }
                
                incrementCounter("http_requests_total", 1.0, labels);
                observeHistogram("http_request_duration_ms", duration_value, labels);
                
                if (status_code >= 400) {
                    incrementCounter("http_errors_total", 1.0, labels);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to record HTTP request metrics for " + method + " " + path + ": " + std::string(e.what()));
            }
        }

        void PerformanceMonitor::recordDatabaseQuery(const std::string& operation, const std::string& table,
                                                   std::chrono::milliseconds duration, bool success) {
            try {
                std::unordered_map<std::string, std::string> labels = {
                    {"operation", operation},
                    {"table", table},
                    {"success", success ? "true" : "false"}
                };
                
                // 🔧 安全处理duration，防止NaN/Inf导致JSON序列化错误
                auto duration_value = static_cast<double>(duration.count());
                if (std::isnan(duration_value) || std::isinf(duration_value) || duration_value < 0) {
                    LOG_WARNING("⚠️ Invalid database query duration: " + std::to_string(duration_value) + "ms for " + operation + " on " + table + ", using 0");
                    duration_value = 0.0;
                }
                
                incrementCounter("db_queries_total", 1.0, labels);
                observeHistogram("db_query_duration_ms", duration_value, labels);
                
                if (!success) {
                    incrementCounter("db_errors_total", 1.0, labels);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to record database query metrics for " + operation + " on " + table + ": " + std::string(e.what()));
            }
        }

        void PerformanceMonitor::recordWebSocketEvent(const std::string& event, int current_connections) {
            std::unordered_map<std::string, std::string> labels = {
                {"event", event}
            };
            
            incrementCounter("websocket_events_total", 1.0, labels);
            setGauge("websocket_connections_current", static_cast<double>(current_connections));
        }

        void PerformanceMonitor::recordError(const std::string& error_type, const std::string& error_code,
                                           const std::string& service) {
            std::unordered_map<std::string, std::string> labels = {
                {"error_type", error_type},
                {"error_code", error_code}
            };
            
            if (!service.empty()) {
                labels["service"] = service;
            }
            
            incrementCounter("errors_total", 1.0, labels);
        }

        nlohmann::json PerformanceMonitor::getAllMetrics() const {
            nlohmann::json result;

            std::shared_lock<std::shared_mutex> lock(metrics_mutex_);

            // 🔧 安全的计数器序列化
            nlohmann::json counters = nlohmann::json::object();
            for (const auto& [key, value] : counters_) {
                double val = value.load();
                // 验证数值有效性
                if (std::isnan(val) || std::isinf(val)) {
                    val = 0.0;  // 使用安全默认值
                }
                counters[key] = val;
            }
            result["counters"] = counters;

            // 🔧 安全的仪表序列化
            nlohmann::json gauges = nlohmann::json::object();
            for (const auto& [key, value] : gauges_) {
                double val = value.load();
                // 验证数值有效性
                if (std::isnan(val) || std::isinf(val)) {
                    val = 0.0;  // 使用安全默认值
                }
                gauges[key] = val;
            }
            result["gauges"] = gauges;

            // 🔧 安全的直方图序列化
            nlohmann::json histograms = nlohmann::json::object();
            for (const auto& [key, hist] : histograms_) {
                try {
                    histograms[key] = hist.toJson();
                } catch (const std::exception& e) {
                    // 如果直方图序列化失败，记录错误并跳过
                    LOG_WARNING("Failed to serialize histogram '" + key + "': " + e.what());
                    histograms[key] = nlohmann::json::object();
                }
            }
            result["histograms"] = histograms;

            result["timestamp"] = getCurrentTimestamp();

            return result;
        }

        nlohmann::json PerformanceMonitor::getMetric(const std::string& name) const {
            nlohmann::json result;

            std::shared_lock<std::shared_mutex> lock(metrics_mutex_);

            // 查找计数器
            for (const auto& [key, value] : counters_) {
                if (key.find(name) == 0) {
                    result["counters"][key] = value.load();
                }
            }

            // 查找仪表
            for (const auto& [key, value] : gauges_) {
                if (key.find(name) == 0) {
                    result["gauges"][key] = value.load();
                }
            }

            // 查找直方图
            for (const auto& [key, hist] : histograms_) {
                if (key.find(name) == 0) {
                    result["histograms"][key] = hist.toJson();
                }
            }

            return result;
        }

        nlohmann::json PerformanceMonitor::getSystemStats() const {
            nlohmann::json stats;

            stats["cpu_usage_percent"] = getSystemCpuUsage();
            stats["memory_usage_percent"] = getSystemMemoryUsage();
            stats["timestamp"] = getCurrentTimestamp();

            return stats;
        }

        nlohmann::json PerformanceMonitor::getPerformanceReport(int duration_hours) const {
            nlohmann::json report;

            auto now = std::chrono::system_clock::now();
            auto cutoff_time = now - std::chrono::hours(duration_hours);

            std::shared_lock<std::shared_mutex> history_lock(history_mutex_);

            // 统计各类指标
            int total_requests = 0;
            int error_count = 0;
            double total_response_time = 0.0;
            int response_time_count = 0;

            auto temp_history = metric_history_;
            while (!temp_history.empty()) {
                const auto& metric = temp_history.front();
                temp_history.pop();

                if (metric.timestamp < cutoff_time) {
                    continue;
                }

                if (metric.name == "http_requests_total") {
                    total_requests += static_cast<int>(metric.value);
                } else if (metric.name == "http_errors_total") {
                    error_count += static_cast<int>(metric.value);
                } else if (metric.name == "http_request_duration_ms") {
                    total_response_time += metric.value;
                    response_time_count++;
                }
            }

            report["duration_hours"] = duration_hours;
            report["total_requests"] = total_requests;
            report["error_count"] = error_count;
            report["error_rate_percent"] = total_requests > 0 ?
                (static_cast<double>(error_count) / total_requests * 100.0) : 0.0;
            report["average_response_time_ms"] = response_time_count > 0 ?
                (total_response_time / response_time_count) : 0.0;
            report["requests_per_hour"] = static_cast<double>(total_requests) / duration_hours;
            report["timestamp"] = getCurrentTimestamp();

            return report;
        }

        std::string PerformanceMonitor::exportPrometheusMetrics() const {
            std::ostringstream oss;

            std::shared_lock<std::shared_mutex> lock(metrics_mutex_);

            // 导出计数器
            for (const auto& [key, value] : counters_) {
                oss << "# TYPE " << key << " counter\n";
                oss << key << " " << value.load() << "\n";
            }

            // 导出仪表
            for (const auto& [key, value] : gauges_) {
                oss << "# TYPE " << key << " gauge\n";
                oss << key << " " << value.load() << "\n";
            }

            // 导出直方图
            for (const auto& [key, hist] : histograms_) {
                oss << "# TYPE " << key << " histogram\n";
                for (size_t i = 0; i < hist.buckets.size(); ++i) {
                    oss << key << "_bucket{le=\"" << hist.buckets[i] << "\"} " << hist.counts[i] << "\n";
                }
                oss << key << "_sum " << hist.sum << "\n";
                oss << key << "_count " << hist.total_count << "\n";
            }

            return oss.str();
        }

        void PerformanceMonitor::startCollectionTask() {
            if (!thread_pool_) {
                LOG_WARNING("No thread pool available for collection task");
                return;
            }

            thread_pool_->submit([this]() {
                LOG_INFO("🚀 PerformanceMonitor collection task started (interval: " + std::to_string(config_.collection_interval_seconds) + "s)");
                
                while (running_.load() && !stopping_.load()) {
                    try {
                        if (config_.enable_system_metrics) {
                            try {
                                LOG_INFO("这里提交的任务");
                                collectSystemMetrics();
                            } catch (const nlohmann::json::exception& json_e) {
                                LOG_ERROR("❌ JSON error in system metrics collection: " + std::string(json_e.what()));
                            } catch (const std::exception& e) {
                                LOG_ERROR("❌ Error collecting system metrics: " + std::string(e.what()));
                            }
                        }

                        if (config_.enable_application_metrics) {
                            try {
                                collectApplicationMetrics();
                            } catch (const nlohmann::json::exception& json_e) {
                                LOG_ERROR("❌ JSON error in application metrics collection: " + std::string(json_e.what()));
                            } catch (const std::exception& e) {
                                LOG_ERROR("❌ Error collecting application metrics: " + std::string(e.what()));
                            }
                        }

                        try {
                            cleanupExpiredData();
                        } catch (const std::exception& e) {
                            LOG_ERROR("❌ Error in cleanup expired data: " + std::string(e.what()));
                        }

                    } catch (const nlohmann::json::exception& json_e) {
                        LOG_ERROR("❌ JSON error in PerformanceMonitor collection task: " + std::string(json_e.what()));
                        LOG_ERROR("❌ This indicates JSON serialization with invalid double values");
                        std::this_thread::sleep_for(std::chrono::seconds(30));
                    } catch (const std::exception& e) {
                        LOG_ERROR("❌ Collection task error: " + std::string(e.what()));
                        std::this_thread::sleep_for(std::chrono::seconds(30));
                    } catch (...) {
                        LOG_ERROR("❌ Unknown error in PerformanceMonitor collection task");
                        std::this_thread::sleep_for(std::chrono::seconds(30));
                    }
                }
                
                LOG_INFO("🛑 PerformanceMonitor collection task stopped");
            });
        }

        void PerformanceMonitor::collectSystemMetrics() {
            try {
                // 🔧 安全收集CPU使用率
                auto cpu_usage = getSystemCpuUsage();
                if (std::isnan(cpu_usage) || std::isinf(cpu_usage) || cpu_usage < 0 || cpu_usage > 100) {
                    LOG_WARNING("⚠️ Invalid CPU usage: " + std::to_string(cpu_usage) + "%, using 0.0");
                    cpu_usage = 0.0;
                }
                setGauge("system_cpu_usage_percent", cpu_usage);
                
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to collect CPU usage: " + std::string(e.what()));
                setGauge("system_cpu_usage_percent", 0.0);
            }
            
            try {
                // 🔧 安全收集内存使用率
                auto memory_usage = getSystemMemoryUsage();
                if (std::isnan(memory_usage) || std::isinf(memory_usage) || memory_usage < 0 || memory_usage > 100) {
                    LOG_WARNING("⚠️ Invalid memory usage: " + std::to_string(memory_usage) + "%, using 0.0");
                    memory_usage = 0.0;
                }
                setGauge("system_memory_usage_percent", memory_usage);
                
            } catch (const std::exception& e) {
                LOG_ERROR("❌ Failed to collect memory usage: " + std::string(e.what()));
                setGauge("system_memory_usage_percent", 0.0);
            }
        }

        void PerformanceMonitor::collectApplicationMetrics() {
            // 这里可以添加应用特定的指标收集
            // 例如：线程池状态、连接池状态等
        }

        void PerformanceMonitor::cleanupExpiredData() {
            auto now = std::chrono::system_clock::now();
            auto cutoff_time = now - std::chrono::hours(config_.retention_hours);

            std::unique_lock<std::shared_mutex> history_lock(history_mutex_);

            while (!metric_history_.empty() && metric_history_.front().timestamp < cutoff_time) {
                metric_history_.pop();
            }
        }

        double PerformanceMonitor::getSystemCpuUsage() const {
#ifdef __linux__
            static long long last_total = 0, last_idle = 0;

            std::ifstream file("/proc/stat");
            if (!file.is_open()) {
                return 0.0;
            }

            std::string line;
            std::getline(file, line);

            std::istringstream iss(line);
            std::string cpu;
            long long user, nice, system, idle, iowait, irq, softirq, steal;

            iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

            long long total = user + nice + system + idle + iowait + irq + softirq + steal;
            long long total_diff = total - last_total;
            long long idle_diff = idle - last_idle;

            double cpu_usage = 0.0;
            if (total_diff > 0) {
                cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
            }

            last_total = total;
            last_idle = idle;

            // 🔧 验证结果是否为有效数值
            if (std::isnan(cpu_usage) || std::isinf(cpu_usage) || cpu_usage < 0.0 || cpu_usage > 100.0) {
                return 0.0;  // 返回安全的默认值
            }

            return cpu_usage;
#else
            return 0.0; // 非Linux系统暂不支持
#endif
        }

        double PerformanceMonitor::getSystemMemoryUsage() const {
#ifdef __linux__
            struct sysinfo info;
            if (sysinfo(&info) == 0) {
                double total_mem = static_cast<double>(info.totalram * info.mem_unit);
                double free_mem = static_cast<double>(info.freeram * info.mem_unit);
                
                // 🔧 防止除零和无效计算
                if (total_mem <= 0.0) {
                    return 0.0;
                }
                
                double memory_usage = ((total_mem - free_mem) / total_mem) * 100.0;
                
                // 🔧 验证结果是否为有效数值
                if (std::isnan(memory_usage) || std::isinf(memory_usage) || memory_usage < 0.0 || memory_usage > 100.0) {
                    return 0.0;  // 返回安全的默认值
                }
                
                return memory_usage;
            }
#endif
            return 0.0;
        }

        std::string PerformanceMonitor::getCurrentTimestamp() const {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
            return std::to_string(timestamp.count());
        }

        std::string PerformanceMonitor::generateMetricKey(const std::string& name,
                                                        const std::unordered_map<std::string, std::string>& labels) const {
            std::string key = name;
            for (const auto& [label_key, label_value] : labels) {
                key += "{" + label_key + "=" + label_value + "}";
            }
            return key;
        }

        // 单例实现
        std::unique_ptr<PerformanceMonitor> PerformanceMonitorSingleton::instance_;
        std::once_flag PerformanceMonitorSingleton::initialized_;

        PerformanceMonitor& PerformanceMonitorSingleton::getInstance() {
            std::call_once(initialized_, []() {
                try {
                    // 🔧 修复JSON类型错误：安全的配置加载
                    auto config = PerformanceMonitor::Config::fromConfigManager();
                    instance_ = std::make_unique<PerformanceMonitor>(config);
                    instance_->start();
                    LOG_INFO("PerformanceMonitorSingleton单例初始化成功");
                } catch (const std::exception& e) {
                    LOG_ERROR("PerformanceMonitorSingleton初始化失败: " + std::string(e.what()) + ", 使用默认配置");
                    
                    // 🔧 失败时创建一个禁用监控的默认实例
                    PerformanceMonitor::Config default_config;
                    default_config.enable_monitoring = false;  // 禁用监控避免进一步错误
                    default_config.collection_interval_seconds = 60;
                    default_config.retention_hours = 24;
                    default_config.enable_system_metrics = false;
                    default_config.enable_application_metrics = false;
                    default_config.enable_export = false;
                    default_config.export_endpoint = "/metrics";
                    default_config.export_format = "json";
                    
                    instance_ = std::make_unique<PerformanceMonitor>(default_config);
                    // 不启动，因为监控已禁用
                }
            });
            return *instance_;
        }

        void PerformanceMonitorSingleton::initialize(const PerformanceMonitor::Config& config,
                                                    std::shared_ptr<common::thread_pool::ThreadPool> thread_pool) {
            std::call_once(initialized_, [&config, &thread_pool]() {
                instance_ = std::make_unique<PerformanceMonitor>(config, thread_pool);
                instance_->start();
            });
        }

    } // namespace monitoring
} // namespace common
