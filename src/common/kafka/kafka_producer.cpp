//
// Created by 29108 on 2025/7/3.
//
// #include
#include "common/kafka/kafka_producer.h"
#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"
#include <memory>
#include <string>
#include <librdkafka/rdkafkacpp.h>
#include <map>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <sstream>
#include <algorithm>

using namespace common::logger;

namespace common {
    namespace messaging {
        class DeliveryReportCb : public RdKafka::DeliveryReportCb {
        public:
            explicit DeliveryReportCb(DeliveryCallback callback) : callback_(std::move(callback)) {}

            void dr_cb(RdKafka::Message& message) override {
                if (callback_) {
                    callback_(message.err() == RdKafka::ERR_NO_ERROR,
                             message.errstr());
                }
            }

        private:
            DeliveryCallback callback_;  // 函数对象，用于进一步处理结果
        };

        class ErrorCb : public RdKafka::EventCb {
        public:
            explicit ErrorCb(ProducerErrorCallback callback) : callback_(std::move(callback)) {}

            void event_cb(RdKafka::Event& event) override {
                const std::string eventMsg = event.str(); // 避免重复计算
                switch (event.type()) {
                    case RdKafka::Event::EVENT_ERROR: {
                        // 错误处理：提取错误码
                        const RdKafka::ErrorCode errCode = event.err();
                        LOG_ERROR("Kafka错误[" + std::to_string(static_cast<int>(errCode)) + "]: " + eventMsg);
                        if (callback_) {
                            callback_(eventMsg, errCode);
                        }
                        break;
                    }
                    case RdKafka::Event::EVENT_STATS:
                        LOG_INFO("Kafka统计: " + eventMsg);
                        break;
                    case RdKafka::Event::EVENT_LOG: {
                        // 日志分级处理
                        if (event.severity() <= RdKafka::Event::EVENT_SEVERITY_WARNING) {
                            LOG_WARNING("Kafka日志: " + eventMsg);
                        } else {
                            LOG_INFO("Kafka日志: " + eventMsg);
                        }
                        break;
                    }
                    case RdKafka::Event::EVENT_THROTTLE:
                        LOG_WARNING("Kafka限流: " + eventMsg);
                        break;
                    default:
                        LOG_INFO("Kafka事件: " + eventMsg);
                        break;
                }
            }

        private:
            ProducerErrorCallback callback_;  // 函数对象，用于处理错误
        };

        void handleKafkaError(const std::string& error, RdKafka::ErrorCode errCode) {
            // 重构1：基于错误码而非字符串匹配[5](@ref)
            switch (errCode) {
                case RdKafka::ERR__TRANSPORT:  // Broker连接错误
                    LOG_WARNING("Kafka网络波动: " + error);
                    break;
                case RdKafka::ERR__TIMED_OUT: // 消息超时
                    LOG_ERROR("Kafka消息超时: " + error);
                    // 优化：提供参数调整建议[1](@ref)
                    LOG_INFO("解决方案: 1. 增大queue.buffering.max.ms 2. 检查Broker负载");
                    break;
                case RdKafka::ERR__AUTHENTICATION: // 认证失败
                    LOG_FATAL("Kafka认证失败: " + error);
                    std::terminate();
                    break;
                case RdKafka::ERR__QUEUE_FULL: // 关键新增：队列满错误[7](@ref)
                    LOG_FATAL("Kafka队列已满: " + error);
                    break;
                default:
                    LOG_ERROR("Kafka系统错误[" + std::to_string(errCode) + "]: " + error);
            }
        }

        std::shared_ptr<KafkaProducer> KafkaProducer::create(const KafkaProducerConfig &config) {
            auto producer = std::shared_ptr<KafkaProducer>(new KafkaProducer(config));
            if (!producer->init(config)) {
                return nullptr;
            }
            return producer;
        }

        std::shared_ptr<KafkaProducer> KafkaProducer::create(const common::config::kafkaProducerConfig& config) {
            auto producer = std::shared_ptr<KafkaProducer>(new KafkaProducer(config));
            if (!producer->init(config)) {
                return nullptr;
            }
            return producer;
        }

