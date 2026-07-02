#include "gomoku_server.h"
#include "gomoku_player_session.h"
#include "event_system.h"
#include "service_client.h"
#include "common/logger/logger.h"
#include "common/config/config_manager.h"
#include "common/http/http_client.h"
#include "common/network/event_loop.h"
#include <fstream>
#include <filesystem>
#include <thread>

namespace game_services {
    namespace gomoku {

        // ==================== GomokuServerConfig 实现 ====================
        
        nlohmann::json GomokuServerConfig::toJson() const {
            nlohmann::json json;
            
            // 服务基本信息
            json["service_name"] = service_name;
            json["service_version"] = service_version;
            json["description"] = description;
            json["environment"] = environment;
            
            // 五子棋特定配置
            json["gomoku"] = {
                {"default_game_config", default_game_config.toJson()},
                {"max_concurrent_games", max_concurrent_games},
                {"allow_spectators", allow_spectators},
                {"enable_ranking", enable_ranking},
                {"enable_replay", enable_replay},
                {"game_data_path", game_data_path},
                {"enable_database", enable_database},
                {"save_game_records", save_game_records},
                {"save_move_history", save_move_history},
                {"save_chat_messages", save_chat_messages},
                {"enable_statistics", enable_statistics}
            };
            
            // 缓存配置
            json["cache"] = {
                {"enable_cache", enable_cache},
                {"cache_leaderboard", cache_leaderboard},
                {"cache_user_stats", cache_user_stats},
                {"cache_room_states", cache_room_states},
                {"cache_expire_seconds", cache_expire_seconds}
            };

            // WebSocket配置
            json["websocket"] = {
                {"enabled", enable_websocket},
                {"port", websocket_port}
            };

            // 服务元数据
            json["service_tags"] = service_tags;
            json["metadata"] = metadata;
            
            return json;
        }
        
        GomokuServerConfig GomokuServerConfig::fromConfigManager() {
            auto& config_manager = common::config::ConfigManager::getInstance();
            GomokuServerConfig config;
            
            // 服务基本信息
            config.service_name = config_manager.get<std::string>("service.name", "gomoku_service");
            config.service_version = config_manager.get<std::string>("service.version", "2.0.0");
            config.description = config_manager.get<std::string>("service.description", "五子棋游戏服务");
            config.environment = config_manager.get<std::string>("service.environment", "development");
            
            // 🎯 加载架构模式配置
            config.architecture_mode = config_manager.get<std::string>("service.mode", "simple");
            LOG_INFO("加载架构模式: " + config.architecture_mode);
            
            // 使用预定义配置结构体的fromConfigManager方法
            config.network_config = common::config::NetworkConfig::fromConfigManager();
            config.database_config = common::config::DatabaseConfig::fromConfigManager();
            config.redis_config = common::config::RedisConfig::fromConfigManager();
            config.thread_pool_config = common::config::ThreadPoolConfig::fromConfigManager();
            
            // 五子棋特定配置
            config.default_game_config = GomokuConfig::fromConfigManager();
            config.max_concurrent_games = config_manager.get<int>("gomoku.max_concurrent_games", 100);
            config.allow_spectators = config_manager.get<bool>("gomoku.allow_spectators", true);
            config.enable_ranking = config_manager.get<bool>("gomoku.enable_ranking", true);
            config.enable_replay = config_manager.get<bool>("gomoku.enable_replay", false);
            config.game_data_path = config_manager.get<std::string>("gomoku.game_data_path", "data/gomoku");
            
            // 数据库持久化配置
            config.enable_database = config_manager.get<bool>("gomoku.enable_database", true);
            config.save_game_records = config_manager.get<bool>("gomoku.save_game_records", true);
            config.save_move_history = config_manager.get<bool>("gomoku.save_move_history", true);
            config.save_chat_messages = config_manager.get<bool>("gomoku.save_chat_messages", true);
            config.enable_statistics = config_manager.get<bool>("gomoku.enable_statistics", true);
            
            // 缓存配置
            config.enable_cache = config_manager.get<bool>("cache.enable", true);
            config.cache_leaderboard = config_manager.get<bool>("cache.leaderboard", true);
            config.cache_user_stats = config_manager.get<bool>("cache.user_stats", true);
            config.cache_room_states = config_manager.get<bool>("cache.room_states", true);
            config.cache_expire_seconds = config_manager.get<int>("cache.expire_seconds", 3600);

            // 服务间通信配置
            config.auth_service_url = config_manager.get<std::string>("security.authentication.token_verification_url", "http://127.0.0.1:8083/api/v1/auth/validate");
            // 从 token_verification_url 中提取基础 URL
            if (config.auth_service_url.find("/api/v1/auth/validate") != std::string::npos) {
                config.auth_service_url = config.auth_service_url.substr(0, config.auth_service_url.find("/api/v1/auth/validate"));
            }

            // WebSocket配置
            config.enable_websocket = config_manager.get<bool>("websocket.enabled", true);
            config.websocket_port = config_manager.get<int>("websocket.port", 8085);

            // 服务标签（默认为数组）
            config.service_tags = {"game", "gomoku", "chess"};

            return config;
        }
        
        GomokuServerConfig GomokuServerConfig::fromConfigFile(const std::string& config_file) {
            try {
                // 先加载配置文件
                auto& config_manager = common::config::ConfigManager::getInstance();

                if (!std::filesystem::exists(config_file)) {
                    LOG_ERROR("配置文件不存在: " + config_file);
                    auto default_config = GomokuServer::getDefaultConfig();
                    default_config.config_file = config_file;
                    return default_config;
                }

                if (!config_manager.loadFromFile(config_file)) {
                    LOG_ERROR("无法加载配置文件: " + config_file);
                    auto default_config = GomokuServer::getDefaultConfig();
                    default_config.config_file = config_file;
                    return default_config;
                }

                LOG_INFO("配置文件加载成功: " + config_file);

                // 然后使用ConfigManager加载配置
                auto config = fromConfigManager();
                config.config_file = config_file;

                return config;
                
            } catch (const std::exception& e) {
                LOG_ERROR("解析配置文件异常: " + std::string(e.what()));
                // 🔧 修复：返回有效的默认配置而不是空配置
                auto default_config = GomokuServer::getDefaultConfig();
                default_config.config_file = config_file;
                return default_config;
            }
        }

        GomokuServer::GomokuServer(const GomokuServerConfig& config)
            : GameServerBase(createBaseConfig(config)), gomoku_config_(config), 
              server_start_time_(std::chrono::steady_clock::now()) {
            LOG_INFO("五子棋游戏服务器创建，监听端口: " + std::to_string(config.network_config.listen_port));
            
            // 🎯 应用架构模式配置
            applyArchitectureMode(config.architecture_mode);
        }

        // 🔧 架构简化：移除DynamicConfigManager构造函数实现
        
        // 🎯 架构模式处理方法实现
        void GomokuServer::applyArchitectureMode(const std::string& mode) {
            LOG_INFO("应用架构模式: " + mode);
            
            // 复制配置结构引用以便修改
            auto& config = const_cast<GomokuServerConfig&>(gomoku_config_);
            
            if (mode == "simple") {
                // 🎯 简化模式配置
                config.threads.worker_threads = 4;
                config.threads.io_threads = 2;
                config.threads.event_threads = 1;
                
                config.components.enable_kafka = false;
                config.components.enable_redis = false;
                config.components.enable_monitoring = false;
                config.components.enable_circuit_breaker = false;
                config.components.enable_time_wheel = false;
                config.components.enable_distributed_cache = false;
                
                config.storage.mysql_pool_size = 3;
                config.storage.redis_pool_size = 0;
                
                LOG_INFO("✅ 简化模式已应用 - 线程: 6个, 组件: 核心功能, 连接池: 3个");
                
            } else if (mode == "standard") {
                // 🔧 标准模式配置
                config.threads.worker_threads = 8;
                config.threads.io_threads = 4;
                config.threads.event_threads = 2;
                
                config.components.enable_kafka = true;
                config.components.enable_redis = true;
                config.components.enable_monitoring = true;
                config.components.enable_circuit_breaker = true;
                config.components.enable_time_wheel = true;
                config.components.enable_distributed_cache = false;
                
                config.storage.mysql_pool_size = 8;
                config.storage.redis_pool_size = 4;
                
                LOG_INFO("✅ 标准模式已应用 - 线程: 14个, 组件: 标准功能, 连接池: 12个");
                
            } else if (mode == "enterprise") {
                // 🏢 企业模式配置
                config.threads.worker_threads = 16;
                config.threads.io_threads = 8;
                config.threads.event_threads = 4;
                
                config.components.enable_kafka = true;
                config.components.enable_redis = true;
                config.components.enable_monitoring = true;
                config.components.enable_circuit_breaker = true;
                config.components.enable_time_wheel = true;
                config.components.enable_distributed_cache = true;
                
                config.storage.mysql_pool_size = 20;
                config.storage.redis_pool_size = 8;
                
                LOG_INFO("✅ 企业模式已应用 - 线程: 28个, 组件: 全功能, 连接池: 28个");
                
            } else {
                LOG_WARNING("未知架构模式: " + mode + "，使用默认简化模式");
                applyArchitectureMode("simple");
                return;
            }
            
            // 应用线程池配置
            config.thread_pool_config.core_pool_size = config.threads.worker_threads;
            config.thread_pool_config.maximum_pool_size = config.threads.worker_threads;
        }
        
        bool GomokuServer::shouldEnableComponent(const std::string& component_name) const {
            if (component_name == "kafka") {
                return gomoku_config_.components.enable_kafka;
            } else if (component_name == "redis") {
                return gomoku_config_.components.enable_redis;
            } else if (component_name == "monitoring") {
                return gomoku_config_.components.enable_monitoring;
            } else if (component_name == "circuit_breaker") {
                return gomoku_config_.components.enable_circuit_breaker;
            } else if (component_name == "time_wheel") {
                return gomoku_config_.components.enable_time_wheel;
            } else if (component_name == "distributed_cache") {
                return gomoku_config_.components.enable_distributed_cache;
            }

            LOG_WARNING("未知组件名称: " + component_name + "，默认禁用");
            return false;
        }

        game_base::BaseConfig GomokuServer::createBaseConfig(const GomokuServerConfig& config) {
            game_base::BaseConfig base_config;
            base_config.host = config.network_config.bind_address;
            base_config.port = config.network_config.listen_port;
            base_config.websocket_port = config.websocket_port;
            base_config.max_rooms = config.max_concurrent_games;
            base_config.max_players_per_room = 2;  // 五子棋固定为2人
            base_config.enable_websocket = config.enable_websocket;
            base_config.enable_leaderboard = config.enable_ranking;
            base_config.game_type = game_base::GameType::CHESS;
            base_config.config_file = "";  // 由ConfigManager管理
            base_config.qt6_optimizations = true;
            base_config.enable_compression = false;

            // 🔧 修复：传递 MySQL 配置
            base_config.mysql_host = config.database_config.mysql_host;
            base_config.mysql_port = config.database_config.mysql_port;
            base_config.mysql_database = config.database_config.mysql_database;
            base_config.mysql_username = config.database_config.mysql_user;
            base_config.mysql_password = config.database_config.mysql_password;
            base_config.mysql_max_connections = config.database_config.mysql_max_pool_size;
            base_config.mysql_connection_timeout = config.database_config.mysql_connect_timeout_ms;

            // 🔧 修复：传递 Redis 配置
            base_config.redis_host = config.redis_config.redis_host;
            base_config.redis_port = config.redis_config.redis_port;
            base_config.redis_database = config.redis_config.redis_database;
            base_config.redis_password = config.redis_config.redis_password;
            base_config.redis_max_connections = config.redis_config.redis_max_pool_size;
            base_config.redis_connection_timeout = config.redis_config.redis_connect_timeout_ms;

            // 传递服务间通信配置
            base_config.auth_service_url = config.auth_service_url;

            return base_config;
        }

        GomokuServer::~GomokuServer() {
            LOG_INFO("五子棋游戏服务器销毁");
        }

