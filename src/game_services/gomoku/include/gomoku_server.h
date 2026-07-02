#pragma once

#include "game_server_base.h"
#include "gomoku_room.h"
#include "gomoku_websocket_handler.h"
#include "gomoku_types.h"
#include "gomoku_database.h"
#include "match_making_manager.h"
#include "common/thread_pool/thread_pool.h"
#include "common/config/config_manager.h"
#include "common/service_registry/service_registry_client.h"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <set>
#include <mutex>

/**
 * @file gomoku_server.h
 * @brief 五子棋游戏服务器类定义
 * @details 继承自GameServerBase，实现五子棋游戏服务器的完整功能
 */

namespace game_services {
    namespace gomoku {

        /**
         * @brief 五子棋游戏服务器配置
         * @details 基于ConfigManager的统一配置结构，类似于用户服务的配置模式
         */
        struct GomokuServerConfig {
            // 服务基本信息
            std::string service_name = "gomoku_service";
            std::string service_version = "2.0.0";
            std::string description = "五子棋游戏服务";
            std::string environment = "development";
            
            // [ARCH] 架构模式配置
            std::string architecture_mode = "simple";  // simple|standard|enterprise
            
            // [THREAD] 动态线程配置 (根据架构模式设置)
            struct {
                int worker_threads = 4;
                int io_threads = 2;
                int event_threads = 1;
            } threads;
            
            // [COMPONENT] 组件开关配置 (根据架构模式设置)
            struct {
                bool enable_kafka = false;
                bool enable_redis = false;
                bool enable_monitoring = false;
                bool enable_circuit_breaker = false;
                bool enable_time_wheel = false;
                bool enable_distributed_cache = false;
            } components;
            
            // [STORAGE] 动态存储配置 (根据架构模式设置)
            struct {
                int mysql_pool_size = 3;
                int redis_pool_size = 0;
            } storage;
            
            // 使用预定义的配置结构体
            common::config::NetworkConfig network_config;
            common::config::DatabaseConfig database_config;
            common::config::RedisConfig redis_config;
            common::config::ThreadPoolConfig thread_pool_config;
            
            // 五子棋特定配置
            GomokuConfig default_game_config;
            int max_concurrent_games = 100;
            bool allow_spectators = true;
            bool enable_ranking = true;
            bool enable_replay = false;
            std::string game_data_path = "data/gomoku";
            
            // 数据库持久化配置
            bool enable_database = true;
            bool save_game_records = true;
            bool save_move_history = true;
            bool save_chat_messages = true;
            bool enable_statistics = true;

            // 缓存配置
            bool enable_cache = true;
            bool cache_leaderboard = true;
            bool cache_user_stats = true;
            bool cache_room_states = true;
            int cache_expire_seconds = 3600;

            // 服务间通信配置
            std::string auth_service_url = "http://127.0.0.1:8083";    // 认证服务地址

            // 服务标签和元数据
            std::vector<std::string> service_tags = {"game", "gomoku", "chess"};
            nlohmann::json metadata = {
                {"team", "game-services"},
                {"maintainer", "game-microservices-team"},
                {"game_type", "gomoku"},
                {"version", "2.0.0"}
            };

            // WebSocket配置
            bool enable_websocket = true;
            int websocket_port = 8085;

            // 配置文件路径
            std::string config_file;

            /**
             * @brief 验证配置
             */
            bool validate() const {
                try {
                    // 验证各个配置模块
                    network_config.validate();
                    database_config.validate();
                    redis_config.validate();
                    thread_pool_config.validate();
                    
                    // 验证服务特定配置
                    if (service_name.empty()) {
                        LOG_ERROR("服务名称不能为空");
                        return false;
                    }
                    
                    if (max_concurrent_games <= 0) {
                        LOG_ERROR("最大并发游戏数必须大于0");
                        return false;
                    }
                    
                    if (websocket_port <= 0 || websocket_port > 65535) {
                        LOG_ERROR("WebSocket端口必须在1-65535之间");
                        return false;
                    }
                    
                    if (!GomokuLogic::validateConfig(default_game_config)) {
                        LOG_ERROR("默认游戏配置无效");
                        return false;
                    }
                    
                    return true;
                } catch (const std::exception& e) {
                    LOG_ERROR("配置验证失败: " + std::string(e.what()));
                    return false;
                }
            }
            
