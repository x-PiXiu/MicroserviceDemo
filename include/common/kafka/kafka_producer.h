//
// Created by 29108 on 2025/7/3.
//

#ifndef KAFKA_PRODUCER_H
#define KAFKA_PRODUCER_H

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <future>
#include <map>
#include <librdkafka/rdkafkacpp.h>
#include "./common/config/config_manager.h"
#include "common/thread_pool/thread_pool.h"

// 前向声明，避免直接引入librdkafka的头文件
namespace RdKafka {
    class Producer;
    class Topic;
    class Message;
    class Conf;
}


namespace common {
    namespace messaging {
        class ErrorCb;
        class DeliveryReportCb;

        // 消息发送回调函数类型
        using DeliveryCallback = std::function<void(bool success, const std::string& error)>;
        // 错误回调函数类型
        using ProducerErrorCallback = std::function<void(const std::string& error, RdKafka::ErrorCode errCode)>;

        // Kafka生产者配置
        struct KafkaProducerConfig {
            std::string brokers;                  // Kafka broker地址列表，如 "localhost:9092"
            std::string clientId;                 // 客户端ID
            int timeoutMs;                        // 超时时间（毫秒）
            int queueBufferingMaxMs;              // 消息缓冲最大时间（毫秒）
            int queueBufferingMaxMessages;        // 缓冲区最大消息数
            int queueBufferingMaxKbytes;          // 缓冲区最大大小（KB）
            int requestRequiredAcks;              // 确认级别 (0, 1, -1)
            int retries = 3;
            int batch_size = 16384;               //批处理大小，单个批次的最大字节数
            std::string compression_type = "none";//消息压缩算法，减少网络传输和存储开销
            int delivery_timeout_ms = 120000;     //消息投递的总超时时间
            int polling_interval_ms = 100;        // 轮询间隔（毫秒）

            // ==================== 线程池集成配置 ====================
            bool enable_async_operations = true;                    ///< 是否启用异步操作
            int thread_pool_core_size = 4;                         ///< 线程池核心线程数
            int thread_pool_max_size = 16;                         ///< 线程池最大线程数
            int thread_pool_keep_alive_ms = 60000;                 ///< 线程保活时间(毫秒)
            int thread_pool_queue_capacity = 1000;                 ///< 任务队列容量
            bool thread_pool_enable_monitoring = true;             ///< 是否启用线程池监控
            std::string thread_pool_name_prefix = "kafka-producer-"; ///< 线程名前缀

            KafkaProducerConfig()
                : brokers("kafka:9092"), clientId("kafka-producer"),
                  timeoutMs(30000), queueBufferingMaxMs(5),  // 确保 message.timeout.ms > linger.ms
                  queueBufferingMaxMessages(10000), queueBufferingMaxKbytes(1024),
                  requestRequiredAcks(1) {}

            // ==================== 新增ConfigManager集成方法 ====================
            /**
             * @brief 从ConfigManager加载Kafka生产者配置
             */
            static KafkaProducerConfig fromConfigManager() {
                auto& config = common::config::ConfigManager::getInstance();
                KafkaProducerConfig result;

                // 基础连接配置
                result.brokers = config.get<std::string>("kafka.producer.brokers", "localhost:9092");
                result.clientId = config.get<std::string>("kafka.producer.client_id", "game-service-producer");

                // 可靠性配置
                result.requestRequiredAcks = config.get<int>("kafka.producer.acks", 1);
                result.retries = config.get<int>("kafka.producer.retries", 3);

                // 性能配置
                result.batch_size = config.get<int>("kafka.producer.batch_size", 16384);
                result.queueBufferingMaxMs = config.get<int>("kafka.producer.linger_ms", 5);
                result.queueBufferingMaxKbytes = config.get<int>("kafka.producer.buffer_memory", 33554432);
                result.compression_type = config.get<std::string>("kafka.producer.compression_type", "none");
                result.polling_interval_ms = config.get<int>("kafka.producer.polling_interval_ms", 100);

                // 超时配置
                result.timeoutMs = config.get<int>("kafka.producer.request_timeout_ms", 30000);
                result.delivery_timeout_ms = config.get<int>("kafka.producer.delivery_timeout_ms", 120000);

                // 线程池集成配置
                result.enable_async_operations = config.get<bool>("kafka.producer.thread_pool.enable_async_operations", true);
                result.thread_pool_core_size = config.get<int>("kafka.producer.thread_pool.core_size", 4);
                result.thread_pool_max_size = config.get<int>("kafka.producer.thread_pool.max_size", 16);
                result.thread_pool_keep_alive_ms = config.get<int>("kafka.producer.thread_pool.keep_alive_ms", 60000);
                result.thread_pool_queue_capacity = config.get<int>("kafka.producer.thread_pool.queue_capacity", 1000);
                result.thread_pool_enable_monitoring = config.get<bool>("kafka.producer.thread_pool.enable_monitoring", true);
                result.thread_pool_name_prefix = config.get<std::string>("kafka.producer.thread_pool.name_prefix", "kafka-producer-");

                return result;
            }