        bool GomokuServer::initializeGameSpecific() {
            try {
                // 创建游戏数据目录
                if (!std::filesystem::exists(gomoku_config_.game_data_path)) {
                    std::filesystem::create_directories(gomoku_config_.game_data_path);
                    LOG_INFO("创建游戏数据目录: " + gomoku_config_.game_data_path);
                }

                // 初始化事件驱动系统
                if (!initializeEventSystem()) {
                    LOG_WARNING("事件系统初始化失败，将以传统同步模式运行");
                } else {
                    LOG_INFO("✅ 事件驱动架构初始化成功");
                }

                // 初始化数据库（如果启用）
                if (gomoku_config_.enable_database) {
                    if (!initializeDatabase()) {
                        LOG_WARNING("数据库初始化失败，将以非持久化模式运行");
                        gomoku_config_.enable_database = false;
                    }
                }

                // 初始化Redis缓存（如果启用）
                if (!gomoku_config_.redis_config.redis_host.empty()) {
                    if (!initializeRedis()) {
                        LOG_WARNING("Redis缓存初始化失败，将以无缓存模式运行");
                        // 清空Redis主机以标记为禁用
                    }
                }

                // 🔧 新增：初始化统一线程池
                if (!initializeThreadPool()) {
                    LOG_ERROR("线程池初始化失败");
                    return false;
                }

                // 🎯 初始化匹配管理器
                MatchPoolConfig match_config = MatchPoolConfig::fromConfig();
                match_making_manager_ = std::make_unique<MatchMakingManager>(match_config);

                // 设置匹配结果回调
                match_making_manager_->setMatchResultCallback(
                    [this](const MatchResult& result) {
                        handleMatchResult(result);
                    });

                // 启动匹配管理器
                if (!match_making_manager_->start()) {
                    LOG_WARNING("匹配管理器启动失败，自动匹配功能将不可用");
                } else {
                    LOG_INFO("✅ 匹配管理器初始化成功");
                }

                // 🔧 新增：设置 CORS 中间件
                setupCorsMiddleware();

                // 初始化HTTP路由
                initializeHttpRoutes();

                // 🔧 修复：设置房间销毁回调，清理 player_room_mapping_
                setRoomDestroyedCallback([this](const std::string& room_id) {
                    LOG_INFO("🧹 清理房间 " + room_id + " 的玩家映射");
                    std::unique_lock<std::shared_mutex> lock(player_room_mutex_);
                    for (auto it = player_room_mapping_.begin(); it != player_room_mapping_.end();) {
                        if (it->second == room_id) {
                            LOG_INFO("  - 移除玩家 " + it->first + " 的房间映射");
                            it = player_room_mapping_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                });

                LOG_INFO("五子棋游戏特定初始化完成");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("五子棋游戏初始化失败: " + std::string(e.what()));
                return false;
            }
        }

        bool GomokuServer::initializeEventSystem() {
            try {
                LOG_INFO("🎯 开始初始化简化事件驱动系统...");
                
                // 🎯 创建统一线程池（根据架构模式配置）
                common::config::ThreadPoolConfig pool_config;
                auto thread_pool = std::make_shared<common::thread_pool::ThreadPool>(gomoku_config_.thread_pool_config);
                if (!thread_pool) {
                    LOG_ERROR("无法创建统一线程池");
                    return false;
                }
                
                LOG_INFO("✅ 统一线程池创建成功: " + std::to_string(gomoku_config_.threads.worker_threads) + "个工作线程");
                
                // 🎯 根据架构模式决定是否启用Kafka
                bool enable_kafka = shouldEnableComponent("kafka");
                std::string kafka_brokers = "localhost:9092";
                std::string topic_prefix = "gomoku_events";
                
                if (enable_kafka) {
                    auto& config_mgr = common::config::ConfigManager::getInstance();
                    kafka_brokers = config_mgr.get<std::string>("kafka.brokers", kafka_brokers);
                    topic_prefix = config_mgr.get<std::string>("kafka.topic_prefix", topic_prefix);
                    LOG_INFO("Kafka已启用: brokers=" + kafka_brokers + ", prefix=" + topic_prefix);
                } else {
                    LOG_INFO("Kafka已禁用（简化模式），跨服务事件将被忽略");
                }
                
                // 🎯 初始化简化事件管理器（使用统一线程池，无独立事件线程）
                SimplifiedEventManagerInstance::initialize(thread_pool, enable_kafka, kafka_brokers, topic_prefix);
                
                LOG_INFO("✅ 简化事件系统初始化完成 - 使用统一线程池，无独立线程");
                
                // 🎯 发布服务启动事件（使用简化事件系统）
                try {
                    nlohmann::json startup_data = {
                        {"service_name", "gomoku_game_service"},
                        {"version", "2.0.0"},
                        {"architecture_mode", gomoku_config_.architecture_mode},
                        {"startup_time", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()}
                    };
                    
                    // 明确发布跨服务事件到Kafka（如果启用）
                    auto& event_manager = SimplifiedEventManagerInstance::getInstance();
                    event_manager.publishCrossServiceEvent(EventTypes::CrossService::SERVICE_HEALTH_CHANGED, startup_data);
                    
                    LOG_INFO("✅ 服务启动事件已发布");
                } catch (const std::exception& e) {
                    LOG_WARNING("发布服务启动事件失败: " + std::string(e.what()));
                }
                
                return true;
                
            } catch (const std::exception& e) {
                LOG_ERROR("初始化事件系统失败: " + std::string(e.what()));
                return false;
            }
        }

        std::shared_ptr<ServiceClientManager> GomokuServer::getServiceClientManager() {
            // 从WebSocket处理器获取服务客户端管理器
            if (gomoku_websocket_) {
                return gomoku_websocket_->getServiceClientManager();
            }
            return nullptr;
        }

        std::string GomokuServer::getGameTypeName() const {
            return "gomoku";
        }

        int GomokuServer::getWebSocketPort() const {
            return gomoku_config_.websocket_port;
        }

        std::shared_ptr<game_base::GameRoomBase> GomokuServer::createGameRoom(
            const std::string& room_id,
            const std::string& creator_id,
            const nlohmann::json& config) {
            
            GomokuConfig gomoku_config = gomoku_config_.default_game_config;
            if (!config.empty()) {
                gomoku_config = GomokuConfig::fromJson(config);
            }
            
            return createGomokuRoom(room_id, creator_id, gomoku_config);
        }

        std::shared_ptr<GomokuRoom> GomokuServer::createGomokuRoom(const std::string& room_id,
                                                                  const std::string& creator_id,
                                                                  const GomokuConfig& config) {
            // 检查房间数限制
            if (rooms_.size() >= static_cast<size_t>(gomoku_config_.max_concurrent_games)) {
                LOG_WARNING("达到最大并发游戏数限制: " + std::to_string(gomoku_config_.max_concurrent_games));
                return nullptr;
            }

            // 获取EventLoop用于时间轮定时器
            auto event_loop = http_server_ ? http_server_->getEventLoop() : nullptr;
            auto room = GomokuRoom::create(room_id, creator_id, config, event_loop);
            if (!room) {
                LOG_ERROR("创建五子棋房间失败: " + room_id);
                return nullptr;
            }

            // 设置房间回调
            auto room_created_cb = [this, room](const std::string& room_id) {
                if (room_created_callback_) {
                    room_created_callback_(room_id);
                }
                // 广播房间创建事件
                broadcastRoomEvent("created", room_id, room->getRoomStats());
                LOG_INFO("五子棋房间创建完成: " + room_id);
            };
            room->setRoomCreatedCallback(room_created_cb);

            // 手动触发房间创建回调（因为构造函数中回调尚未设置）
            room_created_cb(room_id);

            room->setRoomDestroyedCallback([this](const std::string& room_id) {
                if (room_destroyed_callback_) {
                    room_destroyed_callback_(room_id);
                }
                // 广播房间移除事件（符合文档 §4.2 格式）
                broadcastRoomEvent("removed", room_id, {}, "room_closed");
                LOG_INFO("五子棋房间销毁完成: " + room_id);
            });

            // 设置玩家加入/离开回调
            room->setPlayerJoinCallback([this, room](const std::string& room_id, const std::string& player_id) {
                if (player_joined_callback_) {
                    player_joined_callback_(room_id, player_id);
                }
                // 广播房间更新事件
                broadcastRoomEvent("updated", room_id, room->getRoomStats());
                LOG_INFO("玩家加入五子棋房间: " + player_id + " -> " + room_id);
            });

            room->setPlayerLeaveCallback([this, room](const std::string& room_id, const std::string& player_id) {
                if (player_left_callback_) {
                    player_left_callback_(room_id, player_id);
                }
                // 广播房间更新事件
                broadcastRoomEvent("updated", room_id, room->getRoomStats());
                LOG_INFO("玩家离开五子棋房间: " + player_id + " <- " + room_id);
            });

            room->setGameEndCallback([this, room](const std::string& room_id, const std::vector<std::string>& winners) {
                // 🔧 修复：根据获胜者正确推断GameResult，而不是硬编码为DRAW
                GameResult result = GameResult::DRAW;
                std::string winner_id;

                if (!winners.empty()) {
                    winner_id = winners[0];

                    // 获取房间以确定获胜者的棋子颜色
                    auto room_ptr = getGomokuRoom(room_id);
                    if (room_ptr) {
                        PieceType winner_piece = room_ptr->getPlayerPiece(winner_id);
                        result = (winner_piece == PieceType::BLACK) ? GameResult::BLACK_WIN : GameResult::WHITE_WIN;

                        LOG_INFO("🎯 推断游戏结果: 获胜者 " + winner_id +
                                " 棋子类型: " + std::to_string(static_cast<int>(winner_piece)) +
                                " 结果: " + std::to_string(static_cast<int>(result)));
                    } else {
                        LOG_WARNING("无法获取房间信息，使用默认结果判断");
                        // 备用方案：假设第一个获胜者是黑棋（通常情况）
                        result = GameResult::BLACK_WIN;
                    }
                }

                // 广播房间更新事件（游戏结束后房间状态变化）
                broadcastRoomEvent("updated", room_id, room->getRoomStats());

                handleGameFinished(room_id, result, winner_id);
                LOG_INFO("五子棋游戏结束: " + room_id + ", 结果: " +
                        std::to_string(static_cast<int>(result)) + ", 获胜者: " +
                        (winner_id.empty() ? "平局" : winner_id));

                // 报告对局完成到服务注册中心
                reportMatchCompletion();
            });

            // [NEW] 设置带上下文的游戏结束回调（用于调用 Game Data Service 结算 API）
            room->setGameEndCallbackWithContext(
                [this](const std::string& room_id,
                       const std::vector<std::string>& winners,
                       const GameEndContext& context) {
                    handleGameFinishedWithContext(room_id, winners, context);
                });

            // 设置玩家准备状态变化回调
            // [FIX] 避免死锁：使用基本房间信息，不调用 getRoomStats()
            room->setPlayerReadyCallback([this, room](const std::string& room_id, const std::string& player_id, bool ready) {
                // 使用基本房间信息，避免获取锁
                nlohmann::json basic_stats = {
                    {"current_players", room->getPlayerCount()},
                    {"state", "waiting_players"}
                };
                broadcastRoomEvent("updated", room_id, basic_stats);
                LOG_DEBUG("玩家准备状态变化: " + player_id + " -> " + (ready ? "ready" : "not ready"));
            });

            // 设置游戏开始回调
            // [FIX] 避免死锁：不在回调中调用 getRoomStats()，因为它需要获取 room_mutex_
            // 而 startGame() 已经持有该锁。改为使用基本房间信息。
            room->setGameStartCallback([this, room](const std::string& room_id) {
                // 使用基本房间信息，避免获取锁
                nlohmann::json basic_stats = {
                    {"current_players", room->getPlayerCount()},
                    {"state", "playing"}
                };
                broadcastRoomEvent("updated", room_id, basic_stats);
                LOG_INFO("游戏开始回调触发: " + room_id);
            });

            // 设置获取玩家信息回调（用于 player_joined 消息中的 playerInfo 字段）
            room->setPlayerInfoCallback([this](const std::string& player_id) -> nlohmann::json {
                if (gomoku_websocket_) {
                    auto player_info = gomoku_websocket_->getPlayerInfo(player_id);
                    if (player_info.has_value()) {
                        return {
                            {"playerId", player_info->player_id},
                            {"username", player_info->username},
                            {"nickname", player_info->nickname},
                            {"avatarUrl", player_info->avatar_url},
                            {"level", player_info->level},
                            {"rating", player_info->rating},
                            {"totalGames", player_info->total_games},
                            {"wins", player_info->wins},
                            {"losses", player_info->losses}
                        };
                    }
                }
                // 返回基本信息
                return {
                    {"playerId", player_id},
                    {"username", player_id},
                    {"nickname", player_id}
                };
            });

            // 添加到房间管理
            {
                std::unique_lock<std::shared_mutex> lock(rooms_mutex_);
                rooms_[room_id] = room;
            }

            LOG_INFO("创建五子棋房间成功: " + room_id + ", 创建者: " + creator_id);
            return room;
        }

        std::shared_ptr<GomokuRoom> GomokuServer::getGomokuRoom(const std::string& room_id) {
            std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
            auto it = rooms_.find(room_id);
            if (it != rooms_.end()) {
                return std::dynamic_pointer_cast<GomokuRoom>(it->second);
            }
            return nullptr;
        }

        bool GomokuServer::handlePlayerJoinRoom(const std::string& player_id, const std::string& room_id) {
            auto room = getGomokuRoom(room_id);
            if (!room) {
                LOG_WARNING("房间不存在: " + room_id);
                return false;
            }

            // 🔧 修复：检查玩家是否已在当前房间中（避免重复加入）
            if (room->isPlayerInRoom(player_id)) {
                LOG_INFO("玩家已在房间中，跳过重复加入: " + player_id + " -> " + room_id);
                return true;  // 返回true，因为玩家已经在房间中
            }

            // 检查玩家是否已在其他房间
            std::string current_room = getPlayerRoomId(player_id);
            if (!current_room.empty() && current_room != room_id) {
                LOG_WARNING("玩家 " + player_id + " 已在房间 " + current_room + " 中");
                return false;
            }

            // 获取或创建玩家会话
            auto session = getOrCreatePlayerSession(player_id);
            if (!session) {
                LOG_WARNING("无法创建玩家会话: " + player_id);
                return false;
            }

            if (room->addPlayer(player_id, session)) {
                updatePlayerRoomMapping(player_id, room_id);
                
                // 通知WebSocket处理器更新玩家房间映射
                if (gomoku_websocket_) {
                    gomoku_websocket_->setPlayerRoom(player_id, room_id);
                }
                
                LOG_INFO("玩家加入房间成功: " + player_id + " -> " + room_id);
                return true;
            }

            return false;
        }

        std::shared_ptr<game_base::PlayerSessionBase> GomokuServer::getOrCreatePlayerSession(const std::string& player_id) {
            // 如果WebSocket处理器存在，从那里获取会话
            if (gomoku_websocket_) {
                auto session = gomoku_websocket_->getPlayerSession(player_id);
                if (session) {
                    return session;
                }
            }

            // 创建新的会话（HTTP 创建房间时，尚无 WebSocket 连接）
            // 注意：实际应用中，这里应该从认证系统获取会话信息
            try {
                auto session = GomokuPlayerSession::create(player_id, -1, "127.0.0.1");
                if (session && gomoku_websocket_) {
                    // 🔧 关键修复：设置发送回调，使用 WebSocket Handler 的 sendRawMessage
                    // 即使 WebSocket 连接尚未建立，也能正确处理消息发送（会记录日志或缓存）
                    auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(session);
                    if (gomoku_session) {
                        gomoku_session->setSendCallback([this](const std::string& pid, const std::string& msg) -> bool {
                            if (gomoku_websocket_) {
                                return gomoku_websocket_->sendRawMessage(pid, msg);
                            }
                            LOG_WARNING("WebSocket handler not available for sending message to: " + pid);
                            return false;
                        });
                        LOG_DEBUG("为 HTTP 创建的会话设置发送回调: " + player_id);
                    }
                    gomoku_websocket_->addPlayerSession(player_id, session);
                }
                return session;
            } catch (const std::exception& e) {
                LOG_ERROR("创建玩家会话失败: " + std::string(e.what()));
                return nullptr;
            }
        }

        bool GomokuServer::handlePlayerLeaveRoom(const std::string& player_id) {
            LOG_INFO("[handlePlayerLeaveRoom] 玩家离开房间: " + player_id);
            std::string room_id = getPlayerRoomId(player_id);
            if (room_id.empty()) {
                LOG_INFO("[handlePlayerLeaveRoom] 玩家不在任何房间中: " + player_id);
                return false;
            }

            auto room = getGomokuRoom(room_id);
            if (!room) {
                return false;
            }

            // 保存玩家状态用于可能的重连
            DisconnectedPlayerInfo disconnect_info;
            disconnect_info.room_id = room_id;
            disconnect_info.disconnect_time = std::chrono::system_clock::now();
            disconnect_info.player_state = {
                {"playerId", player_id},
                {"pieceType", static_cast<int>(room->getPlayerPiece(player_id))},
                {"isReady", false}  // 重连后需要重新准备
            };

            {
                std::lock_guard<std::mutex> lock(disconnected_players_mutex_);
                disconnected_players_[player_id] = disconnect_info;
            }

            if (room->removePlayer(player_id)) {
                updatePlayerRoomMapping(player_id, "");

                // 通知WebSocket处理器更新玩家房间映射
                if (gomoku_websocket_) {
                    gomoku_websocket_->removePlayerRoom(player_id);
                }

                LOG_INFO("玩家离开房间（已保存重连信息）: " + player_id + " <- " + room_id);
                return true;
            }

            return false;
        }

        bool GomokuServer::handlePlayerReconnect(const std::string& player_id) {
            // 检查是否有保存的重连信息
            DisconnectedPlayerInfo reconnect_info;
            bool has_reconnect_info = false;

            {
                std::lock_guard<std::mutex> lock(disconnected_players_mutex_);
                auto it = disconnected_players_.find(player_id);
                if (it != disconnected_players_.end()) {
                    // 检查是否超时
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now() - it->second.disconnect_time).count();

                    if (elapsed <= RECONNECT_TIMEOUT_SECONDS) {
                        reconnect_info = it->second;
                        has_reconnect_info = true;
                    }
                    // 无论是否超时，都移除保存的信息
                    disconnected_players_.erase(it);
                }
            }

            if (!has_reconnect_info) {
                LOG_WARNING("无可用的重连信息或已超时: " + player_id);
                return false;
            }

            // 检查房间是否还存在
            auto room = getGomokuRoom(reconnect_info.room_id);
            if (!room) {
                LOG_WARNING("重连失败，房间已不存在: " + reconnect_info.room_id);
                return false;
            }

            // 尝试重新加入房间
            if (!handlePlayerJoinRoom(player_id, reconnect_info.room_id)) {
                LOG_WARNING("重连失败，无法加入房间: " + player_id + " -> " + reconnect_info.room_id);
                return false;
            }

            // 恢复玩家状态（如棋子颜色）
            if (reconnect_info.player_state.contains("pieceType")) {
                PieceType piece = static_cast<PieceType>(
                    reconnect_info.player_state["pieceType"].get<int>());
                room->setPlayerPiece(player_id, piece);
            }

            // 发送当前游戏状态给重连的玩家
            if (gomoku_websocket_) {
                nlohmann::json reconnect_success = {
                    {"type", "reconnect_success"},
                    {"data", {
                        {"roomId", reconnect_info.room_id},
                        {"playerState", reconnect_info.player_state},
                        {"gameState", room->getGameSpecificState()},
                        {"roomInfo", room->getRoomStats()}
                    }}
                };
                gomoku_websocket_->sendToPlayer(player_id, reconnect_success);
            }

            LOG_INFO("玩家重连成功: " + player_id + " -> " + reconnect_info.room_id);
            return true;
        }

        bool GomokuServer::handleSpectatorJoin(const std::string& spectator_id, const std::string& room_id) {
            if (!gomoku_config_.allow_spectators) {
                LOG_WARNING("服务器不允许观战");
                return false;
            }

            auto room = getGomokuRoom(room_id);
            if (!room) {
                LOG_WARNING("房间不存在: " + room_id);
                return false;
            }

            // 实现玩家会话管理
            // 检查观众是否已认证（使用基类认证系统）
            bool is_authenticated = isPlayerAuthenticated(spectator_id);
            if (!is_authenticated) {
                LOG_WARNING("观众未认证: " + spectator_id);
                // 对于观众，我们可以允许未认证的情况，但要记录
                // 在实际实现中，可能需要要求观众也必须认证
            } else {
                LOG_DEBUG("观众认证通过: " + spectator_id);
            }
            /*
            if (!session) {
                LOG_WARNING("观战者会话不存在: " + spectator_id);
                return false;
            }
            */

            // 临时使用 nullptr 代替 session
            if (room->addSpectator(spectator_id, nullptr)) {
                LOG_INFO("观战者加入房间: " + spectator_id + " -> " + room_id);
                return true;
            }

            return false;
        }

        bool GomokuServer::handleSpectatorLeave(const std::string& spectator_id) {
            // 查找观战者所在的房间
            std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
            for (const auto& pair : rooms_) {
                auto gomoku_room = std::dynamic_pointer_cast<GomokuRoom>(pair.second);
                if (gomoku_room && gomoku_room->isSpectator(spectator_id)) {
                    lock.unlock();
                    if (gomoku_room->removeSpectator(spectator_id)) {
                        LOG_INFO("观战者离开房间: " + spectator_id);
                        return true;
                    }
                    return false;
                }
            }
            return false;
        }

        std::vector<nlohmann::json> GomokuServer::getRoomList(bool include_private) const {
            std::vector<nlohmann::json> room_list;
            
            std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
            for (const auto& pair : rooms_) {
                auto gomoku_room = std::dynamic_pointer_cast<GomokuRoom>(pair.second);
                if (gomoku_room) {
                    // 简化的私人房间检查逻辑
                    bool is_private = false; // 这里可以根据房间配置判断
                    
                    if (!is_private || include_private) {
                        room_list.push_back(gomoku_room->getRoomStats());
                    }
                }
            }
            
            return room_list;
        }

        // ========== 房间列表订阅管理实现 ==========

        void GomokuServer::subscribeRoomList(const std::string& player_id) {
            std::lock_guard<std::mutex> lock(subscribers_mutex_);
            room_list_subscribers_.insert(player_id);
            LOG_DEBUG("玩家订阅房间列表: " + player_id + ", 当前订阅者数量: " +
                     std::to_string(room_list_subscribers_.size()));
        }

        void GomokuServer::unsubscribeRoomList(const std::string& player_id) {
            std::lock_guard<std::mutex> lock(subscribers_mutex_);
            room_list_subscribers_.erase(player_id);
            LOG_DEBUG("玩家取消订阅房间列表: " + player_id + ", 当前订阅者数量: " +
                     std::to_string(room_list_subscribers_.size()));
        }

        bool GomokuServer::isRoomListSubscriber(const std::string& player_id) const {
            std::lock_guard<std::mutex> lock(subscribers_mutex_);
            return room_list_subscribers_.find(player_id) != room_list_subscribers_.end();
        }

        void GomokuServer::broadcastToSubscribers(const nlohmann::json& message) {
            std::vector<std::string> subscribers;
            {
                std::lock_guard<std::mutex> lock(subscribers_mutex_);
                subscribers = std::vector<std::string>(room_list_subscribers_.begin(),
                                                       room_list_subscribers_.end());
            }

            if (subscribers.empty()) {
                return;
            }

            // 通过 WebSocket 处理器发送消息
            if (gomoku_websocket_) {
                for (const auto& player_id : subscribers) {
                    gomoku_websocket_->sendToPlayer(player_id, message);
                }
            }

            LOG_DEBUG("广播消息给 " + std::to_string(subscribers.size()) + " 个订阅者");
        }

        nlohmann::json GomokuServer::getRoomListData() const {
            nlohmann::json rooms_array = nlohmann::json::array();

            std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
            for (const auto& pair : rooms_) {
                auto gomoku_room = std::dynamic_pointer_cast<GomokuRoom>(pair.second);
                if (gomoku_room) {
                    nlohmann::json room_info = gomoku_room->getRoomStats();
                    // getRoomStats() 已经包含 roomId，无需重复设置
                    rooms_array.push_back(room_info);
                }
            }

            return rooms_array;
        }

        void GomokuServer::broadcastRoomEvent(const std::string& event_type,
                                              const std::string& room_id,
                                              const nlohmann::json& data,
                                              const std::string& reason) {
            nlohmann::json message;

            if (event_type == "created") {
                // 符合文档 §4.2 格式
                message = {
                    {"type", "room_added"},
                    {"data", {{"room", data}}}
                };
            } else if (event_type == "updated") {
                // 符合文档 §4.2 格式
                message = {
                    {"type", "room_updated"},
                    {"data", {{"roomId", room_id}, {"changes", data}}}
                };
            } else if (event_type == "removed") {
                // 符合文档 §4.2 格式（添加 reason 字段）
                std::string remove_reason = reason.empty() ? "room_closed" : reason;
                message = {
                    {"type", "room_removed"},
                    {"data", {{"roomId", room_id}, {"reason", remove_reason}}}
                };
            } else {
                LOG_WARNING("未知的房间事件类型: " + event_type);
                return;
            }

            broadcastToSubscribers(message);
            LOG_DEBUG("广播房间事件: " + event_type + ", room_id: " + room_id);
        }

        // ========== 玩家在线状态管理实现 ==========

        void GomokuServer::onPlayerOnline(const std::string& player_id) {
            // LOG_INFO("[OnlinePlayer] onPlayerOnline 被调用: player_id=" + player_id);

            bool is_new_player = false;
            {
                std::unique_lock<std::shared_mutex> lock(online_players_mutex_);

                // 检查玩家是否已经在线（重连场景）
                auto it = online_players_.find(player_id);
                if (it != online_players_.end()) {
                    // 玩家已在线，仅更新上线时间（重连）
                    it->second.online_time = std::chrono::system_clock::now();
                    // LOG_INFO("[OnlinePlayer] 玩家重连（不增加计数）: " + player_id +
                    //          ", 当前在线人数: " + std::to_string(online_players_.size()));
                    return;  // 不增加计数
                }

                // 新玩家上线
                OnlinePlayerInfo info;
                info.player_id = player_id;
                info.online_time = std::chrono::system_clock::now();
                info.current_room_id = "";
                online_players_[player_id] = info;
                is_new_player = true;

                // LOG_INFO("[OnlinePlayer] 新玩家加入在线列表: " + player_id +
                //          ", 在线人数变为: " + std::to_string(online_players_.size()));
            }

            // 只有新玩家才增加活跃计数
            if (is_new_player) {
                incrementActivePlayerCount(player_id);
            }

            // 打印当前所有在线玩家
            // {
            //     std::shared_lock<std::shared_mutex> lock(online_players_mutex_);
            //     std::string all_players;
            //     for (const auto& [id, _] : online_players_) {
            //         if (!all_players.empty()) all_players += ", ";
            //         all_players += id;
            //     }
            //     LOG_INFO("[OnlinePlayer] 当前所有在线玩家: [" + all_players + "]");
            // }
        }

        void GomokuServer::onPlayerOffline(const std::string& player_id) {
            // LOG_INFO("[OnlinePlayer] onPlayerOffline 被调用: player_id=" + player_id);

            // 广播玩家下线事件
            broadcastPlayerOffline(player_id);

            bool was_online = false;
            {
                std::unique_lock<std::shared_mutex> lock(online_players_mutex_);
                auto it = online_players_.find(player_id);
                if (it != online_players_.end()) {
                    online_players_.erase(it);
                    was_online = true;  // 只有真正移除了玩家才标记
                    // LOG_INFO("[OnlinePlayer] 玩家从在线列表移除: " + player_id +
                    //          ", 在线人数变为: " + std::to_string(online_players_.size()));
                } else {
                    LOG_WARNING("[OnlinePlayer] 玩家不在在线列表中: " + player_id +
                                ", 当前在线人数: " + std::to_string(online_players_.size()));
                }
            }

            // 取消房间列表订阅
            unsubscribeRoomList(player_id);

            // 只有玩家确实在线时才减少计数
            if (was_online) {
                decrementActivePlayerCount(player_id);
            }

            // 打印当前所有在线玩家
            // {
            //     std::shared_lock<std::shared_mutex> lock(online_players_mutex_);
            //     std::string all_players;
            //     for (const auto& [id, _] : online_players_) {
            //         if (!all_players.empty()) all_players += ", ";
            //         all_players += id;
            //     }
            //     LOG_INFO("[OnlinePlayer] 当前所有在线玩家: [" + all_players + "]");
            // }
        }

        void GomokuServer::updateOnlinePlayerId(const std::string& temp_id, const std::string& real_id) {
            // LOG_INFO("[OnlinePlayer] 更新玩家ID: " + temp_id + " -> " + real_id);

            std::unique_lock<std::shared_mutex> lock(online_players_mutex_);

            // 检查临时ID是否存在
            auto it = online_players_.find(temp_id);
            if (it == online_players_.end()) {
                // 临时ID不存在，可能已经用真实ID在线，或者从未上线
                if (online_players_.find(real_id) != online_players_.end()) {
                    // LOG_INFO("[OnlinePlayer] 真实ID已在线，无需更新: " + real_id);
                } else {
                    LOG_WARNING("[OnlinePlayer] 临时ID不存在，无法更新: " + temp_id);
                }
                return;
            }

            // 将临时ID的信息移动到真实ID
            OnlinePlayerInfo info = it->second;
            info.player_id = real_id;  // 更新player_id

            // 先删除临时ID，再添加真实ID
            online_players_.erase(it);
            online_players_[real_id] = info;

            // LOG_INFO("[OnlinePlayer] ID更新成功: " + temp_id + " -> " + real_id +
            //          ", 当前在线人数: " + std::to_string(online_players_.size()));
        }

        bool GomokuServer::isPlayerOnline(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(online_players_mutex_);
            return online_players_.find(player_id) != online_players_.end();
        }

        int GomokuServer::getOnlinePlayerCount() const {
            std::shared_lock<std::shared_mutex> lock(online_players_mutex_);
            return static_cast<int>(online_players_.size());
        }

        std::vector<std::string> GomokuServer::getOnlinePlayerIds() const {
            std::vector<std::string> ids;
            std::shared_lock<std::shared_mutex> lock(online_players_mutex_);
            for (const auto& pair : online_players_) {
                ids.push_back(pair.first);
            }
            return ids;
        }

        void GomokuServer::broadcastPlayerOnline(const std::string& player_id, const nlohmann::json& player_info) {
            nlohmann::json message = {
                {"type", "player_online"},
                {"data", {
                    {"playerId", player_id},
                    {"playerInfo", player_info},
                    {"onlineCount", getOnlinePlayerCount()},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                }}
            };
            broadcastToSubscribers(message);
            LOG_DEBUG("广播玩家上线: " + player_id);
        }

        void GomokuServer::broadcastPlayerOffline(const std::string& player_id) {
            nlohmann::json message = {
                {"type", "player_offline"},
                {"data", {
                    {"playerId", player_id},
                    {"onlineCount", getOnlinePlayerCount() - 1},  // 减1因为还没移除
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                }}
            };
            broadcastToSubscribers(message);
            LOG_DEBUG("广播玩家下线: " + player_id);
        }

        std::string GomokuServer::getPlayerRoomId(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(player_room_mutex_);
            auto it = player_room_mapping_.find(player_id);
            return it != player_room_mapping_.end() ? it->second : "";
        }

        void GomokuServer::updateDefaultGameConfig(const GomokuConfig& config) {
            if (GomokuLogic::validateConfig(config)) {
                gomoku_config_.default_game_config = config;
                LOG_INFO("更新默认游戏配置");
            } else {
                LOG_WARNING("无效的游戏配置，更新失败");
            }
        }

        nlohmann::json GomokuServer::getGameStatistics() const {
            nlohmann::json stats = nlohmann::json::object();

            // 获取权威的在线玩家数（来自 online_players_ 映射）
            int online_count = getOnlinePlayerCount();
            int atomic_count = active_player_count_.load();

            // 一致性检查：如果两个计数不一致，记录警告
            if (online_count != atomic_count) {
                LOG_WARNING("玩家计数不一致: online_players_.size()=" + std::to_string(online_count) +
                           ", active_player_count_=" + std::to_string(atomic_count));
            }

            // 五子棋特定统计 - 使用 online_players_.size() 作为权威来源
            stats["gomokuStats"] = nlohmann::json{
                {"totalGamesPlayed", total_games_played_.load()},
                {"totalGamesFinished", total_games_finished_.load()},
                {"totalMovesMade", total_moves_made_.load()},
                {"activeRooms", static_cast<int>(rooms_.size())},
                {"activePlayers", online_count},  // 使用 getOnlinePlayerCount() 作为权威来源
                {"peakPlayers", peak_player_count_.load()},
                {"maxConcurrentGames", gomoku_config_.max_concurrent_games},
                {"allowSpectators", gomoku_config_.allow_spectators},
                {"enableRanking", gomoku_config_.enable_ranking}
            };

            // 添加WebSocket统计
        if (gomoku_websocket_) {
            stats["websocketStats"] = gomoku_websocket_->getHandlerStats();
        }

            return stats;
        }

        nlohmann::json GomokuServer::getAllRoomsStatus() const {
            nlohmann::json rooms_status = nlohmann::json::array();
            
            std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
            for (const auto& pair : rooms_) {
                auto gomoku_room = std::dynamic_pointer_cast<GomokuRoom>(pair.second);
                if (gomoku_room) {
                    rooms_status.push_back(gomoku_room->getFullGameState());
                }
            }
            
            return rooms_status;
        }

        bool GomokuServer::forceCloseRoom(const std::string& room_id, const std::string& reason) {
            auto room = getGomokuRoom(room_id);
            if (!room) {
                return false;
            }

            // 通知房间内所有玩家
            room->broadcastToPlayers(nlohmann::json{
                {"type", "room_force_closed"},
                {"reason", reason}
            });

            room->forceStop(reason);
            
            // 从房间管理中移除
            {
                std::unique_lock<std::shared_mutex> lock(rooms_mutex_);
                rooms_.erase(room_id);
            }

            // 清理玩家映射
            {
                std::unique_lock<std::shared_mutex> lock(player_room_mutex_);
                for (auto it = player_room_mapping_.begin(); it != player_room_mapping_.end();) {
                    if (it->second == room_id) {
                        it = player_room_mapping_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            LOG_INFO("强制关闭房间: " + room_id + ", 原因: " + reason);
            return true;
        }

        int GomokuServer::broadcastAnnouncement(const std::string& announcement) {
            nlohmann::json announcement_msg = {
                {"type", "system_announcement"},
                {"message", announcement},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };

            int count = 0;
            if (gomoku_websocket_) {
                count = gomoku_websocket_->broadcastToAll(announcement_msg);
            }

            LOG_INFO("广播系统公告给 " + std::to_string(count) + " 个玩家: " + announcement);
            return count;
        }

        // 私有方法实现
        bool GomokuServer::initializeDatabase() {
            try {
                LOG_INFO("开始初始化五子棋数据库系统");
                
                // 创建MySQL连接池
                // common::config::DatabaseConfig db_config;
                mysql_pool_ = std::make_shared<common::database::MySQLPool>(gomoku_config_.database_config);
                mysql_pool_->start();
                if (!mysql_pool_->isRunning()) {
                    LOG_ERROR("MySQL连接池初始化失败");
                    return false;
                }
                
                // 创建数据库访问层
                database_ = std::make_shared<GomokuDatabase>(mysql_pool_);
                if (!database_->initialize()) {
                    LOG_ERROR("五子棋数据库初始化失败");
                    return false;
                }
                
                LOG_INFO("五子棋数据库系统初始化成功");
                return true;
                
            } catch (const std::exception& e) {
                LOG_ERROR("数据库初始化异常: " + std::string(e.what()));
                return false;
            }
        }

        bool GomokuServer::initializeRedis() {
            try {
                if (gomoku_config_.redis_config.redis_host.empty()) {
                    LOG_INFO("Redis缓存未配置，跳过初始化");
                    return true;
                }

                LOG_INFO("开始初始化Redis缓存系统");

                // 创建Redis连接池配置
                // common::config::RedisConfig redis_config;
                redis_pool_ = std::make_shared<common::database::RedisPool>(gomoku_config_.redis_config);
                redis_pool_->start();

                if (!redis_pool_->isRunning()) {
                    LOG_WARNING("Redis连接池初始化失败，将以无缓存模式运行");
                    redis_pool_.reset();
                    return false;
                }

                // 测试Redis连接
                {
                    common::database::RedisConnectionGuard redis_guard(*redis_pool_);
                    if (!redis_guard.isValid()) {
                        LOG_WARNING("Redis连接测试失败，将以无缓存模式运行");
                        redis_pool_.reset();
                        return false;
                    }

                    // 设置一个测试键值对
                    if (!redis_guard->set("gomoku:test", "connection_ok", 60)) {
                        LOG_WARNING("Redis写入测试失败，将以无缓存模式运行");
                        redis_pool_.reset();
                        return false;
                    }
                }

                LOG_INFO("Redis缓存系统初始化成功");
                return true;

            } catch (const std::exception& e) {
                LOG_WARNING("Redis初始化异常，将以无缓存模式运行: " + std::string(e.what()));
                redis_pool_.reset();
                return false;
            }
        }

        bool GomokuServer::initializeThreadPool() {
            try {
                LOG_INFO("开始初始化统一线程池");
                
                // 🔧 修复：使用正确的ThreadPool构造函数
                common::config::ThreadPoolConfig config = gomoku_config_.thread_pool_config;
                // 验证配置
                config.validate();
                
                // 创建线程池（构造函数自动启动）
                thread_pool_ = std::make_shared<common::thread_pool::ThreadPool>(config);
                
                LOG_INFO("统一线程池初始化成功，线程数: " + std::to_string(gomoku_config_.thread_pool_config.core_pool_size) +
                        ", 队列大小: " + std::to_string(gomoku_config_.thread_pool_config.queue_capacity));
                
                return true;
                
            } catch (const std::exception& e) {
                LOG_ERROR("初始化线程池异常: " + std::string(e.what()));
                thread_pool_.reset();
                return false;
            }
        }

        /**
         * @brief 设置 CORS 中间件
         */
        void GomokuServer::setupCorsMiddleware() {
            if (!http_server_) {
                return;
            }

            LOG_DEBUG("设置 CORS 中间件...");

            // 创建 CORS 中间件函数
            auto cors_middleware = [this](const common::http::HttpRequest& request, common::http::HttpResponse& response) {
                // 处理 OPTIONS 预检请求
                if (request.getMethod() == common::http::HttpMethod::OPTIONS) {
                    response.setHeader("Access-Control-Allow-Origin", "*");
                    response.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH");
                    response.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Accept, Origin");
                    response.setHeader("Access-Control-Max-Age", "86400");
                    response.setStatus(200);
                    response.setBody("");
                    return false; // 停止处理
                }

                // 为其他请求设置 CORS 头
                std::string origin = request.getHeader("origin");
                if (!origin.empty()) {
                    response.setHeader("Access-Control-Allow-Origin", origin);
                } else {
                    response.setHeader("Access-Control-Allow-Origin", "*");
                }
                response.setHeader("Access-Control-Allow-Credentials", "true");

                return true; // 继续处理
            };

            http_server_->use(cors_middleware, "CorsMiddleware");
            LOG_DEBUG("CORS 中间件设置完成");
        }

        void GomokuServer::initializeHttpRoutes() {
            if (!http_server_) {
                return;
            }

            // ========== 系统级 API (统一前缀 /api/v1/gomoku/) ==========
            http_server_->route("GET", "/api/v1/gomoku/health",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleHealthCheck(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/stats",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetGameStats(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/service/endpoints",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetServiceEndpoints(req, res);
                });

            // ========== 房间管理 API ==========
            http_server_->route("POST", "/api/v1/gomoku/rooms",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleCreateGomokuRoom(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/rooms",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetRoomList(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/rooms/:room_id",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetRoomDetails(req, res);
                });

            http_server_->route("POST", "/api/v1/gomoku/rooms/:room_id/join",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleJoinRoom(req, res);
                });

            http_server_->route("POST", "/api/v1/gomoku/rooms/:room_id/leave",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleLeaveRoom(req, res);
                });

            http_server_->route("POST", "/api/v1/gomoku/rooms/:room_id/start",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleStartGame(req, res);
                });

            // ========== 排行榜 API ==========
            http_server_->route("GET", "/api/v1/gomoku/leaderboard",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetLeaderboard(req, res);
                });

            // ========== 匹配系统 API ==========
            http_server_->route("POST", "/api/v1/gomoku/match/start",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleMatchStart(req, res);
                });

