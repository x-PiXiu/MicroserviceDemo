#pragma once

#include "game_types.h"
#include "player_session_base.h"
#include "common/network/event_loop.h"
#include "common/network/inet_address.h"
#include "common/network/socket.h"
#include "common/network/channel.h"
#include <unordered_map>
#include <memory>
#include <functional>
#include <shared_mutex>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>

/**
 * @file websocket_handler_base.h
 * @brief WebSocket处理基类定义
 * @details 提供WebSocket通信的通用功能，包括连接管理、消息处理等，特别优化Qt6客户端
 *
 * 在Snake游戏中的作用：
 * 1. 实时游戏通信：处理玩家的方向控制、游戏状态同步
 * 2. 多人游戏支持：管理房间内多个玩家的WebSocket连接
 * 3. Qt6客户端优化：专门为Qt6客户端提供优化的消息格式和压缩
 * 4. 心跳检测：维持连接活跃性，及时发现断线玩家
 * 5. 实时状态广播：向房间内所有玩家广播游戏状态更新
 *
 * 使用时机：
 * - 玩家连接游戏时：建立WebSocket连接
 * - 游戏进行中：实时发送游戏状态、接收玩家操作
 * - 多人对战：同步所有玩家的蛇的位置和状态
 * - 观战模式：向观众实时推送游戏画面
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief WebSocket处理基类
         * @details 所有具体WebSocket处理器的基类，提供通用的WebSocket通信功能
         *
         * 核心职责：
         * 1. 网络连接管理：监听端口、接受连接、维护会话
         * 2. 消息路由：解析消息类型，分发到对应处理器
         * 3. 玩家会话管理：跟踪在线玩家、房间分组、状态同步
         * 4. Qt6客户端支持：提供专门的消息格式和优化
         * 5. 心跳检测：定期检查连接状态，清理断线连接
         *
         * 在Snake游戏中的具体应用：
         * - SnakeWebSocketHandler继承此类，处理贪吃蛇特定逻辑
         * - 管理多个玩家在同一房间的实时通信
         * - 广播游戏状态更新（蛇的位置、食物位置、分数等）
         * - 接收玩家操作（方向改变、暂停、退出等）
         */
        class WebSocketHandlerBase {
        public:
            // 回调函数类型定义
            using MessageCallback = std::function<void(const std::string&, const nlohmann::json&)>;
            using ConnectionCallback = std::function<void(const std::string&, bool)>;
            using ErrorCallback = std::function<void(const std::string&, const std::string&)>;
            using PlayerIdChangeCallback = std::function<void(const std::string&, const std::string&)>;  // temp_id, real_id

            /**
             * @brief Qt6客户端信息结构
             */
            struct Qt6ClientInfo {
                std::string qt_version;                     // Qt版本
                std::string client_version;                 // 客户端版本
                bool supports_compression = false;          // 是否支持压缩
                int preferred_fps = 60;                     // 首选帧率
                std::string platform;                       // 平台信息
                std::chrono::system_clock::time_point connect_time; // 连接时间

                nlohmann::json toJson() const {
                    return nlohmann::json{
                        {"qt_version", qt_version},
                        {"client_version", client_version},
                        {"supports_compression", supports_compression},
                        {"preferred_fps", preferred_fps},
                        {"platform", platform}
                    };
                }
            };

            /**
             * @brief 心跳配置结构
             */
            struct HeartbeatConfig {
                int interval_ms = 30000;       // 心跳发送间隔（毫秒）
                int timeout_ms = 60000;        // 连接超时时间（毫秒）
                int max_missed = 3;            // 最大允许丢失心跳次数

                nlohmann::json toJson() const {
                    return nlohmann::json{
                        {"interval_ms", interval_ms},
                        {"timeout_ms", timeout_ms},
                        {"max_missed", max_missed}
                    };
                }

                bool validate() const {
                    return interval_ms > 0 && timeout_ms > 0 && max_missed > 0;
                }
            };

            /**
             * @brief 构造函数
             * @param event_loop 事件循环，用于处理网络I/O事件
             *
             * Snake游戏中的使用：
             * - 在SnakeGameServer::initializeWebSocket()中创建
             * - 传入共享的EventLoop，与HTTP服务器共享事件处理
             * - 初始化玩家会话管理和Qt6客户端信息存储
             */
            explicit WebSocketHandlerBase(std::shared_ptr<common::network::EventLoop> event_loop);

            /**
             * @brief 虚析构函数
             *
             * 功能：
             * - 停止WebSocket服务
             * - 清理所有网络资源（套接字、通道、地址）
             * - 断开所有玩家连接
             * - 清理Qt6客户端信息
             *
             * Snake游戏中的使用：
             * - 服务器关闭时自动调用
             * - 确保所有玩家连接正确断开
             * - 防止资源泄漏
             */
            virtual ~WebSocketHandlerBase();

            // 禁用拷贝构造和赋值
            WebSocketHandlerBase(const WebSocketHandlerBase&) = delete;
            WebSocketHandlerBase& operator=(const WebSocketHandlerBase&) = delete;

            /**
             * @brief 初始化WebSocket处理器
             * @param port 监听端口（Snake游戏默认8084，与HTTP共用）
             * @return 是否初始化成功
             *
             * 功能：
             * - 创建监听套接字并绑定端口
             * - 设置套接字选项（地址重用、TCP_NODELAY等）
             * - 创建监听通道并注册到EventLoop
             * - 初始化玩家会话和Qt6客户端信息存储
             *
             * Snake游戏中的使用：
             * - 在SnakeGameServer启动时调用
             * - 端口通常与HTTP服务器共用（8084）
             * - 初始化失败会导致WebSocket功能不可用
             */
            bool initialize(int port);

            /**
             * @brief 启动WebSocket服务
             *
             * 功能：
             * - 设置运行状态为true
             * - 启动心跳检测线程
             * - 开始接受客户端连接
             *
             * Snake游戏中的使用：
             * - 在服务器完全启动后调用
             * - 开始接受玩家的WebSocket连接
             * - 启动后玩家可以进行实时游戏
             */
            void start();

            /**
             * @brief 等待WebSocket初始化完成
             * @param timeout_ms 超时时间（毫秒）
             * @return 是否初始化成功
             *
             * 功能：
             * - 等待异步Channel创建完成
             * - 支持超时处理，避免无限等待
             * - 用于确保WebSocket服务就绪
             */
            bool waitForInitialization(int timeout_ms = 5000);

            /**
             * @brief 停止WebSocket服务
             *
             * 功能：
             * - 设置运行状态为false
             * - 停止心跳检测
             * - 断开所有玩家连接
             * - 清理Qt6客户端信息
             *
             * Snake游戏中的使用：
             * - 服务器关闭时调用
             * - 维护时临时停止WebSocket服务
             * - 确保所有玩家连接优雅断开
             */
            void stop();

            /**
             * @brief 获取运行状态
             * @return 是否正在运行
             *
             * Snake游戏中的使用：
             * - 检查WebSocket服务是否可用
             * - 健康检查接口中报告状态
             * - 防止重复启动或停止
             */
            bool isRunning() const { return running_.load(); }

            // ==================== 连接管理 ====================
            /**
             * @brief 添加玩家会话
             * @param player_id 玩家ID（通常是用户名或UUID）
             * @param session 玩家会话对象，包含连接信息和状态
             * @return 是否添加成功
             *
             * Snake游戏中的使用：
             * - 玩家通过WebSocket连接后调用
             * - 将玩家加入到会话管理中
             * - 用于后续的消息发送和状态跟踪
             * - 支持多人游戏的玩家管理
             */
            bool addPlayerSession(const std::string& player_id, std::shared_ptr<PlayerSessionBase> session);

            /**
             * @brief 移除玩家会话
             * @param player_id 玩家ID
             * @return 是否移除成功
             *
             * Snake游戏中的使用：
             * - 玩家断开连接时调用
             * - 玩家主动退出游戏时调用
             * - 清理玩家的WebSocket连接和状态
             * - 通知房间内其他玩家该玩家已离开
             */
            bool removePlayerSession(const std::string& player_id);

            /**
             * @brief 获取玩家会话
             * @param player_id 玩家ID
             * @return 玩家会话指针，如果玩家不在线则返回nullptr
             *
             * Snake游戏中的使用：
             * - 向特定玩家发送消息前获取会话
             * - 检查玩家连接状态
             * - 获取玩家的房间信息和游戏状态
             */
            std::shared_ptr<PlayerSessionBase> getPlayerSession(const std::string& player_id);

            /**
             * @brief 检查玩家是否在线
             * @param player_id 玩家ID
             * @return 是否在线
             *
             * Snake游戏中的使用：
             * - 游戏开始前检查所有玩家是否在线
             * - 发送消息前验证玩家连接状态
             * - 房间管理中显示在线玩家列表
             */
            bool isPlayerOnline(const std::string& player_id) const;

            /**
             * @brief 获取在线玩家数量
             * @return 在线玩家数量
             *
             * Snake游戏中的使用：
             * - 服务器状态监控
             * - 负载均衡决策
             * - 健康检查接口中报告在线人数
             */
            int getOnlinePlayerCount() const;

            /**
             * @brief 获取所有在线玩家ID
             * @return 玩家ID列表
             *
             * Snake游戏中的使用：
             * - 管理员查看在线玩家
             * - 全服广播消息
             * - 统计和分析功能
             */
            std::vector<std::string> getOnlinePlayerIds() const;
            
            /**
             * @brief 通过文件描述符获取玩家ID
             * @param client_fd 客户端文件描述符
             * @return 玩家ID，未找到返回空字符串
             */
            std::string getPlayerIdByFd(int client_fd) const;

            // ==================== 消息发送 - 通用版本 ====================
            /**
             * @brief 向指定玩家发送消息
             * @param player_id 玩家ID
             * @param message 消息内容（JSON格式）
             * @return 是否发送成功
             *
             * Snake游戏中的使用：
             * - 发送玩家个人游戏状态（分数、生命值等）
             * - 发送错误消息或系统通知
             * - 发送私人聊天消息
             * - 发送游戏操作确认
             */
            bool sendToPlayer(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 向房间内所有玩家广播消息
             * @param room_id 房间ID
             * @param message 消息内容
             * @return 发送成功的玩家数量
             *
             * Snake游戏中的使用：
             * - 广播游戏状态更新（所有蛇的位置、食物位置）
             * - 广播游戏事件（玩家死亡、游戏结束等）
             * - 广播房间聊天消息
             * - 广播游戏开始/暂停/结束通知
             * 这是Snake游戏中最重要的功能之一
             */
            int sendToRoom(const std::string& room_id, const nlohmann::json& message);

            /**
             * @brief 向所有在线玩家广播消息
             * @param message 消息内容
             * @return 发送成功的玩家数量
             *
             * Snake游戏中的使用：
             * - 服务器维护通知
             * - 全服活动公告
             * - 系统重启通知
             * - 紧急消息广播
             */
            int broadcastToAll(const nlohmann::json& message);

            /**
             * @brief 发送原始消息（WebSocket协议层处理）
             * @param player_id 玩家ID
             * @param message 消息内容
             * @return 是否发送成功
             *
             * 功能：
             * - 将消息格式化为WebSocket帧
             * - 通过底层socket发送到客户端
             * - 处理网络发送错误和重试
             * - 更新连接状态和统计信息
             *
             * 使用场景：
             * - PlayerSession委托WebSocket协议处理
             * - 实现正确的架构分层（会话层 → 协议层 → 网络层）
             */
            bool sendRawMessage(const std::string& player_id, const std::string& message);

            /**
             * @brief 发送WebSocket关闭帧（协议层处理）
             * @param player_id 玩家ID
             * @param close_code 关闭代码（默认1000=正常关闭）
             * @param reason 关闭原因（可选）
             * @return 是否发送成功
             *
             * 功能：
             * - 创建标准的WebSocket关闭帧
             * - 通过底层socket发送
             * - 符合RFC 6455标准
             *
             * 使用场景：
             * - PlayerSession请求优雅关闭连接
             * - 服务器主动断开连接
             */
            bool sendCloseFrame(const std::string& player_id, uint16_t close_code = 1000, const std::string& reason = "");

            /**
             * @brief 向指定玩家列表发送消息
             * @param player_ids 玩家ID列表
             * @param message 消息内容
             * @return 发送成功的玩家数量
             *
             * Snake游戏中的使用：
             * - 向观战玩家发送游戏画面
             * - 向特定组玩家发送消息
             * - 批量通知功能
             */
            int sendToPlayers(const std::vector<std::string>& player_ids, const nlohmann::json& message);

            // ==================== Qt6专用消息发送方法 ====================
            /**
             * @brief 发送Qt6优化的游戏状态更新
             * @param room_id 房间ID
             * @param game_state 游戏状态
             * @return 发送成功的玩家数量
             *
             * Snake游戏中的使用：
             * - 向Qt6客户端发送优化的游戏状态
             * - 包含压缩的蛇位置数据、食物位置、分数等
             * - 针对Qt6客户端的渲染特性优化消息格式
             * - 支持高帧率游戏更新（60FPS）
             * - 这是Qt6客户端实时游戏体验的核心功能
             */
            int sendQt6GameState(const std::string& room_id, const nlohmann::json& game_state);

            /**
             * @brief 发送Qt6优化的玩家操作结果
             * @param player_id 玩家ID
             * @param action_type 操作类型（如"direction_change", "pause", "resume"）
             * @param result 操作结果
             * @return 是否发送成功
             *
             * Snake游戏中的使用：
             * - 确认玩家方向改变操作
             * - 确认游戏暂停/恢复操作
             * - 反馈操作是否成功执行
             * - 提供即时的操作响应给Qt6客户端
             */
            bool sendQt6PlayerActionResult(const std::string& player_id,
                                          const std::string& action_type,
                                          const nlohmann::json& result);

            /**
             * @brief 发送Qt6优化的系统消息
             * @param player_id 玩家ID
             * @param message_type 消息类型（如"error", "info", "warning"）
             * @param content 消息内容
             * @return 是否发送成功
             *
             * Snake游戏中的使用：
             * - 发送游戏错误信息（无效操作、连接问题等）
             * - 发送游戏提示信息（游戏规则、操作指南等）
             * - 发送状态通知（房间满员、游戏即将开始等）
             * - Qt6客户端可以根据消息类型显示不同的UI效果
             */
            bool sendQt6SystemMessage(const std::string& player_id,
                                     const std::string& message_type,
                                     const std::string& content);

            /**
             * @brief 发送Qt6优化的房间状态更新
             * @param room_id 房间ID
             * @param room_state 房间状态
             * @return 发送成功的玩家数量
             *
             * Snake游戏中的使用：
             * - 更新房间玩家列表（玩家加入/离开）
             * - 更新房间游戏状态（等待中、游戏中、已结束）
             * - 更新房间设置（地图大小、游戏模式等）
             * - Qt6客户端可以实时更新房间UI界面
             */
            int sendQt6RoomStateUpdate(const std::string& room_id, const nlohmann::json& room_state);

            // ==================== Qt6客户端管理 ====================
            /**
             * @brief 设置Qt6客户端信息
             * @param player_id 玩家ID
             * @param info Qt6客户端信息（版本、平台、性能偏好等）
             *
             * Snake游戏中的使用：
             * - 玩家连接时保存Qt6客户端的详细信息
             * - 根据客户端版本提供兼容性支持
             * - 根据客户端性能调整游戏参数（帧率、压缩等）
             * - 用于客户端特定的优化和功能启用
             */
            void setQt6ClientInfo(const std::string& player_id, const Qt6ClientInfo& info);

            /**
             * @brief 获取Qt6客户端信息
             * @param player_id 玩家ID
             * @return Qt6客户端信息
             *
             * Snake游戏中的使用：
             * - 发送消息前检查客户端能力（是否支持压缩等）
             * - 根据客户端偏好调整游戏设置（帧率、画质等）
             * - 调试和问题排查时查看客户端信息
             */
            Qt6ClientInfo getQt6ClientInfo(const std::string& player_id) const;

            /**
             * @brief 检查是否为Qt6客户端
             * @param player_id 玩家ID
             * @return 是否为Qt6客户端
             *
             * Snake游戏中的使用：
             * - 决定使用哪种消息格式（Qt6优化 vs 通用格式）
             * - 启用Qt6特定功能（如高级图形效果）
             * - 统计Qt6客户端使用情况
             */
            bool isQt6Client(const std::string& player_id) const;

            /**
             * @brief 获取所有Qt6客户端信息
             * @return Qt6客户端信息映射
             *
             * Snake游戏中的使用：
             * - 管理员查看所有Qt6客户端状态
             * - 性能监控和统计分析
             * - 批量操作Qt6客户端
             */
            std::unordered_map<std::string, Qt6ClientInfo> getAllQt6ClientInfo() const;

            // ==================== 回调函数设置 ====================
            /**
             * @brief 设置消息回调函数
             * @param callback 消息处理回调
             *
             * Snake游戏中的使用：
             * - 在SnakeGameServer中设置，处理玩家发送的游戏消息
             * - 解析方向改变、游戏操作等消息
             * - 将WebSocket消息转发给游戏逻辑处理
             */
            void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }

            /**
             * @brief 设置连接状态回调函数
             * @param callback 连接状态变化回调
             *
             * Snake游戏中的使用：
             * - 玩家连接时加入房间或创建会话
             * - 玩家断开时从房间移除并通知其他玩家
             * - 更新在线玩家列表
             */
            void setConnectionCallback(ConnectionCallback callback) { connection_callback_ = callback; }

            /**
             * @brief 设置错误回调函数
             * @param callback 错误处理回调
             *
             * Snake游戏中的使用：
             * - 记录WebSocket通信错误
             * - 处理消息解析失败
             * - 网络连接异常处理
             */
            void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }

            /**
             * @brief 设置玩家ID变更回调函数
             * @param callback ID变更回调 (temp_id, real_id)
             *
             * 当临时玩家ID映射为真实ID时调用，用于更新在线玩家列表。
             */
            void setPlayerIdChangeCallback(PlayerIdChangeCallback callback) { player_id_change_callback_ = callback; }

            /**
             * @brief 启动心跳检测
             * @param interval_ms 心跳间隔(毫秒，默认30秒)
             *
             * Snake游戏中的使用：
             * - 定期检测玩家连接状态
             * - 及时发现断线玩家并从游戏中移除
             * - 维持WebSocket连接活跃性
             * - 防止网络中间设备关闭空闲连接
             */
            void startHeartbeat(int interval_ms = 30000);

            /**
             * @brief 停止心跳检测
             *
             * Snake游戏中的使用：
             * - 服务器关闭时停止心跳
             * - 维护模式时暂停心跳检测
             */
            void stopHeartbeat();

            /**
             * @brief 设置心跳配置
             * @param config 心跳配置结构
             */
            void setHeartbeatConfig(const HeartbeatConfig& config);

            /**
             * @brief 获取当前心跳配置
             * @return 心跳配置结构
             */
            const HeartbeatConfig& getHeartbeatConfig() const { return heartbeat_config_; }

            /**
             * @brief 清理离线连接
             *
             * Snake游戏中的使用：
             * - 清理长时间无响应的连接
             * - 释放断线玩家占用的资源
             * - 从房间中移除离线玩家
             * - 更新游戏状态和玩家列表
             */
            void cleanupOfflineConnections();

        protected:
            std::shared_ptr<common::network::EventLoop> event_loop_;
            std::unordered_map<std::string, std::shared_ptr<PlayerSessionBase>> player_sessions_;
            std::unordered_map<std::string, Qt6ClientInfo> qt6_client_info_;
            mutable std::shared_mutex sessions_mutex_;
            mutable std::shared_mutex qt6_info_mutex_;

            MessageCallback message_callback_;
            ConnectionCallback connection_callback_;
            ErrorCallback error_callback_;
            PlayerIdChangeCallback player_id_change_callback_;  // 临时ID映射为真实ID时的回调

            std::atomic<bool> running_{false};
            int websocket_port_;

            // 网络资源（对照HTTP服务器）
            std::unique_ptr<common::network::InetAddress> listen_address_;
            std::unique_ptr<common::network::Socket> listen_socket_;
            std::unique_ptr<common::network::Channel> listen_channel_;

            // 🔧 异步初始化相关成员变量
            std::atomic<bool> initialization_completed_{false};
            std::atomic<bool> initialization_failed_{false};
            std::mutex init_mutex_;
            std::condition_variable init_cv_;

            // 连接限制和管理
            static constexpr int MAX_CONNECTIONS = 1024;
            static constexpr int MAX_MESSAGE_SIZE = 65536;
            static constexpr int MESSAGE_RATE_LIMIT = 100; // 每秒最大消息数

            /**
             * @brief 处理新连接（内部方法）
             *
             * 功能：
             * - 接受新的TCP连接
             * - 获取客户端地址信息
             * - 调用虚函数handleWebSocketConnection进行具体处理
             *
             * Snake游戏中的使用：
             * - EventLoop检测到新连接时自动调用
             * - 为每个新连接创建处理流程
             */
            void handleNewConnection();

            /**
             * @brief 处理WebSocket连接（虚函数，子类可重写）
             * @param client_fd 客户端文件描述符
             * @param client_ip 客户端IP地址
             *
             * Snake游戏中的使用：
             * - SnakeWebSocketHandler重写此方法
             * - 进行WebSocket握手协议处理
             * - 创建SnakePlayerSession对象
             * - 将玩家加入到会话管理中
             */
            virtual void handleWebSocketConnection(int client_fd, const std::string& client_ip);

            /**
             * @brief 处理WebSocket消息（虚函数，子类可重写）
             * @param player_id 玩家ID
             * @param message 消息内容（原始字符串）
             *
             * Snake游戏中的使用：
             * - SnakeWebSocketHandler重写此方法
             * - 解析贪吃蛇特定消息（方向改变、游戏操作等）
             * - 调用游戏逻辑处理玩家操作
             * - 更新玩家活动时间和消息统计
             */
            virtual void handleWebSocketMessage(const std::string& player_id, const std::string& message);

            /**
             * @brief 处理WebSocket断开连接（虚函数，子类可重写）
             * @param player_id 玩家ID
             *
             * Snake游戏中的使用：
             * - SnakeWebSocketHandler重写此方法
             * - 从房间中移除断线玩家
             * - 通知房间内其他玩家
             * - 清理玩家相关资源
             * - 更新游戏状态（如果是游戏中断线）
             */
            virtual void handleWebSocketDisconnection(const std::string& player_id);

            /**
             * @brief 处理Qt6特定消息（虚函数，子类可重写）
             * @param player_id 玩家ID
             * @param message 消息内容（JSON格式）
             *
             * Snake游戏中的使用：
             * - 处理Qt6客户端信息注册
             * - 处理Qt6心跳消息
             * - 处理Qt6特定的游戏设置
             * - 根据Qt6客户端能力调整服务
             */
            virtual void handleQt6SpecificMessage(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 为Qt6客户端格式化消息
             * @param message 原始消息
             * @param player_id 玩家ID
             * @return 格式化后的消息
             *
             * Snake游戏中的使用：
             * - 根据Qt6客户端能力调整消息格式
             * - 启用或禁用消息压缩
             * - 调整消息结构以适配Qt6客户端
             * - 添加Qt6特定的元数据
             */
            nlohmann::json formatForQt6(const nlohmann::json& message, const std::string& player_id);
            nlohmann::json formatForQt6(const nlohmann::json& message, std::shared_ptr<PlayerSessionBase> session);

            /**
             * @brief 创建Qt6优化的消息包装
             * @param type 消息类型
             * @param data 消息数据
             * @return 包装后的消息
             *
             * Snake游戏中的使用：
             * - 创建标准化的Qt6消息格式
             * - 添加时间戳、序列号等元信息
             * - 确保消息格式与Qt6客户端兼容
             */
            nlohmann::json createQt6Message(const std::string& type, const nlohmann::json& data);

        private:
            // ==================== 心跳管理 ====================
            std::atomic<bool> heartbeat_running_{false};  // 心跳运行状态
            int heartbeat_interval_ms_ = 30000;           // 心跳间隔（毫秒）
            uint64_t heartbeat_timer_id_ = 0;             // 🔧 优化：心跳定时器ID（EventLoop时间轮）
            uint64_t cleanup_timer_id_ = 0;               // 🔧 优化：清理定时器ID（EventLoop时间轮）
            uint64_t connection_monitor_timer_id_ = 0;    // 🔧 新增：连接监控定时器ID
            HeartbeatConfig heartbeat_config_;            // 心跳配置

            /**
             * @brief 心跳循环（已废弃 - 替代为EventLoop时间轮）
             * @deprecated 使用startTimerBasedHeartbeat()替代
             * 
             * 废弃原因：
             * - 使用独立线程，资源开销大
             * - 与EventLoop时间轮架构不统一
             * - 无法利用统一的事件调度机制
             */
            void heartbeatLoop();

            /**
             * @brief 🔧 优化：启动基于EventLoop时间轮的心跳检测
             * 
             * 功能：
             * - 使用EventLoop定时器替代独立线程
             * - 统一事件调度机制
             * - 更低的资源开销
             */
            void startTimerBasedHeartbeat();

            /**
             * @brief 🔧 优化：停止基于EventLoop时间轮的心跳检测
             */
            void stopTimerBasedHeartbeat();
            
            /**
             * @brief 🔧 新增：启动连接状态监控定时器
             * 
             * 功能：
             * - 定期检查无效连接并清理
             * - 防止僵尸连接导致的资源泄漏
             * - 监控连接状态异常
             */
            void startConnectionMonitor();
            
            /**
             * @brief 🔧 新增：停止连接状态监控
             */
            void stopConnectionMonitor();
            
            /**
             * @brief 🔧 新增：监控连接健康状态
             * 
             * 功能：
             * - 检查所有连接的有效性
             * - 清理无效连接
             * - 统计连接状态
             */
            void monitorConnectionHealth();

            /**
             * @brief 向所有客户端发送心跳消息（私有方法）
             *
             * 功能：
             * - 根据客户端类型发送不同格式的心跳消息
             * - Qt6客户端：发送优化的心跳格式
             * - 通用客户端：发送标准ping消息
             * - 记录心跳发送统计
             */
            void sendHeartbeatToAllClients();

            /**
             * @brief 执行WebSocket握手协议
             * @param client_fd 客户端文件描述符
             * @return 是否握手成功
             */
            bool performWebSocketHandshake(int client_fd);

            /**
             * @brief 在握手阶段验证游戏会话令牌（优化版本）
             * @param headers HTTP请求头
             * @param client_fd 客户端文件描述符
             * @return 是否验证成功
             */
            bool validateGameSessionTokenInHandshake(const std::unordered_map<std::string, std::string>& headers, int client_fd);

            /**
             * @brief 验证游戏会话令牌（虚函数，子类可重写）
             * @param token 游戏会话令牌
             * @param client_fd 客户端文件描述符
             * @return 是否验证成功
             */
            virtual bool validateGameSessionToken(const std::string& token, int client_fd);

            /**
             * @brief 生成临时玩家ID
             * @param client_ip 客户端IP
             * @param client_fd 客户端文件描述符
             * @return 临时玩家ID
             */
            std::string generateTempPlayerId(const std::string& client_ip, int client_fd);

            /**
             * @brief 创建玩家会话（纯虚函数）
             * @param player_id 玩家ID
             * @param client_fd 客户端文件描述符
             * @param client_ip 客户端IP
             * @return 玩家会话指针
             * @note 子类必须实现此方法，因为PlayerSessionBase是抽象类
             */
            virtual std::shared_ptr<PlayerSessionBase> createPlayerSession(
                const std::string& player_id, int client_fd, const std::string& client_ip) = 0;

            /**
             * @brief 发送欢迎消息
             * @param player_id 玩家ID
             */
            void sendWelcomeMessage(const std::string& player_id);
            void scheduleWelcomeMessage(const std::string& player_id);
            void sendWelcomeMessageDirect(const std::string& player_id, int client_fd);
            void handleWebSocketRead(const std::string& player_id, int client_fd);
            std::string parseWebSocketFrame(const char* buffer, size_t length);
            void removePlayerSessionSafely(const std::string& player_id);

            /**
             * @brief 检查消息速率限制
             * @param player_id 玩家ID
             * @return 是否超过限制
             */
            bool checkMessageRateLimit(const std::string& player_id);

            /**
             * @brief 验证消息格式
             * @param message 消息内容
             * @return 是否格式有效
             */
            bool validateMessageFormat(const nlohmann::json& message);

            // WebSocket握手相关的辅助方法
            /**
             * @brief 读取HTTP请求
             * @param client_fd 客户端文件描述符
             * @return HTTP请求字符串
             */
            std::string readHttpRequest(int client_fd);

            /**
             * @brief 解析HTTP请求头
             * @param request HTTP请求字符串
             * @return 请求头映射
             */
            std::unordered_map<std::string, std::string> parseHttpHeaders(const std::string& request);

            /**
             * @brief 验证WebSocket升级请求
             * @param headers HTTP请求头
             * @return 是否为有效的WebSocket请求
             */
            bool validateWebSocketRequest(const std::unordered_map<std::string, std::string>& headers);

            /**
             * @brief 计算WebSocket Accept密钥
             * @param client_key 客户端提供的密钥
             * @return 计算出的Accept密钥
             */
            std::string calculateWebSocketAccept(const std::string& client_key);

            /**
             * @brief 获取支持的WebSocket协议列表
             * @return 支持的协议列表
             */
            virtual std::vector<std::string> getSupportedProtocols();

            /**
             * @brief 从客户端请求中选择协议
             * @param headers HTTP请求头
             * @param supported_protocols 支持的协议列表
             * @return 选择的协议
             */
            std::string selectProtocol(const std::unordered_map<std::string, std::string>& headers,
                                     const std::vector<std::string>& supported_protocols);

            /**
             * @brief 构建WebSocket握手响应
             * @param accept_key Accept密钥
             * @param protocol 选择的协议
             * @return 响应字符串
             */
            std::string buildWebSocketResponse(const std::string& accept_key, const std::string& protocol);

            /**
             * @brief 发送HTTP错误响应
             * @param client_fd 客户端文件描述符
             * @param status_code 状态码
             * @param status_text 状态文本
             * @param message 错误消息
             */
            void sendHttpError(int client_fd, int status_code, const std::string& status_text, const std::string& message);

            /**
             * @brief 发送握手响应（使用项目网络抽象层）
             * @param client_fd 客户端文件描述符
             * @param response 响应内容
             * @return 是否发送成功
             */
            bool sendHandshakeResponse(int client_fd, const std::string& response);

            /**
             * @brief 重置套接字为WebSocket模式
             * @param client_fd 客户端文件描述符
             */
            void resetSocketForWebSocket(int client_fd);

            /**
             * @brief 处理认证消息
             * @param player_id 玩家ID
             * @param message 认证消息
             */
            virtual void handleAuthenticationMessage(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理房间操作消息
             * @param player_id 玩家ID
             * @param message 房间操作消息
             */
            virtual void handleRoomOperationMessage(const std::string& player_id, const nlohmann::json& message);

        protected:
            /**
             * @brief 检查玩家是否已认证（供子类使用）
             * @param player_id 玩家ID
             * @return 是否已认证
             */
            virtual bool isPlayerAuthenticated(const std::string& player_id);

            /**
             * @brief 设置玩家认证状态（供子类使用）
             * @param player_id 玩家ID
             * @param authenticated 认证状态
             */
            virtual void setPlayerAuthenticated(const std::string& player_id, bool authenticated);

            /**
             * @brief 发送错误消息（供子类使用）
             * @param player_id 玩家ID
             * @param error_code 错误代码
             * @param error_message 错误消息
             */
            void sendErrorMessage(const std::string& player_id, const std::string& error_code, const std::string& error_message);

            /**
             * @brief 创建WebSocket帧
             * @param message 消息内容
             * @return WebSocket帧数据
             */
            std::vector<uint8_t> createWebSocketFrame(const std::string& message);

            /**
             * @brief 处理WebSocket创建房间消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            virtual void handleCreateRoomWebSocket(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理WebSocket加入房间消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            virtual void handleJoinRoomWebSocket(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理WebSocket离开房间消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            virtual void handleLeaveRoomWebSocket(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 处理WebSocket开始游戏消息
             * @param player_id 玩家ID
             * @param message 消息内容
             */
            virtual void handleStartGameWebSocket(const std::string& player_id, const nlohmann::json& message);

            // ==================== 内部工具方法 ====================

            /**
             * @brief 通知连接状态变化（私有方法）
             * @param player_id 玩家ID
             * @param connected 是否连接
             *
             * 功能：
             * - 调用注册的连接回调函数
             * - 处理连接状态变化的内部逻辑
             */
            void notifyConnection(const std::string& player_id, bool connected);

            /**
             * @brief 通知消息接收（私有方法）
             * @param player_id 玩家ID
             * @param message 接收到的消息
             *
             * 功能：
             * - 调用注册的消息回调函数
             * - 处理消息接收的内部逻辑
             */
            void notifyMessage(const std::string& player_id, const nlohmann::json& message);

            /**
             * @brief 通知错误发生（私有方法）
             * @param player_id 玩家ID
             * @param error 错误信息
             *
             * 功能：
             * - 调用注册的错误回调函数
             * - 处理错误的内部逻辑
             * - 记录错误日志
             */
            void notifyError(const std::string& player_id, const std::string& error);
        };

    } // namespace game_base
} // namespace game_services
