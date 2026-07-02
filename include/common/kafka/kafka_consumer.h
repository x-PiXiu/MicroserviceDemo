//
// Created by 29108 on 2025/7/3.
//

#ifndef KAFKA_CONSUMER_H
#define KAFKA_CONSUMER_H

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <bits/stdint-intn.h>
#include "./common/config/config_manager.h"
#include "common/thread_pool/thread_pool.h"


namespace RdKafka {
    class KafkaConsumer;
    class Topic;
    class Message;
    class Conf;
    class TopicPartition;
    class EventCb;
}

namespace common {
    namespace messaging {
        // 消息处理回调函数类型
        using MessageCallback = std::function<void(const std::string& topic, const std::string& key,
                                                  const std::string& value, int64_t offset)>;

        // 错误回调函数类型
        using ConsumerErrorCallback = std::function<void(const std::string& error)>;

        // Kafka消费者配置
        struct KafkaConsumerConfig {
            std::string brokers;                  // Kafka broker地址列表，如 "localhost:9092"
            std::string groupId;                  // 消费者组ID
            std::string clientId;                 // 客户端ID
            int sessionTimeoutMs;                 // 会话超时时间（毫秒）
            int maxPollIntervalMs;                // 最大轮询间隔（毫秒）
            std::string offsetReset;              // 偏移量重置策略（earliest, latest）
            int fetchMaxBytes;                    // 最大拉取字节数
            int fetchMaxWaitMs;                   // 拉取等待时间（毫秒）
            bool enable_auto_commit = false;
            int auto_commit_interval_ms = 5000;

            // ==================== 线程池集成配置 ====================
            bool enable_async_operations = true;                    ///< 是否启用异步操作
            int thread_pool_core_size = 4;                         ///< 线程池核心线程数
            int thread_pool_max_size = 16;                         ///< 线程池最大线程数
            int thread_pool_keep_alive_ms = 60000;                 ///< 线程保活时间(毫秒)
            int thread_pool_queue_capacity = 1000;                 ///< 任务队列容量
            bool thread_pool_enable_monitoring = true;             ///< 是否启用线程池监控
            std::string thread_pool_name_prefix = "kafka-consumer-"; ///< 线程名前缀

            KafkaConsumerConfig()
                : brokers("kafka:9092"), groupId("default-group"), clientId("kafka-consumer"),
                  sessionTimeoutMs(10000), maxPollIntervalMs(300000), offsetReset("earliest"),
                  fetchMaxBytes(52428800), fetchMaxWaitMs(500) {}

            /**
     * @brief 从ConfigManager加载Kafka消费者配置
     */
            static KafkaConsumerConfig fromConfigManager() {
                auto& config = common::config::ConfigManager::getInstance();
                KafkaConsumerConfig result;

                result.brokers = config.get<std::string>("kafka.consumer.brokers", "localhost:9092");
                result.groupId = config.get<std::string>("kafka.consumer.group_id", "game-service-group");
                result.clientId = config.get<std::string>("kafka.consumer.client_id", "game-service-consumer");
                result.sessionTimeoutMs = config.get<int>("kafka.consumer.session_timeout_ms", 30000);
                result.maxPollIntervalMs = config.get<int>("kafka.consumer.max_poll_interval_ms", 300000);
                result.offsetReset = config.get<std::string>("kafka.consumer.offset_reset", "earliest");
                result.enable_auto_commit= config.get<bool>("kafka.consumer.enable_auto_commit", false);
                result.auto_commit_interval_ms = config.get<int>("kafka.consumer.auto_commit_interval_ms", 5000);

                // 线程池集成配置
                result.enable_async_operations = config.get<bool>("kafka.consumer.thread_pool.enable_async_operations", true);
                result.thread_pool_core_size = config.get<int>("kafka.consumer.thread_pool.core_size", 4);
                result.thread_pool_max_size = config.get<int>("kafka.consumer.thread_pool.max_size", 16);
                result.thread_pool_keep_alive_ms = config.get<int>("kafka.consumer.thread_pool.keep_alive_ms", 60000);
                result.thread_pool_queue_capacity = config.get<int>("kafka.consumer.thread_pool.queue_capacity", 1000);
                result.thread_pool_enable_monitoring = config.get<bool>("kafka.consumer.thread_pool.enable_monitoring", true);
                result.thread_pool_name_prefix = config.get<std::string>("kafka.consumer.thread_pool.name_prefix", "kafka-consumer-");

                return result;
            }

            void validate() const {
                if (brokers.empty()) {
                    throw std::runtime_error("Kafka consumer brokers cannot be empty");
                }
                if (groupId.empty()) {
                    throw std::runtime_error("Kafka consumer group_id cannot be empty");
                }
                if (sessionTimeoutMs <= 0) {
                    throw std::runtime_error("Kafka session_timeout_ms must be > 0");
                }
                if (maxPollIntervalMs <= sessionTimeoutMs) {
                    throw std::runtime_error("Kafka max_poll_interval_ms must be > session_timeout_ms");
                }

                // 线程池配置验证
                if (enable_async_operations) {
                    if (thread_pool_core_size <= 0) {
                        throw std::runtime_error("Kafka consumer thread_pool_core_size must be > 0");
                    }
                    if (thread_pool_max_size < thread_pool_core_size) {
                        throw std::runtime_error("Kafka consumer thread_pool_max_size must be >= thread_pool_core_size");
                    }
                    if (thread_pool_queue_capacity <= 0) {
                        throw std::runtime_error("Kafka consumer thread_pool_queue_capacity must be > 0");
                    }
                    if (thread_pool_keep_alive_ms < 0) {
                        throw std::runtime_error("Kafka consumer thread_pool_keep_alive_ms must be >= 0");
                    }
                }
            }
        };