            /**
             * @brief 验证Kafka生产者配置
             */
            void validate() const {
                if (brokers.empty()) {
                    throw std::runtime_error("Kafka brokers cannot be empty");
                }
                if (clientId.empty()) {
                    throw std::runtime_error("Kafka client_id cannot be empty");
                }
                if (requestRequiredAcks < 0 || requestRequiredAcks > 1) {
                    throw std::runtime_error("Kafka acks must be 0 or 1");
                }
                if (retries < 0) {
                    throw std::runtime_error("Kafka retries must be >= 0");
                }
                if (batch_size <= 0) {
                    throw std::runtime_error("Kafka batch_size must be > 0");
                }
                if (queueBufferingMaxKbytes <= 0) {
                    throw std::runtime_error("Kafka buffer_memory must be > 0");
                }
                if (polling_interval_ms <= 0) {
                    throw std::runtime_error("Kafka polling_interval_ms must be > 0");
                }

                // 线程池配置验证
                if (enable_async_operations) {
                    if (thread_pool_core_size <= 0) {
                        throw std::runtime_error("Kafka producer thread_pool_core_size must be > 0");
                    }
                    if (thread_pool_max_size < thread_pool_core_size) {
                        throw std::runtime_error("Kafka producer thread_pool_max_size must be >= thread_pool_core_size");
                    }
                    if (thread_pool_queue_capacity <= 0) {
                        throw std::runtime_error("Kafka producer thread_pool_queue_capacity must be > 0");
                    }
                    if (thread_pool_keep_alive_ms < 0) {
                        throw std::runtime_error("Kafka producer thread_pool_keep_alive_ms must be >= 0");
                    }
                }
            }
        };

        // Kafka生产者类
        class KafkaProducer {
        public:
            // 创建Kafka生产者
            static std::shared_ptr<KafkaProducer> create(const KafkaProducerConfig& config);
            static std::shared_ptr<KafkaProducer> create(const common::config::kafkaProducerConfig& config);
            // 添加使用ConfigManager的create方法重载
            static std::shared_ptr<KafkaProducer> createFromConfig();

            /**
             * @brief 统一关闭入口函数
             * @details 优雅地关闭Kafka生产者，包括停止轮询线程、关闭线程池、
             *          刷新未发送的消息并清理资源
             */
            void shutdown();

            ~KafkaProducer();

            // 发送消息到指定主题
            bool send(const std::string& topic, const std::string& key, const std::string& value,
                      DeliveryCallback callback = nullptr);

            // 发送消息并等待结果
            std::future<bool> sendAsync(const std::string& topic, const std::string& key, const std::string& value);

            // 刷新所有未发送的消息
            void flush(int timeoutMs = 5000);

            // 轮询事件
            void poll(int timeoutMs = 0);
            
            // 获取轮询线程状态
            bool isPollingThreadRunning() const { return polling_thread_running_.load(); }
            
            // 设置轮询间隔（毫秒）
            void setPollingInterval(int intervalMs) { 
                if (intervalMs > 0) {
                    polling_interval_ms_.store(intervalMs); 
                }
            }
            
            // 获取轮询间隔（毫秒）
            int getPollingInterval() const { return polling_interval_ms_.load(); }

            // 在KafkaProducer类中添加配置热更新支持
            void enableConfigHotReload();

            // ==================== 线程池集成方法 ====================

            /**
             * @brief 初始化线程池
             * @details 根据配置创建和初始化线程池，启用异步操作支持
             */
            void initializeThreadPool();

            /**
             * @brief 关闭线程池
             * @details 优雅关闭线程池，等待所有任务完成
             */
            void shutdownThreadPool();

            /**
             * @brief 获取线程池状态信息
             * @return 包含线程池详细状态的字符串
             */
            std::string getThreadPoolStatus() const;

            /**
             * @brief 批量异步发送消息
             * @param messages 消息列表，每个元素包含topic、key、value
             * @return 返回future，包含发送结果列表
             */
            std::future<std::vector<bool>> sendBatchAsync(
                const std::vector<std::tuple<std::string, std::string, std::string>>& messages);