        std::shared_ptr<KafkaProducer> KafkaProducer::createFromConfig() {
            auto config = KafkaProducerConfig::fromConfigManager();
            config.validate();

            LOG_INFO("Creating Kafka producer with ConfigManager settings");
            LOG_DEBUG("Kafka brokers: " + config.brokers);
            LOG_DEBUG("Kafka client_id: " + config.clientId);

            return create(config);
        }

        // 🔧 新增：统一关闭入口函数
        void KafkaProducer::shutdown() {
            LOG_INFO("开始关闭Kafka生产者...");

            try {
                // 1. 停止轮询线程
                stopPollingThread();

                // 2. 关闭线程池
                shutdownThreadPool();

                // 3. 刷新并关闭生产者
                if (producer_) {
                    LOG_INFO("刷新Kafka生产者中的未发送消息...");
                    flush();
                    
                    // 清理主题
                    {
                        std::lock_guard<std::mutex> lock(topicsMutex_);
                        topics_.clear();
                    }
                    
                    // 重置生产者
                    producer_.reset();
                    LOG_INFO("Kafka生产者已关闭");
                }

                // 4. 重置回调对象
                eventCb_.reset();
                
                // 5. 设置运行状态为false
                isRunning_ = false;

                LOG_INFO("Kafka生产者关闭完成");
            } catch (const std::exception& e) {
                LOG_ERROR("关闭Kafka生产者时发生异常: " + std::string(e.what()));
            }
        }

        KafkaProducer::~KafkaProducer() {
            // 使用统一的关闭入口函数
            shutdown();
        }

        bool KafkaProducer::send(const std::string &topic, const std::string &key, const std::string &value,
                                 DeliveryCallback callback) {
            // 🔧 修复：检查生产者是否已关闭
            if (!isRunning_ || !producer_) {
                LOG_ERROR("Kafka生产者未初始化或已停止");
                return false;
            }

            try {
                //获取或者创建主题
                auto topicObj = getTopic(topic);
                if (!topicObj) {
                    return false;
                }

                //设置delivery回调
                if (callback) {
                    std::string errstr;
                    auto deliveryCb = std::make_shared<DeliveryReportCb>(std::move(callback));
                    if (conf_->set("dr_cb", deliveryCb.get(), errstr) != RdKafka::Conf::CONF_OK) {
                        LOG_ERROR("设置dr_cb失败: " + errstr);
                        return false;
                    }
                }
                //发送消息
                void* valueDate = const_cast<char*>(value.c_str());
                size_t valueLen = value.size();
                void* keyDate = key.empty() ? nullptr : const_cast<char*>(key.c_str());
                size_t keyLen = key.empty() ? 0 : key.size();

                RdKafka::ErrorCode err = producer_->produce(
                    topicObj,                                  //主题对象
                    RdKafka::Topic::PARTITION_UA,              // 自动选择分区
                    RdKafka::Producer::RK_MSG_COPY,    //消息标志
                    valueDate, valueLen,    //消息值和长度
                    keyDate,keyLen,         //消息键和长度
                    nullptr);    //消息opaque

                if (err != RdKafka::ERR_NO_ERROR) {
                    LOG_ERROR("发送消息失败: " + RdKafka::err2str(err));
                    return false;
                }

                poll(0);
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("发送Kafka消息异常: " + std::string(e.what()));
                return false;
            }
        }

        std::future<bool> KafkaProducer::sendAsync(const std::string &topic, const std::string &key,
        const std::string &value) {
            // 🔧 修复：检查生产者是否已关闭
            if (!isRunning_ || !producer_) {
                // 返回一个立即完成的future，值为false
                auto promise = std::make_shared<std::promise<bool>>();
                promise->set_value(false);
                return promise->get_future();
            }

            auto promise = std::make_shared<std::promise<bool>>();
            auto future = promise->get_future();

            auto callback = [promise](bool success, const std::string& /* error */) {
                promise->set_value(success);
            };

            if (!send(topic, key, value, callback)) {
                promise->set_value(false);
            }

            return future;
        }