            /**
             * @brief 转换为JSON格式
             */
            nlohmann::json toJson() const;
            
            /**
             * @brief 从配置管理器加载配置
             */
            static GomokuServerConfig fromConfigManager();
            
            /**
             * @brief 从配置文件加载配置
             */
            static GomokuServerConfig fromConfigFile(const std::string& config_file);
        };

        /**
         * @brief 五子棋游戏服务器类
         * @details 继承自GameServerBase，提供完整的五子棋游戏服务
         */
        class GomokuServer : public game_base::GameServerBase {
        public:
            /**
             * @brief 构造函数
             * @param config 服务器配置
             */
            explicit GomokuServer(const GomokuServerConfig& config);

            // [FIX] 架构简化：移除DynamicConfigManager构造函数

            /**
             * @brief 虚析构函数
             */
            virtual ~GomokuServer();

            // 继承自GameServerBase的纯虚函数实现
            bool initializeGameSpecific() override;
            std::string getGameTypeName() const override;
            int getWebSocketPort() const override;
            std::shared_ptr<game_base::GameRoomBase> createGameRoom(
                const std::string& room_id,
                const std::string& creator_id,
                const nlohmann::json& config = {}) override;

            // [ARCH] 架构模式处理方法
            /**
             * @brief 应用架构模式配置
             * @param mode 架构模式 (simple|standard|enterprise)
             */
            void applyArchitectureMode(const std::string& mode);
            
            /**
             * @brief 检查组件是否应该启用
             * @param component_name 组件名称
             * @return 是否启用
             */
            bool shouldEnableComponent(const std::string& component_name) const;
            
            // [FIX] 使用基类的start()方法，通过initializeGameSpecific()实现游戏特定逻辑

            // 五子棋特定功能
            /**
             * @brief 创建五子棋房间
             * @param room_id 房间ID
             * @param creator_id 创建者ID
             * @param config 五子棋配置
             * @return 房间对象
             */
            std::shared_ptr<GomokuRoom> createGomokuRoom(const std::string& room_id,
                                                        const std::string& creator_id,
                                                        const GomokuConfig& config = GomokuConfig());

            /**
             * @brief 获取五子棋房间
             * @param room_id 房间ID
             * @return 房间对象，如果不存在则返回nullptr
             */
            std::shared_ptr<GomokuRoom> getGomokuRoom(const std::string& room_id);

            /**
             * @brief 处理玩家加入房间
             * @param player_id 玩家ID
             * @param room_id 房间ID
             * @return 是否成功
             */
            bool handlePlayerJoinRoom(const std::string& player_id, const std::string& room_id);

            /**
             * @brief 处理玩家离开房间
             * @param player_id 玩家ID
             * @return 是否成功
             */
            bool handlePlayerLeaveRoom(const std::string& player_id);

            /**
             * @brief 处理玩家断线重连
             * @param player_id 玩家ID
             * @return 是否成功
             */
            bool handlePlayerReconnect(const std::string& player_id);

            /**
             * @brief 处理观战者加入房间
             * @param spectator_id 观战者ID
             * @param room_id 房间ID
             * @return 是否成功
             */
            bool handleSpectatorJoin(const std::string& spectator_id, const std::string& room_id);

            /**
             * @brief 处理观战者离开房间
             * @param spectator_id 观战者ID
             * @return 是否成功
             */
            bool handleSpectatorLeave(const std::string& spectator_id);

            /**
             * @brief 获取房间列表
             * @param include_private 是否包含私人房间
             * @return 房间信息列表
             */
            std::vector<nlohmann::json> getRoomList(bool include_private = false) const;

            // ========== 房间列表订阅管理 ==========
            /**
             * @brief 订阅房间列表更新
             * @param player_id 玩家ID
             */
            void subscribeRoomList(const std::string& player_id);

            /**
             * @brief 取消订阅房间列表更新
             * @param player_id 玩家ID
             */
            void unsubscribeRoomList(const std::string& player_id);