        // Kafka消费者类
        class KafkaConsumer {
        public:
            // 创建Kafka消费者
            static std::shared_ptr<KafkaConsumer> create(const KafkaConsumerConfig& config);
            static std::shared_ptr<KafkaConsumer> create(const common::config::kafkaConsumerConfig& config);
            static std::shared_ptr<KafkaConsumer> createFromConfig() ;

            ~KafkaConsumer();

            // 订阅主题
            bool subscribe(const std::vector<std::string>& topics);

            // 取消订阅
            void unsubscribe();

            // 开始消费消息
            bool start(MessageCallback messageCallback, ConsumerErrorCallback errorCallback = nullptr);

            // 停止消费
            void stop();

            // 手动提交偏移量
            bool commitSync();

            // 异步提交偏移量
            void commitAsync();

            // 手动指定偏移量
            bool seek(const std::string& topic, int partition, int64_t offset);

            /**
            * @brief 启用Kafka消费者配置热更新监听
            * @details 监听ConfigManager中Kafka消费者相关配置的变化
            */
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
             * @brief 异步执行健康检查
             * @details 在线程池中异步检查消费者健康状态
             */
            void performAsyncHealthCheck();

            /**
             * @brief 检查是否启用了异步操作
             * @return true如果启用了异步操作
             */
            bool isAsyncOperationsEnabled() const { return config_.enable_async_operations; }

        private:
            KafkaConsumer(const KafkaConsumerConfig& config);
            KafkaConsumer(const common::config::kafkaConsumerConfig& config);

            // 初始化消费者
            bool init(const KafkaConsumerConfig& config);
            bool init(const common::config::kafkaConsumerConfig& config);

            void handleMessage(const std::string& topic, const std::string& key, const std::string& value, int64_t offset, int partition);
            void dispatchMessageToProcessing(std::unique_ptr<RdKafka::Message> msg);
            // 消费线程函数
            void consumeThread();

        private:
            std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
            std::unique_ptr<RdKafka::Conf> conf_;
            std::unique_ptr<RdKafka::EventCb> errorCb_;
            std::thread consumeThread_;
            std::atomic<bool> isRunning_{false};

            MessageCallback messageCallback_;
            ConsumerErrorCallback errorCallback_;

            std::atomic<bool> reconnect_required_{false};           ///< 是否需要重新连接
            mutable std::mutex consumer_mutex_;                      ///< 保护consumer操作的互斥锁
            std::vector<std::string> subscribed_topics_;             ///< 当前订阅的主题列表
            KafkaConsumerConfig config_;
            common::config::kafkaConsumerConfig managerConfig_;

            // ==================== 线程池集成私有成员 ====================
            std::shared_ptr<common::thread_pool::ThreadPool> main_thread_pool_;    ///< 主线程池，用于异步操作
            std::atomic<bool> thread_pool_initialized_{false};                     ///< 线程池是否已初始化
            mutable std::mutex thread_pool_mutex_;                                 ///< 线程池操作互斥锁

            /**
 * @brief 标记消费者需要重新连接
 * @details 某些配置变化需要重新创建consumer实例
 */
            void markForReconnect() ;

            /**
             * @brief 更新会话超时设置
             * @param new_timeout_ms 新的会话超时时间（毫秒）
             * @details 动态调整会话超时，无需重启consumer
             */
            void updateSessionTimeout(int new_timeout_ms);

            /**
             * @brief 更新自动提交设置
             * @param enable 是否启用自动提交
             * @details 动态调整自动提交设置
             */
            void updateAutoCommitSetting(bool enable);

            /**
 * @brief 调度重连检查任务
 * @details 异步执行重连检查，避免阻塞配置更新线程
 */
            void scheduleReconnectCheck() ;

            /**
             * @brief 检查并处理重连需求
             * @details 实际执行重连逻辑的核心方法
             */
            void checkAndHandleReconnect() ;

            /**
             * @brief 获取当前订阅的主题列表
             * @return 当前订阅的主题列表
             */
            std::vector<std::string> getCurrentSubscriptions();

            /**
             * @brief 优雅关闭当前consumer
             * @details 停止消费，提交偏移量，关闭连接
             */
            void gracefulShutdown() ;

            /**
             * @brief 重新创建consumer实例
             * @return 是否创建成功
             */
            bool recreateConsumer() ;

            /**
             * @brief 重新订阅主题
             * @param topics 要订阅的主题列表
             */
            void resubscribeTopics(const std::vector<std::string>& topics);

            /**
             * @brief 检查consumer是否正在运行
             * @return 是否正在运行
             */
            bool isRunning() const;

        };
    }
}


#endif //KAFKA_CONSUMER_H