            http_server_->route("POST", "/api/v1/gomoku/match/cancel",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleMatchCancel(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/match/status",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetMatchStatus(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/match/pool",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetMatchPoolStatus(req, res);
                });

            // ========== 服务管理 API ==========
            http_server_->route("POST", "/api/v1/gomoku/announcement",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleBroadcastAnnouncement(req, res);
                });

            http_server_->route("GET", "/api/v1/gomoku/server/status",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleGetServerStatus(req, res);
                });

            // ========== 管理员 API ==========
            http_server_->route("POST", "/api/v1/gomoku/admin/reset-stats",
                [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
                    handleResetPlayerStats(req, res);
                });

            LOG_INFO("五子棋HTTP路由初始化完成，共注册 17 个路由（含匹配系统API）");
        }

        void GomokuServer::setupWebSocketCallbacks() {
            if (!gomoku_websocket_) {
                return;
            }

            // 🔧 修复：移除重复的连接回调设置
            // 连接回调已在WebSocket服务初始化时设置，避免重复调用
            
            // 只设置游戏特定的回调（如果需要的话）
            // 注意：这些回调也可能在初始化时已经设置，需要检查是否重复
            
            LOG_INFO("🔧 WebSocket回调设置完成（已修复重复设置问题）");
        }

        void GomokuServer::handleCreateGomokuRoom(const common::http::HttpRequest& req, 
                                                 common::http::HttpResponse& res) {
            nlohmann::json request_data;
            if (!parseJsonFromRequest(req, request_data)) {
                sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                return;
            }

            try {
                std::string creator_id = request_data.value("creatorId", "");
                if (creator_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_CREATOR_ID", "缺少创建者ID");
                    return;
                }

                std::string room_id = generateRoomId();
                GomokuConfig config = gomoku_config_.default_game_config;
                
                if (request_data.contains("config")) {
                    config = GomokuConfig::fromJson(request_data["config"]);
                }

                auto room = createGomokuRoom(room_id, creator_id, config);
                if (room) {
                    // 🔧 修复：创建房间后立即将创建者加入房间并分配棋子
                    if (handlePlayerJoinRoom(creator_id, room_id)) {
                        // 为创建者分配黑子（第一个玩家）
                        assignPieceImmediately(room, creator_id);
                        
                        // 获取分配的棋子类型
                        PieceType assigned_piece = room->getPlayerPiece(creator_id);
                        std::string piece_type = (assigned_piece == PieceType::BLACK) ? "black" : 
                                                (assigned_piece == PieceType::WHITE) ? "white" : "none";
                        
                    sendJsonResponse(res, 201, {
                        {"roomId", room_id},
                        {"creatorId", creator_id},
                        {"config", config.toJson()},
                            {"pieceType", piece_type},
                            {"message", "房间创建成功，创建者已自动加入"},
                            {"roomInfo", {
                                {"currentPlayers", room->getPlayerCount()},
                                {"maxPlayers", room->getMaxPlayers()},
                                {"roomState", room->getGamePhaseString()}
                            }}
                        });
                        
                        LOG_INFO("房间创建成功，创建者已加入: " + creator_id + " -> " + room_id + 
                               ", 棋子: " + piece_type);
                    } else {
                        // 如果创建者加入失败，删除房间
                        {
                            std::unique_lock<std::shared_mutex> lock(rooms_mutex_);
                            rooms_.erase(room_id);
                        }
                        sendErrorResponse(res, 500, "CREATOR_JOIN_FAILED", "创建者加入房间失败");
                    }
                } else {
                    sendErrorResponse(res, 500, "ROOM_CREATION_FAILED", "房间创建失败");
                }

            } catch (const std::exception& e) {
                LOG_ERROR("创建房间处理失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetRoomList(const common::http::HttpRequest& req, 
                                           common::http::HttpResponse& res) {
            try {
                // 🔧 完整的查询参数解析（匹配文档规范）
                bool include_private = req.getQueryParam("include_private") == "true";
                int page = std::stoi(req.getQueryParam("page", "1"));
                int limit = std::stoi(req.getQueryParam("limit", "20"));
                std::string game_mode = req.getQueryParam("game_mode", "all");
                std::string room_state = req.getQueryParam("room_state", "all");
                
                // 🔧 参数验证和范围限制
                page = std::max(1, page);
                limit = std::max(1, std::min(limit, 100)); // 最大100，如文档所述
                
                LOG_DEBUG("Room list query - page: " + std::to_string(page) + 
                         ", limit: " + std::to_string(limit) + 
                         ", game_mode: " + game_mode + 
                         ", room_state: " + room_state + 
                         ", include_private: " + (include_private ? "true" : "false"));

                // 🔧 获取完整房间列表进行过滤
                auto all_rooms = getRoomList(include_private);
                nlohmann::json filtered_rooms = nlohmann::json::array();
                
                // 🔧 应用筛选条件
                for (const auto& room_json : all_rooms) {
                    bool match = true;
                    
                    // 游戏模式筛选
                    if (game_mode != "all") {
                        std::string room_game_mode = room_json.value("gameMode", "freestyle");
                        if (room_game_mode != game_mode) {
                            match = false;
                        }
                    }
                    
                    // 房间状态筛选 
                    if (room_state != "all") {
                        std::string current_state = room_json.value("roomState", "waiting");
                        if (current_state != room_state) {
                            match = false;
                        }
                    }
                    
                    if (match) {
                        filtered_rooms.push_back(room_json);
                    }
                }
                
                // 🔧 计算分页
                int total_count = filtered_rooms.size();
                int total_pages = (total_count + limit - 1) / limit; // 向上取整
                int start_index = (page - 1) * limit;
                int end_index = std::min(start_index + limit, total_count);
                
                nlohmann::json paged_rooms = nlohmann::json::array();
                for (int i = start_index; i < end_index; ++i) {
                    paged_rooms.push_back(filtered_rooms[i]);
                }
                
                // 🔧 返回完整的分页响应（匹配文档格式）
                sendJsonResponse(res, 200, {
                    {"rooms", paged_rooms},
                    {"count", paged_rooms.size()},
                    {"totalCount", total_count},
                    {"page", page},
                    {"totalPages", total_pages}
                });

            } catch (const std::exception& e) {
                LOG_ERROR("获取房间列表失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetRoomDetails(const common::http::HttpRequest& req,
                                              common::http::HttpResponse& res) {
            try {
                // 从路径中提取房间ID - 使用正确的路径模式
                std::string room_id = extractPathParam(req.getPath(), "/api/v1/gomoku/rooms/:room_id", "room_id");
                if (room_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_ROOM_ID", "缺少房间ID");
                    return;
                }

                auto room = getGomokuRoom(room_id);
                if (!room) {
                    sendErrorResponse(res, 404, "ROOM_NOT_FOUND", "房间不存在");
                    return;
                }

                // 🔧 修复：构建结构清晰的响应数据，避免字段重复
                auto game_state = room->getGameSpecificState();
                
                // 增强房间基础信息 - 安全地获取时间戳
                auto now = std::chrono::system_clock::now();
                auto last_activity = room->getLastActivity();
                
                int64_t last_activity_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    last_activity.time_since_epoch()).count();
                
                // 🔧 修复：直接获取创建时间，避免使用getRoomInfo()导致字段重复
                auto created_at = room->getCreatedAt();
                int64_t created_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    created_at.time_since_epoch()).count();
                
                // 🔧 修复：构建清晰的响应结构，统一字段命名，避免重复
                nlohmann::json response = {
                    // 房间基础信息
                    {"roomId", room->getRoomId()},
                    {"creatorId", room->getCreatorId()},
                    {"gameType", static_cast<int>(room->getGameType())},
                    {"maxPlayers", room->getMaxPlayers()},
                    {"currentPlayers", room->getPlayerCount()},  // 统一使用驼峰格式
                    {"state", static_cast<int>(room->getState())},
                    {"stateDescription", game_state.value("gamePhase", "unknown")},
                    
                    // 时间信息（格式化为易于处理的格式）
                    {"timestamps", {
                        {"createdAt", created_time},
                        {"lastActivity", last_activity_time},
                        {"serverTime", std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count()}
                    }},
                    
                    // 游戏特定信息（避免嵌套重复）
                    {"gameConfig", game_state.value("config", nlohmann::json{})},
                    {"boardState", game_state.value("board", nlohmann::json{})},
                    {"currentTurn", game_state.value("currentTurn", "")},
                    {"playerPieces", game_state.value("playerPieces", nlohmann::json{})}
                };
                
                // 🔧 修复：添加玩家详细信息，确保创建者排在第一位
                auto players_info = nlohmann::json::array();
                auto all_player_ids = room->getPlayerIds();
                std::string creator_id = room->getCreatorId();
                
                // 构建排序后的玩家ID列表：创建者优先
                std::vector<std::string> sorted_player_ids;
                
                // 1. 先添加创建者
                if (std::find(all_player_ids.begin(), all_player_ids.end(), creator_id) != all_player_ids.end()) {
                    sorted_player_ids.push_back(creator_id);
                }
                
                // 2. 再添加其他玩家
                for (const auto& player_id : all_player_ids) {
                    if (player_id != creator_id) {
                        sorted_player_ids.push_back(player_id);
                    }
                }
                
                // 3. 按排序后的顺序构建玩家信息
                for (const auto& player_id : sorted_player_ids) {
                    PieceType piece_type = room->getPlayerPiece(player_id);
                    std::string piece_color;
                    
                    // 🔧 修复：正确处理棋子颜色，包括EMPTY情况
                    switch (piece_type) {
                        case PieceType::BLACK:
                            piece_color = "black";
                            break;
                        case PieceType::WHITE:
                            piece_color = "white";
                            break;
                        case PieceType::EMPTY:
                        default:
                            piece_color = "none";  // 未分配棋子
                            break;
                    }
                    
                    nlohmann::json player_info = {
                        {"playerId", player_id},
                        {"pieceType", static_cast<int>(piece_type)},
                        {"pieceColor", piece_color},
                        {"isCreator", player_id == creator_id}  // 🔧 修复：直接比较，确保准确性
                    };
                    // TODO: 可以从用户服务获取更详细的用户信息
                    players_info.push_back(player_info);
                }
                response["players"] = players_info;
                
                // 观战者信息
                response["spectators"] = {
                    {"count", room->getSpectatorCount()},
                    {"limit", gomoku_config_.default_game_config.maxSpectators},
                    {"ids", room->getSpectatorIds()}
                };
                
                // 添加实时游戏分析（如果游戏正在进行）
                if (auto logic = room->getGomokuLogic()) {
                    if (logic->isGameRunning()) {
                        response["gameAnalysis"] = {
                            {"positionAnalysis", logic->analyzePosition()},
                            {"isGameRunning", true}
                        };
                        
                        // 添加推荐移动
                        auto black_moves = logic->getRecommendedMoves(PieceType::BLACK, 3);
                        auto white_moves = logic->getRecommendedMoves(PieceType::WHITE, 3);
                        
                        nlohmann::json black_array = nlohmann::json::array();
                        for (const auto& pos : black_moves) {
                            black_array.push_back(pos.toJson());
                        }
                        
                        nlohmann::json white_array = nlohmann::json::array();
                        for (const auto& pos : white_moves) {
                            white_array.push_back(pos.toJson());
                        }
                        
                        response["recommendations"] = {
                            {"black", black_array},
                            {"white", white_array}
                        };
                    } else {
                        response["gameAnalysis"] = {
                            {"isGameRunning", false}
                        };
                    }
                }
                
                sendJsonResponse(res, 200, response);

            } catch (const std::exception& e) {
                LOG_ERROR("获取房间详情失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetGameStats(const common::http::HttpRequest& req, 
                                            common::http::HttpResponse& res) {
            try {
                sendJsonResponse(res, 200, getGameStatistics());

            } catch (const std::exception& e) {
                LOG_ERROR("获取游戏统计失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleBroadcastAnnouncement(const common::http::HttpRequest& req, 
                                                     common::http::HttpResponse& res) {
            nlohmann::json request_data;
            if (!parseJsonFromRequest(req, request_data)) {
                sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                return;
            }

            try {
                // 🔧 解析完整的公告参数（匹配文档规范）
                std::string message = request_data.value("message", "");
                if (message.empty()) {
                    sendErrorResponse(res, 400, "MISSING_MESSAGE", "缺少公告内容");
                    return;
                }
                
                // 🔧 公告类型：info/warning/maintenance/event
                std::string type = request_data.value("type", "info");
                if (type != "info" && type != "warning" && type != "maintenance" && type != "event") {
                    sendErrorResponse(res, 400, "INVALID_TYPE", "无效的公告类型");
                    return;
                }
                
                // 🔧 显示持续时间(毫秒)，默认10秒
                int duration = request_data.value("duration", 10000);
                duration = std::max(1000, std::min(duration, 300000)); // 1秒到5分钟
                
                // 🔧 目标房间列表，空数组表示全服
                nlohmann::json target_rooms = request_data.value("targetRooms", nlohmann::json::array());
                
                // 🔧 优先级：low/normal/high
                std::string priority = request_data.value("priority", "normal");
                if (priority != "low" && priority != "normal" && priority != "high") {
                    priority = "normal";
                }
                
                // 🔧 生成广播ID
                std::string broadcast_id = "broadcast_" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                
                LOG_INFO("Broadcasting announcement - ID: " + broadcast_id + 
                        ", Type: " + type + ", Priority: " + priority + 
                        ", Duration: " + std::to_string(duration) + "ms");
                
                // 🔧 构建增强的公告消息
                nlohmann::json announcement_data = {
                    {"type", "server_announcement"},
                    {"data", {
                        {"broadcastId", broadcast_id},
                        {"message", message},
                        {"announcementType", type},
                        {"duration", duration},
                        {"priority", priority},
                        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()}
                    }}
                };
                
                // 🔧 发送公告逻辑
                int sent_count = 0;
                int target_count = 0;
                
                if (target_rooms.empty()) {
                    // 全服广播
                    if (gomoku_websocket_) {
                        sent_count = gomoku_websocket_->broadcastToAll(announcement_data);
                        target_count = getActivePlayerCount();
                    }
                    LOG_INFO("Sent announcement to all players: " + std::to_string(sent_count));
                } else {
                    // 特定房间广播
                    for (const auto& room_id_json : target_rooms) {
                        if (room_id_json.is_string()) {
                            std::string room_id = room_id_json.get<std::string>();
                            auto room = getGomokuRoom(room_id);
                            if (room) {
                                room->broadcastToPlayers(announcement_data);
                                int room_players = room->getPlayerCount() + room->getSpectatorCount();
                                sent_count += room_players;
                                target_count += room_players;
                            }
                        }
                    }
                    LOG_INFO("Sent announcement to specific rooms: " + std::to_string(sent_count));
                }
                
                // 🔧 返回完整的广播响应（匹配文档格式）
                sendJsonResponse(res, 200, {
                    {"message", "公告发送成功"},
                    {"broadcastId", broadcast_id},
                    {"targetCount", target_count},
                    {"sentCount", sent_count},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                });

            } catch (const std::exception& e) {
                LOG_ERROR("广播公告失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        bool GomokuServer::handleGameAction(const std::string& player_id, const nlohmann::json& action) {
            try {
                std::string room_id = getPlayerRoomId(player_id);
                if (room_id.empty()) {
                    LOG_WARNING("玩家不在任何房间中: " + player_id);
                    return false;
                }

                auto room = getGomokuRoom(room_id);
                if (!room) {
                    LOG_WARNING("房间不存在: " + room_id);
                    return false;
                }

                bool result = room->processGameAction(player_id, action);
                if (result) {
                    // 🔧 修复：安全地获取action type
                    std::string action_type = action.value("type", "");
                    recordGameStats("game_action", {
                        {"playerId", player_id},
                        {"roomId", room_id},
                        {"action", action_type}
                    });
                    
                    if (action_type == constants::MSG_PLACE_PIECE) {
                        total_moves_made_++;
                    }
                }

                return result;

            } catch (const std::exception& e) {
                LOG_ERROR("处理游戏操作失败: " + std::string(e.what()) + ", 玩家: " + player_id);
                return false;
            }
        }

        bool GomokuServer::handleRoomOperation(const std::string& player_id, const nlohmann::json& operation) {
            try {
                std::string op_type = operation.value("type", "");

                if (op_type == "create_room") {
                    std::string room_id = generateRoomId();
                    GomokuConfig config = gomoku_config_.default_game_config;

                    if (operation.contains("config")) {
                        config = GomokuConfig::fromJson(operation["config"]);
                    }

                    auto room = createGomokuRoom(room_id, player_id, config);
                    if (!room) {
                        return false;
                    }

                    // 创建者自动加入房间
                    if (!handlePlayerJoinRoom(player_id, room_id)) {
                        LOG_WARNING("创建者自动加入房间失败: " + player_id + " -> " + room_id);
                        // 不返回 false，因为房间已创建成功
                    }

                    return true;

                } else if (op_type == "join_room") {
                    std::string room_id = operation.value("roomId", "");
                    return handlePlayerJoinRoom(player_id, room_id);
                    
                } else if (op_type == "leave_room") {
                    return handlePlayerLeaveRoom(player_id);
                    
                } else if (op_type == "spectate_room") {
                    std::string room_id = operation.value("roomId", "");
                    return handleSpectatorJoin(player_id, room_id);
                    
                } else if (op_type == "stop_spectating") {
                    return handleSpectatorLeave(player_id);

                } else if (op_type == "reconnect") {
                    return handlePlayerReconnect(player_id);

                } else if (op_type == "set_ready") {
                    // 处理准备状态设置
                    std::string room_id = getPlayerRoomId(player_id);
                    if (room_id.empty()) {
                        LOG_WARNING("玩家不在任何房间中，无法设置准备状态: " + player_id);
                        return false;
                    }

                    auto room = getGomokuRoom(room_id);
                    if (!room) {
                        LOG_WARNING("房间不存在，无法设置准备状态: " + room_id);
                        return false;
                    }

                    bool ready = operation.value("ready", true);
                    return room->setPlayerReady(player_id, ready);
                }

                // [新增] 处理开始游戏操作（房主手动开始）
                else if (op_type == "start_game") {
                    std::string room_id = operation.value("roomId", "");
                    if (room_id.empty()) {
                        room_id = getPlayerRoomId(player_id);
                    }

                    if (room_id.empty()) {
                        LOG_WARNING("玩家不在任何房间中，无法开始游戏: " + player_id);
                        return false;
                    }

                    auto room = getGomokuRoom(room_id);
                    if (!room) {
                        LOG_WARNING("房间不存在，无法开始游戏: " + room_id);
                        return false;
                    }

                    // 检查是否是房主
                    if (room->getCreatorId() != player_id) {
                        LOG_WARNING("非房主无法开始游戏: " + player_id + " (房主: " + room->getCreatorId() + ")");
                        return false;
                    }

                    // 检查是否可以开始游戏
                    if (!room->canStartGame()) {
                        LOG_WARNING("游戏条件不满足，无法开始: " + room_id);
                        return false;
                    }

                    // 开始游戏
                    if (room->startGame()) {
                        recordGameStats("game_started", {{"roomId", room_id}, {"type", "manual_start"}});
                        LOG_INFO("房主手动开始游戏成功: " + player_id + " -> " + room_id);
                        return true;
                    } else {
                        LOG_ERROR("游戏开始失败: " + room_id);
                        return false;
                    }
                }

                return false;

            } catch (const std::exception& e) {
                LOG_ERROR("处理房间操作失败: " + std::string(e.what()) + ", 玩家: " + player_id);
                return false;
            }
        }

        void GomokuServer::updatePlayerRoomMapping(const std::string& player_id, const std::string& room_id) {
            std::unique_lock<std::shared_mutex> lock(player_room_mutex_);
            
            if (room_id.empty()) {
                player_room_mapping_.erase(player_id);
            } else {
                player_room_mapping_[player_id] = room_id;
            }
        }

        bool GomokuServer::validatePlayerPermission(const std::string& player_id, const std::string& action) {
            // 简化的权限验证
            return isPlayerAuthenticated(player_id);
        }

        void GomokuServer::recordGameStats(const std::string& event, const nlohmann::json& data) {
            LOG_DEBUG("游戏统计: " + event + (data.empty() ? "" : " - " + data.dump()));
            
            if (event == "game_started") {
                total_games_played_++;
            }
        }

        void GomokuServer::cleanupPlayerResources(const std::string& player_id) {
            handlePlayerLeaveRoom(player_id);
            handleSpectatorLeave(player_id);
        }

        bool GomokuServer::parseJsonFromRequest(const common::http::HttpRequest& req, nlohmann::json& json_data) {
            try {
                std::string body = req.getBody();
                if (body.empty()) {
                    return false;
                }
                json_data = nlohmann::json::parse(body);
                return true;
            } catch (const nlohmann::json::parse_error&) {
                return false;
            }
        }

        void GomokuServer::sendJsonResponse(common::http::HttpResponse& res, int status_code, 
                                          const nlohmann::json& data) {
            res.setStatus(status_code);
            res.setHeader("Content-Type", "application/json; charset=utf-8");
            res.setBody(data.dump());
        }

        void GomokuServer::sendErrorResponse(common::http::HttpResponse& res, int status_code,
                                           const std::string& error_code, const std::string& error_message) {
            sendJsonResponse(res, status_code, {
                {"error", {
                    {"code", error_code},
                    {"message", error_message}
                }}
            });
        }

        std::string GomokuServer::extractPathParam(const std::string& path, const std::string& pattern, 
                                                  const std::string& param_name) {
            // 简单的路径参数提取实现
            // 将pattern中的:param_name替换为(.+)，然后进行正则匹配
            std::string regex_pattern = pattern;
            std::string placeholder = ":" + param_name;
            
            size_t pos = regex_pattern.find(placeholder);
            if (pos != std::string::npos) {
                regex_pattern.replace(pos, placeholder.length(), "([^/]+)");
                
                try {
                    std::regex regex(regex_pattern);
                    std::smatch match;
                    if (std::regex_match(path, match, regex) && match.size() > 1) {
                        return match[1].str();
                    }
                } catch (const std::exception&) {
                    // 正则表达式失败，返回空字符串
                }
            }
            
            return "";
        }

        // 静态方法实现
        std::unique_ptr<GomokuServer> GomokuServer::createFromConfig(const std::string& config_file) {
            try {
                LOG_INFO("正在从配置文件创建服务器: " + config_file);
                
                // 使用新的配置加载方法
                auto config = GomokuServerConfig::fromConfigFile(config_file);
                
                // 验证配置
                try {
                    if (!config.validate()) {
                        LOG_WARNING("配置验证失败，使用默认配置");
                        config = GomokuServer::getDefaultConfig();
                        config.config_file = config_file;
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("配置验证异常: " + std::string(e.what()) + "，使用默认配置");
                    config = GomokuServer::getDefaultConfig();
                    config.config_file = config_file;
                }
                
                // 创建服务器实例
                return std::unique_ptr<GomokuServer>(new GomokuServer(config));

            } catch (const std::exception& e) {
                LOG_ERROR("从配置文件创建服务器失败: " + std::string(e.what()));
                return nullptr;
            }
        }

        GomokuServerConfig GomokuServer::getDefaultConfig() {
            GomokuServerConfig config;
            
            // 🔧 修复：确保默认配置包含有效的MySQL配置
            config.database_config.mysql_host = "localhost";
            config.database_config.mysql_port = 3306;
            config.database_config.mysql_user = "root";  // 🔧 修复：设置默认用户名
            config.database_config.mysql_password = "";
            config.database_config.mysql_database = "gomoku_game_db";
            
            // 确保Redis配置也有合理默认值
            config.redis_config.redis_host = "localhost";
            config.redis_config.redis_port = 6379;
            config.redis_config.redis_password = "";
            config.redis_config.redis_database = 0;
            
            // 使用结构体中定义的其他默认值
            return config;
        }

        bool GomokuServer::validateConfig(const GomokuServerConfig& config) {
            return config.validate() && 
                   config.max_concurrent_games > 0 &&
                   !config.game_data_path.empty();
        }

        void GomokuServer::incrementActivePlayerCount(const std::string& player_id) {
            int current_count = active_player_count_.fetch_add(1) + 1;
            
            // 更新峰值记录
            int current_peak = peak_player_count_.load();
            while (current_count > current_peak) {
                if (peak_player_count_.compare_exchange_weak(current_peak, current_count)) {
                    LOG_INFO("活跃玩家数量达到新峰值: " + std::to_string(current_count));
                    break;
                }
            }
            
            LOG_DEBUG("玩家连接: " + player_id + ", 当前活跃玩家数: " + std::to_string(current_count));
        }

        void GomokuServer::decrementActivePlayerCount(const std::string& player_id) {
            int current_count = active_player_count_.fetch_sub(1) - 1;
            if (current_count < 0) {
                active_player_count_.store(0);  // 防止负数
                current_count = 0;
                LOG_WARNING("活跃玩家计数出现负数，已重置为0。玩家: " + player_id);
            }
            
            LOG_DEBUG("玩家断开: " + player_id + ", 当前活跃玩家数: " + std::to_string(current_count));
        }

        // 🔧 新增：缺失的API处理函数实现
        void GomokuServer::handleJoinRoom(const common::http::HttpRequest& req,
                                        common::http::HttpResponse& res) {
            try {
                // 从路径中提取房间ID - 使用正确的路径模式
                std::string room_id = extractPathParam(req.getPath(), "/api/v1/gomoku/rooms/:room_id/join", "room_id");
                if (room_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_ROOM_ID", "缺少房间ID");
                    return;
                }

                // 解析请求JSON
                nlohmann::json request_data;
                if (!parseJsonFromRequest(req, request_data)) {
                    sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                    return;
                }

                // 提取必需参数
                std::string player_id = request_data.value("playerId", "");
                if (player_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_PLAYER_ID", "缺少玩家ID");
                    return;
                }

                // 提取可选参数
                std::string role = request_data.value("role", "player");  // 默认为玩家
                std::string password = request_data.value("password", "");  // 房间密码（暂未实现）

                // 验证角色参数
                if (role != "player" && role != "spectator") {
                    sendErrorResponse(res, 400, "INVALID_ROLE", "无效的角色类型，必须是player或spectator");
                    return;
                }

                // 获取房间信息
                auto room = getGomokuRoom(room_id);
                if (!room) {
                    sendErrorResponse(res, 404, "ROOM_NOT_FOUND", "房间不存在");
                    return;
                }

                // 根据角色执行不同的加入逻辑
                bool join_success = false;
                std::string piece_type = "none";
                std::string actual_role = role;

                if (role == "spectator") {
                    // 作为观战者加入
                    auto session = getOrCreatePlayerSession(player_id);
                    if (session) {
                        join_success = room->addSpectator(player_id, session);
                        if (join_success) {
                            updatePlayerRoomMapping(player_id, room_id);
                            if (gomoku_websocket_) {
                                gomoku_websocket_->setPlayerRoom(player_id, room_id);
                            }
                            LOG_INFO("观战者加入房间成功: " + player_id + " -> " + room_id);
                        }
                    }
                } else {
                    // 作为玩家加入
                    if (room->getPlayerCount() >= 2) {
                        // 房间已满，自动转为观战者
                        auto session = getOrCreatePlayerSession(player_id);
                        if (session && room->addSpectator(player_id, session)) {
                            join_success = true;
                            actual_role = "spectator";  // 实际角色变为观战者
                            updatePlayerRoomMapping(player_id, room_id);
                            if (gomoku_websocket_) {
                                gomoku_websocket_->setPlayerRoom(player_id, room_id);
                            }
                            LOG_INFO("房间已满，用户自动转为观战者: " + player_id + " -> " + room_id);
                        }
                    } else {
                        // 正常加入为玩家
                        join_success = handlePlayerJoinRoom(player_id, room_id);
                        if (join_success) {
                            // 🔧 关键改进：立即分配棋子颜色
                            assignPieceImmediately(room, player_id);
                            
                            // 获取分配的棋子类型
                            PieceType assigned_piece = room->getPlayerPiece(player_id);
                            switch (assigned_piece) {
                                case PieceType::BLACK:
                                    piece_type = "black";
                                    break;
                                case PieceType::WHITE:
                                    piece_type = "white";
                                    break;
                                default:
                                    piece_type = "none";
                                    break;
                            }
                            
                            LOG_INFO("玩家加入房间成功: " + player_id + " -> " + room_id + 
                                   ", 棋子: " + piece_type);
                        }
                    }
                }

                if (join_success) {
                    // 🔧 完善的响应结构
                    nlohmann::json response = {
                        {"message", "成功加入房间"},
                        {"roomId", room_id},
                        {"playerId", player_id},
                        {"role", actual_role}
                    };

                    // 只有玩家才返回棋子类型
                    if (actual_role == "player") {
                        response["pieceType"] = piece_type;
                    }

                    // 添加房间当前状态信息
                    response["roomInfo"] = {
                        {"currentPlayers", room->getPlayerCount()},
                        {"maxPlayers", room->getMaxPlayers()},
                        {"spectatorCount", room->getSpectatorCount()},
                        {"roomState", room->getGamePhaseString()}
                    };

                    sendJsonResponse(res, 200, response);
                } else {
                    std::string error_msg = (role == "spectator") ? "观战加入失败" : "玩家加入失败";
                    sendErrorResponse(res, 400, "JOIN_FAILED", error_msg);
                }

            } catch (const std::exception& e) {
                LOG_ERROR("处理加入房间请求失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        /**
         * @brief 立即为加入的玩家分配棋子
         */
        void GomokuServer::assignPieceImmediately(std::shared_ptr<GomokuRoom> room, 
                                                const std::string& player_id) {
            if (!room) {
                LOG_WARNING("无效的房间指针，跳过棋子分配");
                return;
            }

            // 🔧 修复：检查玩家是否已有棋子分配
            PieceType current_piece = room->getPlayerPiece(player_id);
            if (current_piece != PieceType::EMPTY) {
                LOG_INFO("玩家已分配棋子，跳过重复分配: " + player_id + 
                        ", 棋子: " + std::to_string(static_cast<int>(current_piece)));
                return;
            }

            auto player_ids = room->getPlayerIds();
            int player_count = player_ids.size();

            // 🔧 改进：更智能的棋子分配逻辑
            if (player_count == 1) {
                // 第一个玩家（通常是房主）分配黑子
                if (room->setPlayerPiece(player_id, PieceType::BLACK)) {
                    LOG_INFO("分配黑子给第一位玩家: " + player_id);
                } else {
                    LOG_WARNING("分配黑子失败: " + player_id);
                }
            } else if (player_count == 2) {
                // 第二个玩家分配白子
                if (room->setPlayerPiece(player_id, PieceType::WHITE)) {
                LOG_INFO("分配白子给第二位玩家: " + player_id);
                } else {
                    LOG_WARNING("分配白子失败: " + player_id);
                }
            } else {
                // 超过2个玩家时，尝试分配空闲的棋子
                if (room->getPlayerPiece(room->getPlayerIds()[0]) == PieceType::EMPTY) {
                    // 第一个玩家没有棋子，分配黑子
                    if (room->setPlayerPiece(room->getPlayerIds()[0], PieceType::BLACK)) {
                        LOG_INFO("补分配黑子给第一位玩家: " + room->getPlayerIds()[0]);
                    }
                }
                LOG_WARNING("房间已有" + std::to_string(player_count) + "个玩家，无法为 " + 
                          player_id + " 分配棋子");
            }
        }

        void GomokuServer::handleLeaveRoom(const common::http::HttpRequest& req, 
                                         common::http::HttpResponse& res) {
            try {
                // 从路径中提取房间ID - 使用正确的路径模式
                std::string room_id = extractPathParam(req.getPath(), "/api/v1/gomoku/rooms/:room_id/leave", "room_id");
                if (room_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_ROOM_ID", "缺少房间ID");
                    return;
                }

                nlohmann::json request_data;
                if (!parseJsonFromRequest(req, request_data)) {
                    sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                    return;
                }

                std::string player_id = request_data.value("playerId", "");
                if (player_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_PLAYER_ID", "缺少玩家ID");
                    return;
                }

                if (handlePlayerLeaveRoom(player_id)) {
                    sendJsonResponse(res, 200, {
                        {"message", "离开房间成功"},
                        {"roomId", room_id},
                        {"playerId", player_id}
                    });
                } else {
                    sendErrorResponse(res, 400, "LEAVE_FAILED", "离开房间失败");
                }

            } catch (const std::exception& e) {
                LOG_ERROR("处理离开房间请求失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleStartGame(const common::http::HttpRequest& req, 
                                         common::http::HttpResponse& res) {
            try {
                // 从路径中提取房间ID - 使用正确的路径模式
                std::string room_id = extractPathParam(req.getPath(), "/api/v1/gomoku/rooms/:room_id/start", "room_id");
                if (room_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_ROOM_ID", "缺少房间ID");
                    return;
                }

                nlohmann::json request_data;
                if (!parseJsonFromRequest(req, request_data)) {
                    sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                    return;
                }

                std::string player_id = request_data.value("playerId", "");
                if (player_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_PLAYER_ID", "缺少玩家ID");
                    return;
                }

                auto room = getGomokuRoom(room_id);
                if (!room) {
                    sendErrorResponse(res, 404, "ROOM_NOT_FOUND", "房间不存在");
                    return;
                }

                // 🔧 增强：详细的开始游戏条件检查和错误提示
                if (!room->canPlayerStartGame(player_id)) {
                    // 详细分析失败原因
                    if (player_id != room->getCreatorId()) {
                        sendErrorResponse(res, 403, "PERMISSION_DENIED", 
                                        "只有房主才能开始游戏，当前房主ID: " + room->getCreatorId());
                        return;
                    }
                    
                    if (room->getPlayerCount() != 2) {
                        sendErrorResponse(res, 400, "INSUFFICIENT_PLAYERS", 
                                        "需要2个玩家才能开始游戏，当前玩家数: " + 
                                        std::to_string(room->getPlayerCount()));
                        return;
                    }
                    
                    // 检查玩家准备状态
                    sendErrorResponse(res, 400, "PLAYERS_NOT_READY", 
                                    "所有玩家必须先发送准备就绪状态才能开始游戏");
                    return;
                }
                
                if (room->startGame()) {
                    // 记录游戏开始统计
                    recordGameStats("game_started", {{"roomId", room_id}, {"type", "http_api_start"}});

                    // 广播房间状态更新（游戏开始）
                    broadcastRoomEvent("updated", room_id, room->getRoomStats());

                    sendJsonResponse(res, 200, {
                        {"message", "游戏开始成功"},
                        {"roomId", room_id},
                        {"gameState", room->getGameSpecificState()}
                    });
                } else {
                    sendErrorResponse(res, 500, "START_GAME_FAILED", 
                                    "开始游戏失败，可能是游戏逻辑初始化错误");
                }

            } catch (const std::exception& e) {
                LOG_ERROR("处理开始游戏请求失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetLeaderboard(const common::http::HttpRequest& req, 
                                              common::http::HttpResponse& res) {
            try {
                // 🔧 解析完整的查询参数（匹配文档规范）
                int limit = std::stoi(req.getQueryParam("limit", "50"));
                std::string game_mode = req.getQueryParam("mode", "all");
                std::string period = req.getQueryParam("period", "all");
                
                // 🔧 参数验证和范围限制
                limit = std::max(1, std::min(limit, 100)); // 最大100，如文档所述
                
                // 🔧 验证游戏模式参数
                if (game_mode != "all" && game_mode != "freestyle" && 
                    game_mode != "renju" && game_mode != "swap2") {
                    game_mode = "all";
                }
                
                // 🔧 验证时间周期参数
                if (period != "all" && period != "daily" && 
                    period != "weekly" && period != "monthly") {
                    period = "all";
                }
                
                LOG_DEBUG("Leaderboard query - limit: " + std::to_string(limit) + 
                         ", mode: " + game_mode + ", period: " + period);

                nlohmann::json leaderboard = nlohmann::json::array();
                
                // 🔧 尝试从数据库查询排行榜数据
                if (database_ && gomoku_config_.enable_database) {
                    try {
                        LOG_DEBUG("Querying leaderboard from database");
                        
                        // 🔧 转换string到GameMode枚举
                        GameMode db_game_mode = GameMode::FREESTYLE;
                        if (game_mode == "freestyle") db_game_mode = GameMode::FREESTYLE;
                        else if (game_mode == "renju") db_game_mode = GameMode::RENJU;
                        else if (game_mode == "swap2") db_game_mode = GameMode::SWAP2;
                        else if (game_mode == "pro") db_game_mode = GameMode::PRO;
                        else if (game_mode == "tournament") db_game_mode = GameMode::TOURNAMENT;
                        
                        auto db_leaderboard = database_->getLeaderboard(db_game_mode, limit, 0);
                        
                        // 转换数据库结果为JSON格式
                        for (const auto& entry : db_leaderboard) {
                            nlohmann::json entry_json = nlohmann::json::object({
                                {"rank", entry.rank_position},
                                {"playerId", entry.user_id},
                                {"playerName", entry.username.empty() ? entry.user_id : entry.username},
                                {"rating", entry.current_rating},
                                {"wins", entry.wins},
                                {"losses", entry.losses},
                                {"draws", entry.draws},
                                {"winRate", entry.win_rate},
                                {"gameMode", gameModeToString(entry.game_mode)}
                            });
                            leaderboard.push_back(entry_json);
                        }
                        
                        LOG_INFO("✅ 从数据库获取排行榜成功，记录数: " + std::to_string(leaderboard.size()));
                        
                    } catch (const std::exception& db_e) {
                        LOG_ERROR("❌ 数据库查询排行榜失败: " + std::string(db_e.what()));
                        leaderboard.clear();
                    }
                }
                
                // 🔧 删除模拟数据：如果数据库为空，返回空列表
                if (leaderboard.empty()) {
                    LOG_INFO("📊 排行榜当前为空 - 数据库中暂无游戏记录");
                }

                // 🔧 返回完整的排行榜响应（匹配文档格式）
                sendJsonResponse(res, 200, {
                    {"leaderboard", leaderboard},
                    {"count", leaderboard.size()},
                    {"gameMode", game_mode},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                });

            } catch (const std::exception& e) {
                LOG_ERROR("获取排行榜失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        // ==================== 匹配系统 HTTP 处理器实现 ====================

        void GomokuServer::handleMatchStart(const common::http::HttpRequest& req,
                                           common::http::HttpResponse& res) {
            nlohmann::json request_data;
            if (!parseJsonFromRequest(req, request_data)) {
                sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                return;
            }

            try {
                std::string user_id = request_data.value("userId", "");
                if (user_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_USER_ID", "缺少用户ID");
                    return;
                }

                // 检查用户是否已在匹配池中
                if (match_making_manager_ && match_making_manager_->isInPool(user_id)) {
                    sendErrorResponse(res, 409, "ALREADY_IN_POOL", "用户已在匹配池中");
                    return;
                }

                // 检查用户是否已在房间中
                std::string current_room = getPlayerRoomId(user_id);
                if (!current_room.empty()) {
                    sendErrorResponse(res, 409, "ALREADY_IN_ROOM", "用户已在房间中，请先离开当前房间");
                    return;
                }

                // 解析匹配参数
                std::string mode_str = request_data.value("mode", "ranked");
                MatchMode mode = (mode_str == "casual") ? MatchMode::CASUAL : MatchMode::RANKED;

                std::string game_mode_str = request_data.value("gameMode", "freestyle");
                GameMode game_mode = GomokuConfig::stringToGameMode(game_mode_str);

                // 获取用户评分（从游戏数据服务获取或使用默认值）
                int rating = 1200;
                std::string username = request_data.value("username", user_id);

                auto* game_data_client = ServiceClientManager::getInstance().getGameDataServiceClient();
                if (game_data_client) {
                    auto profile = game_data_client->getUserGameProfile(user_id);
                    if (profile) {
                        rating = profile->current_rating;
                    }
                }

                // 创建匹配请求
                MatchRequest match_request(user_id, username, rating, mode);
                match_request.game_mode = game_mode;

                // 添加到匹配池
                if (match_making_manager_ && match_making_manager_->addRequest(match_request)) {
                    sendJsonResponse(res, 200, {
                        {"success", true},
                        {"message", "已加入匹配队列"},
                        {"data", {
                            {"userId", user_id},
                            {"mode", mode_str},
                            {"gameMode", game_mode_str},
                            {"rating", rating},
                            {"requestId", match_request.request_id}
                        }}
                    });

                    LOG_INFO("用户开始匹配: " + user_id + ", 模式: " + mode_str +
                            ", 评分: " + std::to_string(rating));
                } else {
                    sendErrorResponse(res, 500, "MATCH_START_FAILED", "加入匹配队列失败");
                }

            } catch (const std::exception& e) {
                LOG_ERROR("开始匹配处理失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleMatchCancel(const common::http::HttpRequest& req,
                                            common::http::HttpResponse& res) {
            nlohmann::json request_data;
            if (!parseJsonFromRequest(req, request_data)) {
                sendErrorResponse(res, 400, "INVALID_JSON", "无效的JSON格式");
                return;
            }

            try {
                std::string user_id = request_data.value("userId", "");
                if (user_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_USER_ID", "缺少用户ID");
                    return;
                }

                // 检查用户是否在匹配池中
                if (!match_making_manager_ || !match_making_manager_->isInPool(user_id)) {
                    sendErrorResponse(res, 404, "NOT_IN_POOL", "用户不在匹配池中");
                    return;
                }

                // 取消匹配
                if (match_making_manager_->cancelRequest(user_id)) {
                    sendJsonResponse(res, 200, {
                        {"success", true},
                        {"message", "已取消匹配"},
                        {"data", {
                            {"userId", user_id}
                        }}
                    });

                    LOG_INFO("用户取消匹配: " + user_id);
                } else {
                    sendErrorResponse(res, 500, "CANCEL_FAILED", "取消匹配失败");
                }

            } catch (const std::exception& e) {
                LOG_ERROR("取消匹配处理失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetMatchStatus(const common::http::HttpRequest& req,
                                               common::http::HttpResponse& res) {
            try {
                std::string user_id = req.getQueryParam("userId");

                if (user_id.empty()) {
                    sendErrorResponse(res, 400, "MISSING_USER_ID", "缺少用户ID参数");
                    return;
                }

                // 检查用户是否在匹配池中
                if (!match_making_manager_ || !match_making_manager_->isInPool(user_id)) {
                    sendJsonResponse(res, 200, {
                        {"success", true},
                        {"inPool", false},
                        {"message", "用户不在匹配池中"}
                    });
                    return;
                }

                // 获取等待时间
                int wait_seconds = match_making_manager_->getWaitTime(user_id);
                auto request = match_making_manager_->getRequest(user_id);

                nlohmann::json status_data = {
                    {"success", true},
                    {"inPool", true},
                    {"waitSeconds", wait_seconds},
                    {"poolSize", static_cast<int>(match_making_manager_->getPoolSize())}
                };

                if (request) {
                    status_data["request"] = request->toJson();
                }

                sendJsonResponse(res, 200, status_data);

            } catch (const std::exception& e) {
                LOG_ERROR("获取匹配状态失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetMatchPoolStatus(const common::http::HttpRequest& req,
                                                   common::http::HttpResponse& res) {
            try {
                if (!match_making_manager_) {
                    sendJsonResponse(res, 200, {
                        {"success", true},
                        {"running", false},
                        {"message", "匹配系统未初始化"}
                    });
                    return;
                }

                auto pool_status = match_making_manager_->getPoolStatus();
                auto statistics = match_making_manager_->getStatistics();

                sendJsonResponse(res, 200, {
                    {"success", true},
                    {"running", match_making_manager_->isRunning()},
                    {"poolStatus", pool_status.toJson()},
                    {"statistics", statistics},
                    {"config", match_making_manager_->getConfig().toJson()}
                });

            } catch (const std::exception& e) {
                LOG_ERROR("获取匹配池状态失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleMatchResult(const MatchResult& result) {
            try {
                LOG_INFO("匹配成功: " + result.match_id +
                        ", 玩家: " + result.players[0].user_id + " vs " + result.players[1].user_id +
                        ", 质量: " + std::to_string(static_cast<int>(result.quality)));

                // 1. 为匹配的玩家创建房间
                std::string room_id = generateRoomId();
                GomokuConfig config = gomoku_config_.default_game_config;
                config.gameMode = result.game_mode;

                // 根据匹配模式设置房间类型
                if (result.players[0].mode == MatchMode::RANKED) {
                    config.rankingEnabled = true;
                }

                auto room = createGomokuRoom(room_id, result.players[0].user_id, config);
                if (!room) {
                    LOG_ERROR("为匹配结果创建房间失败: " + result.match_id);
                    return;
                }

                // 2. 将两个玩家加入房间
                for (const auto& player : result.players) {
                    if (!handlePlayerJoinRoom(player.user_id, room_id)) {
                        LOG_ERROR("玩家加入房间失败: " + player.user_id);
                    }
                }

                // 3. 分配棋子
                assignPieceImmediately(room, result.players[0].user_id);
                assignPieceImmediately(room, result.players[1].user_id);

                // 4. 更新匹配结果中的房间信息
                const_cast<MatchResult&>(result).room_id = room_id;
                const_cast<MatchResult&>(result).room_created = true;

                // 5. 通过 WebSocket 通知两个玩家匹配成功
                if (gomoku_websocket_) {
                    for (const auto& player : result.players) {
                        gomoku_websocket_->sendMatchFound(player.user_id, result);
                    }

                    // 🔧 新增：发送完整房间信息给玩家（包含ready字段）
                    // 客户端无需发送 enter_room，服务端在匹配成功后自动推送 room_info
                    nlohmann::json room_info_msg = {
                        {"type", "room_info"},
                        {"data", room->getRoomInfo()}
                    };
                    for (const auto& player : result.players) {
                        gomoku_websocket_->sendToPlayer(player.user_id, room_info_msg);
                    }
                }

                // 6. 如果配置为自动开始，则启动游戏
                if (config.autoStartWhenFull && room->getPlayerCount() == 2) {
                    // 延迟启动游戏，给客户端时间准备
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.autoStartDelayMs));
                    if (room->startGame()) {
                        recordGameStats("game_started", {{"roomId", room_id}, {"type", "auto_start"}});
                        LOG_INFO("自动开始游戏，房间: " + room_id);
                    }
                }

                LOG_INFO("匹配房间创建成功: " + room_id);

            } catch (const std::exception& e) {
                LOG_ERROR("处理匹配结果失败: " + std::string(e.what()));
            }
        }

        void GomokuServer::handleGetServerStatus(const common::http::HttpRequest& req, 
                                               common::http::HttpResponse& res) {
            try {
                auto stats = getGameStatistics();
                auto config = getGomokuConfig();

                nlohmann::json server_status = {
                    {"server_name", "gomoku_server"},
                    {"version", "1.0.0"},
                    {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - server_start_time_).count()},
                    {"running", isRunning()},
                    {"config", {
                        {"host", config.network_config.bind_address},
                        {"port", config.network_config.listen_port},
                        {"websocket_port", config.websocket_port},
                        {"max_concurrent_games", config.max_concurrent_games},
                        {"allow_spectators", config.allow_spectators},
                        {"enable_ranking", config.enable_ranking}
                    }},
                    {"current_stats", stats},
                    {"features", {
                        {"game_modes", nlohmann::json::array({"freestyle", "renju", "swap2"})},
                        {"room_types", nlohmann::json::array({"casual", "ranked"})},
                        {"websocket", config.enable_websocket},
                        {"leaderboard", config.enable_ranking},
                        {"spectator_mode", config.allow_spectators},
                        {"replay_system", config.enable_replay}
                    }},
                    {"capacity", {
                        {"max_concurrent_games", config.max_concurrent_games},
                        {"max_players_per_room", 2},
                        {"max_spectators_per_room", config.default_game_config.maxSpectators},
                        {"current_load", calculateServerLoad()}  // 计算实际负载
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };

                sendJsonResponse(res, 200, server_status);

            } catch (const std::exception& e) {
                LOG_ERROR("获取服务器状态失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleGetServiceEndpoints(const common::http::HttpRequest& req, 
                                                   common::http::HttpResponse& res) {
            try {
                LOG_INFO("🔍 [SERVICE_ENDPOINTS] 处理端点查询请求");

                // 构建完整的端点信息 - 符合网关期望的格式
                nlohmann::json endpoints_response = {
                    {"success", true},
                    {"message", "端点信息获取成功"},
                    {"service_name", gomoku_config_.service_name},
                    {"service_version", "2.0.0"},
                    {"game_type", "gomoku"},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()},
                    {"endpoints", nlohmann::json::array({
                        // 房间管理端点
                        {
                            {"path", "/api/gomoku/rooms"},
                            {"method", "GET"},
                            {"description", "获取房间列表"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/rooms"},
                            {"method", "POST"},
                            {"description", "创建游戏房间"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/rooms/{room_id}"},
                            {"method", "GET"},
                            {"description", "获取房间详情"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/rooms/{room_id}/join"},
                            {"method", "POST"},
                            {"description", "加入游戏房间"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/rooms/{room_id}/leave"},
                            {"method", "POST"},
                            {"description", "离开游戏房间"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/rooms/{room_id}/start"},
                            {"method", "POST"},
                            {"description", "开始游戏"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        // 游戏数据端点
                        {
                            {"path", "/api/gomoku/leaderboard"},
                            {"method", "GET"},
                            {"description", "获取排行榜"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/stats"},
                            {"method", "GET"},
                            {"description", "获取游戏统计"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/gomoku/announcement"},
                            {"method", "POST"},
                            {"description", "发布系统公告"},
                            {"auth_required", true},
                            {"version", "v1"}
                        },
                        // 通用版本化API端点
                        {
                            {"path", "/api/v1/rooms"},
                            {"method", "GET"},
                            {"description", "通用版本-房间列表"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/v1/rooms/{room_id}"},
                            {"method", "GET"},
                            {"description", "通用版本-房间详情"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/v1/server/status"},
                            {"method", "GET"},
                            {"description", "服务器状态信息"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        {
                            {"path", "/api/v1/service/endpoints"},
                            {"method", "GET"},
                            {"description", "服务端点信息 (此接口)"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        // 健康检查端点
                        {
                            {"path", "/health"},
                            {"method", "GET"},
                            {"description", "健康检查"},
                            {"auth_required", false},
                            {"version", "v1"}
                        },
                        // 管理员功能端点
                        {
                            {"path", "/api/gomoku/admin/reset-stats"},
                            {"method", "POST"},
                            {"description", "重置玩家统计（管理员功能）"},
                            {"auth_required", true},
                            {"version", "v1"}
                        }
                    })},
                    {"websocket", {
                        {"enabled", gomoku_config_.enable_websocket},
                        {"port", gomoku_config_.websocket_port},
                        {"endpoint", "ws://" + gomoku_config_.network_config.bind_address + ":" + 
                                   std::to_string(gomoku_config_.websocket_port)},
                        {"protocols", nlohmann::json::array({"gomoku-v1", "game-v1", "qt6-optimized"})}
                    }},
                    {"capacity", {
                        {"max_concurrent_games", gomoku_config_.max_concurrent_games},
                        {"max_players_per_room", 2},
                        {"max_spectators_per_room", 50},
                        {"supports_spectators", gomoku_config_.allow_spectators}
                    }},
                    {"features", {
                        {"game_modes", nlohmann::json::array({"freestyle", "renju", "swap2", "pro", "tournament"})},
                        {"room_types", nlohmann::json::array({"casual", "ranked"})},
                        {"time_controls", true},
                        {"ranking_system", gomoku_config_.enable_ranking},
                        {"replay_system", gomoku_config_.enable_replay},
                        {"chat_support", true},
                        {"undo_support", true}
                    }}
                };

                LOG_INFO("✅ [SERVICE_ENDPOINTS] 返回 " + std::to_string(endpoints_response["endpoints"].size()) + " 个端点信息");
                
                // 设置适当的缓存头，允许网关缓存端点信息
                res.setHeader("Cache-Control", "public, max-age=300"); // 5分钟缓存
                res.setHeader("Content-Type", "application/json; charset=utf-8");
                
                sendJsonResponse(res, 200, endpoints_response);

            } catch (const std::exception& e) {
                LOG_ERROR("❌ [SERVICE_ENDPOINTS] 获取服务端点信息失败: " + std::string(e.what()));
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "服务器内部错误");
            }
        }

        void GomokuServer::handleResetPlayerStats(const common::http::HttpRequest& req, 
                                                 common::http::HttpResponse& res) {
            try {
                LOG_WARNING("🔧 [ADMIN] 收到重置玩家统计请求");

                // 1. 安全检查：验证管理员权限
                std::string auth_header = req.getHeader("Authorization");
                std::string client_ip = req.getHeader("X-Real-IP");
                if (client_ip.empty()) {
                    client_ip = req.getHeader("X-Forwarded-For");
                }
                if (client_ip.empty()) {
                    client_ip = "unknown";
                }
                
                // 简化的权限验证（生产环境应该使用JWT或API密钥）
                bool is_authorized = false;
                if (auth_header == "Bearer admin-reset-token-2025" || 
                    auth_header == "Bearer gomoku-admin-2025") {
                    is_authorized = true;
                                    } else {
                    // 检查是否为本地管理请求
                    if (client_ip == "127.0.0.1" || client_ip == "::1" || client_ip == "localhost") {
                        // 允许本地管理员请求（但记录日志）
                        LOG_WARNING("🔧 [ADMIN] 本地管理员请求，跳过Token验证: " + client_ip);
                        is_authorized = true;
                    }
                }
                
                if (!is_authorized) {
                    LOG_ERROR("🚫 [ADMIN] 未授权的重置请求来自: " + client_ip + ", Auth: " + 
                             (auth_header.empty() ? "无" : "***"));
                    sendErrorResponse(res, 401, "UNAUTHORIZED", "需要管理员权限");
                    return;
                }

                // 2. 获取重置前的详细统计
                int old_active_count = active_player_count_.load();
                int old_peak_count = peak_player_count_.load();
                
                // 获取WebSocket相关统计（如果可用）
                int websocket_sessions = 0;
                int authenticated_players = 0;
                
                if (gomoku_websocket_) {
                    // 尝试获取WebSocket统计
                    try {
                        websocket_sessions = gomoku_websocket_->getOnlinePlayerCount();
                        // authenticated_players 暂时等于在线会话数
                        authenticated_players = websocket_sessions;
                        LOG_INFO("🔧 [ADMIN] WebSocket统计 - 在线会话: " + std::to_string(websocket_sessions) + 
                               ", 认证玩家: " + std::to_string(authenticated_players));
                    } catch (const std::exception& e) {
                        LOG_WARNING("🔧 [ADMIN] 无法获取WebSocket统计: " + std::string(e.what()));
                    }
                        } else {
                    LOG_INFO("🔧 [ADMIN] WebSocket处理器未初始化");
                }

                // 3. 执行统计重置
                active_player_count_.store(0);
                peak_player_count_.store(0);
                
                // 可选：重置游戏统计（如果需要完全重置）
                nlohmann::json request_data;
                bool reset_game_stats = false;
                if (parseJsonFromRequest(req, request_data)) {
                    reset_game_stats = request_data.value("reset_game_stats", false);
                    if (reset_game_stats) {
                        total_games_played_.store(0);
                        total_games_finished_.store(0);
                        total_moves_made_.store(0);
                        LOG_WARNING("🔧 [ADMIN] 游戏统计也已重置");
                    }
                }

                // 4. WebSocket会话清理（谨慎处理）
                bool websocket_cleanup_attempted = false;
                if (gomoku_websocket_ && old_active_count > 0) {
                    LOG_WARNING(std::string("🔧 [ADMIN] 重置前活跃计数: ") + 
                              std::to_string(old_active_count) + " - 这可能表明存在资源泄露");
                    
                    // 如果活跃计数异常高，记录为可能的泄露
                    if (old_active_count > 50) { // 假设正常情况下不会超过50个同时连接
                        websocket_cleanup_attempted = true;
                        LOG_WARNING("🔧 [ADMIN] 检测到可能的严重资源泄露，计数: " + 
                                  std::to_string(old_active_count));
                        
                        // 这里可以在未来添加更深入的清理逻辑
                        // 目前主要是记录和重置计数器
                    }
                }

                // 5. 记录详细的重置日志
                std::string reset_details = std::string("🔧 [ADMIN] 玩家统计重置完成: ") + 
                                          "活跃玩家 " + std::to_string(old_active_count) + "→0, " +
                                          "峰值玩家 " + std::to_string(old_peak_count) + "→0";
                if (reset_game_stats) {
                    reset_details += ", 游戏统计已重置";
                }
                if (websocket_cleanup_attempted) {
                    reset_details += ", WebSocket状态已检查";
                }
                LOG_WARNING(reset_details + " [请求来源: " + client_ip + "]");

                // 6. 构建详细的响应数据
                nlohmann::json reset_result = {
                    {"success", true},
                    {"message", "玩家统计已重置"},
                    {"reset_data", {
                        {"old_active_players", old_active_count},
                        {"old_peak_players", old_peak_count},
                        {"new_active_players", 0},
                        {"new_peak_players", 0},
                        {"websocket_handler_available", gomoku_websocket_ != nullptr},
                        {"websocket_sessions_before_reset", websocket_sessions},
                        {"authenticated_players_before_reset", authenticated_players},
                        {"potential_leak_detected", old_active_count > 10},
                        {"game_stats_reset", reset_game_stats}
                    }},
                    {"operation_details", {
                        {"request_source", client_ip},
                        {"websocket_cleanup_attempted", websocket_cleanup_attempted},
                        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()}
                    }}
                };

                sendJsonResponse(res, 200, reset_result);

                    } catch (const std::exception& e) {
                LOG_ERROR(std::string("❌ [ADMIN] 重置玩家统计失败: ") + e.what());
                sendErrorResponse(res, 500, "INTERNAL_ERROR", "重置统计失败: " + std::string(e.what()));
            }
        }

        void GomokuServer::handleGameFinished(const std::string& room_id, GameResult result, 
                                            const std::string& winner_id) {
            try {
                // 🔧 修复：避免在回调中重新获取房间，防止死锁
                // 直接使用传入的参数进行数据库操作
                if (gomoku_config_.enable_database && database_) {
                    // ⚠️ 注意：由于无法获取房间详细信息，暂时只记录基本信息
                    GameRecord game_record;
                    game_record.room_id = room_id;
                    game_record.game_result = result;
                    game_record.winner_id = winner_id.empty() ? std::nullopt : std::make_optional(winner_id);
                    game_record.server_id = gomoku_config_.service_name;
                    game_record.server_version = "2.0.0";
                    game_record.game_type = "casual";  // 默认值
                    game_record.game_mode = GameMode::FREESTYLE;  // 默认值
                    
                    // 尝试创建游戏记录
                    int64_t game_id = database_->createGameRecord(game_record);
                    if (game_id > 0) {
                        // 如果创建成功，更新游戏结果
                        std::string win_type = "five_in_row";  // 默认获胜方式
                        if (result == GameResult::DRAW) {
                            win_type = "draw";
                        }
                        
                        database_->updateGameResult(game_id, result, game_record.winner_id, win_type, std::nullopt);
                        LOG_INFO("游戏记录保存成功，房间: " + room_id + ", 游戏ID: " + std::to_string(game_id));
                    } else {
                        LOG_ERROR("保存游戏记录到数据库失败，房间: " + room_id);
                    }
                } else {
                    LOG_INFO("数据库未启用或不可用，跳过游戏记录保存");
                }

                // 更新统计信息
                total_games_finished_.fetch_add(1);
                
                LOG_INFO("游戏结束处理完成，房间: " + room_id + ", 结果: " + 
                        std::to_string(static_cast<int>(result)) + ", 获胜者: " + winner_id);

            } catch (const std::exception& e) {
                LOG_ERROR("处理游戏结束时发生异常: " + std::string(e.what()) + ", 房间: " + room_id);
            }
        }

        void GomokuServer::reportMatchCompletion() {
            if (!registry_client_) {
                LOG_DEBUG("服务注册客户端未设置，跳过对局报告");
                return;
            }

            try {
                bool success = registry_client_->reportMatchCompletion("gomoku", "gomoku_server");
                if (success) {
                    LOG_INFO("[MATCH_STATS] 对局完成已报告到服务注册中心");
                } else {
                    LOG_WARNING("[MATCH_STATS] 报告对局完成失败");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[MATCH_STATS] 报告对局完成异常: " + std::string(e.what()));
            }
        }

        void GomokuServer::handleGameFinishedWithContext(
            const std::string& room_id,
            const std::vector<std::string>& winners,
            const GameEndContext& context) {

            LOG_INFO("[SETTLEMENT] 处理游戏结束(带上下文): 房间 " + room_id +
                    ", 玩家数: " + std::to_string(context.players.size()) +
                    ", 时长: " + std::to_string(context.duration_seconds) + "秒");

            // 异步调用结算 API
            callGameSettlementApiAsync(context);
        }

        void GomokuServer::callGameSettlementApiAsync(const GameEndContext& context) {
            // 异步执行，不阻塞游戏主流程
            std::thread([this, context]() {
                try {
                    // 使用 GomokuServer 持有的 ServiceClientManager（通过 WebSocket Handler）
                    // 而非单例，因为单例未被初始化
                    auto service_client_manager = getServiceClientManager();
                    if (!service_client_manager) {
                        LOG_WARNING("[SETTLEMENT] ServiceClientManager 不可用，跳过结算");
                        return;
                    }

                    auto* client = service_client_manager->getGameDataServiceClient();
                    if (!client) {
                        LOG_WARNING("[SETTLEMENT] GameDataServiceClient 不可用，跳过结算");
                        return;
                    }

                    // 先获取玩家统计数据，丰富上下文
                    GameEndContext enriched_context = context;
                    for (auto& player : enriched_context.players) {
                        auto stats = client->getPlayerGameStats(player.user_id);
                        if (stats) {
                            player.rating_before = stats->current_rating;
                            player.games_played = stats->games_played;
                            player.win_streak = stats->win_streak;
                            player.tier_level = stats->tier_level;
                            player.is_first_win_today = stats->is_first_win_today;
                            player.games_today = stats->games_today;

                            LOG_DEBUG("[SETTLEMENT] 玩家 " + player.user_id +
                                    " 统计: rating=" + std::to_string(player.rating_before) +
                                    ", games=" + std::to_string(player.games_played) +
                                    ", streak=" + std::to_string(player.win_streak));
                        }
                    }

                    // 调用结算 API
                    auto response = client->submitGameSettlement(enriched_context);
                    if (response && response->success) {
                        handleSettlementResponse(enriched_context, *response);
                        LOG_INFO("[SETTLEMENT] 游戏结算成功，房间: " + enriched_context.room_id);
                    } else {
                        LOG_ERROR("[SETTLEMENT] 游戏结算失败: " +
                                (response ? response->error_message : "无响应"));
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("[SETTLEMENT] 结算 API 调用异常: " + std::string(e.what()));
                }
            }).detach();
        }

        void GomokuServer::handleSettlementResponse(
            const GameEndContext& context,
            const SettlementResponse& response) {

            LOG_INFO("[SETTLEMENT] 处理结算响应: 房间 " + context.room_id +
                    ", 结算玩家数: " + std::to_string(response.settlements.size()));

            // 将结算结果推送给每个玩家
            for (const auto& settlement : response.settlements) {
                std::string user_id = settlement.value("user_id", "");

                if (user_id.empty()) {
                    continue;
                }

                // 构建结算消息
                nlohmann::json msg = {
                    {"type", "game_settlement"},
                    {"data", settlement}
                };

                // 发送给对应玩家（通过 WebSocket Handler）
                if (gomoku_websocket_) {
                    gomoku_websocket_->sendToPlayer(user_id, msg);
                }

                LOG_INFO("[SETTLEMENT] 发送结算消息给玩家: " + user_id +
                        ", rating_change=" + std::to_string(settlement.value("rating_change", 0)));
            }
        }

        /**
         * @brief 计算服务器负载
         */
        double GomokuServer::calculateServerLoad() const {
            try {
                // 计算各项负载指标（使用基类的房间和玩家数据）
                int current_games = 0;
                int current_players = 0;
                
                // 统计活跃房间数量
                {
                    std::shared_lock<std::shared_mutex> lock(rooms_mutex_);
                    for (const auto& [room_id, room] : rooms_) {
                        if (room && room->getState() == game_base::RoomState::PLAYING) {
                            current_games++;
                        }
                    }
                }
                
                // 统计活跃玩家数量
                {
                    std::shared_lock<std::shared_mutex> lock(players_mutex_);
                    current_players = static_cast<int>(players_.size());
                }
                
                int max_games = gomoku_config_.max_concurrent_games;
                int max_players = max_games * 2; // 每个游戏最多2个玩家
                
                // 游戏负载 (当前游戏数 / 最大游戏数)
                double game_load = max_games > 0 ? static_cast<double>(current_games) / max_games : 0.0;
                
                // 玩家负载 (当前玩家数 / 最大玩家数)
                double player_load = max_players > 0 ? static_cast<double>(current_players) / max_players : 0.0;
                
                // 内存负载 (可以根据实际内存使用情况调整)
                // 这里使用一个简化的计算：基于活跃房间数的内存估算
                double memory_load = std::min(1.0, static_cast<double>(current_games) / (max_games * 0.8));
                
                // CPU负载 (简化计算，基于活跃会话和游戏数)
                double cpu_load = std::min(1.0, static_cast<double>(current_games + current_players) / (max_games + max_players));
                
                // 综合负载计算 (使用加权平均)
                double total_load = (game_load * 0.4 + player_load * 0.3 + memory_load * 0.2 + cpu_load * 0.1);
                
                // 确保负载值在0.0-1.0范围内
                total_load = std::max(0.0, std::min(1.0, total_load));
                
                LOG_DEBUG("服务器负载计算 - 游戏: " + std::to_string(current_games) + "/" + std::to_string(max_games) +
                         ", 玩家: " + std::to_string(current_players) + "/" + std::to_string(max_players) +
                         ", 总负载: " + std::to_string(total_load));
                
                return total_load;
                
            } catch (const std::exception& e) {
                LOG_ERROR("计算服务器负载失败: " + std::string(e.what()));
                return 0.0; // 发生错误时返回0负载
            }
        }

        // 🔧 修复：使用基类GameServerBase的start()方法，它提供完整的启动流程
        // 五子棋特定的初始化逻辑在initializeGameSpecific()中实现
        // 重复的函数定义已经移动到文件前面

        /**
         * @brief 重写基类的WebSocket初始化方法
         * @return 初始化成功返回true
         */
        bool GomokuServer::initializeWebSocket() {
            try {
                if (!gomoku_config_.enable_websocket) {
                    LOG_INFO("WebSocket已禁用，跳过初始化");
                    return true;
                }

                LOG_INFO("开始初始化五子棋WebSocket服务");
                
                // 🔧 修复：使用HTTP服务器的EventLoop而不是创建独立的EventLoop
                // 获取HTTP服务器的EventLoop用于WebSocket
                auto http_server_eventloop = http_server_ ? http_server_->getEventLoop() : nullptr;
                if (!http_server_eventloop) {
                    LOG_ERROR("HTTP服务器EventLoop不可用，WebSocket无法初始化");
                    return false;
                }

                // 🔧 修复：创建五子棋WebSocket处理器
                auto gomoku_websocket_handler = std::make_unique<GomokuWebSocketHandler>(http_server_eventloop);
                if (!gomoku_websocket_handler) {
                    LOG_ERROR("创建五子棋WebSocket处理器失败");
                    return false;
                }

                // 🔧 修复：调用WebSocket处理器的initialize方法
                if (!gomoku_websocket_handler->initialize(gomoku_config_.websocket_port)) {
                    LOG_ERROR("初始化五子棋WebSocket处理器失败");
                    return false;
                }

                // 🔧 关键修复：先设置回调，再转移所有权
                // 保存原始指针（在move之前）
                GomokuWebSocketHandler* ws_handler_ptr = gomoku_websocket_handler.get();
                
                // 将所有权转移给gomoku_websocket_
                gomoku_websocket_ = std::move(gomoku_websocket_handler);
                
                // 也将基类指针指向同一个对象
                websocket_handler_.reset(ws_handler_ptr);

                // 使用原始指针设置WebSocket回调
                if (ws_handler_ptr) {
                    ws_handler_ptr->setConnectionCallback(
                        [this](const std::string& player_id, bool connected) {
                            LOG_INFO("[ConnectionCallback] WebSocket连接状态变化: player_id=" + player_id +
                                     ", connected=" + (connected ? "true" : "false"));
                            if (connected) {
                                onPlayerOnline(player_id);
                            } else {
                                LOG_INFO("[ConnectionCallback] 玩家断开连接，先离开房间再下线: " + player_id);
                                handlePlayerLeaveRoom(player_id);
                                onPlayerOffline(player_id);
                            }
                        }
                    );

                    // 🔧 修复：设置玩家ID变更回调，用于更新在线玩家列表中的ID
                    ws_handler_ptr->setPlayerIdChangeCallback(
                        [this](const std::string& temp_id, const std::string& real_id) {
                            // LOG_INFO("[PlayerIdChange] 临时ID映射为真实ID: " + temp_id + " -> " + real_id);
                            updateOnlinePlayerId(temp_id, real_id);
                        }
                    );

                    ws_handler_ptr->setGameActionCallback(
                        [this](const std::string& player_id, const nlohmann::json& action) -> bool {
                            return handleGameAction(player_id, action);
                        }
                    );

                    ws_handler_ptr->setRoomOperationCallback(
                        [this](const std::string& player_id, const nlohmann::json& operation) -> bool {
                            return handleRoomOperation(player_id, operation);
                        }
                    );

                    // 设置房间列表订阅回调
                    ws_handler_ptr->setSubscriptionCallback(
                        [this](const std::string& player_id, bool subscribe) {
                            if (subscribe) {
                                subscribeRoomList(player_id);
                            } else {
                                unsubscribeRoomList(player_id);
                            }
                        }
                    );

                    // 设置获取房间列表数据回调
                    ws_handler_ptr->setRoomListDataCallback(
                        [this]() -> nlohmann::json {
                            return getRoomListData();
                        }
                    );

                    // ========== 设置匹配系统回调 ==========
                    // 设置开始匹配回调
                    ws_handler_ptr->setMatchStartCallback(
                        [this](const std::string& player_id, MatchMode mode, GameMode game_mode) -> bool {
                            if (!match_making_manager_) {
                                LOG_WARNING("匹配管理器未初始化，无法开始匹配");
                                return false;
                            }

                            // 获取玩家信息
                            auto player_info = gomoku_websocket_->getPlayerInfo(player_id);
                            if (!player_info) {
                                LOG_WARNING("无法获取玩家信息: " + player_id);
                                return false;
                            }

                            // 创建匹配请求
                            MatchRequest request(
                                player_id,
                                player_info->username,
                                player_info->rating,
                                mode
                            );
                            request.game_mode = game_mode;

                            bool success = match_making_manager_->addRequest(request);
                            if (success) {
                                LOG_INFO("玩家 " + player_id + " 成功加入匹配队列");
                            }
                            return success;
                        }
                    );

                    // 设置取消匹配回调
                    ws_handler_ptr->setMatchCancelCallback(
                        [this](const std::string& player_id) -> bool {
                            if (!match_making_manager_) {
                                return false;
                            }
                            bool success = match_making_manager_->cancelRequest(player_id);
                            if (success) {
                                LOG_INFO("玩家 " + player_id + " 已取消匹配");
                            }
                            return success;
                        }
                    );

                    // 设置获取匹配池状态回调
                    ws_handler_ptr->setMatchPoolStatusCallback(
                        [this]() -> MatchPoolStatus {
                            if (!match_making_manager_) {
                                return MatchPoolStatus{};
                            }
                            return match_making_manager_->getPoolStatus();
                        }
                    );

                    LOG_INFO("✅ WebSocket回调设置完成（含匹配系统回调）");
                }

                LOG_INFO("五子棋WebSocket服务初始化成功，端口: " + std::to_string(gomoku_config_.websocket_port));
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("初始化五子棋WebSocket服务失败: " + std::string(e.what()));
                return false;
            }
        }

    } // namespace gomoku
} // namespace game_services