            /**
             * @brief 检查玩家是否已订阅房间列表
             * @param player_id 玩家ID
             * @return 是否已订阅
             */
            bool isRoomListSubscriber(const std::string& player_id) const;

            /**
             * @brief 向所有订阅者广播消息
             * @param message 消息内容
             */
            void broadcastToSubscribers(const nlohmann::json& message);

            /**
             * @brief 获取房间列表数据（用于推送）
             * @return 房间列表JSON数组
             */
            nlohmann::json getRoomListData() const;

            /**
             * @brief 广播房间事件
             * @param event_type 事件类型 (created/updated/removed)
             * @param room_id 房间ID
             * @param data 事件数据
             * @param reason 原因（仅用于 removed 事件）
             */
            void broadcastRoomEvent(const std::string& event_type,
                                   const std::string& room_id,
                                   const nlohmann::json& data = {},
                                   const std::string& reason = "");

            // ========== 玩家在线状态管理 ==========
            /**
             * @brief 玩家上线通知
             * @param player_id 玩家ID
             */
            void onPlayerOnline(const std::string& player_id);

            /**
             * @brief 玩家下线通知
             * @param player_id 玩家ID
             */
            void onPlayerOffline(const std::string& player_id);

            /**
             * @brief 更新在线玩家的ID（当临时ID映射为真实ID时调用）
             * @param temp_id 临时玩家ID
             * @param real_id 真实玩家ID
             */
            void updateOnlinePlayerId(const std::string& temp_id, const std::string& real_id);

            /**
             * @brief 检查玩家是否在线
             * @param player_id 玩家ID
             * @return 是否在线
             */
            bool isPlayerOnline(const std::string& player_id) const;

            /**
             * @brief 获取在线玩家数量
             * @return 在线玩家数量
             */
            int getOnlinePlayerCount() const;

            /**
             * @brief 获取所有在线玩家ID
             * @return 在线玩家ID列表
             */
            std::vector<std::string> getOnlinePlayerIds() const;

            /**
             * @brief 广播玩家上线事件
             * @param player_id 上线的玩家ID
             * @param player_info 玩家信息
             */
            void broadcastPlayerOnline(const std::string& player_id, const nlohmann::json& player_info);

            /**
             * @brief 广播玩家下线事件
             * @param player_id 下线的玩家ID
             */
            void broadcastPlayerOffline(const std::string& player_id);

            /**
             * @brief 获取玩家所在房间
             * @param player_id 玩家ID
             * @return 房间ID，如果不在房间则返回空字符串
             */
            std::string getPlayerRoomId(const std::string& player_id) const;

            /**
             * @brief 获取服务器配置
             */
            const GomokuServerConfig& getGomokuConfig() const { return gomoku_config_; }

            /**
             * @brief 更新默认游戏配置
             * @param config 新配置
             */
            void updateDefaultGameConfig(const GomokuConfig& config);

            /**
             * @brief 获取游戏统计信息
             */
            nlohmann::json getGameStatistics() const;
            
            /**
             * @brief 获取统一线程池
             * @return 线程池共享指针
             */
            std::shared_ptr<common::thread_pool::ThreadPool> getThreadPool() const { 
                return thread_pool_; 
            }

            /**
             * @brief 获取活跃玩家计数
             * @return 当前活跃玩家数量
             */
            int getActivePlayerCount() const { return active_player_count_.load(); }

            /**
             * @brief 获取峰值玩家计数
             * @return 峰值玩家数量
             */
            int getPeakPlayerCount() const { return peak_player_count_.load(); }

            /**
             * @brief 获取所有房间状态
             */
            nlohmann::json getAllRoomsStatus() const;

            /**
             * @brief 强制关闭房间
             * @param room_id 房间ID
             * @param reason 关闭原因
             * @return 是否成功
             */
            bool forceCloseRoom(const std::string& room_id, const std::string& reason = "管理员关闭");

            /**
             * @brief 广播系统公告
             * @param announcement 公告内容
             * @return 接收公告的玩家数量
             */
            int broadcastAnnouncement(const std::string& announcement);

            // ========== 服务注册相关 ==========