        void KafkaProducer::flush(int timeoutMs) {
            // 🔧 修复：检查生产者是否已关闭
            if (!producer_) {
                LOG_ERROR("Flush失败: 生产者未初始化或已关闭");
                return;
            }

            RdKafka::ErrorCode err = producer_->flush(timeoutMs);
            if (err != RdKafka::ERR_NO_ERROR) {
                std::string errorMsg = "Flush操作失败: " + RdKafka::err2str(err);
                LOG_ERROR(errorMsg);

                // 触发错误回调（若有）
                // if (eventCb_) eventCb_->event_cb(errorMsg, err);
            }
        }

        void KafkaProducer::poll(int timeoutMs) {
            // 🔧 修复：检查生产者是否已关闭
            if (!producer_) {
                LOG_ERROR("Poll失败: 生产者未初始化或已关闭");
                return;
            }

            int eventCount = producer_->poll(timeoutMs);
            if (eventCount < 0) {
                std::string errorMsg = "Poll操作异常: 事件处理失败";
                LOG_ERROR(errorMsg);
                // if (eventCb_) eventCb_->event_cb();
            }
        }

        void KafkaProducer::enableConfigHotReload() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            // 修复：使用异步方式处理配置变化，避免在ConfigManager回调中执行复杂操作
            config_manager.addChangeListener("kafka.producer.brokers",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_WARNING("Kafka brokers changed from " + old_val + " to " + new_val +
                               ". Producer restart required for changes to take effect.");

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

            // 监听压缩类型变化
            config_manager.addChangeListener("kafka.producer.compression_type",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Kafka compression type changed from " + old_val + " to " + new_val);

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

            // ==================== 轮询配置监听 ====================

            // 监听轮询间隔变化
            config_manager.addChangeListener("kafka.producer.polling_interval_ms",
                [this](const auto& /*key*/, const auto& old_val, const auto& new_val) {
                    LOG_INFO("Kafka producer polling interval changed from " + old_val + "ms to " + new_val + "ms");
                    try {
                        int new_interval = std::stoi(new_val);
                        if (new_interval > 0) {
                            polling_interval_ms_.store(new_interval);
                            LOG_INFO("Kafka producer polling interval updated to: " + std::to_string(new_interval) + "ms");
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to update Kafka producer polling interval: " + std::string(e.what()));
                    }
                });

            // ==================== 线程池配置监听 ====================

            // 监听异步操作启用状态变化
            config_manager.addChangeListener("kafka.producer.thread_pool.enable_async_operations",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka producer async operations changed from " + old_val + " to " + new_val);
                    bool enable_async = (new_val == "true" || new_val == "1");

                    if (enable_async && !thread_pool_initialized_.load()) {
                        // 启用异步操作，初始化线程池
                        try {
                            this->initializeThreadPool();

                        } catch (const std::exception& e) {
                            LOG_ERROR("启用Kafka生产者线程池失败: " + std::string(e.what()));
                        }
                    } else if (!enable_async && thread_pool_initialized_.load()) {
                        // 禁用异步操作，关闭线程池
                        this->shutdownThreadPool();

                    }
                });

            // 监听线程池核心线程数变化
            config_manager.addChangeListener("kafka.producer.thread_pool.core_size",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka producer thread pool core size changed from " + old_val + " to " + new_val);
                    try {
                        int new_core_size = std::stoi(new_val);
                        if (thread_pool_initialized_.load() && main_thread_pool_) {
                            main_thread_pool_->adjustCorePoolSize(new_core_size);
                            config_.thread_pool_core_size = new_core_size;
                            LOG_INFO("Kafka生产者线程池核心大小已动态调整为: " + std::to_string(new_core_size));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("调整Kafka生产者线程池核心大小失败: " + std::string(e.what()));
                    }
                });

            // 监听线程池最大线程数变化
            config_manager.addChangeListener("kafka.producer.thread_pool.max_size",
                [this](const std::string& key, const std::string& old_val, const std::string& new_val) {
                    LOG_INFO("Kafka producer thread pool max size changed from " + old_val + " to " + new_val);
                    try {
                        int new_max_size = std::stoi(new_val);
                        if (thread_pool_initialized_.load() && main_thread_pool_) {
                            main_thread_pool_->adjustMaximumPoolSize(new_max_size);
                            config_.thread_pool_max_size = new_max_size;
                            LOG_INFO("Kafka生产者线程池最大大小已动态调整为: " + std::to_string(new_max_size));
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("调整Kafka生产者线程池最大大小失败: " + std::string(e.what()));
                    }
                });


        }

        KafkaProducer::KafkaProducer(const KafkaProducerConfig& config)
                :isRunning_(false), config_(config) {
            // 验证配置
            config_.validate();

            std::string errstr;
            defaultTopicConf_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC));
            // 加载默认配置（如压缩算法、分区策略）
            defaultTopicConf_->set("compression.codec", "snappy", errstr);

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
            
            // 启动轮询线程
            startPollingThread();
        }

