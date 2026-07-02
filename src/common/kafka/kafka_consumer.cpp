//
// Created by 29108 on 2025/7/3.
//
#include "common/kafka/kafka_consumer.h"
#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"
#include <librdkafka/rdkafkacpp.h>
#include <utility>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

using namespace common::logger;

namespace common
{
    namespace messaging {

        class ConsumerErrorCb : public RdKafka::EventCb {
        public:
            explicit ConsumerErrorCb(ConsumerErrorCallback callback) : callback_(std::move(callback)) {}

            void event_cb(RdKafka::Event& event) override {
                switch (event.type()) {
                case RdKafka::Event::EVENT_ERROR:
                    LOG_ERROR("Kafka错误: " + event.str());
                    if (callback_) {
                        callback_(event.str());
                    }
                    break;
                case RdKafka::Event::EVENT_STATS:
                    LOG_INFO("Kafka统计: " + event.str());
                    break;
                case RdKafka::Event::EVENT_LOG:
                    LOG_INFO("Kafka日志: " + event.str());
                    break;
                case RdKafka::Event::EVENT_THROTTLE:
                    LOG_WARNING("Kafka限流: " + event.str());
                    break;
                default:
                    LOG_INFO("Kafka事件: " + event.str());
                    break;
                }
            }

        private:
            ConsumerErrorCallback callback_;
        };

        std::shared_ptr<common::messaging::KafkaConsumer> common::messaging::KafkaConsumer::create(
                const KafkaConsumerConfig &config) {
            auto consumer = std::shared_ptr<KafkaConsumer>(new KafkaConsumer(config));
            if (!consumer->init(config)) {
                return nullptr;
            }
            return consumer;
        }

        std::shared_ptr<KafkaConsumer> KafkaConsumer::create(const common::config::kafkaConsumerConfig& config) {
            auto consumer = std::shared_ptr<KafkaConsumer>(new KafkaConsumer(config));
            if (!consumer->init(config)) {
                return nullptr;
            }
            return consumer;
        }

        std::shared_ptr<KafkaConsumer> KafkaConsumer::createFromConfig() {
            auto config = KafkaConsumerConfig::fromConfigManager();
            config.validate();

            LOG_INFO("Creating Kafka consumer with ConfigManager settings");
            LOG_WARNING("Kafka consumer brokers: " + config.brokers);
            LOG_WARNING("Kafka consumer group_id: " + config.groupId);

            return create(config);
        }

        KafkaConsumer::~KafkaConsumer() {
            // 先关闭线程池，再关闭消费者
            shutdownThreadPool();

            stop();
            if (consumer_) {
                consumer_->close();
                consumer_.reset();
            }
        }

        bool KafkaConsumer::subscribe(const std::vector<std::string> &topics) {
            if (!consumer_) {
                LOG_ERROR("Kafka消费者未初始化");
                return false;
            }

            RdKafka::ErrorCode err = consumer_->subscribe(topics);
            if (err != RdKafka::ERR_NO_ERROR) {
                LOG_ERROR("订阅主题失败: " + RdKafka::err2str(err));
                return false;
            }

            // 更新订阅主题列表
            {
                std::lock_guard<std::mutex> lock(consumer_mutex_);
                subscribed_topics_ = topics;
            }

            LOG_INFO("已订阅主题: " + std::to_string(topics.size()) + "个");
            for (const auto& topic : topics) {
                LOG_INFO("  - " + topic);
            }
            return true;
        }

        void KafkaConsumer::unsubscribe() {
            if (consumer_) {
                consumer_->unsubscribe();
                LOG_INFO("已取消订阅所有主题");
            }
            
            // 清空订阅主题列表
            {
                std::lock_guard<std::mutex> lock(consumer_mutex_);
                subscribed_topics_.clear();
            }
        }

