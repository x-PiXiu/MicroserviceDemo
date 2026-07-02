#pragma once

#include "game_types.h"
#include "game_room_base.h"
#include "websocket_handler_base.h"
#include "common/http/http_server.h"
#include "common/database/mysql_pool.h"
#include "common/database/redis_pool.h"
#include "common/config/config_manager.h"
#include "common/logger/logger.h"
#include "common/thread_pool/thread_pool.h"
#include "common/monitoring/performance_monitor.h"
// 🔧 架构简化：移除DynamicConfigManager依赖
#include "security_manager.h"
#include "alert_manager.h"

#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <functional>

// 前向声明
namespace common { namespace network { class EventLoop; } }

/**
 * @file game_server_base.h
 * @brief 游戏服务器基类定义
 * @details 提供所有游戏服务器的通用功能，包括HTTP服务、WebSocket通信、数据库操作等
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 游戏服务器基类
         * @details 所有具体游戏服务器的基类，提供通用功能的实现
         */
        class GameServerBase {
        public:
            // 回调函数类型定义
            using RoomCreatedCallback = std::function<void(const std::string&)>;
            using RoomDestroyedCallback = std::function<void(const std::string&)>;
            using PlayerJoinedCallback = std::function<void(const std::string&, const std::string&)>;
            using PlayerLeftCallback = std::function<void(const std::string&, const std::string&)>;

            /**
             * @brief 构造函数
             * @param config 服务器配置
             */
            explicit GameServerBase(const BaseConfig& config);

            // 🔧 架构简化：移除DynamicConfigManager，统一使用ConfigManager

            /**
             * @brief 虚析构函数
             */
            virtual ~GameServerBase();

            // 禁用拷贝构造和赋值
            GameServerBase(const GameServerBase&) = delete;
            GameServerBase& operator=(const GameServerBase&) = delete;

            /**
             * @brief 初始化服务器
             * @return 是否初始化成功
             */
            bool initialize();

            /**
             * @brief 启动服务器
             * @return 是否启动成功
             */
            bool start();

            /**
             * @brief 启动服务器（基于EventLoop定时器）
             * @param event_loop EventLoop实例
             * @return 是否启动成功
             */
            bool start(std::shared_ptr<common::network::EventLoop> event_loop);

            /**
             * @brief 停止服务器
             */
            void stop();

            /**
             * @brief 获取服务器运行状态
             * @return 是否正在运行
             */
            bool isRunning() const { return running_.load(); }

            /**
             * @brief 获取游戏统计信息
             * @return 统计信息
             */
            GameStats getGameStats() const;

            /**
             * @brief 获取HTTP服务器的EventLoop
             * @return EventLoop指针，用于WebSocket Handler共享
             */
            std::shared_ptr<common::network::EventLoop> getHttpServerEventLoop() const;

            // ==================== 新增组件访问接口 ====================
            
            // 🔧 架构简化：移除动态配置管理器，统一使用ConfigManager
            
            /**
             * @brief 获取安全管理器
             * @return 安全管理器指针
             */
            SecurityManager* getSecurityManager() const { return security_manager_.get(); }
            
            /**
             * @brief 获取告警管理器
             * @return 告警管理器指针
             */
            AlertManager* getAlertManager() const { return alert_manager_.get(); }
            
            /**
             * @brief 更新安全配置
             * @param security_config 新的安全配置
             */
            void updateSecurityConfig(const SecurityConfig& security_config);
            
            /**
             * @brief 更新告警配置
             * @param alert_config 新的告警配置
             */
            void updateAlertConfig(const AlertConfig& alert_config);
            
            /**
             * @brief 获取服务器完整状态（包含新组件信息）
             * @return 状态信息JSON
             */
            nlohmann::json getEnhancedServerStatus() const;

            /**
             * @brief 获取WebSocket端口（虚函数，子类实现）
             * @return WebSocket端口号
             */
            virtual int getWebSocketPort() const = 0;

            // 纯虚函数 - 子类必须实现
            /**
             * @brief 初始化游戏特定功能
             * @return 是否初始化成功
             */
            virtual bool initializeGameSpecific() = 0;

            /**
             * @brief 获取游戏类型名称
             * @return 游戏类型名称
             */
            virtual std::string getGameTypeName() const = 0;

            // [REMOVED] API Gateway 相关方法已移除
            // 现在使用 Nginx + Service Registry (端口 8090) 进行服务发现

            /**
             * @brief 创建游戏房间
             * @param room_id 房间ID
             * @param creator_id 创建者ID
             * @param config 房间配置
             * @return 房间对象指针
             */
            virtual std::shared_ptr<GameRoomBase> createGameRoom(
                const std::string& room_id,
                const std::string& creator_id,
                const nlohmann::json& config = {}) = 0;

            // 回调函数设置
            void setRoomCreatedCallback(RoomCreatedCallback callback) { room_created_callback_ = callback; }
            void setRoomDestroyedCallback(RoomDestroyedCallback callback) { room_destroyed_callback_ = callback; }
            void setPlayerJoinedCallback(PlayerJoinedCallback callback) { player_joined_callback_ = callback; }
            void setPlayerLeftCallback(PlayerLeftCallback callback) { player_left_callback_ = callback; }

        protected:
            BaseConfig config_;                                     // 服务器配置
            std::atomic<bool> running_{false};                     // 运行状态

            // 核心组件
            std::unique_ptr<common::http::HttpServer> http_server_;
            std::shared_ptr<common::database::MySQLPool> mysql_pool_;
            std::shared_ptr<common::database::RedisPool> redis_pool_;
            std::shared_ptr<common::thread_pool::ThreadPool> thread_pool_;
            std::unique_ptr<WebSocketHandlerBase> websocket_handler_;

            // 房间管理
            std::unordered_map<std::string, std::shared_ptr<GameRoomBase>> rooms_;
            mutable std::shared_mutex rooms_mutex_;

            // 玩家管理
            std::unordered_map<std::string, PlayerInfo> players_;
            mutable std::shared_mutex players_mutex_;

            // WebSocket认证状态管理
            std::unordered_map<std::string, bool> authenticated_players_;
            mutable std::shared_mutex auth_mutex_;

            // 回调函数
            RoomCreatedCallback room_created_callback_;
            RoomDestroyedCallback room_destroyed_callback_;
            PlayerJoinedCallback player_joined_callback_;
            PlayerLeftCallback player_left_callback_;

            // 通用HTTP处理器
            /**
             * @brief 处理创建房间请求
             */
            void handleCreateRoom(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            /**
             * @brief 处理加入房间请求
             */
            void handleJoinRoom(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            /**
             * @brief 处理获取房间列表请求
             */
            void handleGetRoomList(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            /**
             * @brief 处理获取房间信息请求
             */
            void handleGetRoomInfo(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            /**
             * @brief 处理离开房间请求
             */
            void handleLeaveRoom(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            /**
             * @brief 处理获取服务器状态请求
             */
            void handleGetServerStatus(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            /**
             * @brief 处理健康检查请求
             */
            void handleHealthCheck(const common::http::HttpRequest& req, common::http::HttpResponse& res);

            // 工具方法
            /**
             * @brief 生成唯一房间ID
             * @return 房间ID
             */
            std::string generateRoomId();

            /**
             * @brief 验证用户令牌（已废弃 - 仅用于兼容性）
             * @param req HTTP请求
             * @param user_id 输出用户ID
             * @return 是否验证成功
             * @deprecated 使用WebSocket认证替代HTTP令牌验证
             */
            bool validateUserToken(const common::http::HttpRequest& req, std::string& user_id);

            /**
             * @brief 设置玩家认证状态
             * @param player_id 玩家ID
             * @param authenticated 是否已认证
             */
            void setPlayerAuthenticated(const std::string& player_id, bool authenticated);

            /**
             * @brief 检查玩家是否已认证
             * @param player_id 玩家ID
             * @return 是否已认证
             */
            bool isPlayerAuthenticated(const std::string& player_id) const;

            /**
             * @brief 移除玩家认证状态
             * @param player_id 玩家ID
             */
            void removePlayerAuthentication(const std::string& player_id);

            /**
             * @brief 清理过期房间
             */
            void cleanupExpiredRooms();

            /**
             * @brief 清理离线玩家
             */
            void cleanupOfflinePlayers();

            /**
             * @brief 更新游戏统计信息
             */
            void updateGameStats();

            // [REMOVED] registerToApiGateway() 和 sendHeartbeatToApiGateway() 已移除
            // 服务发现现在通过 Service Registry (端口 8090) 进行

        private:
            // 初始化方法
            bool initializeDatabasePools();
            bool initializeHttpServer();
            virtual bool initializeWebSocket();  // 声明为虚函数，允许子类重写
            bool initializeThreadPool();

            // 定时任务
            void startCleanupTimer();
            void startStatsUpdateTimer();
            
            // 🔧 优化：EventLoop定时器替代线程池循环
            void startTimerBasedTasks(std::shared_ptr<common::network::EventLoop> event_loop);
            void stopTimerBasedTasks();

            // 内部状态
            std::chrono::system_clock::time_point start_time_;
            GameStats current_stats_;
            mutable std::shared_mutex stats_mutex_;
            
            // 🔧 优化：EventLoop定时器相关
            std::shared_ptr<common::network::EventLoop> event_loop_;  ///< EventLoop实例（用于定时器）
            uint64_t cleanup_timer_id_ = 0;                           ///< 清理定时器ID
            uint64_t stats_update_timer_id_ = 0;                      ///< 统计信息更新定时器ID
            // [REMOVED] heartbeat_timer_id_ - API Gateway心跳已移除
            
            // 🔧 优化：性能监控集成
            bool performance_monitoring_enabled_ = true;              ///< 性能监控启用状态
            
            // 🔧 新增：企业级组件集成（简化架构）
            std::unique_ptr<SecurityManager> security_manager_;        ///< 安全管理器  
            std::unique_ptr<AlertManager> alert_manager_;              ///< 告警管理器
        };

    } // namespace game_base
} // namespace game_services