            /**
            * @brief 异步刷新所有未发送的消息
            * @param timeoutMs 超时时间（毫秒）
            * @return std::future<void> 可用于等待刷新完成
            * @note 如果线程池未初始化，则立即返回无效 future（调用 wait() 将立即返回）
            */
            std::future<void> flushAsync(int timeoutMs = 5000);

            /**
             * @brief 异步轮询操作
             * @details 在线程池中异步执行poll操作
             */
            void pollAsync(int timeoutMs = 0);

            /**
             * @brief 检查是否启用了异步操作
             * @return true如果启用了异步操作
             */
            bool isAsyncOperationsEnabled() const { return config_.enable_async_operations; }

            /**
            * @brief 停止轮询线程
            * @details 停止并等待轮询线程结束
            */
            void stopPollingThread();

        private:
            KafkaProducer(const KafkaProducerConfig& config);
            KafkaProducer(const common::config::kafkaProducerConfig& config);

            // 初始化生产者
            bool init(const KafkaProducerConfig& config);
            bool init(const common::config::kafkaProducerConfig& config);

            // 获取或创建主题
            RdKafka::Topic* getTopic(const std::string& topic);

            std::unique_ptr<RdKafka::Producer> producer_;
            std::unique_ptr<RdKafka::Conf> conf_;
            std::unique_ptr<RdKafka::Conf> defaultTopicConf_; // 类成员存储默认配置
            std::shared_ptr<ErrorCb> eventCb_; // 保证回调对象持续有效
            std::mutex topicsMutex_;
            std::map<std::string, std::unique_ptr<RdKafka::Topic>> topics_;
            bool isRunning_;
            KafkaProducerConfig config_;
            common::config::kafkaProducerConfig managerConfig_;

            std::atomic<bool> reconnect_required_{false};           ///< 是否需要重新连接
            std::atomic<bool> accepting_new_messages_{true};        ///< 是否接受新消息
            mutable std::mutex producer_mutex_;                      ///< 保护producer操作的互斥锁
            std::set<std::string> pending_messages_;                ///< 待发送消息集合（用于跟踪）

            // ==================== 轮询线程集成私有成员 ====================
            std::atomic<bool> polling_thread_running_{false};       ///< 轮询线程是否正在运行
            std::unique_ptr<std::thread> polling_thread_;           ///< 轮询线程
            std::atomic<int> polling_interval_ms_{100};             ///< 轮询间隔（毫秒）

            // ==================== 线程池集成私有成员 ====================
            std::shared_ptr<common::thread_pool::ThreadPool> main_thread_pool_;    ///< 主线程池，用于异步操作
            std::atomic<bool> thread_pool_initialized_{false};                     ///< 线程池是否已初始化
            mutable std::mutex thread_pool_mutex_;                                 ///< 线程池操作互斥锁

            void markForReconnect() ;

            /**
            * @brief 调度生产者重连检查任务
            * @details 异步执行重连检查，避免阻塞配置更新线程
            */
            void scheduleProducerReconnectCheck();

            /**
             * @brief 检查并处理生产者重连需求
             * @details 实际执行重连逻辑的核心方法
             */
            void checkAndHandleProducerReconnect();

            /**
             * @brief 设置是否接受新消息
             * @param accepting 是否接受新消息
             */
            void setAcceptingNewMessages(bool accepting);

            /**
             * @brief 等待待发送消息完成
             * @details 等待所有已提交的消息发送完成
             */
            void waitForPendingMessagesToComplete();

            /**
             * @brief 刷新并关闭当前producer
             * @details 确保所有消息都已发送，然后关闭producer
             */
            void flushAndCloseCurrentProducer() ;

            /**
             * @brief 重新加载生产者配置
             * @details 从ConfigManager重新加载最新配置
             */
            void reloadProducerConfiguration();

            /**
             * @brief 重新创建producer实例
             * @return 是否创建成功
             */
            bool recreateProducerInstance();

            /**
             * @brief 检查producer是否正在运行
             * @return 是否正在运行
             */
            bool isProducerRunning() const ;

            /**
             * @brief 启动轮询线程
             * @details 创建并启动专门用于轮询事件的线程
             */
            void startPollingThread();



            /**
             * @brief 轮询线程主函数
             * @details 轮询线程执行的主循环
             */
            void pollingThreadMain();
        };

    }
}

#endif //KAFKA_PRODUCER_H