        bool KafkaConsumer::start(MessageCallback messageCallback, ConsumerErrorCallback errorCallback) {
            if (!consumer_) {
                LOG_ERROR("Kafka消费者未初始化");
                return false;
            }

            if (isRunning_) {
                LOG_WARNING("Kafka消费者已经在运行");
                return true;
            }
            
            // 检查是否已订阅主题
            if (subscribed_topics_.empty()) {
                LOG_WARNING("Kafka消费者未订阅任何主题，无法开始消费");
                return false;
            }

            messageCallback_ = std::move(messageCallback);
            errorCallback_ = std::move(errorCallback);

            if (errorCallback_) {
                std::string errstr;
                errorCb_ = std::make_unique<ConsumerErrorCb>(errorCallback_);
                if (conf_->set("event_cb", errorCb_.get(), errstr) != RdKafka::Conf::CONF_OK) {
                    LOG_ERROR("设置事件回调失败: " + errstr);
                    return false;
                }
            }

            isRunning_ = true;
            consumeThread_ = std::thread(&KafkaConsumer::consumeThread, this);

            LOG_INFO("Kafka消费者已启动");
            return true;
        }

        void KafkaConsumer::stop() {
            if (!isRunning_.exchange(false)) {
                return;
            }

            if (consumeThread_.joinable()) {
                consumeThread_.join();
            }

            LOG_INFO("Kafka消费者已停止");
        }

        bool KafkaConsumer::commitSync() {
            if (!consumer_) {
                LOG_ERROR("Kafka消费者未初始化");
                return false;
            }

            RdKafka::ErrorCode err = consumer_->commitSync();
            if (err != RdKafka::ERR_NO_ERROR) {
                LOG_ERROR("提交偏移量失败: " + RdKafka::err2str(err));
                return false;
            }

            return true;
        }

        bool KafkaConsumer::seek(const std::string &topic, int partition, int64_t offset) {
            if (!consumer_) {
                LOG_ERROR("Kafka消费者未初始化");
                return false;
            }

            auto* tp = RdKafka::TopicPartition::create(topic, partition, offset);
            std::vector<RdKafka::TopicPartition*> partitions;
            partitions.push_back(tp);

            RdKafka::ErrorCode err = consumer_->assign(partitions);

            delete tp;

            if (err != RdKafka::ERR_NO_ERROR) {
                LOG_ERROR("设置偏移量失败: " + RdKafka::err2str(err));
                return false;
            }

            return true;
        }

        void KafkaConsumer::enableConfigHotReload() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            // 修复：使用异步方式处理配置变化，避免在ConfigManager回调中执行复杂操作
            config_manager.addChangeListener("kafka.consumer.brokers",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_WARNING("Kafka consumer brokers changed from " + old_val + " to " + new_val +
                               ". Consumer restart required for changes to take effect.");

                    // 异步处理重连，避免在ConfigManager回调中阻塞
                    if (main_thread_pool_) {
                        main_thread_pool_->submit([this]() {
                            this->markForReconnect();
                        });
                    } else {
                        std::thread([this]() {
                            this->markForReconnect();
                        }).detach();
                    }
                });