        KafkaProducer::KafkaProducer(const common::config::kafkaProducerConfig& config):isRunning_(false), managerConfig_(config) {
            // 验证配置
            managerConfig_.validate();

            std::string errstr;
            defaultTopicConf_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC));
            // 加载默认配置（如压缩算法、分区策略）
            defaultTopicConf_->set("compression.codec", "snappy", errstr);

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
            
            // 启动轮询线程
            startPollingThread();
        }

        bool KafkaProducer::init(const KafkaProducerConfig &config) {
            try {
                std::string errstr;
                conf_ = std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

                // 优化点1：使用配置Map简化设置逻辑
                std::map<std::string, std::string> kafkaConfigs = {
                    {"bootstrap.servers", config.brokers},
                    {"client.id", config.clientId},
                    {"message.timeout.ms", std::to_string(config.timeoutMs)},
                    {"queue.buffering.max.ms", std::to_string(config.queueBufferingMaxMs)},
                    {"queue.buffering.max.messages", std::to_string(config.queueBufferingMaxMessages)},
                    {"queue.buffering.max.kbytes", std::to_string(config.queueBufferingMaxKbytes)},
                    {"request.required.acks", std::to_string(config.requestRequiredAcks)},
                    {"compression.codec", "snappy"}  // 新增关键配置：压缩算法
                };

                for (const auto& [key, value] : kafkaConfigs) {
                    if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
                        LOG_ERROR("设置Kafka配置 " + key + "=" + value + " 失败: " + errstr);
                        return false;
                    }
                }

                // 优化点2：修复回调生命周期管理
                auto errorCb = std::make_shared<ErrorCb>(handleKafkaError); // 智能指针管理
                if (conf_->set("event_cb", errorCb.get(), errstr) != RdKafka::Conf::CONF_OK) {
                    LOG_ERROR("设置event_cb失败: " + errstr);
                    return false;
                }
                eventCb_ = errorCb; // 保存智能指针避免析构

                // 增加生产者重试逻辑
                for (int i = 0; i < 3; ++i) { // 最多重试3次
                    producer_ = std::unique_ptr<RdKafka::Producer>(
                        RdKafka::Producer::create(conf_.get(), errstr)
                    );
                    if (producer_) break;
                    std::this_thread::sleep_for(std::chrono::seconds(1)); // 延迟重试
                }
                if (!producer_) {
                    LOG_ERROR("创建生产者失败: " + errstr);
                    return false;
                }

                // 优化点4：添加关键参数校验
                if (config.requestRequiredAcks < -1 || config.requestRequiredAcks > 1) {
                    LOG_ERROR("无效acks值: " + std::to_string(config.requestRequiredAcks));
                    return false;
                }

                isRunning_ = true;
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        bool KafkaProducer::init(const common::config::kafkaProducerConfig& config) {
            try {
                std::string errstr;
                conf_ = std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

                // 优化点1：使用配置Map简化设置逻辑
                std::map<std::string, std::string> kafkaConfigs = {
                    {"bootstrap.servers", config.brokers},
                    {"client.id", config.client_id},
                    {"message.timeout.ms", std::to_string(config.request_timeout_ms)},
                    {"queue.buffering.max.ms", std::to_string(config.linger_ms)},
                    {"queue.buffering.max.messages", std::to_string(config.max_request_size)},
                    {"queue.buffering.max.kbytes", std::to_string(config.buffer_memory)},
                    {"request.required.acks", std::to_string(config.acks)},
                    {"compression.codec", config.compression_type}  // 新增关键配置：压缩算法
                };

                for (const auto& [key, value] : kafkaConfigs) {
                    if (conf_->set(key, value, errstr) != RdKafka::Conf::CONF_OK) {
                        LOG_ERROR("设置Kafka配置 " + key + "=" + value + " 失败: " + errstr);
                        return false;
                    }
                }

                // 优化点2：修复回调生命周期管理
                auto errorCb = std::make_shared<ErrorCb>(handleKafkaError); // 智能指针管理
                if (conf_->set("event_cb", errorCb.get(), errstr) != RdKafka::Conf::CONF_OK) {
                    LOG_ERROR("设置event_cb失败: " + errstr);
                    return false;
                }
                eventCb_ = errorCb; // 保存智能指针避免析构

                // 增加生产者重试逻辑
                for (int i = 0; i < 3; ++i) { // 最多重试3次
                    producer_ = std::unique_ptr<RdKafka::Producer>(
                        RdKafka::Producer::create(conf_.get(), errstr)
                    );
                    if (producer_) break;
                    std::this_thread::sleep_for(std::chrono::seconds(1)); // 延迟重试
                }
                if (!producer_) {
                    LOG_ERROR("创建生产者失败: " + errstr);
                    return false;
                }

                // 优化点4：添加关键参数校验
                if (config.acks < -1 || config.acks > 1) {
                    LOG_ERROR("无效acks值: " + std::to_string(config.acks));
                    return false;
                }

                isRunning_ = true;
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        RdKafka::Topic * KafkaProducer::getTopic(const std::string &topic) {
            std::lock_guard<std::mutex> lock(topicsMutex_);
            //检查主题是否存在
            auto it  = topics_.find(topic);
            if (it != topics_.end()) {
                return it->second.get();
            }

            //创建主题
            std::string errstr;
            RdKafka::Conf* topicConf = RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);
            // topicConf = defaultTopicConf_->dup(); // 复用基础配置
            std::unique_ptr<RdKafka::Topic> topicPtr(
                RdKafka::Topic::create(producer_.get(), topic, topicConf, errstr)
            );
            if (!topicPtr) {
                LOG_ERROR("创建主题失败: " + errstr + " | Topic: " + topic);
                delete topicConf; // 仅需销毁未被接管的配置
                return nullptr;
            }

            topics_[topic] = std::move(topicPtr);
            return topics_[topic].get();
        }

        void KafkaProducer::markForReconnect() {
            reconnect_required_ = true;
            LOG_INFO("Kafka producer marked for reconnection");

            // 立即触发重连检查（异步执行）
            scheduleProducerReconnectCheck();
        }

        void KafkaProducer::scheduleProducerReconnectCheck() {
            if (main_thread_pool_) {
                main_thread_pool_->submit([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 短暂延迟，避免频繁重连
                    checkAndHandleProducerReconnect();
                });
            } else {
                // 如果没有线程池，创建临时线程
                std::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    checkAndHandleProducerReconnect();
                }).detach();
            }
        }

        void KafkaProducer::checkAndHandleProducerReconnect() {
            if (!reconnect_required_.load()) {
                return; // 不需要重连
            }

            LOG_INFO("Starting Kafka producer reconnection process");

            try {
                // 1. 停止接受新的消息发送请求
                setAcceptingNewMessages(false);

                // 2. 等待当前正在发送的消息完成
                waitForPendingMessagesToComplete();

                // 3. 刷新并关闭当前producer
                flushAndCloseCurrentProducer();

                // 4. 重新加载配置
                reloadProducerConfiguration();

                // 5. 重新创建producer实例
                if (!recreateProducerInstance()) {
                    LOG_ERROR("Failed to recreate Kafka producer");
                    return;
                }

                // 6. 恢复接受新消息
                setAcceptingNewMessages(true);

                // 7. 重置重连标志
                reconnect_required_ = false;

                LOG_INFO("Kafka producer reconnection completed successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Kafka producer reconnection failed: " + std::string(e.what()));

                // 修复：避免递归调用，使用异步重试机制
                if (reconnect_required_.load()) {
                    LOG_INFO("Scheduling Kafka producer reconnection retry in 3 seconds");

                    // 使用异步方式重试，避免递归调用
                    if (main_thread_pool_) {
                        main_thread_pool_->submit([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(3));
                            if (reconnect_required_.load()) {
                                checkAndHandleProducerReconnect();
                            }
                        });
                    } else {
                        std::thread([this]() {
                            std::this_thread::sleep_for(std::chrono::seconds(3));
                            if (reconnect_required_.load()) {
                                checkAndHandleProducerReconnect();
                            }
                        }).detach();
                    }
                }
            }
        }

        void KafkaProducer::setAcceptingNewMessages(bool accepting) {
            std::lock_guard<std::mutex> lock(producer_mutex_);
            accepting_new_messages_ = accepting;

            if (accepting) {
                LOG_INFO("Kafka producer now accepting new messages");
            } else {
                LOG_INFO("Kafka producer stopped accepting new messages");
            }
        }

        void KafkaProducer::waitForPendingMessagesToComplete() {
            LOG_INFO("Waiting for pending Kafka messages to complete");

            const int max_wait_seconds = 30; // 最多等待30秒
            const int check_interval_ms = 100; // 每100ms检查一次

            for (int i = 0; i < max_wait_seconds * 1000 / check_interval_ms; ++i) {
                {
                    std::lock_guard<std::mutex> lock(producer_mutex_);
                    if (pending_messages_.empty()) {
                        LOG_INFO("All pending Kafka messages completed");
                        return;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
            }

            LOG_WARNING("Timeout waiting for pending Kafka messages to complete, proceeding with reconnection");
        }

        void KafkaProducer::flushAndCloseCurrentProducer() {
            std::lock_guard<std::mutex> lock(producer_mutex_);

            if (producer_) {
                LOG_INFO("Flushing and closing current Kafka producer");

                try {
                    // 刷新所有待发送的消息
                    producer_->flush(5000); // 最多等待5秒
                } catch (const std::exception& e) {
                    LOG_WARNING("Error during producer flush/close: " + std::string(e.what()));
                }

                producer_.reset();
            }
        }

        void KafkaProducer::reloadProducerConfiguration() {
            LOG_INFO("Reloading Kafka producer configuration");

            try {
                auto new_config = KafkaProducerConfig::fromConfigManager();
                new_config.validate();

                config_ = new_config;

                LOG_INFO("Kafka producer configuration reloaded successfully");
                LOG_DEBUG("New Kafka brokers: " + config_.brokers );
                LOG_DEBUG("New compression type: " + config_.compression_type);

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to reload Kafka producer configuration: " + std::string(e.what()));
                throw;
            }
        }

        bool KafkaProducer::recreateProducerInstance() {
            std::lock_guard<std::mutex> lock(producer_mutex_);

            try {
                LOG_INFO("Recreating Kafka producer with updated configuration");

                if (!init(config_)) {
                    LOG_ERROR("Failed to create new Kafka producer instance");
                    return false;
                }

                LOG_INFO("Kafka producer recreated successfully");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception while recreating Kafka producer: " + std::string(e.what()));
                return false;
            }
        }

        bool KafkaProducer::isProducerRunning() const {
            std::lock_guard<std::mutex> lock(producer_mutex_);
            return producer_ != nullptr;
        }

        void KafkaProducer::pollingThreadMain() {
            LOG_INFO("Kafka producer polling thread started");
            
            while (polling_thread_running_.load()) {
                try {
                    // 🔧 修复：检查生产者是否已关闭
                    if (producer_ && isRunning_) {
                        // 执行轮询操作
                        poll(0); // 使用0超时，避免阻塞
                    }
                    
                    // 根据配置的间隔时间休眠
                    std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval_ms_.load()));
                } catch (const std::exception& e) {
                    LOG_ERROR("Error in Kafka producer polling thread: " + std::string(e.what()));
                    // 发生异常时短暂休眠，避免忙等待
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                } catch (...) {
                    LOG_ERROR("Unknown error in Kafka producer polling thread");
                    // 发生未知异常时短暂休眠，避免忙等待
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
            
            LOG_INFO("Kafka producer polling thread stopped");
        }

        void KafkaProducer::startPollingThread() {
            if (polling_thread_running_.load()) {
                LOG_WARNING("Kafka producer polling thread is already running");
                return;
            }
            
            polling_thread_running_.store(true);
            polling_thread_ = std::make_unique<std::thread>(&KafkaProducer::pollingThreadMain, this);
            
            LOG_INFO("Kafka producer polling thread started with interval: " + 
                    std::to_string(polling_interval_ms_.load()) + "ms");
        }

        void KafkaProducer::stopPollingThread() {
            if (!polling_thread_running_.load()) {
                return;
            }
            
            LOG_INFO("Stopping Kafka producer polling thread");
            
            polling_thread_running_.store(false);
            
            if (polling_thread_ && polling_thread_->joinable()) {
                polling_thread_->join();
            }
            
            polling_thread_.reset();
            
            LOG_INFO("Kafka producer polling thread stopped");
        }

        // ==================== 线程池集成实现 ====================

        void KafkaProducer::initializeThreadPool() {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (thread_pool_initialized_.load()) {
                LOG_WARNING("Kafka生产者线程池已经初始化，跳过重复初始化");
                return;
            }

            if (!config_.enable_async_operations) {

                return;
            }

            try {
                // 创建线程池配置
                common::config::ThreadPoolConfig thread_config = common::config::ThreadPoolConfig::fromConfigManager();
                thread_config.validate();

                // 创建主线程池
                main_thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(thread_config);

                thread_pool_initialized_.store(true);

                LOG_INFO("Kafka生产者线程池初始化成功 - 核心线程数: " +
                        std::to_string(config_.thread_pool_core_size) +
                        ", 最大线程数: " + std::to_string(config_.thread_pool_max_size));

            } catch (const std::exception& e) {
                LOG_ERROR("Kafka生产者线程池初始化失败: " + std::string(e.what()));
                thread_pool_initialized_.store(false);
                throw;
            }
        }

        void KafkaProducer::shutdownThreadPool() {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (!thread_pool_initialized_.load()) {
                return;
            }

            LOG_INFO("开始关闭Kafka生产者线程池");

            try {
                // 关闭主线程池
                if (main_thread_pool_) {
                    main_thread_pool_->shutdown();
                    main_thread_pool_.reset();
                }

                thread_pool_initialized_.store(false);
                LOG_INFO("Kafka生产者线程池已关闭");

            } catch (const std::exception& e) {
                LOG_ERROR("关闭Kafka生产者线程池时发生异常: " + std::string(e.what()));
            }
        }

        std::string KafkaProducer::getThreadPoolStatus() const {
            std::lock_guard<std::mutex> lock(thread_pool_mutex_);

            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                return "线程池未初始化或已关闭";
            }

            std::ostringstream status;
            status << "Kafka生产者线程池状态:\n";
            status << "  异步操作: " << (config_.enable_async_operations ? "启用" : "禁用") << "\n";
            status << "  主线程池:\n";
            status << "    活跃线程数: " << main_thread_pool_->getActiveThreadCount() << "\n";
            status << "    总线程数: " << main_thread_pool_->getTotalThreadCount() << "\n";
            status << "    队列大小: " << main_thread_pool_->getQueueSize() << "\n";
            status << "    核心线程数: " << config_.thread_pool_core_size << "\n";
            status << "    最大线程数: " << config_.thread_pool_max_size << "\n";


            return status.str();
        }

        std::future<std::vector<bool>> KafkaProducer::sendBatchAsync(
            const std::vector<std::tuple<std::string, std::string, std::string>>& messages) {

            // 🔧 修复：检查线程池是否已初始化
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                // 如果线程池未初始化，返回一个立即完成的future
                std::promise<std::vector<bool>> promise;
                std::vector<bool> results(messages.size(), false);
                promise.set_value(results);
                return promise.get_future();
            }

            return main_thread_pool_->submit([this, messages]() -> std::vector<bool> {
                // 🔧 修复：检查生产者是否已关闭
                if (!this->isRunning_ || !this->producer_) {
                    std::vector<bool> results(messages.size(), false);
                    LOG_WARNING("[KafkaProducer::sendBatchAsync] 生产者已关闭，批量发送失败");
                    return results;
                }

                std::vector<bool> results;
                results.reserve(messages.size());

                LOG_WARNING("[KafkaProducer::sendBatchAsync] 异步批量发送 " + std::to_string(messages.size()) + " 条消息");

                for (const auto& msg : messages) {
                    const std::string& topic = std::get<0>(msg);
                    const std::string& key = std::get<1>(msg);
                    const std::string& value = std::get<2>(msg);

                    try {
                        bool success = this->send(topic, key, value);
                        results.push_back(success);

                        if (success) {
                            LOG_WARNING("[KafkaProducer::sendBatchAsync] 异步发送消息成功: topic=" + topic + ", key=" + key);
                        } else {
                            LOG_WARNING("[KafkaProducer::sendBatchAsync] 异步发送消息失败: topic=" + topic + ", key=" + key);
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("[KafkaProducer::sendBatchAsync] 异步发送消息异常: topic=" + topic + ", error=" + std::string(e.what()));
                        results.push_back(false);
                    }
                }

                LOG_INFO("[KafkaProducer::sendBatchAsync] 异步批量发送完成，成功: " + std::to_string(std::count(results.begin(), results.end(), true)) +
                        "/" + std::to_string(results.size()));
                return results;
            });
        }


        std::future<void> KafkaProducer::flushAsync(int timeoutMs) {
            // 🔧 修复：检查线程池是否已初始化
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("[KafkaProducer::flushAsync] 线程池未初始化或已关闭，跳过异步flush");

                // 返回一个已经完成的 future
                std::promise<void> promise;
                promise.set_value();
                return promise.get_future();
            }

            // 创建 promise，用于返回 future 并通知完成
            auto promise_ptr = std::make_shared<std::promise<void>>();

            auto future = promise_ptr->get_future();

            main_thread_pool_->submit([this, timeoutMs, promise_ptr]() {
                try {
                    LOG_INFO("[KafkaProducer::flushAsync] 开始异步flush操作，超时: " + std::to_string(timeoutMs) + "ms");

                    std::lock_guard<std::mutex> lock(producer_mutex_);
                    // 🔧 修复：检查生产者是否已关闭
                    if (producer_) {
                        RdKafka::ErrorCode err = producer_->flush(timeoutMs);  // 同步阻塞直到完成或超时
                        if (err != RdKafka::ERR_NO_ERROR) {
                            std::string errorMsg = "异步Flush操作失败: " + RdKafka::err2str(err);
                            LOG_ERROR(errorMsg);
                        }
                        
                        auto events = producer_->poll(0);
                        LOG_INFO("[KafkaProducer::flushAsync] 异步flush操作完成，处理事件数: " + std::to_string(events));
                    } else {
                        LOG_WARNING("[KafkaProducer::flushAsync] 生产者已关闭，跳过flush操作");
                    }

                    // 通知完成
                    promise_ptr->set_value();

                } catch (const std::exception& e) {
                    LOG_ERROR("[KafkaProducer::flushAsync] 异步flush操作异常: " + std::string(e.what()));
                    promise_ptr->set_exception(std::current_exception());

                } catch (...) {
                    LOG_ERROR("[KafkaProducer::flushAsync] 异步flush操作发生未知异常");
                    promise_ptr->set_exception(std::make_exception_ptr(std::runtime_error("Unknown error during flush")));
                }
            });

            return future;
        }

        void KafkaProducer::pollAsync(int timeoutMs) {
            // 🔧 修复：检查线程池是否已初始化
            if (!thread_pool_initialized_.load() || !main_thread_pool_) {
                LOG_WARNING("[KafkaProducer::pollAsync] 线程池未初始化或已关闭，跳过异步poll");
                return;
            }

            main_thread_pool_->submit([this, timeoutMs]() {
                try {
                    LOG_WARNING("[KafkaProducer::pollAsync] 开始异步poll操作，超时: " + std::to_string(timeoutMs) + "ms");

                    std::lock_guard<std::mutex> lock(producer_mutex_);
                    // 🔧 修复：检查生产者是否已关闭
                    if (producer_) {
                        int events = producer_->poll(timeoutMs);
                        LOG_WARNING("[KafkaProducer::pollAsync] 异步poll操作完成，处理事件数: " + std::to_string(events));
                    } else {
                        LOG_WARNING("[KafkaProducer::pollAsync] 生产者已关闭，跳过poll操作");
                    }

                } catch (const std::exception& e) {
                    LOG_ERROR("[KafkaProducer::pollAsync] 异步poll操作异常: " + std::string(e.what()));
                }
            });
        }

    }
}