            /**
             * @brief 设置服务注册客户端
             * @param registry_client 服务注册客户端
             */
            void setRegistryClient(std::shared_ptr<common::service_registry::ServiceRegistryClient> registry_client) {
                registry_client_ = registry_client;
            }

            /**
             * @brief 报告对局完成到服务注册中心
             */
            void reportMatchCompletion();

        protected:
            // 重写基类的虚函数以提供五子棋特定实现
            bool initializeWebSocket() override;

        private:
            GomokuServerConfig gomoku_config_;                          // 服务器配置
            std::unique_ptr<GomokuWebSocketHandler> gomoku_websocket_;  // WebSocket处理器

            // 玩家-房间映射
            std::unordered_map<std::string, std::string> player_room_mapping_; // player_id -> room_id
            mutable std::shared_mutex player_room_mutex_;

            // 断线玩家临时存储（用于重连）
            struct DisconnectedPlayerInfo {
                std::string room_id;
                std::chrono::system_clock::time_point disconnect_time;
                nlohmann::json player_state;  // 玩家状态快照
            };
            std::unordered_map<std::string, DisconnectedPlayerInfo> disconnected_players_;
            mutable std::mutex disconnected_players_mutex_;
            static constexpr int RECONNECT_TIMEOUT_SECONDS = 300;  // 5分钟重连超时

            // 房间列表订阅者管理
            std::set<std::string> room_list_subscribers_;                      // 订阅房间列表的玩家ID集合
            mutable std::mutex subscribers_mutex_;                             // 订阅者集合互斥锁

            // 在线玩家管理
            struct OnlinePlayerInfo {
                std::string player_id;
                std::chrono::system_clock::time_point online_time;
                std::string current_room_id;  // 当前所在房间（空表示在大厅）
                nlohmann::json player_data;   // 玩家扩展信息
            };
            std::unordered_map<std::string, OnlinePlayerInfo> online_players_;
            mutable std::shared_mutex online_players_mutex_;

            // 数据库访问层
            std::shared_ptr<GomokuDatabase> database_;                      // 数据库访问对象
            std::shared_ptr<common::database::MySQLPool> mysql_pool_;       // MySQL连接池
            
            // Redis缓存层
            std::shared_ptr<common::database::RedisPool> redis_pool_;       // Redis连接池
            
            // [FIX] 新增：统一线程池管理
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_; // 统一线程池
            
            // 统计信息
            std::atomic<int64_t> total_games_played_{0};
            std::atomic<int64_t> total_games_finished_{0};
            std::atomic<int64_t> total_moves_made_{0};
            std::atomic<int> active_player_count_{0};        // 活跃玩家计数
            std::atomic<int> peak_player_count_{0};          // 峰值玩家计数
            std::chrono::steady_clock::time_point server_start_time_;

            // 服务注册客户端（用于报告对局完成）
            std::shared_ptr<common::service_registry::ServiceRegistryClient> registry_client_;

            // 匹配管理器
            std::unique_ptr<MatchMakingManager> match_making_manager_;

            /**
             * @brief 从GomokuServerConfig创建BaseConfig
             * @param config 五子棋服务器配置
             * @return BaseConfig对象
             */
            static game_base::BaseConfig createBaseConfig(const GomokuServerConfig& config);

            /**
             * @brief 初始化数据库
             * @return 是否初始化成功
             */
            bool initializeDatabase();

            /**
             * @brief 初始化Redis缓存
             * @return 是否初始化成功
             */
            bool initializeRedis();
            
            /**
             * @brief 初始化统一线程池
             * @return 是否初始化成功
             */
            bool initializeThreadPool();

            /**
             * @brief 设置 CORS 中间件
             */
            void setupCorsMiddleware();

            /**
             * @brief 初始化HTTP路由
             */
            void initializeHttpRoutes();

            /**
             * @brief 设置WebSocket回调
             */
            void setupWebSocketCallbacks();

            /**
             * @brief 处理游戏结束事件
             * @param room_id 房间ID
             * @param result 游戏结果
             * @param winner_id 获胜者ID（可选）
             */
            void handleGameFinished(const std::string& room_id, GameResult result,
                                  const std::string& winner_id = "");