            // 监听消费者组ID变化
            config_manager.addChangeListener("kafka.consumer.group_id",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_WARNING("Kafka consumer group_id changed from " + old_val + " to " + new_val +
                               ". Consumer restart required for changes to take effect.");
                    this->markForReconnect();
                });

            // 监听会话超时变化
            config_manager.addChangeListener("kafka.consumer.session_timeout_ms",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka consumer session_timeout_ms changed from " + old_val + " to " + new_val);
                    try {
                        int new_timeout = std::stoi(new_val);
                        this->updateSessionTimeout(new_timeout);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to update session timeout: " + std::string(e.what()));
                    }
                });

            // 监听偏移量重置策略变化
            config_manager.addChangeListener("kafka.consumer.offset_reset",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka consumer offset_reset changed from " + old_val + " to " + new_val);
                    // 偏移量重置策略变化需要重新创建consumer
                    this->markForReconnect();
                });

            // 监听自动提交设置变化
            config_manager.addChangeListener("kafka.consumer.enable_auto_commit",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka consumer enable_auto_commit changed from " + old_val + " to " + new_val);
                    try {
                        bool enable_auto_commit = (new_val == "true");
                        this->updateAutoCommitSetting(enable_auto_commit);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to update auto commit setting: " + std::string(e.what()));
                    }
                });

            // ==================== 线程池配置监听 ====================

            // 监听异步操作启用状态变化
            config_manager.addChangeListener("kafka.consumer.thread_pool.enable_async_operations",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka consumer async operations changed from " + old_val + " to " + new_val);
                    bool enable_async = (new_val == "true" || new_val == "1");

                    if (enable_async && !thread_pool_initialized_.load()) {
                        // 启用异步操作，初始化线程池
                        try {
                            this->initializeThreadPool();

                        } catch (const std::exception& e) {
                            LOG_ERROR("启用Kafka消费者线程池失败: " + std::string(e.what()));
                        }
                    } else if (!enable_async && thread_pool_initialized_.load()) {
                        // 禁用异步操作，关闭线程池
                        this->shutdownThreadPool();

                    }
                });

            // 监听线程池核心线程数变化
            config_manager.addChangeListener("kafka.consumer.thread_pool.core_size",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka consumer thread pool core size changed from " + old_val + " to " + new_val);
                    try {
                        int new_core_size = std::stoi(new_val);
                        if (thread_pool_initialized_.load() && main_thread_pool_) {
                            main_thread_pool_->adjustCorePoolSize(new_core_size);
                            config_.thread_pool_core_size = new_core_size;
                            LOG_INFO("Kafka消费者线程池核心大小已动态调整为: " + std::to_string(new_core_size));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("调整Kafka消费者线程池核心大小失败: " + std::string(e.what()));
                    }
                });

            // 监听线程池最大线程数变化
            config_manager.addChangeListener("kafka.consumer.thread_pool.max_size",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka consumer thread pool max size changed from " + old_val + " to " + new_val);
                    try {
                        int new_max_size = std::stoi(new_val);
                        if (thread_pool_initialized_.load() && main_thread_pool_) {
                            main_thread_pool_->adjustMaximumPoolSize(new_max_size);
                            config_.thread_pool_max_size = new_max_size;
                            LOG_INFO("Kafka消费者线程池最大大小已动态调整为: " + std::to_string(new_max_size));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("调整Kafka消费者线程池最大大小失败: " + std::string(e.what()));
                    }
                });


        }

        KafkaConsumer::KafkaConsumer(const KafkaConsumerConfig& config)
                :isRunning_(false), config_(config){
            // 验证配置
            config_.validate();

            // 初始化重连线程池，避免nullptr导致的问题
            try {
                // 初始化主线程池（如果启用异步操作）
                if (config_.enable_async_operations) {
                    initializeThreadPool();
                }
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to initialize reconnect thread pool: " + std::string(e.what()));
                // 线程池初始化失败不是致命错误，会回退到临时线程
            }
        }

        KafkaConsumer::KafkaConsumer(const common::config::kafkaConsumerConfig& config):isRunning_(false), managerConfig_(config){
            // 验证配置
            managerConfig_.validate();

            // 初始化重连线程池，避免nullptr导致的问题
            try {
                // 初始化主线程池（如果启用异步操作）
                if (managerConfig_.enable_async_operations) {
                    initializeThreadPool();
                }
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to initialize reconnect thread pool: " + std::string(e.what()));
                // 线程池初始化失败不是致命错误，会回退到临时线程
            }
        }

        bool KafkaConsumer::init(const KafkaConsumerConfig &config) {
            try {
                std::string errstr;
                conf_ = std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

                // 优化点1：使用配置Map简化设置逻辑
                std::map<std::string, std::string> kafkaConfigs = {
                    {"bootstrap.servers", config.brokers},
                    {"client.id", config.clientId},
                    {"group.id", config.groupId},
                    {"session.timeout.ms", std::to_string(config.sessionTimeoutMs)},
                    {"max.poll.interval.ms", std::to_string(config.maxPollIntervalMs)},
                    {"auto.offset.reset", config.offsetReset},
                    {"fetch.message.max.bytes", std::to_string(config.fetchMaxBytes)},
                    {"fetch.wait.max.ms", std::to_string(config.fetchMaxWaitMs)},
                    {"enable.auto.commit", "false"}
                };

                for (const auto& [key, value] : kafkaConfigs) {
                    if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
                        LOG_ERROR("设置Kafka配置 " + key + "=" + value + " 失败: " + errstr);
                        return false;
                    }
                }

                // 增加生产者重试逻辑
                for (int i = 0; i < 3; ++i) { // 最多重试3次
                    consumer_ = std::unique_ptr<RdKafka::KafkaConsumer>(
                    RdKafka::KafkaConsumer::create(conf_.get(), errstr)
                    );
                    if (consumer_) break;
                    std::this_thread::sleep_for(std::chrono::seconds(1)); // 延迟重试
                }

                if (!consumer_) {
                    LOG_ERROR("创建消费者失败: " + errstr);
                    return false;
                }

                LOG_INFO("Kafka消费者已创建，brokers: " + config.brokers + ", group: " + config.groupId);
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        bool KafkaConsumer::init(const common::config::kafkaConsumerConfig& config) {
            try {
                std::string errstr;
                conf_ = std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

                // 优化点1：使用配置Map简化设置逻辑
                std::map<std::string, std::string> kafkaConfigs = {
                    {"bootstrap.servers", config.brokers},
                    {"client.id", config.client_id},
                    {"group.id", config.group_id},
                    {"session.timeout.ms", std::to_string(config.session_timeout_ms)},
                    {"max.poll.interval.ms", std::to_string(config.max_poll_interval_ms)},
                    {"auto.offset.reset", config.auto_offset_reset},
                    {"fetch.message.max.bytes", std::to_string(config.max_partition_fetch_bytes)},
                    {"fetch.wait.max.ms", std::to_string(config.fetch_max_wait_ms)},
                    {"enable.auto.commit", "false"}
                };

                for (const auto& [key, value] : kafkaConfigs) {
                    if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
                        LOG_ERROR("设置Kafka配置 " + key + "=" + value + " 失败: " + errstr);
                        return false;
                    }
                }

                // 增加生产者重试逻辑
                for (int i = 0; i < 3; ++i) { // 最多重试3次
                    consumer_ = std::unique_ptr<RdKafka::KafkaConsumer>(
                    RdKafka::KafkaConsumer::create(conf_.get(), errstr)
                    );
                    if (consumer_) break;
                    std::this_thread::sleep_for(std::chrono::seconds(1)); // 延迟重试
                }

                if (!consumer_) {
                    LOG_ERROR("创建消费者失败: " + errstr);
                    return false;
                }

                LOG_INFO("Kafka消费者已创建，brokers: " + config.brokers + ", group: " + config.group_id);
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        void KafkaConsumer::handleMessage(const std::string& topic, const std::string& key, const std::string& value,
                                          int64_t offset, int partition)
        {
            try {
                if (messageCallback_) {
                    messageCallback_(topic, key, value, offset);
                    // 只有在启用自动提交时才自动提交
                    if (config_.enable_auto_commit) {
                        commitAsync();
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[KafkaConsumer] 消息处理异常: " + std::string(e.what()) +
                          ", topic=" + topic + ", offset=" + std::to_string(offset));
            } catch (...) {
                LOG_ERROR("[KafkaConsumer] 消息处理未知异常, topic=" + topic +
                          ", offset=" + std::to_string(offset));
            }
        }

        void KafkaConsumer::dispatchMessageToProcessing(std::unique_ptr<RdKafka::Message> msg) {
            // 提取必要信息，避免跨线程传递 RdKafka::Message
            std::string topic = msg->topic_name();
            std::string key = msg->key() ? *msg->key() : "";
            std::string value = msg->payload() ?
                std::string(static_cast<const char*>(msg->payload()), msg->len()) : "";
            int64_t offset = msg->offset();
            int partition = msg->partition();


            // 异步处理
            if (isAsyncOperationsEnabled() && main_thread_pool_ && thread_pool_initialized_.load()) {
                main_thread_pool_->submit([this, topic, key, value, offset, partition]() {
                    handleMessage(topic, key, value, offset, partition);
                });
            } else {
                // 降级：同步处理
                handleMessage(topic, key, value, offset, partition);
            }
        }

        void KafkaConsumer::consumeThread()
        {
            LOG_INFO("[KafkaConsumer] 消费线程启动");

            // 检查是否已订阅主题
            if (subscribed_topics_.empty()) {
                LOG_ERROR("[KafkaConsumer] 未订阅任何主题，无法开始消费");
                return;
            }

            LOG_INFO("[KafkaConsumer] 开始消费已订阅的主题: ");
            for (const auto& topic : subscribed_topics_) {
                LOG_INFO("  - " + topic);
            }

            while (isRunning_.load()) {
                // 拉取单条消息（也可改为 batch）
                std::unique_ptr<RdKafka::Message> msg(consumer_->consume(1000)); // 1000ms 超时

                if (!msg) {
                    continue; // 超时，继续循环
                }

                switch (msg->err()) {
                case RdKafka::ERR__PARTITION_EOF: {
                        LOG_DEBUG("[KafkaConsumer::consumeThread] ERR__PARTITION_EOF");

                        // 分区到末尾，无需处理
                        continue;
                }

                case RdKafka::ERR__TIMED_OUT: {
                        // 超时，无需处理
                        LOG_DEBUG("[KafkaConsumer::consumeThread] ERR__TIMED_OUT");

                        continue;
                }

                case RdKafka::ERR_NO_ERROR: {
                        // 成功拉取消息，异步处理
                        LOG_DEBUG("[KafkaConsumer::consumeThread] ERR_NO_ERROR");
                        dispatchMessageToProcessing(std::move(msg));
                        break;
                }

                default: {
                        // 真正的错误
                        std::string errStr = msg->errstr();
                        LOG_ERROR("[KafkaConsumer] 消费错误: " + errStr);
                        if (errorCallback_) {
                            errorCallback_(errStr);
                        }
                        // 根据错误类型决定是否继续
                        if (msg->err() == RdKafka::ERR_UNKNOWN_TOPIC_OR_PART ||
                            msg->err() == RdKafka::ERR_TOPIC_EXCEPTION) {
                            LOG_ERROR("[KafkaConsumer] 主题相关错误，停止消费");
                            isRunning_ = false;
                        }
                        break;
                }
                }
            }

            LOG_DEBUG("[KafkaConsumer] 消费线程退出");
        }

        void KafkaConsumer::markForReconnect() {
            reconnect_required_ = true;
            LOG_INFO("Kafka consumer marked for reconnection");

            // 立即触发重连检查（异步执行，避免阻塞配置更新）
            scheduleReconnectCheck();
        }

        void KafkaConsumer::updateSessionTimeout(int new_timeout_ms) {
            if (new_timeout_ms > 0) {
                // 直接更新Config中的值，不需要重复的成员变量
                config_.sessionTimeoutMs = new_timeout_ms;
                LOG_INFO("Session timeout updated to " + std::to_string(new_timeout_ms) + "ms");

                // 如果consumer正在运行，需要重新创建以应用新的超时设置
                if (isRunning()) {
                    LOG_INFO("Consumer is running, scheduling reconnect to apply new session timeout");
                    markForReconnect();
                }
            }
        }

        void KafkaConsumer::updateAutoCommitSetting(bool enable) {
            // 直接更新Config中的值，不需要重复的成员变量
            config_.enable_auto_commit = enable;
            LOG_INFO("Auto commit setting updated to " + std::string(enable ? "enabled" : "disabled"));

            // 自动提交设置变化需要重新创建consumer
            if (isRunning()) {
                LOG_INFO("Consumer is running, scheduling reconnect to apply new auto commit setting");
                markForReconnect();
            }
        }

        void KafkaConsumer::scheduleReconnectCheck() {
            // 使用线程池异步执行重连检查
            if (main_thread_pool_) {
                main_thread_pool_->submit([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂延迟，避免频繁重连
                    checkAndHandleReconnect();
                });
            } else {
                // 如果没有线程池，创建临时线程
                std::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    checkAndHandleReconnect();
                }).detach();
            }
        }

        void KafkaConsumer::checkAndHandleReconnect() {
            if (!reconnect_required_.load()) {
                return; // 不需要重连
            }

            LOG_INFO("Starting Kafka consumer reconnection process");

            try {
                // 1. 保存当前订阅的主题
                auto subscribed_topics = getCurrentSubscriptions();

                // 2. 优雅关闭当前consumer
                gracefulShutdown();

                // 3. 重新创建consumer（使用更新后的配置）
                if (!recreateConsumer()) {
                    LOG_ERROR("Failed to recreate Kafka consumer");
                    return;
                }

                // 4. 重新订阅主题
                if (!subscribed_topics.empty()) {
                    resubscribeTopics(subscribed_topics);
                }

                // 5. 重置重连标志
                reconnect_required_ = false;

                LOG_INFO("Kafka consumer reconnection completed successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Kafka consumer reconnection failed: " + std::string(e.what()));

                // 修复：避免递归调用，使用异步重试机制
                if (reconnect_required_.load()) {
                    LOG_INFO("Scheduling Kafka consumer reconnection retry in 5 seconds");

                    // 使用异步方式重试，避免递归调用
                    if (main_thread_pool_) {
                        main_thread_pool_->submit([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(5));
                            if (reconnect_required_.load()) {
                                checkAndHandleReconnect();
                            }
                        });
                    } else {
                        std::thread([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(5));
                            if (reconnect_required_.load()) {
                                checkAndHandleReconnect();
                            }
                        }).detach();
                    }
                }
            }
        }

        std::vector<std::string> KafkaConsumer::getCurrentSubscriptions() {
            std::lock_guard<std::mutex> lock(consumer_mutex_);
            return subscribed_topics_; // 需要在类中维护订阅的主题列表
        }

        void KafkaConsumer::gracefulShutdown() {
            std::lock_guard<std::mutex> lock(consumer_mutex_);

            if (consumer_) {
                LOG_INFO("Gracefully shutting down Kafka consumer");

                try {
                    // 停止消费
                    isRunning_ = false;
                    
                    // 如果启用了手动提交，提交当前偏移量
                    if (!config_.enable_auto_commit) {
                        consumer_->commitSync();
                    }

                    // 关闭consumer
                    consumer_->close();

                } catch (const std::exception& e) {
                    LOG_WARNING("Error during consumer shutdown: " + std::string(e.what()));
                }

                consumer_.reset();
            }
        }

        bool KafkaConsumer::recreateConsumer() {
            // 修复：避免在持有锁的情况下调用可能长时间阻塞的init方法
            try {
                LOG_INFO("Recreating Kafka consumer with updated configuration");

                // 在不持有锁的情况下调用init，避免死锁
                if (!init(config_)) {
                    LOG_ERROR("Failed to create new Kafka consumer instance");
                    return false;
                }

                LOG_INFO("Kafka consumer recreated successfully");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception while recreating Kafka consumer: " + std::string(e.what()));
                return false;
            }
        }

        void KafkaConsumer::resubscribeTopics(const std::vector<std::string> &topics) {
            std::lock_guard<std::mutex> lock(consumer_mutex_);

            if (!consumer_ || topics.empty()) {
                return;
            }

            try {
                LOG_INFO("Resubscribing to " + std::to_string(topics.size()) + " topics");

                RdKafka::ErrorCode err = consumer_->subscribe(topics);
                if (err != RdKafka::ERR_NO_ERROR) {
                    LOG_ERROR("重新订阅主题失败: " + RdKafka::err2str(err));
                    throw std::runtime_error("Failed to resubscribe topics: " + RdKafka::err2str(err));
                }
                
                subscribed_topics_ = topics;

                LOG_INFO("Successfully resubscribed to topics");

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to resubscribe to topics: " + std::string(e.what()));
                throw;
            }
        }

        bool KafkaConsumer::isRunning() const {
            std::lock_guard<std::mutex> lock(consumer_mutex_);
            return isRunning_.load() && consumer_ != nullptr;
        }

        // ==================== 线程池集成实现 ====================

        void KafkaConsumer::initializeThreadPool() {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (thread_pool_initialized_.load()) {
                LOG_WARNING("Kafka消费者线程池已经初始化，跳过重复初始化");
                return;
            }

            if (!config_.enable_async_operations) {
                LOG_INFO("Kafka消费者异步操作未启用，跳过线程池初始化");
                return;
            }

            try {
                // 创建线程池配置
                common::config::ThreadPoolConfig thread_config = common::config::ThreadPoolConfig::fromConfigManager();
                // 验证配置
                thread_config.validate();

                // 创建主线程池
                main_thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(thread_config);

                thread_pool_initialized_.store(true);

                LOG_INFO("Kafka消费者线程池初始化成功 - 核心线程数: " +
                        std::to_string(config_.thread_pool_core_size) +
                        ", 最大线程数: " + std::to_string(config_.thread_pool_max_size));

            } catch (const std::exception& e) {
                LOG_ERROR("Kafka消费者线程池初始化失败: " + std::string(e.what()));
                thread_pool_initialized_.store(false);
                throw;
            }
        }

        void KafkaConsumer::shutdownThreadPool() {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (!thread_pool_initialized_.load()) {
                return;
            }

            LOG_INFO("开始关闭Kafka消费者线程池");

            try {
                // 关闭主线程池
                if (main_thread_pool_) {
                    main_thread_pool_->shutdown();
                    main_thread_pool_.reset();
                }

                thread_pool_initialized_.store(false);
                LOG_INFO("Kafka消费者线程池已关闭");

            } catch (const std::exception& e) {
                LOG_ERROR("关闭Kafka消费者线程池时发生异常: " + std::string(e.what()));
            }
        }

        std::string KafkaConsumer::getThreadPoolStatus() const {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                return "线程池未初始化或已关闭";
            }

            std::ostringstream status;
            status << "Kafka消费者线程池状态:\n";
            status << "  异步操作: " << (config_.enable_async_operations ? "启用" : "禁用") << "\n";
            status << "  主线程池:\n";
            status << "    活跃线程数: " << main_thread_pool_->getActiveThreadCount() << "\n";
            status << "    总线程数: " << main_thread_pool_->getTotalThreadCount() << "\n";
            status << "    队列大小: " << main_thread_pool_->getQueueSize() << "\n";
            status << "    核心线程数: " << config_.thread_pool_core_size << "\n";
            status << "    最大线程数: " << config_.thread_pool_max_size << "\n";

            return status.str();
        }



        void KafkaConsumer::commitAsync() {
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("线程池未初始化，跳过异步偏移量提交");
                return;
            }

            main_thread_pool_->submit([this]() {
                try {
                    LOG_DEBUG("开始异步偏移量提交");

                    std::lock_guard<std::mutex> lock(consumer_mutex_);
                    if (consumer_) {
                        RdKafka::ErrorCode err = consumer_->commitAsync();
                        if (err != RdKafka::ERR_NO_ERROR) {
                            LOG_WARNING("异步偏移量提交失败: " + RdKafka::err2str(err));
                        } else {
                            LOG_DEBUG("异步偏移量提交成功");
                        }
                    }

                } catch (const std::exception& e) {
                    LOG_ERROR("异步偏移量提交异常: " + std::string(e.what()));
                }
            });
        }

        void KafkaConsumer::performAsyncHealthCheck() {
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("线程池未初始化，跳过异步健康检查");
                return;
            }

            main_thread_pool_->submit([this]() {
                try {
                    LOG_DEBUG("开始异步健康检查");

                    bool is_healthy = true;

                    // 检查消费者状态
                    {
                        std::lock_guard<std::mutex> lock(consumer_mutex_);
                        if (!consumer_) {
                            is_healthy = false;
                            LOG_WARNING("健康检查：消费者未初始化");
                        }
                    }

                    // 检查订阅状态
                    if (subscribed_topics_.empty()) {
                        is_healthy = false;
                        LOG_WARNING("健康检查：未订阅任何主题");
                    }

                    // 检查运行状态
                    if (!isRunning()) {
                        is_healthy = false;
                        LOG_WARNING("健康检查：消费者未运行");
                    }

                    if (is_healthy) {
                        LOG_DEBUG("异步健康检查完成：消费者状态正常");
                    } else {
                        LOG_WARNING("异步健康检查完成：发现健康问题");
                    }

                } catch (const std::exception& e) {
                    LOG_ERROR("异步健康检查异常: " + std::string(e.what()));
                }
            });
        }
    }
}