            /**
             * @brief 处理游戏结束事件（带上下文）- 用于调用 Game Data Service 结算 API
             * @param room_id 房间ID
             * @param winners 获胜者ID列表
             * @param context 游戏结束上下文
             */
            void handleGameFinishedWithContext(const std::string& room_id,
                                               const std::vector<std::string>& winners,
                                               const GameEndContext& context);

            /**
             * @brief 异步调用 Game Data Service 结算 API
             * @param context 游戏结束上下文
             */
            void callGameSettlementApiAsync(const GameEndContext& context);

            /**
             * @brief 处理结算 API 响应
             * @param context 游戏结束上下文
             * @param response 结算响应
             */
            void handleSettlementResponse(const GameEndContext& context,
                                          const SettlementResponse& response);

            /**
             * @brief 增加活跃玩家计数
             * @param player_id 玩家ID
             */
            void incrementActivePlayerCount(const std::string& player_id);

            /**
             * @brief 减少活跃玩家计数
             * @param player_id 玩家ID
             */
            void decrementActivePlayerCount(const std::string& player_id);

            // HTTP路由处理器
            /**
             * @brief 处理创建房间请求
             */
            void handleCreateGomokuRoom(const common::http::HttpRequest& req, 
                                      common::http::HttpResponse& res);

            /**
             * @brief 处理获取房间列表请求
             */
            void handleGetRoomList(const common::http::HttpRequest& req, 
                                 common::http::HttpResponse& res);

            /**
             * @brief 处理获取房间详情请求
             */
            void handleGetRoomDetails(const common::http::HttpRequest& req, 
                                    common::http::HttpResponse& res);

            /**
             * @brief 处理游戏统计请求
             */
            void handleGetGameStats(const common::http::HttpRequest& req, 
                                  common::http::HttpResponse& res);

            /**
             * @brief 处理系统公告请求
             */
            void handleBroadcastAnnouncement(const common::http::HttpRequest& req, 
                                           common::http::HttpResponse& res);

            /**
             * @brief 处理玩家加入房间请求
             */
            void handleJoinRoom(const common::http::HttpRequest& req, 
                              common::http::HttpResponse& res);

            /**
             * @brief 处理玩家离开房间请求
             */
            void handleLeaveRoom(const common::http::HttpRequest& req, 
                               common::http::HttpResponse& res);

            /**
             * @brief 处理开始游戏请求
             */
            void handleStartGame(const common::http::HttpRequest& req, 
                               common::http::HttpResponse& res);

            /**
             * @brief 处理获取排行榜请求
             */
            void handleGetLeaderboard(const common::http::HttpRequest& req,
                                    common::http::HttpResponse& res);

            // ========== 匹配系统 HTTP 处理器 ==========

            /**
             * @brief 处理开始匹配请求
             */
            void handleMatchStart(const common::http::HttpRequest& req,
                                 common::http::HttpResponse& res);

            /**
             * @brief 处理取消匹配请求
             */
            void handleMatchCancel(const common::http::HttpRequest& req,
                                  common::http::HttpResponse& res);

            /**
             * @brief 处理获取匹配状态请求
             */
            void handleGetMatchStatus(const common::http::HttpRequest& req,
                                     common::http::HttpResponse& res);

            /**
             * @brief 处理获取匹配池状态请求
             */
            void handleGetMatchPoolStatus(const common::http::HttpRequest& req,
                                         common::http::HttpResponse& res);

            /**
             * @brief 处理匹配结果回调
             * @param result 匹配结果
             */
            void handleMatchResult(const MatchResult& result);

            /**
             * @brief 处理获取服务器状态请求
             */
            void handleGetServerStatus(const common::http::HttpRequest& req, 
                                     common::http::HttpResponse& res);

            /**
             * @brief 处理获取服务端点信息请求
             * @details 为API网关提供服务端点列表，用于动态路由创建
             */
            void handleGetServiceEndpoints(const common::http::HttpRequest& req, 
                                         common::http::HttpResponse& res);

            /**
             * @brief 重置活跃玩家统计（管理员功能）
             * @details 用于修复资源泄露导致的错误统计
             */
            void handleResetPlayerStats(const common::http::HttpRequest& req, 
                                      common::http::HttpResponse& res);

            // WebSocket回调处理
            /**
             * @brief 处理游戏操作回调
             * @param player_id 玩家ID
             * @param action 操作数据
             * @return 是否处理成功
             */
            bool handleGameAction(const std::string& player_id, const nlohmann::json& action);

            /**
             * @brief 处理房间操作回调
             * @param player_id 玩家ID
             * @param operation 操作数据
             * @return 是否处理成功
             */
            bool handleRoomOperation(const std::string& player_id, const nlohmann::json& operation);

            /**
             * @brief 获取或创建玩家会话
             * @param player_id 玩家ID
             * @return 玩家会话指针，失败返回nullptr
             */
            std::shared_ptr<game_base::PlayerSessionBase> getOrCreatePlayerSession(const std::string& player_id);

            /**
             * @brief 更新玩家房间映射
             * @param player_id 玩家ID
             * @param room_id 房间ID（空字符串表示移除映射）
             */
            void updatePlayerRoomMapping(const std::string& player_id, const std::string& room_id);

            /**
             * @brief 验证玩家权限
             * @param player_id 玩家ID
             * @param action 操作类型
             * @return 是否有权限
             */
            bool validatePlayerPermission(const std::string& player_id, const std::string& action);

            /**
             * @brief 记录游戏统计
             * @param event 事件类型
             * @param data 相关数据
             */
            void recordGameStats(const std::string& event, const nlohmann::json& data = {});

            /**
             * @brief 立即为加入的玩家分配棋子
             * @param room 房间对象
             * @param player_id 玩家ID
             */
            void assignPieceImmediately(std::shared_ptr<GomokuRoom> room, const std::string& player_id);

            /**
             * @brief 清理玩家相关资源
             * @param player_id 玩家ID
             */
            void cleanupPlayerResources(const std::string& player_id);

            /**
             * @brief 解析HTTP请求中的JSON数据
             * @param req HTTP请求
             * @param json_data 输出的JSON数据
             * @return 是否解析成功
             */
            bool parseJsonFromRequest(const common::http::HttpRequest& req, nlohmann::json& json_data);

            /**
             * @brief 发送JSON响应
             * @param res HTTP响应
             * @param status_code 状态码
             * @param data JSON数据
             */
            void sendJsonResponse(common::http::HttpResponse& res, int status_code, 
                                const nlohmann::json& data);

            /**
             * @brief 发送错误响应
             * @param res HTTP响应
             * @param status_code 状态码
             * @param error_code 错误代码
             * @param error_message 错误消息
             */
            void sendErrorResponse(common::http::HttpResponse& res, int status_code,
                                 const std::string& error_code, const std::string& error_message);

            /**
             * @brief 从路径中提取路径参数
             * @param path 请求路径
             * @param pattern 路径模式 (如 "/api/rooms/:room_id/join")
             * @param param_name 参数名 (如 "room_id")
             * @return 参数值，未找到返回空字符串
             */
            std::string extractPathParam(const std::string& path, const std::string& pattern, 
                                       const std::string& param_name);
            
            /**
             * @brief 计算服务器负载
             * @return 负载值 (0.0-1.0)
             */
            double calculateServerLoad() const;
            
            /**
             * @brief 初始化事件驱动系统
             * @return 是否初始化成功
             */
            bool initializeEventSystem();
            
            /**
             * @brief 获取服务客户端管理器
             * @return 服务客户端管理器指针
             */
            std::shared_ptr<ServiceClientManager> getServiceClientManager();

        public:
            // 静态方法
            /**
             * @brief 从配置文件创建服务器
             * @param config_file 配置文件路径
             * @return 服务器实例
             */
            static std::unique_ptr<GomokuServer> createFromConfig(const std::string& config_file);

            /**
             * @brief 获取默认服务器配置
             */
            static GomokuServerConfig getDefaultConfig();

            /**
             * @brief 验证服务器配置
             */
            static bool validateConfig(const GomokuServerConfig& config);
        };

    } // namespace gomoku
} // namespace game_services
