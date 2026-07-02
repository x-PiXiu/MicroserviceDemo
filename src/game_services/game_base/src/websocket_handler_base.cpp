#include "websocket_handler_base.h"
#include "common/logger/logger.h"
#include "common/monitoring/performance_monitor.h"
#include <thread>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <set>
#include <future>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <regex>
#include <cctype>

namespace game_services {
    namespace game_base {

        WebSocketHandlerBase::WebSocketHandlerBase(std::shared_ptr<common::network::EventLoop> event_loop)
            : event_loop_(event_loop), websocket_port_(8081) {
            LOG_INFO("WebSocketHandlerBase created");
        }

        WebSocketHandlerBase::~WebSocketHandlerBase() {
            try {
                if (running_.load()) {
                    stop();
                }

                // 清理网络资源（确保在EventLoop线程中执行Channel操作）
                if (listen_channel_) {
                    try {
                        if (event_loop_ && !event_loop_->isInLoopThread()) {
                            // 使用shared_ptr包装promise以支持复制构造
                            auto promise_ptr = std::make_shared<std::promise<void>>();
                            auto future = promise_ptr->get_future();

                            event_loop_->runInLoop([this, promise_ptr]() {
                                try {
                                    if (listen_channel_) {
                                        listen_channel_->disableAll();
                                        listen_channel_->remove();
                                        listen_channel_.reset();
                                    }
                                } catch (const std::exception& e) {
                                    LOG_WARNING("Error cleaning listen channel in loop thread (dtor): " + std::string(e.what()));
                                    listen_channel_.reset();
                                }
                                promise_ptr->set_value();
                            });
                            future.wait();
                        } else {
                            listen_channel_->disableAll();
                            listen_channel_->remove();
                            listen_channel_.reset();
                        }
                    } catch (const std::exception& e) {
                        LOG_WARNING("Error during listen channel cleanup (dtor): " + std::string(e.what()));
                        listen_channel_.reset();
                    }
                }

                if (listen_socket_) {
                    listen_socket_->forceClose();
                    listen_socket_.reset();
                }

                listen_address_.reset();

                LOG_INFO("WebSocketHandlerBase destroyed");

            } catch (const std::exception& e) {
                LOG_ERROR("Error in WebSocketHandlerBase destructor: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown error in WebSocketHandlerBase destructor");
            }
        }

        bool WebSocketHandlerBase::initialize(int port) {
            try {
                websocket_port_ = port;

                LOG_INFO("Initializing WebSocket handler on port " + std::to_string(port));

                // 1. 创建监听地址
                listen_address_ = std::make_unique<common::network::InetAddress>("0.0.0.0", port);

                // 2. 创建监听套接字
                listen_socket_ = std::make_unique<common::network::Socket>();

                // 3. 设置套接字选项（对照HTTP服务器）
                listen_socket_->setReuseAddr(true);
                listen_socket_->setTcpNoDelay(true);
                listen_socket_->setKeepAlive(true);

                // 4. 绑定和监听
                listen_socket_->bindAddress(*listen_address_);
                listen_socket_->listen();

                // 5. 🔧 线程安全修复：在EventLoop线程中创建监听通道
                if (!event_loop_) {
                    LOG_ERROR("EventLoop is null, cannot create WebSocket server");
                    return false;
                }

                // 🔧 修复：异步创建Channel，避免线程安全冲突
                // 使用runInLoop确保在EventLoop线程中操作，但不等待结果
                initialization_completed_ = false;
                
                event_loop_->runInLoop([this]() {
                    try {
                        // 在EventLoop线程中安全创建Channel
                        listen_channel_ = std::make_unique<common::network::Channel>(event_loop_.get(), listen_socket_->fd());
                        listen_channel_->setReadCallback([this] { handleNewConnection(); });
                        listen_channel_->enableReading();
                        
                        // 标记初始化完成
                        {
                            std::lock_guard<std::mutex> lock(init_mutex_);
                            initialization_completed_ = true;
                            init_cv_.notify_all();
                        }
                        
                        LOG_INFO("WebSocket listen channel创建成功（EventLoop线程）");
                    } catch (const std::exception& e) {
                        LOG_ERROR("WebSocket listen channel创建失败: " + std::string(e.what()));
                        {
                            std::lock_guard<std::mutex> lock(init_mutex_);
                            initialization_completed_ = true;
                            initialization_failed_ = true;
                            init_cv_.notify_all();
                        }
                    }
                });
                
                LOG_INFO("WebSocket Channel初始化已提交到EventLoop线程");

                // 6. 初始化连接管理
                {
                    std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
                    player_sessions_.clear();
                }

                {
                    std::unique_lock<std::shared_mutex> lock(qt6_info_mutex_);
                    qt6_client_info_.clear();
                }

                LOG_INFO("WebSocket handler initialized successfully on port " + std::to_string(port));
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize WebSocket handler: " + std::string(e.what()));
                return false;
            }
        }

        bool WebSocketHandlerBase::waitForInitialization(int timeout_ms) {
            std::unique_lock<std::mutex> lock(init_mutex_);
            
            // 如果已经完成，直接返回结果
            if (initialization_completed_) {
                return !initialization_failed_;
            }
            
            // 等待初始化完成或超时
            bool completed = init_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                [this] { return initialization_completed_.load(); });
            
            if (!completed) {
                LOG_ERROR("WebSocket初始化超时（" + std::to_string(timeout_ms) + "ms）");
                return false;
            }
            
            if (initialization_failed_) {
                LOG_ERROR("WebSocket初始化失败");
                return false;
            }
            
            LOG_INFO("WebSocket初始化成功完成");
            return true;
        }

        void WebSocketHandlerBase::start() {
            if (running_.load()) {
                LOG_WARNING("WebSocket handler already running");
                return;
            }

            LOG_INFO("Starting WebSocket handler...");

            // 检查是否已经初始化
            if (!event_loop_) {
                LOG_ERROR("EventLoop is null, cannot start WebSocket handler");
                return;
            }

            // 🔧 修复：等待异步初始化完成
            if (!waitForInitialization(5000)) { // 5秒超时
                LOG_ERROR("WebSocket handler initialization failed or timed out");
                return;
            }
            
            LOG_INFO("WebSocket handler initialization completed successfully");

            running_.store(true);

            // 🔧 启动心跳检测 - 使用EventLoop时间轮优化版本
            startTimerBasedHeartbeat();
            
            // 🔧 新增：启动连接状态监控定时器
            startConnectionMonitor();

            LOG_INFO("WebSocket handler started successfully");
            LOG_INFO("Listening on port: " + std::to_string(websocket_port_));

            // 注意：不在这里启动EventLoop！
            // EventLoop应该由HTTP服务器启动，WebSocket Handler只是注册监听通道
            // 这样HTTP和WebSocket可以共享同一个事件循环
            LOG_INFO("WebSocket handler is ready to accept connections");
        }

        void WebSocketHandlerBase::stop() {
            if (!running_.load()) {
                return;
            }

            LOG_INFO("Stopping WebSocket handler...");
            running_.store(false);
            
            // 🔧 停止心跳检测 - 使用EventLoop时间轮优化版本
            stopTimerBasedHeartbeat();
            
            // 🔧 停止连接监控
            stopConnectionMonitor();
            
            // 关闭所有连接，确保所有相关的Channel都被清理
            std::vector<std::shared_ptr<PlayerSessionBase>> sessions_to_close;
            {
                std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
                LOG_INFO("Closing " + std::to_string(player_sessions_.size()) + " WebSocket connections...");

                // 先收集所有会话，避免在锁内调用closeConnection
                for (auto& [player_id, session] : player_sessions_) {
                    if (session) {
                        sessions_to_close.push_back(session);
                    }
                }
                player_sessions_.clear();
            }

            // 在锁外关闭连接，避免死锁
            for (auto& session : sessions_to_close) {
                try {
                    session->closeConnection();
                    LOG_DEBUG("Closed connection for player");
                } catch (const std::exception& e) {
                    LOG_ERROR("Error closing connection: " + std::string(e.what()));
                }
            }
            LOG_INFO("All WebSocket connections closed");

            // 等待所有连接的Channel被EventLoop处理完毕
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // 清理Qt6客户端信息
            {
                std::unique_lock<std::shared_mutex> lock(qt6_info_mutex_);
                qt6_client_info_.clear();
            }

            // 清理监听资源 - 确保正确的清理顺序，避免Channel警告
            LOG_INFO("Cleaning up WebSocket listen resources...");

            if (listen_channel_) {
                try {
                    if (event_loop_) {
                        if (event_loop_->isInLoopThread()) {
                            listen_channel_->disableAll();
                            listen_channel_->remove();
                            listen_channel_.reset();
                        } else {
                            // 在EventLoop线程中安全清理并同步等待
                            auto promise_ptr = std::make_shared<std::promise<void>>();
                            auto future = promise_ptr->get_future();
                            int fd_snapshot = listen_channel_->fd();
                            LOG_DEBUG("Scheduling listen channel cleanup in loop thread (fd=" + std::to_string(fd_snapshot) + ")...");

                            event_loop_->runInLoop([this, promise_ptr]() {
                                try {
                                    if (listen_channel_) {
                                        listen_channel_->disableAll();
                                        listen_channel_->remove();
                                        listen_channel_.reset();
                                    }
                                } catch (const std::exception& e) {
                                    LOG_WARNING("Error during listen channel cleanup in loop thread: " + std::string(e.what()));
                                    listen_channel_.reset();
                                }
                                promise_ptr->set_value();
                            });
                            // 等待清理完成，避免跨线程非法调用
                            future.wait();
                            LOG_DEBUG("Listen channel cleanup completed in loop thread");
                        }
                    } else {
                        // 无EventLoop时尽力清理
                        listen_channel_->disableAll();
                        listen_channel_->remove();
                        listen_channel_.reset();
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("Error during listen channel cleanup: " + std::string(e.what()));
                    // 强制重置，避免资源泄露
                    listen_channel_.reset();
                }
            }

            if (listen_socket_) {
                // 4. 关闭socket
                LOG_DEBUG("Closing listen socket...");
                listen_socket_->forceClose();
                listen_socket_.reset();
                LOG_DEBUG("Listen socket closed");
            }

            listen_address_.reset();

            LOG_INFO("WebSocket handler stopped and resources cleaned up");
        }

        bool WebSocketHandlerBase::addPlayerSession(const std::string& player_id, std::shared_ptr<PlayerSessionBase> session) {
            std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
            
            // 检查是否已存在
            if (player_sessions_.find(player_id) != player_sessions_.end()) {
                LOG_WARNING("Player session already exists: " + player_id);
                return false;
            }

            player_sessions_[player_id] = session;
            LOG_INFO("Player session added: " + player_id);
            
            // 通知连接建立
            notifyConnection(player_id, true);
            
            return true;
        }

        bool WebSocketHandlerBase::removePlayerSession(const std::string& player_id) {
            std::unique_lock<std::shared_mutex> sessions_lock(sessions_mutex_);
            
            auto it = player_sessions_.find(player_id);
            if (it == player_sessions_.end()) {
                return false;
            }

            // 关闭连接
            if (it->second) {
                it->second->closeConnection();
            }
            
            player_sessions_.erase(it);
            sessions_lock.unlock();
            
            // 清理Qt6客户端信息
            {
                std::unique_lock<std::shared_mutex> qt6_lock(qt6_info_mutex_);
                qt6_client_info_.erase(player_id);
            }
            
            LOG_INFO("Player session removed: " + player_id);
            
            // 通知连接断开
            notifyConnection(player_id, false);
            
            return true;
        }

        std::shared_ptr<PlayerSessionBase> WebSocketHandlerBase::getPlayerSession(const std::string& player_id) {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            auto it = player_sessions_.find(player_id);
            if (it != player_sessions_.end()) {
                return it->second;
            }
            
            return nullptr;
        }

        bool WebSocketHandlerBase::isPlayerOnline(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            auto it = player_sessions_.find(player_id);
            return it != player_sessions_.end() && it->second && it->second->isConnectionValid();
        }

        int WebSocketHandlerBase::getOnlinePlayerCount() const {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            int count = 0;
            for (const auto& [player_id, session] : player_sessions_) {
                if (session && session->isConnectionValid()) {
                    count++;
                }
            }
            
            return count;
        }

        std::vector<std::string> WebSocketHandlerBase::getOnlinePlayerIds() const {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            std::vector<std::string> online_players;
            for (const auto& [player_id, session] : player_sessions_) {
                if (session && session->isConnectionValid()) {
                    online_players.push_back(player_id);
                }
            }
            
            return online_players;
        }
        
        std::string WebSocketHandlerBase::getPlayerIdByFd(int client_fd) const {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            // 遍历所有会话，找到匹配fd的player_id
            for (const auto& [player_id, session] : player_sessions_) {
                if (session && session->getConnectionFd() == client_fd) {
                    // 🔧 新增：检查连接是否仍然有效
                    if (session->isConnectionValid()) {
                        return player_id;  // 返回当前的player_id（可能是真实ID）
                    } else {
                        // 连接无效，记录并返回空
                        LOG_DEBUG("🔧 发现无效连接: fd=" + std::to_string(client_fd) + 
                                 ", player_id=" + player_id);
                        return "";
                    }
                }
            }
            
            return "";  // 未找到
        }

        bool WebSocketHandlerBase::sendToPlayer(const std::string& player_id, const nlohmann::json& message) {
            auto session = getPlayerSession(player_id);
            if (!session) {
                LOG_WARNING("❌ 未找到会话: " + player_id);
                return false;
            }
            
            if (!session->isConnectionValid()) {
                LOG_WARNING("❌ 连接无效: " + player_id);
                return false;
            }

            // 为Qt6客户端格式化消息
            auto formatted_message = formatForQt6(message, session);
            
            bool result = session->sendMessage(formatted_message);
            
            // 🔧 优化：只记录重要消息的发送，减少日志量
            std::string msg_type = message.value("type", "unknown");
            if (msg_type == "auth_success" || msg_type == "error" || msg_type == "game_end") {
                LOG_INFO(result ? "✅ 发送成功: " + msg_type + " to " + player_id 
                                : "❌ 发送失败: " + msg_type + " to " + player_id);
            } else {
                LOG_DEBUG("📤 发送: " + msg_type + " to " + player_id);
            }
            
            return result;
        }

        int WebSocketHandlerBase::sendToRoom(const std::string& room_id, const nlohmann::json& message) {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            int sent_count = 0;
            for (const auto& [player_id, session] : player_sessions_) {
                if (session && session->isConnectionValid() && session->getRoomId() == room_id) {
                    auto formatted_message = formatForQt6(message, session);
                    if (session->sendMessage(formatted_message)) {
                        sent_count++;
                    }
                }
            }
            
            return sent_count;
        }

        int WebSocketHandlerBase::broadcastToAll(const nlohmann::json& message) {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
            
            int sent_count = 0;
            for (const auto& [player_id, session] : player_sessions_) {
                if (session && session->isConnectionValid()) {
                    auto formatted_message = formatForQt6(message, player_id);
                    if (session->sendMessage(formatted_message)) {
                        sent_count++;
                    }
                }
            }
            
            return sent_count;
        }

        int WebSocketHandlerBase::sendToPlayers(const std::vector<std::string>& player_ids, const nlohmann::json& message) {
            int sent_count = 0;
            
            for (const auto& player_id : player_ids) {
                if (sendToPlayer(player_id, message)) {
                    sent_count++;
                }
            }
            
            return sent_count;
        }

        int WebSocketHandlerBase::sendQt6GameState(const std::string& room_id, const nlohmann::json& game_state) {
            auto qt6_message = createQt6Message("game_state_update", game_state);
            return sendToRoom(room_id, qt6_message);
        }

        bool WebSocketHandlerBase::sendQt6PlayerActionResult(const std::string& player_id, 
                                                            const std::string& action_type, 
                                                            const nlohmann::json& result) {
            auto qt6_message = createQt6Message("player_action_result", {
                {"action_type", action_type},
                {"result", result},
                {"success", true}
            });
            
            return sendToPlayer(player_id, qt6_message);
        }

        bool WebSocketHandlerBase::sendQt6SystemMessage(const std::string& player_id, 
                                                       const std::string& message_type, 
                                                       const std::string& content) {
            auto qt6_message = createQt6Message("system_message", {
                {"message_type", message_type},
                {"content", content}
            });
            
            return sendToPlayer(player_id, qt6_message);
        }

        int WebSocketHandlerBase::sendQt6RoomStateUpdate(const std::string& room_id, const nlohmann::json& room_state) {
            auto qt6_message = createQt6Message("room_state_update", room_state);
            return sendToRoom(room_id, qt6_message);
        }

        void WebSocketHandlerBase::setQt6ClientInfo(const std::string& player_id, const Qt6ClientInfo& info) {
            std::unique_lock<std::shared_mutex> lock(qt6_info_mutex_);
            qt6_client_info_[player_id] = info;
            
            // 更新玩家会话的Qt6标识
            auto session = getPlayerSession(player_id);
            if (session) {
                session->setQt6Client(true);
                session->setQt6Version(info.qt_version);
                session->setClientVersion(info.client_version);
                session->setSupportsCompression(info.supports_compression);
                session->setPreferredFps(info.preferred_fps);
            }
            
            LOG_INFO("Qt6 client info set for player: " + player_id + " (Qt " + info.qt_version + ")");
        }

        WebSocketHandlerBase::Qt6ClientInfo WebSocketHandlerBase::getQt6ClientInfo(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(qt6_info_mutex_);
            
            auto it = qt6_client_info_.find(player_id);
            if (it != qt6_client_info_.end()) {
                return it->second;
            }
            
            return Qt6ClientInfo{};
        }

        bool WebSocketHandlerBase::isQt6Client(const std::string& player_id) const {
            std::shared_lock<std::shared_mutex> lock(qt6_info_mutex_);
            return qt6_client_info_.find(player_id) != qt6_client_info_.end();
        }

        std::unordered_map<std::string, WebSocketHandlerBase::Qt6ClientInfo> WebSocketHandlerBase::getAllQt6ClientInfo() const {
            std::shared_lock<std::shared_mutex> lock(qt6_info_mutex_);
            return qt6_client_info_;
        }

        // ⚠️ 已废弃：此方法已被基于EventLoop时间轮的startTimerBasedHeartbeat()替代
        // 废弃原因：
        // 1. 使用独立线程，资源开销大
        // 2. 与EventLoop时间轮架构不统一
        // 3. 无法利用统一的事件调度机制
        // 替代方案：使用startTimerBasedHeartbeat()
        void WebSocketHandlerBase::startHeartbeat(int interval_ms) {
            LOG_WARNING("startHeartbeat() is deprecated - use startTimerBasedHeartbeat() instead");
            heartbeat_interval_ms_ = interval_ms;
            
            // 🔧 修复：改为使用EventLoop时间轮版本
            if (event_loop_) {
                startTimerBasedHeartbeat();
            } else {
                LOG_ERROR("EventLoop not available for timer-based heartbeat");
            }
        }

        // ⚠️ 已废弃：此方法已被基于EventLoop时间轮的stopTimerBasedHeartbeat()替代
        void WebSocketHandlerBase::stopHeartbeat() {
            LOG_WARNING("stopHeartbeat() is deprecated - use stopTimerBasedHeartbeat() instead");

            // 🔧 修复：改为使用EventLoop时间轮版本
            stopTimerBasedHeartbeat();
        }

        void WebSocketHandlerBase::setHeartbeatConfig(const HeartbeatConfig& config) {
            if (!config.validate()) {
                LOG_WARNING("Invalid heartbeat config, using defaults");
                return;
            }

            heartbeat_config_ = config;
            heartbeat_interval_ms_ = config.interval_ms;

            LOG_INFO("Heartbeat config updated: interval=" + std::to_string(config.interval_ms) +
                    "ms, timeout=" + std::to_string(config.timeout_ms) +
                    "ms, max_missed=" + std::to_string(config.max_missed));
        }

        void WebSocketHandlerBase::cleanupOfflineConnections() {
            std::vector<std::string> offline_players;

            // 使用配置的心跳参数
            int max_missed = heartbeat_config_.max_missed > 0 ?
                            heartbeat_config_.max_missed : 3;
            int timeout_ms = heartbeat_config_.timeout_ms > 0 ?
                            heartbeat_config_.timeout_ms : 60000;

            {
                std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
                for (const auto& [player_id, session] : player_sessions_) {
                    if (!session || !session->isConnectionValid()) {
                        offline_players.push_back(player_id);
                        continue;
                    }

                    // 检查是否超时
                    if (session->isTimeout(timeout_ms)) {
                        // 增加丢失心跳计数
                        session->incrementMissedHeartbeats();
                        LOG_DEBUG("Player " + player_id + " timeout, missed heartbeats: " +
                                 std::to_string(session->getMissedHeartbeats()));

                        // 如果丢失心跳次数超过阈值，标记为离线
                        if (session->getMissedHeartbeats() >= max_missed) {
                            offline_players.push_back(player_id);
                            LOG_WARNING("Player " + player_id + " marked as offline due to " +
                                       std::to_string(session->getMissedHeartbeats()) + " missed heartbeats");
                        }
                    }
                }
            }

            for (const auto& player_id : offline_players) {
                removePlayerSession(player_id);
                LOG_INFO("Cleaned up offline connection: " + player_id);
            }
        }

        nlohmann::json WebSocketHandlerBase::formatForQt6(const nlohmann::json& message, const std::string& player_id) {
            // 优化：从会话中获取客户端类型，避免每次查询Qt6客户端信息
            auto session = getPlayerSession(player_id);
            if (!session) {
                return message;
            }

            // 检查会话中是否标记为Qt6客户端（需要在PlayerSessionBase中添加此字段）
            // 这里暂时保持原有逻辑，但建议在会话创建时就标记客户端类型
            if (!isQt6Client(player_id)) {
                return message;
            }

            auto qt6_message = message;
            qt6_message["qt6_optimized"] = true;
            qt6_message["client_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 如果客户端支持压缩，添加压缩标记
            auto client_info = getQt6ClientInfo(player_id);
            if (client_info.supports_compression) {
                // 实现实际压缩逻辑
                try {
                    // 检查消息大小是否需要压缩
                    std::string message_str = qt6_message.dump();
                    if (message_str.length() > 1024) { // 大于1KB的消息考虑压缩
                        qt6_message["compressed"] = true;
                        qt6_message["compression_type"] = "gzip";
                        LOG_DEBUG("Qt6消息将被压缩: " + player_id + ", 大小: " + std::to_string(message_str.length()));
                    } else {
                        qt6_message["compressed"] = false;
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("Qt6消息压缩处理失败: " + std::string(e.what()));
                    qt6_message["compressed"] = false;
                }
            }

            return qt6_message;
        }

        nlohmann::json WebSocketHandlerBase::formatForQt6(const nlohmann::json& message, std::shared_ptr<PlayerSessionBase> session) {
            if (!session) {
                return message;
            }

            // 从会话中获取玩家ID
            std::string player_id = session->getPlayerId();

            // 检查是否为Qt6客户端（避免重复查询会话）
            if (!isQt6Client(player_id)) {
                return message;
            }

            auto qt6_message = message;
            qt6_message["qt6_optimized"] = true;
            qt6_message["client_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // 如果客户端支持压缩，添加压缩标记
            auto client_info = getQt6ClientInfo(player_id);
            if (client_info.supports_compression) {
                // 实现实际压缩逻辑
                try {
                    // 检查消息大小是否需要压缩
                    std::string message_str = qt6_message.dump();
                    if (message_str.length() > 1024) { // 大于1KB的消息考虑压缩
                        qt6_message["compressed"] = true;
                        qt6_message["compression_type"] = "gzip";
                        LOG_DEBUG("Qt6消息将被压缩: " + player_id + ", 大小: " + std::to_string(message_str.length()));
                    } else {
                        qt6_message["compressed"] = false;
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING("Qt6消息压缩处理失败: " + std::string(e.what()));
                    qt6_message["compressed"] = false;
                }
            }

            return qt6_message;
        }

        nlohmann::json WebSocketHandlerBase::createQt6Message(const std::string& type, const nlohmann::json& data) {
            return nlohmann::json{
                {"type", type},
                {"data", data},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()},
                {"qt6_optimized", true}
            };
        }

        // ⚠️ 已废弃：此方法已被基于EventLoop时间轮的startTimerBasedHeartbeat()替代
        // 废弃原因：
        // 1. 使用独立线程循环，资源开销大
        // 2. 与EventLoop时间轮架构不统一
        // 3. 无法利用统一的事件调度机制
        // 替代方案：使用EventLoop的runEvery()创建定时器
        void WebSocketHandlerBase::heartbeatLoop() {
            LOG_WARNING("heartbeatLoop() is deprecated - functionality moved to EventLoop timers");
            // ❌ 原实现已移除，请使用EventLoop时间轮版本
            // while (heartbeat_running_.load()) {
            //     std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));
            //     if (!heartbeat_running_.load()) break;
            //     sendHeartbeatToAllClients();
            //     cleanupOfflineConnections();
            // }
        }

        void WebSocketHandlerBase::sendHeartbeatToAllClients() {
            std::shared_lock<std::shared_mutex> lock(sessions_mutex_);

            auto server_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            for (const auto& [player_id, session] : player_sessions_) {
                if (!session) continue;

                try {
                    if (isQt6Client(player_id)) {
                        // Qt6客户端：发送优化的心跳消息
                        auto qt6_heartbeat = createQt6Message("heartbeat", {
                            {"server_time", server_time},
                            {"qt6_optimized", true}
                        });
                        sendToPlayer(player_id, qt6_heartbeat);
                    } else {
                        // 通用客户端：发送标准心跳消息
                        nlohmann::json standard_heartbeat = {
                            {"type", "ping"},
                            {"timestamp", server_time},
                            {"server_status", "running"}
                        };
                        sendToPlayer(player_id, standard_heartbeat);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to send heartbeat to player " + player_id + ": " + std::string(e.what()));
                }
            }

            LOG_DEBUG("Heartbeat sent to " + std::to_string(player_sessions_.size()) + " clients");
        }

        void WebSocketHandlerBase::handleNewConnection() {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            try {
                // 接受新连接
                common::network::InetAddress peer_addr;
                int client_fd = listen_socket_->accept(&peer_addr);

                if (client_fd < 0) {
                    LOG_ERROR("Failed to accept WebSocket connection");
                    PERF_ERROR("websocket_connection", "accept_failed", "websocket_handler");
                    return;
                }

                std::string client_ip = peer_addr.toIpPort();
                LOG_INFO("New WebSocket connection from " + client_ip + " (fd: " + std::to_string(client_fd) + ")");

                // 🔧 优化：记录WebSocket连接事件
                int current_connections = static_cast<int>(player_sessions_.size()) + 1;
                PERF_COUNTER("websocket_connections_total", 1.0, {{"event", "connect"}});
                PERF_GAUGE("websocket_connections_current", static_cast<double>(current_connections));

                // 调用虚函数处理连接
                handleWebSocketConnection(client_fd, client_ip);

                // 🔧 优化：记录连接建立性能
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                PERF_HISTOGRAM("websocket_connection_duration_ms", static_cast<double>(duration.count()));

            } catch (const std::exception& e) {
                LOG_ERROR("Error handling new WebSocket connection: " + std::string(e.what()));
                PERF_ERROR("websocket_connection", "handle_error", "websocket_handler");
            }
        }

        void WebSocketHandlerBase::handleWebSocketConnection(int client_fd, const std::string& client_ip) {
            LOG_INFO("📡 [WS] 新连接: " + client_ip + " fd=" + std::to_string(client_fd));

            try {
                // 1. 检查连接数限制
                if (getOnlinePlayerCount() >= MAX_CONNECTIONS) {
                    LOG_WARNING("❌ 连接数达上限: " + std::to_string(getOnlinePlayerCount()) + 
                               "/" + std::to_string(MAX_CONNECTIONS));
                    sendHttpError(client_fd, 503, "Service Unavailable", "Server connection limit reached");
                    close(client_fd);
                    return;
                }

                // 2. 执行WebSocket握手
                if (!performWebSocketHandshake(client_fd)) {
                    LOG_ERROR("❌ 握手失败: " + client_ip);
                    close(client_fd);
                    return;
                }

                // 3. 生成临时玩家ID
                std::string temp_player_id = generateTempPlayerId(client_ip, client_fd);
                LOG_DEBUG("临时ID: " + temp_player_id);

                // 4. 创建玩家会话
                auto session = createPlayerSession(temp_player_id, client_fd, client_ip);
                if (!session) {
                    LOG_ERROR("❌ 创建会话失败: " + client_ip);
                    close(client_fd);
                    return;
                }

                // 5. 添加到会话管理
                if (!addPlayerSession(temp_player_id, session)) {
                    LOG_ERROR("❌ 添加会话失败: " + client_ip);
                    close(client_fd);
                    return;
                }

                // 6. 创建Channel并设置消息读取回调
                // 🔧 关键修复：通过fd动态查找player_id，避免闭包捕获固化的临时ID
                auto channel = std::make_shared<common::network::Channel>(event_loop_.get(), client_fd);
                // 🔧 关键修复：使用shared_ptr管理Channel，避免无限循环
                auto channel_weak = std::weak_ptr<common::network::Channel>(channel);
                channel->setReadCallback([this, client_fd, channel_weak]() {
                    // 动态查找当前的player_id（可能已从temp_id更新为真实ID）
                    std::string current_player_id = getPlayerIdByFd(client_fd);
                    if (!current_player_id.empty()) {
                        handleWebSocketRead(current_player_id, client_fd);
                    } else {
                        // 🔧 关键修复：如果找不到player_id，立即禁用Channel防止无限循环
                        static std::unordered_set<int> warned_fds;  // 避免重复警告
                        if (warned_fds.find(client_fd) == warned_fds.end()) {
                            LOG_WARNING("⚠️ 未找到fd=" + std::to_string(client_fd) + "对应的player_id，立即禁用Channel");
                            warned_fds.insert(client_fd);
                        }
                        
                        // 立即禁用当前Channel，防止重复触发
                        auto channel_shared = channel_weak.lock();
                        if (channel_shared) {
                            // 🔧 关键：在EventLoop的下一个循环中执行禁用操作
                            if (event_loop_) {
                                event_loop_->runInLoop([channel_shared, client_fd, &warned_fds]() {
                                    channel_shared->disableAll();
                                    channel_shared->remove();
                                    warned_fds.erase(client_fd);  // 清理警告记录
                                    LOG_DEBUG("🔧 已禁用僵尸Channel: fd=" + std::to_string(client_fd));
                                });
                            } else {
                                channel_shared->disableAll();
                                channel_shared->remove();
                                warned_fds.erase(client_fd);
                                LOG_DEBUG("🔧 已禁用僵尸Channel: fd=" + std::to_string(client_fd));
                            }
                        }
                    }
                });
                channel->setCloseCallback([this, client_fd]() {
                    std::string current_player_id = getPlayerIdByFd(client_fd);
                    if (!current_player_id.empty()) {
                        LOG_INFO("WebSocket connection closed for " + current_player_id);
                        handleWebSocketDisconnection(current_player_id);
                    } else {
                        // 🔧 修复：连接关闭但找不到player_id，可能是已经清理的连接
                        LOG_DEBUG("🔧 连接关闭但未找到player_id: fd=" + std::to_string(client_fd) + "，可能已清理");
                    }
                });
                channel->setErrorCallback([this, client_fd]() {
                    std::string current_player_id = getPlayerIdByFd(client_fd);
                    if (!current_player_id.empty()) {
                        LOG_WARNING("WebSocket connection error for " + current_player_id);
                        handleWebSocketDisconnection(current_player_id);
                    } else {
                        // 🔧 修复：连接错误但找不到player_id，可能是已经清理的连接
                        LOG_DEBUG("🔧 连接错误但未找到player_id: fd=" + std::to_string(client_fd) + "，可能已清理");
                    }
                });
                channel->enableReading();

                // 保存Channel到会话中
                session->setChannel(channel);
                
                // 🔧 新增：设置Channel超时清理机制，防止僵尸连接
                if (event_loop_) {
                    // 🔧 修复：缩短清理时间从30秒到10秒，加快fd回收
                    event_loop_->runAfter(10000, [this, client_fd, temp_player_id]() {
                        std::string current_player_id = getPlayerIdByFd(client_fd);
                        if (current_player_id.empty()) {
                            // 🔧 修复：10秒后仍然找不到有效的player_id，强制清理
                            LOG_INFO("🔧 [CLEANUP] 清理超时未认证连接: fd=" + std::to_string(client_fd));
                            
                            // 查找并清理对应的会话
                            std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
                            auto it = player_sessions_.find(temp_player_id);
                            if (it != player_sessions_.end() && it->second) {
                                auto session = it->second;
                                if (session->getConnectionFd() == client_fd) {
                                    player_sessions_.erase(it);
                                    lock.unlock();
                                    
                                    // 关闭连接
                                    session->closeConnection();
                                    LOG_INFO("🔧 [CLEANUP] 已清理超时连接: " + temp_player_id);
                                }
                            }
                        }
                    });
                }

                // 7. 发送欢迎消息（使用安全的直接发送方式）
                try {
                    sendWelcomeMessageDirect(temp_player_id, client_fd);
                    LOG_INFO("Welcome message sent to " + temp_player_id);
                } catch (const std::exception& e) {
                    LOG_WARNING("Failed to send welcome message: " + std::string(e.what()));
                }

                LOG_INFO("WebSocket connection established successfully for " + client_ip +
                        " (player_id: " + temp_player_id + ")");

            } catch (const std::exception& e) {
                LOG_ERROR("Error handling WebSocket connection from " + client_ip + ": " + std::string(e.what()));
                close(client_fd);
            }
        }

        void WebSocketHandlerBase::handleWebSocketMessage(const std::string& player_id, const std::string& message) {
            try {
                // 🔧 优化：减少高频路径的日志输出
                LOG_DEBUG("🎯 [MSG_HANDLER] 处理消息 from " + player_id + ", 长度: " + std::to_string(message.length()));

                // 1. 检查消息大小限制
                if (message.length() > MAX_MESSAGE_SIZE) {
                    LOG_WARNING("❌ 消息太大 from " + player_id + ": " + std::to_string(message.length()) + " bytes");
                    notifyError(player_id, "Message too large");
                    return;
                }

                // 2. 检查消息速率限制
                if (checkMessageRateLimit(player_id)) {
                    LOG_WARNING("❌ 消息速率超限 for " + player_id);
                    notifyError(player_id, "Message rate limit exceeded");
                    return;
                }

                // 3. 解析JSON消息
                auto json_message = nlohmann::json::parse(message);

                // 4. 验证消息格式
                if (!validateMessageFormat(json_message)) {
                    LOG_WARNING("❌ 消息格式无效 from " + player_id);
                    notifyError(player_id, "Invalid message format");
                    return;
                }

                // 5. 检查消息类型并分发处理
                if (json_message.contains("type")) {
                    std::string msg_type = json_message["type"];
                    LOG_INFO("📨 [MSG] " + msg_type + " from " + player_id);

                    // Qt6特定消息
                    if (msg_type == "qt6_client_info" || msg_type == "qt6_heartbeat") {
                        handleQt6SpecificMessage(player_id, json_message);
                        return;
                    }

                    // 心跳响应消息
                    if (msg_type == "pong" || msg_type == "heartbeat_ack") {
                        auto session = getPlayerSession(player_id);
                        if (session) {
                            session->recordHeartbeat();
                            LOG_DEBUG("收到心跳响应 from " + player_id);
                        }
                        return;
                    }

                    // 认证消息
                    if (msg_type == "authenticate") {
                        handleAuthenticationMessage(player_id, json_message);
                        return;
                    }

                    // 房间操作消息（需要先认证）
                    if (msg_type == "create_room" || msg_type == "join_room" ||
                        msg_type == "leave_room" || msg_type == "start_game" ||
                        msg_type == "room_operation") {
                        if (!isPlayerAuthenticated(player_id)) {
                            LOG_WARNING("❌ 未认证操作: " + msg_type + " from " + player_id);
                            sendErrorMessage(player_id, "AUTHENTICATION_REQUIRED",
                                           "Please authenticate before performing room operations");
                            return;
                        }
                        handleRoomOperationMessage(player_id, json_message);
                        return;
                    }
                    
                    // 🔧 游戏逻辑消息（通过notifyMessage转发）
                    LOG_DEBUG("🎮 [MSG] 游戏消息: " + msg_type);
                }

                // 6. 更新玩家活动时间和统计
                auto session = getPlayerSession(player_id);
                if (session) {
                    session->updateLastActivity();
                    session->incrementReceivedMessages();

                    // 记录消息大小统计
                    session->addReceivedMessageSize(message.length());
                }

                // 7. 通知消息接收
                notifyMessage(player_id, json_message);

                LOG_INFO("✅ [MSG_HANDLER] 消息处理完成 from " + player_id + " (type: " +
                         json_message.value("type", "unknown") + ")");

            } catch (const nlohmann::json::parse_error& e) {
                LOG_ERROR("JSON parse error from " + player_id + ": " + std::string(e.what()));
                notifyError(player_id, "Invalid JSON format");
            } catch (const std::exception& e) {
                LOG_ERROR("WebSocket message processing error from " + player_id + ": " + std::string(e.what()));
                notifyError(player_id, "Message processing error");
            }
        }

        void WebSocketHandlerBase::handleWebSocketDisconnection(const std::string& player_id) {
            // 🔧 关键修复：使用静态标记防止重复处理同一玩家的断开
            static std::unordered_set<std::string> disconnecting_players;
            static std::mutex disconnect_mutex;
            
            // RAII guard for automatic cleanup
            class DisconnectGuard {
            private:
                std::string player_id_;
                std::mutex& mutex_;
                std::unordered_set<std::string>& set_;
            public:
                DisconnectGuard(const std::string& id, std::mutex& m, std::unordered_set<std::string>& s)
                    : player_id_(id), mutex_(m), set_(s) {}
                ~DisconnectGuard() {
                    std::lock_guard<std::mutex> guard(mutex_);
                    set_.erase(player_id_);
                }
            };
            
            // 检查是否正在处理
            {
                std::lock_guard<std::mutex> guard(disconnect_mutex);
                if (disconnecting_players.count(player_id) > 0) {
                    LOG_DEBUG("⚠️ 重复调用，跳过: " + player_id);
                    return;
                }
                disconnecting_players.insert(player_id);
            }
            
            // 使用RAII确保自动清理
            DisconnectGuard guard(player_id, disconnect_mutex, disconnecting_players);

            LOG_INFO("🔌 断开: " + player_id);

            try {
                // 1. 检查会话是否存在
                {
                    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
                    auto it = player_sessions_.find(player_id);
                    if (it == player_sessions_.end()) {
                        LOG_DEBUG("会话已不存在: " + player_id);
                        return;
                    }
                }

                // 2. 获取玩家会话信息（用于日志记录）
                std::shared_ptr<PlayerSessionBase> session;
                {
                    // 限制锁的作用域，避免死锁
                    std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
                    auto it = player_sessions_.find(player_id);
                    if (it != player_sessions_.end()) {
                        session = it->second;
                    }
                }

                if (session) {
                    auto connect_duration = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now() - session->getConnectTime()).count();

                    LOG_INFO("Player " + player_id + " disconnected after " +
                            std::to_string(connect_duration) + " seconds, " +
                            "sent " + std::to_string(session->getSentMessagesCount()) + " messages (" +
                            std::to_string(session->getSentBytesCount()) + " bytes), " +
                            "received " + std::to_string(session->getReceivedMessagesCount()) + " messages (" +
                            std::to_string(session->getReceivedBytesCount()) + " bytes)");
                }

                // 3. 延迟移除会话，避免在事件处理中立即销毁Channel
                if (event_loop_) {
                    event_loop_->runInLoop([this, player_id]() {
                        removePlayerSessionSafely(player_id);
                    });
                } else {
                    // 如果EventLoop不可用，直接移除
                    removePlayerSessionSafely(player_id);
                }

                // 3. 记录断开连接统计
                LOG_DEBUG("断开处理完成: " + player_id);

            } catch (const std::exception& e) {
                LOG_ERROR("断开处理异常 " + player_id + ": " + std::string(e.what()));

                // 即使出错也要尝试移除会话
                try {
                    removePlayerSession(player_id);
                } catch (...) {
                    LOG_ERROR("移除会话失败: " + player_id);
                }
            }
            
            // DisconnectGuard会在函数结束时自动清理标记
        }

        void WebSocketHandlerBase::handleQt6SpecificMessage(const std::string& player_id, const nlohmann::json& message) {
            try {
                std::string msg_type = message["type"];
                
                if (msg_type == "qt6_client_info") {
                    Qt6ClientInfo info;
                    info.qt_version = message.value("qt_version", "");
                    info.client_version = message.value("client_version", "");
                    info.supports_compression = message.value("supports_compression", false);
                    info.preferred_fps = message.value("preferred_fps", 60);
                    info.platform = message.value("platform", "");
                    info.connect_time = std::chrono::system_clock::now();
                    
                    setQt6ClientInfo(player_id, info);
                    
                    // 发送服务器配置给Qt6客户端
                    auto server_config = createQt6Message("server_config", {
                        {"qt6_support", true},
                        {"compression_enabled", true},
                        {"heartbeat_interval_ms", heartbeat_interval_ms_},
                        {"max_message_size", 65536}
                    });
                    
                    sendToPlayer(player_id, server_config);
                }
                else if (msg_type == "qt6_heartbeat") {
                    auto session = getPlayerSession(player_id);
                    if (session) {
                        session->recordHeartbeat();
                    }
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Qt6 message handling error: " + std::string(e.what()));
                sendQt6SystemMessage(player_id, "error", "Invalid Qt6 message format");
            }
        }

        void WebSocketHandlerBase::notifyConnection(const std::string& player_id, bool connected) {
            if (connection_callback_) {
                try {
                    connection_callback_(player_id, connected);
                } catch (const std::exception& e) {
                    LOG_ERROR("Connection callback error: " + std::string(e.what()));
                }
            }
        }

        void WebSocketHandlerBase::notifyMessage(const std::string& player_id, const nlohmann::json& message) {
            if (message_callback_) {
                try {
                    message_callback_(player_id, message);
                } catch (const std::exception& e) {
                    LOG_ERROR("Message callback error: " + std::string(e.what()));
                }
            }
        }

        void WebSocketHandlerBase::notifyError(const std::string& player_id, const std::string& error) {
            if (error_callback_) {
                try {
                    error_callback_(player_id, error);
                } catch (const std::exception& e) {
                    LOG_ERROR("Error callback error: " + std::string(e.what()));
                }
            }
        }

        bool WebSocketHandlerBase::performWebSocketHandshake(int client_fd) {
            // 完整的WebSocket握手实现，符合RFC 6455标准
            try {
                // 1. 读取HTTP请求头
                std::string request = readHttpRequest(client_fd);
                if (request.empty()) {
                    LOG_ERROR("❌ [HANDSHAKE] 无法读取握手请求");
                    return false;
                }

                LOG_DEBUG("收到握手请求:\n" + request);

                // 2. 解析HTTP请求头
                auto headers = parseHttpHeaders(request);

                // 3. 验证WebSocket升级请求
                if (!validateWebSocketRequest(headers)) {
                    LOG_ERROR("❌ [HANDSHAKE] 升级请求验证失败");
                    sendHttpError(client_fd, 400, "Bad Request", "Invalid WebSocket upgrade request");
                    return false;
                }

                // 4. 提取Sec-WebSocket-Key
                auto key_it = headers.find("sec-websocket-key");
                if (key_it == headers.end()) {
                    LOG_ERROR("❌ [HANDSHAKE] 缺少Sec-WebSocket-Key");
                    sendHttpError(client_fd, 400, "Bad Request", "Missing Sec-WebSocket-Key");
                    return false;
                }

                std::string client_key = key_it->second;

                // 5. 计算Sec-WebSocket-Accept
                std::string accept_key = calculateWebSocketAccept(client_key);
                if (accept_key.empty()) {
                    LOG_ERROR("❌ [HANDSHAKE] 计算Accept密钥失败");
                    sendHttpError(client_fd, 500, "Internal Server Error", "Handshake calculation failed");
                    return false;
                }

                // 6. 检查支持的协议
                std::vector<std::string> supported_protocols = getSupportedProtocols();
                std::string selected_protocol = selectProtocol(headers, supported_protocols);

                // 7. 构建并发送握手响应
                std::string response = buildWebSocketResponse(accept_key, selected_protocol);
                if (!sendHandshakeResponse(client_fd, response)) {
                    LOG_ERROR("❌ [HANDSHAKE] 发送握手响应失败");
                    return false;
                }

                // 8. 重置套接字为WebSocket模式
                resetSocketForWebSocket(client_fd);

                LOG_INFO("✅ [HANDSHAKE] 握手成功 fd=" + std::to_string(client_fd) +
                        (selected_protocol.empty() ? "" : " protocol=" + selected_protocol));
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("❌ [HANDSHAKE] 异常: " + std::string(e.what()));
                sendHttpError(client_fd, 500, "Internal Server Error", "Handshake failed");
                return false;
            }
        }

        std::string WebSocketHandlerBase::generateTempPlayerId(const std::string& client_ip, int client_fd) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            // 🔧 优化：简化临时ID格式，提高可读性
            // 原格式: temp_7783796182752091490_21_1759296938324 (太长，难读)
            // 新格式: temp_21_938324 (简洁，易于调试)
            
            // 只使用时间戳的后6位（毫秒级唯一性在单服务器环境下足够）
            int64_t short_timestamp = timestamp % 1000000;
            
            std::stringstream ss;
            ss << "temp_" << client_fd << "_" << short_timestamp;
            
            LOG_DEBUG("生成临时ID: " + ss.str() + " for fd=" + std::to_string(client_fd));

            return ss.str();
        }

        // 注意：此方法已改为纯虚函数，由子类实现具体的Session创建
        // 因为PlayerSessionBase是抽象类，无法直接实例化

        void WebSocketHandlerBase::sendWelcomeMessage(const std::string& player_id) {
            try {
                nlohmann::json welcome_message = {
                    {"type", "welcome"},
                    {"player_id", player_id},
                    {"server_info", {
                        {"name", "Game WebSocket Server"},
                        {"version", "1.0.0"},
                        {"features", nlohmann::json::array({"qt6_support", "compression", "heartbeat"})}
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };

                sendToPlayer(player_id, welcome_message);
                LOG_DEBUG("Welcome message sent to " + player_id);

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to send welcome message to " + player_id + ": " + std::string(e.what()));
            }
        }

        void WebSocketHandlerBase::scheduleWelcomeMessage(const std::string& player_id) {
            // 使用简单的延迟机制，避免在连接建立时的递归调用
            // 这里使用一个简单的标记，在下次消息处理时发送欢迎消息
            try {
                auto session = getPlayerSession(player_id);
                if (session) {
                    // 标记需要发送欢迎消息
                    // 注意：这里不直接调用sendToPlayer，避免递归
                    LOG_INFO("Welcome message scheduled for " + player_id);

                    // 实现真正的异步调度
                    // 使用异步方式发送欢迎消息，避免阻塞主线程
                    std::thread([this, player_id]() {
                        try {
                            // 短暂延迟确保连接完全建立
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            sendWelcomeMessage(player_id);
                            LOG_DEBUG("异步发送欢迎消息完成: " + player_id);
                        } catch (const std::exception& e) {
                            LOG_ERROR("异步发送欢迎消息失败: " + player_id + ", 错误: " + std::string(e.what()));
                        }
                    }).detach(); // 分离线程
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to schedule welcome message for " + player_id + ": " + std::string(e.what()));
            }
        }

        void WebSocketHandlerBase::sendWelcomeMessageDirect(const std::string& player_id, int client_fd) {
            try {
                // 创建欢迎消息
                nlohmann::json welcome_message = {
                    {"type", "welcome"},
                    {"player_id", player_id},
                    {"server_info", {
                        {"name", "Game WebSocket Server"},
                        {"version", "1.0.0"},
                        {"features", nlohmann::json::array({"qt6_support", "compression", "heartbeat"})}
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };

                // 直接发送到WebSocket连接，避免通过会话管理器
                std::string message_str = welcome_message.dump();

                // 构建WebSocket帧
                std::vector<uint8_t> frame;

                // WebSocket帧头：FIN=1, RSV=000, Opcode=0001 (text frame)
                frame.push_back(0x81);

                // 负载长度
                size_t payload_len = message_str.length();
                if (payload_len < 126) {
                    frame.push_back(static_cast<uint8_t>(payload_len));
                } else if (payload_len < 65536) {
                    frame.push_back(126);
                    frame.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));
                    frame.push_back(static_cast<uint8_t>(payload_len & 0xFF));
                } else {
                    frame.push_back(127);
                    for (int i = 7; i >= 0; i--) {
                        frame.push_back(static_cast<uint8_t>((payload_len >> (i * 8)) & 0xFF));
                    }
                }

                // 添加负载数据
                frame.insert(frame.end(), message_str.begin(), message_str.end());

                // 发送帧
                ssize_t sent = send(client_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
                if (sent != static_cast<ssize_t>(frame.size())) {
                    LOG_WARNING("Failed to send complete welcome message frame");
                } else {
                    LOG_DEBUG("Welcome message sent directly to fd " + std::to_string(client_fd));
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Error sending welcome message directly: " + std::string(e.what()));
            }
        }

        void WebSocketHandlerBase::handleWebSocketRead(const std::string& player_id, int client_fd) {
            try {
                // 读取WebSocket帧
                char buffer[4096];
                ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);

                if (bytes_read <= 0) {
                    if (bytes_read == 0) {
                        LOG_INFO("WebSocket client disconnected: " + player_id);
                    } else {
                        LOG_ERROR("Error reading from WebSocket: " + std::string(strerror(errno)));
                    }

                    // 延迟处理断开，避免在读取回调中立即销毁Channel
                    if (event_loop_) {
                        event_loop_->queueInLoop([this, player_id]() {
                            handleWebSocketDisconnection(player_id);
                        });
                    } else {
                        handleWebSocketDisconnection(player_id);
                    }
                    return;
                }

                // 解析WebSocket帧
                std::string message = parseWebSocketFrame(buffer, bytes_read);
                if (!message.empty()) {
                    // 🔧 优化：只在DEBUG级别记录完整消息内容
                    LOG_DEBUG("📨 [WS] " + player_id + ": " + message);
                    
                    // 调用消息处理函数
                    handleWebSocketMessage(player_id, message);
                } else {
                    // 🔧 修复：检查是否为关闭帧，避免不必要的警告
                    if (bytes_read >= 2) {
                        const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer);
                        uint8_t opcode = data[0] & 0x0F;
                        
                        if (opcode == 8) {
                            // 关闭帧，正常断开连接
                            LOG_DEBUG("📋 [WS] 客户端发送关闭帧，准备断开连接: " + player_id);
                            handleWebSocketDisconnection(player_id);
                            return; // 避免继续处理
                        } else if (opcode == 9 || opcode == 10) {
                            // Ping/Pong帧，静默处理
                            LOG_DEBUG("🏓 [WS] 收到心跳帧: opcode=" + std::to_string(opcode));
                        } else {
                            // 其他未知帧类型才记录警告
                            LOG_WARNING("⚠️ 未处理的帧类型: opcode=" + std::to_string(opcode) + 
                                      ", bytes=" + std::to_string(bytes_read));
                        }
                    } else {
                        LOG_WARNING("⚠️ 帧长度不足: bytes=" + std::to_string(bytes_read));
                    }
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Error handling WebSocket read for " + player_id + ": " + std::string(e.what()));

                // 延迟处理断开，避免在读取回调中立即销毁Channel
                if (event_loop_) {
                    event_loop_->queueInLoop([this, player_id]() {
                        handleWebSocketDisconnection(player_id);
                    });
                } else {
                    handleWebSocketDisconnection(player_id);
                }
            }
        }

        std::string WebSocketHandlerBase::parseWebSocketFrame(const char* buffer, size_t length) {
            try {
                if (length < 2) {
                    LOG_WARNING("WebSocket frame too short");
                    return "";
                }

                const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer);

                // 解析第一个字节
                bool fin = (data[0] & 0x80) != 0;
                uint8_t opcode = data[0] & 0x0F;

                // 解析第二个字节
                bool masked = (data[1] & 0x80) != 0;
                uint64_t payload_len = data[1] & 0x7F;

                size_t header_len = 2;

                // 处理扩展负载长度
                if (payload_len == 126) {
                    if (length < 4) return "";
                    payload_len = (data[2] << 8) | data[3];
                    header_len = 4;
                } else if (payload_len == 127) {
                    if (length < 10) return "";
                    payload_len = 0;
                    for (int i = 0; i < 8; i++) {
                        payload_len = (payload_len << 8) | data[2 + i];
                    }
                    header_len = 10;
                }

                // 处理掩码
                uint8_t mask[4] = {0};
                if (masked) {
                    if (length < header_len + 4) return "";
                    memcpy(mask, data + header_len, 4);
                    header_len += 4;
                }

                // 检查数据完整性
                if (length < header_len + payload_len) {
                    LOG_WARNING("Incomplete WebSocket frame");
                    return "";
                }

                // 提取负载数据
                std::string payload;
                payload.reserve(payload_len);

                for (uint64_t i = 0; i < payload_len; i++) {
                    uint8_t byte = data[header_len + i];
                    if (masked) {
                        byte ^= mask[i % 4];
                    }
                    payload.push_back(static_cast<char>(byte));
                }

                // 处理不同类型的帧
                if (fin) {
                    switch (opcode) {
                        case 1: // 文本帧
                            return payload;
                        case 8: // 关闭帧
                            LOG_DEBUG("📋 [WS] 收到关闭帧: " + std::to_string(payload_len) + " bytes");
                            return ""; // 关闭帧不需要返回消息内容
                        case 9: // Ping帧
                            LOG_DEBUG("🏓 [WS] 收到Ping帧");
                            return ""; 
                        case 10: // Pong帧
                            LOG_DEBUG("🏓 [WS] 收到Pong帧");
                            return "";
                        case 2: // 二进制帧
                            LOG_DEBUG("📦 [WS] 收到二进制帧: " + std::to_string(payload_len) + " bytes");
                            return "";
                        default:
                            LOG_DEBUG("❓ [WS] 未知帧类型: opcode=" + std::to_string(opcode));
                            return "";
                    }
                }

                return "";

            } catch (const std::exception& e) {
                LOG_ERROR("Error parsing WebSocket frame: " + std::string(e.what()));
                return "";
            }
        }

        void WebSocketHandlerBase::removePlayerSessionSafely(const std::string& player_id) {
            try {
                std::shared_ptr<PlayerSessionBase> removed_session;
                std::shared_ptr<common::network::Channel> channel_to_disable;

                // 第一步：获取会话和 Channel，然后在锁外操作
                {
                    std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
                    auto it = player_sessions_.find(player_id);
                    if (it != player_sessions_.end()) {
                        removed_session = it->second;
                        if (removed_session) {
                            channel_to_disable = removed_session->getChannel();
                        }
                        player_sessions_.erase(it);
                    }
                }

                if (!removed_session) {
                    return;
                }

                // 第二步：在 EventLoop 线程中禁用 Channel，确保 epoll 安全移除
                if (channel_to_disable && event_loop_) {
                    // 必须在 EventLoop 线程中禁用 Channel
                    // 使用 shared_ptr 捕获，确保 Channel 在回调执行时仍然有效
                    auto channel_shared = channel_to_disable;
                    event_loop_->runInLoop([channel_shared]() {
                        try {
                            channel_shared->disableAll();
                            channel_shared->remove();
                            LOG_DEBUG("Channel safely disabled and removed from epoll");
                        } catch (const std::exception& e) {
                            LOG_ERROR("Error disabling channel: " + std::string(e.what()));
                        }
                    });
                    // 等待一小段时间确保 Channel 操作完成
                    // 注意：不能在这里等待，因为是单线程 EventLoop
                    // 依靠 shared_ptr 保持 Channel 存活直到回调执行完毕
                }

                // 第三步：触发连接断开回调
                if (connection_callback_) {
                    try {
                        connection_callback_(player_id, false);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Connection callback error during disconnection: " + std::string(e.what()));
                    }
                }

                LOG_INFO("Player session removed: " + player_id);

                // removed_session 和 channel_to_disable 会在这里自动析构
                // 由于使用了 shared_ptr，Channel 会等到所有引用消失后才销毁
                // 这包括上面 runInLoop 中捕获的 shared_ptr

            } catch (const std::exception& e) {
                LOG_ERROR("Error removing player session " + player_id + ": " + std::string(e.what()));
            }
        }

        bool WebSocketHandlerBase::checkMessageRateLimit(const std::string& player_id) {
            auto session = getPlayerSession(player_id);
            if (!session) {
                return false; // 如果会话不存在，不限制
            }

            auto now = std::chrono::system_clock::now();

            // 线程安全的速率限制：每秒最多MESSAGE_RATE_LIMIT条消息
            static std::mutex rate_limit_mutex;
            static std::unordered_map<std::string, std::pair<int, std::chrono::system_clock::time_point>> rate_limits;

            std::lock_guard<std::mutex> lock(rate_limit_mutex);

            auto& [count, last_reset] = rate_limits[player_id];
            auto time_since_reset = std::chrono::duration_cast<std::chrono::seconds>(now - last_reset).count();

            if (time_since_reset >= 1) {
                // 重置计数器
                count = 0;
                last_reset = now;
            }

            count++;
            bool rate_limited = count > MESSAGE_RATE_LIMIT;

            // 定期清理过期的速率限制记录（避免内存泄漏）
            static auto last_cleanup = now;
            if (std::chrono::duration_cast<std::chrono::minutes>(now - last_cleanup).count() >= 5) {
                auto it = rate_limits.begin();
                while (it != rate_limits.end()) {
                    if (std::chrono::duration_cast<std::chrono::minutes>(now - it->second.second).count() >= 5) {
                        it = rate_limits.erase(it);
                    } else {
                        ++it;
                    }
                }
                last_cleanup = now;
            }

            return rate_limited;
        }

        bool WebSocketHandlerBase::validateMessageFormat(const nlohmann::json& message) {
            try {
                // 基本格式验证
                if (!message.is_object()) {
                    LOG_DEBUG("Message validation failed: not an object");
                    return false;
                }

                // 必须包含type字段
                if (!message.contains("type") || !message["type"].is_string()) {
                    LOG_DEBUG("Message validation failed: missing or invalid type field");
                    return false;
                }

                // type字段不能为空
                std::string msg_type = message["type"];
                if (msg_type.empty()) {
                    LOG_DEBUG("Message validation failed: empty type field");
                    return false;
                }

                // 检查消息大小（JSON序列化后的大小）
                std::string serialized = message.dump();
                if (serialized.length() > MAX_MESSAGE_SIZE) {
                    LOG_WARNING("Message validation failed: message too large (" +
                               std::to_string(serialized.length()) + " > " +
                               std::to_string(MAX_MESSAGE_SIZE) + ")");
                    return false;
                }

                // 业务相关验证：检查已知的消息类型
                static const std::set<std::string> valid_message_types = {
                    // 认证相关
                    "authenticate", "client_info", "qt6_client_info", "qt6_heartbeat", "pong",
                    // 心跳相关
                    "ping", "heartbeat", "heartbeat_ack",
                    // 房间操作
                    "create_room", "join_room", "leave_room", "start_game", "room_operation",
                    // 游戏操作
                    "player_action", "change_direction", "pause", "resume",
                    // 其他功能
                    "spectate_request", "chat_message", "client_settings"
                };

                if (valid_message_types.find(msg_type) == valid_message_types.end()) {
                    LOG_DEBUG("Message validation warning: unknown message type '" + msg_type + "'");
                    // 不拒绝未知消息类型，但记录警告，允许扩展性
                }

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Message validation error: " + std::string(e.what()));
                return false;
            }
        }

        std::string WebSocketHandlerBase::readHttpRequest(int client_fd) {
            std::string request;
            char buffer[4096];

            try {
                // 设置接收超时（仅用于握手阶段）
                struct timeval timeout;
                timeout.tv_sec = 10;  // 10秒超时
                timeout.tv_usec = 0;
                if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                    LOG_WARNING("Failed to set receive timeout for handshake");
                }

                // 读取HTTP请求，直到遇到\r\n\r\n
                while (request.find("\r\n\r\n") == std::string::npos) {
                    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read <= 0) {
                        if (bytes_read == 0) {
                            LOG_WARNING("Client closed connection during handshake");
                        } else {
                            LOG_ERROR("Error reading handshake request: " + std::string(strerror(errno)));
                        }
                        return "";
                    }

                    buffer[bytes_read] = '\0';
                    request += buffer;

                    // 防止请求过大
                    if (request.length() > 8192) {
                        LOG_ERROR("HTTP request too large during WebSocket handshake");
                        return "";
                    }
                }

                return request;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception reading HTTP request: " + std::string(e.what()));
                return "";
            }
        }

        std::unordered_map<std::string, std::string> WebSocketHandlerBase::parseHttpHeaders(const std::string& request) {
            std::unordered_map<std::string, std::string> headers;

            try {
                std::istringstream stream(request);
                std::string line;

                // 跳过请求行（GET /path HTTP/1.1）
                if (!std::getline(stream, line)) {
                    return headers;
                }

                // 解析请求头
                while (std::getline(stream, line) && line != "\r") {
                    // 移除\r字符
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    // 查找冒号分隔符
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string key = line.substr(0, colon_pos);
                        std::string value = line.substr(colon_pos + 1);

                        // 去除前后空格并转换为小写
                        key.erase(0, key.find_first_not_of(" \t"));
                        key.erase(key.find_last_not_of(" \t") + 1);
                        value.erase(0, value.find_first_not_of(" \t"));
                        value.erase(value.find_last_not_of(" \t") + 1);

                        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

                        headers[key] = value;
                    }
                }

                LOG_DEBUG("Parsed " + std::to_string(headers.size()) + " HTTP headers");
                return headers;

            } catch (const std::exception& e) {
                LOG_ERROR("Error parsing HTTP headers: " + std::string(e.what()));
                return headers;
            }
        }

        bool WebSocketHandlerBase::validateWebSocketRequest(const std::unordered_map<std::string, std::string>& headers) {
            try {
                // 检查必需的头部字段
                auto upgrade_it = headers.find("upgrade");
                if (upgrade_it == headers.end() || upgrade_it->second != "websocket") {
                    LOG_ERROR("Missing or invalid Upgrade header");
                    return false;
                }

                auto connection_it = headers.find("connection");
                if (connection_it == headers.end()) {
                    LOG_ERROR("Missing Connection header");
                    return false;
                }

                // Connection头可能包含多个值，检查是否包含"upgrade"
                std::string connection_value = connection_it->second;
                std::transform(connection_value.begin(), connection_value.end(), connection_value.begin(), ::tolower);
                if (connection_value.find("upgrade") == std::string::npos) {
                    LOG_ERROR("Connection header does not contain 'upgrade'");
                    return false;
                }

                // 检查WebSocket版本
                auto version_it = headers.find("sec-websocket-version");
                if (version_it == headers.end() || version_it->second != "13") {
                    LOG_ERROR("Missing or unsupported Sec-WebSocket-Version (expected 13)");
                    return false;
                }

                // 检查Sec-WebSocket-Key
                auto key_it = headers.find("sec-websocket-key");
                if (key_it == headers.end() || key_it->second.empty()) {
                    LOG_ERROR("Missing or empty Sec-WebSocket-Key");
                    return false;
                }

                // 验证Sec-WebSocket-Key格式（应该是16字节的base64编码）
                std::string key = key_it->second;
                if (key.length() != 24) {  // 16字节base64编码后是24字符
                    LOG_ERROR("Invalid Sec-WebSocket-Key length");
                    return false;
                }

                LOG_DEBUG("WebSocket request validation passed");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Error validating WebSocket request: " + std::string(e.what()));
                return false;
            }
        }

        std::string WebSocketHandlerBase::calculateWebSocketAccept(const std::string& client_key) {
            try {
                // WebSocket魔法字符串（RFC 6455）
                const std::string magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

                // 连接客户端密钥和魔法字符串
                std::string combined = client_key + magic_string;

                // 计算SHA-1哈希
                unsigned char hash[SHA_DIGEST_LENGTH];
                SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);

                // Base64编码
                BIO* bio = BIO_new(BIO_s_mem());
                BIO* b64 = BIO_new(BIO_f_base64());
                BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
                bio = BIO_push(b64, bio);

                BIO_write(bio, hash, SHA_DIGEST_LENGTH);
                BIO_flush(bio);

                BUF_MEM* buffer_ptr;
                BIO_get_mem_ptr(bio, &buffer_ptr);

                std::string result(buffer_ptr->data, buffer_ptr->length);
                BIO_free_all(bio);

                LOG_DEBUG("Calculated WebSocket accept key: " + result);
                return result;

            } catch (const std::exception& e) {
                LOG_ERROR("Error calculating WebSocket accept key: " + std::string(e.what()));
                return "";
            }
        }

        std::vector<std::string> WebSocketHandlerBase::getSupportedProtocols() {
            // 基类返回默认支持的协议，子类可以重写
            return {
                "snake-game-v1",      // Snake游戏协议v1
                "qt6-optimized",      // Qt6优化协议
                "game-base-v1"        // 通用游戏协议v1
            };
        }

        std::string WebSocketHandlerBase::selectProtocol(const std::unordered_map<std::string, std::string>& headers,
                                                        const std::vector<std::string>& supported_protocols) {
            try {
                auto protocol_it = headers.find("sec-websocket-protocol");
                if (protocol_it == headers.end()) {
                    LOG_DEBUG("No Sec-WebSocket-Protocol header found, using default protocol");
                    return "";  // 没有指定协议，使用默认
                }

                std::string requested_protocols = protocol_it->second;
                LOG_DEBUG("Client requested protocols: " + requested_protocols);

                // 解析客户端请求的协议列表（逗号分隔）
                std::vector<std::string> client_protocols;
                std::stringstream ss(requested_protocols);
                std::string protocol;

                while (std::getline(ss, protocol, ',')) {
                    // 去除前后空格
                    protocol.erase(0, protocol.find_first_not_of(" \t"));
                    protocol.erase(protocol.find_last_not_of(" \t") + 1);
                    if (!protocol.empty()) {
                        client_protocols.push_back(protocol);
                    }
                }

                // 按优先级选择协议（服务器支持的协议优先级）
                for (const auto& supported : supported_protocols) {
                    for (const auto& client : client_protocols) {
                        if (supported == client) {
                            LOG_INFO("Selected WebSocket protocol: " + supported);
                            return supported;
                        }
                    }
                }

                LOG_WARNING("No matching protocol found between client and server");
                return "";  // 没有匹配的协议

            } catch (const std::exception& e) {
                LOG_ERROR("Error selecting WebSocket protocol: " + std::string(e.what()));
                return "";
            }
        }

        std::string WebSocketHandlerBase::buildWebSocketResponse(const std::string& accept_key, const std::string& protocol) {
            try {
                std::ostringstream response;

                // HTTP状态行
                response << "HTTP/1.1 101 Switching Protocols\r\n";

                // 必需的WebSocket头部
                response << "Upgrade: websocket\r\n";
                response << "Connection: Upgrade\r\n";
                response << "Sec-WebSocket-Accept: " << accept_key << "\r\n";

                // 如果选择了协议，添加协议头
                if (!protocol.empty()) {
                    response << "Sec-WebSocket-Protocol: " << protocol << "\r\n";
                }

                // 可选的服务器信息头
                response << "Server: GameWebSocketServer/1.0\r\n";

                // 结束头部
                response << "\r\n";

                std::string result = response.str();
                LOG_DEBUG("Built WebSocket response:\n" + result);
                return result;

            } catch (const std::exception& e) {
                LOG_ERROR("Error building WebSocket response: " + std::string(e.what()));
                return "";
            }
        }

        bool WebSocketHandlerBase::sendHandshakeResponse(int client_fd, const std::string& response) {
            try {
                // 直接使用底层send，但加入项目的错误处理和日志
                // 注意：这里仍然使用send()是合理的，因为：
                // 1. 握手阶段还没有创建Socket对象
                // 2. 这是一次性的HTTP响应，不是持续的WebSocket通信
                // 3. 避免了Socket对象生命周期管理的复杂性

                // 设置发送超时
                struct timeval timeout;
                timeout.tv_sec = 5;  // 5秒超时
                timeout.tv_usec = 0;
                if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
                    LOG_WARNING("Failed to set send timeout for handshake response");
                }

                // 发送响应
                ssize_t bytes_sent = send(client_fd, response.c_str(), response.length(), MSG_NOSIGNAL);
                if (bytes_sent < 0) {
                    LOG_ERROR("Failed to send handshake response: " + std::string(strerror(errno)));
                    return false;
                }

                if (bytes_sent != static_cast<ssize_t>(response.length())) {
                    LOG_ERROR("Incomplete handshake response sent: " +
                             std::to_string(bytes_sent) + "/" + std::to_string(response.length()));
                    return false;
                }

                LOG_DEBUG("WebSocket handshake response sent successfully (" +
                         std::to_string(bytes_sent) + " bytes)");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Error sending handshake response: " + std::string(e.what()));
                return false;
            }
        }

        void WebSocketHandlerBase::sendHttpError(int client_fd, int status_code, const std::string& status_text, const std::string& message) {
            try {
                std::ostringstream response;

                // HTTP状态行
                response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";

                // 头部
                response << "Content-Type: text/plain\r\n";
                response << "Content-Length: " << message.length() << "\r\n";
                response << "Connection: close\r\n";
                response << "\r\n";

                // 消息体
                response << message;

                std::string response_str = response.str();

                // 使用统一的发送方法
                if (!sendHandshakeResponse(client_fd, response_str)) {
                    LOG_ERROR("Failed to send HTTP error response");
                }

                LOG_DEBUG("Sent HTTP error " + std::to_string(status_code) + " to client");

            } catch (const std::exception& e) {
                LOG_ERROR("Error sending HTTP error response: " + std::string(e.what()));
            }
        }

        void WebSocketHandlerBase::resetSocketForWebSocket(int client_fd) {
            try {
                // 重置接收超时为无限制（WebSocket是长连接）
                struct timeval timeout;
                timeout.tv_sec = 0;   // 无超时
                timeout.tv_usec = 0;
                if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                    LOG_WARNING("Failed to reset receive timeout for WebSocket");
                }

                // 重置发送超时为无限制
                if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
                    LOG_WARNING("Failed to reset send timeout for WebSocket");
                }

                // 设置TCP_NODELAY以减少延迟（实时游戏重要）
                int flag = 1;
                if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
                    LOG_WARNING("Failed to set TCP_NODELAY for WebSocket");
                }

                // 设置SO_KEEPALIVE以维持长连接
                flag = 1;
                if (setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag)) < 0) {
                    LOG_WARNING("Failed to set SO_KEEPALIVE for WebSocket");
                }

                LOG_DEBUG("Socket reset for WebSocket mode successfully");

            } catch (const std::exception& e) {
                LOG_ERROR("Error resetting socket for WebSocket: " + std::string(e.what()));
            }
        }

        void WebSocketHandlerBase::handleAuthenticationMessage(const std::string& player_id, const nlohmann::json& message) {
            try {
                // 🔧 优化：提取token和playerId
                std::string token;
                std::string real_player_id;
                
                // 支持两种格式：data嵌套 和 顶层字段
                if (message.contains("data") && message["data"].is_object()) {
                    auto data = message["data"];
                    token = data.value("token", "");
                    real_player_id = data.value("playerId", "");
                } else {
                    token = message.value("token", message.value("session_token", ""));
                }

                // 验证token
                if (token.empty()) {
                    LOG_WARNING("❌ [AUTH] 缺少令牌 from " + player_id);
                    sendErrorMessage(player_id, "MISSING_TOKEN", "Authentication token is required");
                    return;
                }

                // 🔧 优化：ID映射更新（如果提供了真实ID）
                if (!real_player_id.empty() && real_player_id != player_id) {
                    LOG_INFO("🔄 [AUTH] ID映射: " + player_id + " -> " + real_player_id);

                    auto temp_session = getPlayerSession(player_id);
                    if (temp_session) {
                        temp_session->setPlayerId(real_player_id);

                        {
                            std::unique_lock<std::shared_mutex> lock(sessions_mutex_);
                            player_sessions_.erase(player_id);
                            player_sessions_[real_player_id] = temp_session;
                        }

                        LOG_INFO("✅ [AUTH] 映射成功: " + real_player_id);

                        // 🔧 修复：通知外部更新在线玩家列表中的ID
                        if (player_id_change_callback_) {
                            try {
                                player_id_change_callback_(player_id, real_player_id);
                            } catch (const std::exception& e) {
                                LOG_ERROR("Player ID change callback error: " + std::string(e.what()));
                            }
                        }
                    }
                }

                // 发送认证成功响应
                std::string final_player_id = real_player_id.empty() ? player_id : real_player_id;
                
                nlohmann::json auth_response = {
                    {"type", "auth_success"},
                    {"data", {
                        {"playerId", final_player_id},
                        {"authenticated", true}
                    }},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };

                sendToPlayer(final_player_id, auth_response);
                LOG_INFO("✅ [AUTH] 认证成功: " + final_player_id);

                // 🔧 修复：通知子类认证成功，以便子类可以更新认证状态
                setPlayerAuthenticated(final_player_id, true);

            } catch (const std::exception& e) {
                LOG_ERROR("❌ [AUTH] 异常: " + std::string(e.what()));
                sendErrorMessage(player_id, "AUTHENTICATION_ERROR", "Authentication failed");
            }
        }

        void WebSocketHandlerBase::handleRoomOperationMessage(const std::string& player_id, const nlohmann::json& message) {
            try {
                std::string msg_type = message.value("type", "unknown");

                // 处理 room_operation 格式（嵌套操作）
                if (msg_type == "room_operation") {
                    // 从 data 中提取操作类型
                    if (message.contains("data") && message["data"].is_object()) {
                        const auto& data = message["data"];
                        std::string operation = data.value("operation", "");

                        if (operation == "start_game") {
                            // 构建内部消息格式并调用处理器
                            nlohmann::json internal_msg = {
                                {"type", "start_game"},
                                {"data", data}
                            };
                            handleStartGameWebSocket(player_id, internal_msg);
                            return;
                        } else if (operation == "ready") {
                            // 支持通过 room_operation 发送准备状态
                            nlohmann::json internal_msg = {
                                {"type", "ready"},
                                {"data", data}
                            };
                            // 转发到 notifyMessage 让子类处理
                            notifyMessage(player_id, internal_msg);
                            return;
                        } else {
                            LOG_WARNING("Unknown room_operation: " + operation);
                            sendErrorMessage(player_id, "UNKNOWN_OPERATION",
                                           "Unknown room operation: " + operation);
                            return;
                        }
                    } else {
                        LOG_WARNING("room_operation missing data field");
                        sendErrorMessage(player_id, "INVALID_MESSAGE",
                                       "room_operation requires data field");
                        return;
                    }
                }

                // 处理直接消息类型
                if (msg_type == "create_room") {
                    handleCreateRoomWebSocket(player_id, message);
                } else if (msg_type == "join_room") {
                    handleJoinRoomWebSocket(player_id, message);
                } else if (msg_type == "leave_room") {
                    handleLeaveRoomWebSocket(player_id, message);
                } else if (msg_type == "start_game") {
                    handleStartGameWebSocket(player_id, message);
                } else {
                    LOG_WARNING("Unknown room operation: " + msg_type);
                    sendErrorMessage(player_id, "UNKNOWN_OPERATION",
                                   "Unknown room operation: " + msg_type);
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Error handling room operation message: " + std::string(e.what()));
                sendErrorMessage(player_id, "ROOM_OPERATION_ERROR", "Failed to process room operation");
            }
        }

        bool WebSocketHandlerBase::isPlayerAuthenticated(const std::string& player_id) {
            // 基类默认实现：检查玩家会话是否存在
            // 子类应该重写此方法实现实际的认证状态检查
            auto session = getPlayerSession(player_id);
            if (!session) {
                return false;
            }

            // 基类简单认为有会话就是已认证（子类应该实现更严格的检查）
            LOG_DEBUG("Using default authentication check - subclass should override isPlayerAuthenticated()");
            return true;
        }

        void WebSocketHandlerBase::setPlayerAuthenticated(const std::string& player_id, bool authenticated) {
            // 基类默认实现：简单记录日志
            // 子类应该重写此方法实现实际的认证状态设置
            LOG_DEBUG("Setting authentication status for player " + player_id + ": " +
                     (authenticated ? "authenticated" : "not authenticated"));
            LOG_DEBUG("Base class setPlayerAuthenticated() called - subclass should override this method");
        }

        void WebSocketHandlerBase::sendErrorMessage(const std::string& player_id, const std::string& error_code, const std::string& error_message) {
            try {
                nlohmann::json error_response = {
                    {"type", "error"},
                    {"error_code", error_code},
                    {"error_message", error_message},
                    {"player_id", player_id},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };

                sendToPlayer(player_id, error_response);
                LOG_DEBUG("Error message sent to " + player_id + ": " + error_code + " - " + error_message);

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to send error message to " + player_id + ": " + std::string(e.what()));
            }
        }

        bool WebSocketHandlerBase::sendRawMessage(const std::string& player_id, const std::string& message) {
            // 获取玩家会话
            auto session = getPlayerSession(player_id);
            if (!session) {
                LOG_WARNING("No session found for player: " + player_id);
                return false;
            }

            // 获取连接文件描述符
            int connection_fd = session->getConnectionFd();
            if (connection_fd < 0) {
                LOG_ERROR("Invalid connection file descriptor for player: " + player_id);
                return false;
            }

            if (!session->isConnectionValid()) {
                LOG_WARNING("Connection invalid for player: " + player_id);
                return false;
            }

            try {
                // 1. 将消息格式化为WebSocket帧
                std::vector<uint8_t> websocket_frame = createWebSocketFrame(message);

                // 2. 通过socket发送到客户端
                ssize_t total_sent = 0;
                size_t total_size = websocket_frame.size();

                while (total_sent < static_cast<ssize_t>(total_size)) {
                    ssize_t sent = ::send(connection_fd,
                                        websocket_frame.data() + total_sent,
                                        total_size - total_sent,
                                        MSG_NOSIGNAL);  // 避免SIGPIPE信号

                    if (sent < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // 非阻塞socket暂时无法发送，稍后重试
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            continue;
                        } else if (errno == EPIPE || errno == ECONNRESET) {
                            // 连接已断开
                            LOG_WARNING("Connection broken while sending to " + player_id + ": " + std::string(strerror(errno)));
                            session->markConnectionInvalid();
                            return false;
                        } else {
                            // 其他发送错误
                            LOG_ERROR("Send error for " + player_id + ": " + std::string(strerror(errno)));
                            return false;
                        }
                    } else if (sent == 0) {
                        // 连接已关闭
                        LOG_WARNING("Connection closed while sending to " + player_id);
                        session->markConnectionInvalid();
                        return false;
                    }

                    total_sent += sent;
                }

                // 3. 更新统计信息
                session->updateLastActivity();
                LOG_DEBUG("Successfully sent " + std::to_string(total_size) + " bytes to " + player_id);

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Failed to send raw message to " + player_id + ": " + std::string(e.what()));
                session->markConnectionInvalid();
                return false;
            }
        }

        void WebSocketHandlerBase::handleCreateRoomWebSocket(const std::string& player_id, const nlohmann::json& message) {
            // 基类提供默认实现，子类应该重写
            LOG_WARNING("handleCreateRoomWebSocket not implemented in base class - subclass should override");
            sendErrorMessage(player_id, "NOT_IMPLEMENTED",
                           "Create room operation is not implemented in this WebSocket handler");
        }

        void WebSocketHandlerBase::handleJoinRoomWebSocket(const std::string& player_id, const nlohmann::json& message) {
            // 基类提供默认实现，子类应该重写
            LOG_WARNING("handleJoinRoomWebSocket not implemented in base class - subclass should override");
            sendErrorMessage(player_id, "NOT_IMPLEMENTED",
                           "Join room operation is not implemented in this WebSocket handler");
        }

        void WebSocketHandlerBase::handleLeaveRoomWebSocket(const std::string& player_id, const nlohmann::json& message) {
            // 基类提供默认实现，子类应该重写
            LOG_WARNING("handleLeaveRoomWebSocket not implemented in base class - subclass should override");
            sendErrorMessage(player_id, "NOT_IMPLEMENTED",
                           "Leave room operation is not implemented in this WebSocket handler");
        }

        void WebSocketHandlerBase::handleStartGameWebSocket(const std::string& player_id, const nlohmann::json& message) {
            // 基类提供默认实现，子类应该重写
            LOG_WARNING("handleStartGameWebSocket not implemented in base class - subclass should override");
            sendErrorMessage(player_id, "NOT_IMPLEMENTED",
                           "Start game operation is not implemented in this WebSocket handler");
        }

        std::vector<uint8_t> WebSocketHandlerBase::createWebSocketFrame(const std::string& message) {
            std::vector<uint8_t> frame;

            // WebSocket帧格式（RFC 6455标准）
            size_t payload_length = message.length();

            // 第一个字节：FIN=1, RSV=000, Opcode=0001 (text frame)
            frame.push_back(0x81);  // 10000001

            // 第二个字节：MASK=0 (服务器发送给客户端不需要掩码), Payload length
            if (payload_length < 126) {
                frame.push_back(static_cast<uint8_t>(payload_length));
            } else if (payload_length < 65536) {
                frame.push_back(126);
                frame.push_back(static_cast<uint8_t>((payload_length >> 8) & 0xFF));
                frame.push_back(static_cast<uint8_t>(payload_length & 0xFF));
            } else {
                frame.push_back(127);
                // 64位长度，高32位为0
                frame.push_back(0);
                frame.push_back(0);
                frame.push_back(0);
                frame.push_back(0);
                // 低32位
                frame.push_back(static_cast<uint8_t>((payload_length >> 24) & 0xFF));
                frame.push_back(static_cast<uint8_t>((payload_length >> 16) & 0xFF));
                frame.push_back(static_cast<uint8_t>((payload_length >> 8) & 0xFF));
                frame.push_back(static_cast<uint8_t>(payload_length & 0xFF));
            }

            // 添加消息内容
            frame.insert(frame.end(), message.begin(), message.end());

            return frame;
        }

        bool WebSocketHandlerBase::sendCloseFrame(const std::string& player_id, uint16_t close_code, const std::string& reason) {
            // 获取玩家会话
            auto session = getPlayerSession(player_id);
            if (!session) {
                LOG_WARNING("No session found for player: " + player_id);
                return false;
            }

            // 获取连接文件描述符
            int connection_fd = session->getConnectionFd();
            if (connection_fd < 0) {
                LOG_ERROR("Invalid connection file descriptor for player: " + player_id);
                return false;
            }

            if (!session->isConnectionValid()) {
                LOG_DEBUG("Connection already invalid for player: " + player_id);
                return false;
            }

            try {
                // 创建WebSocket关闭帧（RFC 6455标准）
                std::vector<uint8_t> close_frame;

                // 第一个字节：FIN=1, RSV=000, Opcode=1000 (close frame)
                close_frame.push_back(0x88);  // 10001000

                // 计算payload长度（2字节关闭代码 + 原因字符串）
                size_t payload_length = 2 + reason.length();

                // 第二个字节：MASK=0, Payload length
                if (payload_length < 126) {
                    close_frame.push_back(static_cast<uint8_t>(payload_length));
                } else {
                    // 关闭帧通常很短，不需要处理长payload的情况
                    close_frame.push_back(static_cast<uint8_t>(payload_length));
                }

                // 添加关闭代码（大端序）
                close_frame.push_back(static_cast<uint8_t>((close_code >> 8) & 0xFF));
                close_frame.push_back(static_cast<uint8_t>(close_code & 0xFF));

                // 添加关闭原因
                close_frame.insert(close_frame.end(), reason.begin(), reason.end());

                // 发送关闭帧
                ssize_t sent = ::send(connection_fd, close_frame.data(), close_frame.size(), MSG_NOSIGNAL);
                if (sent > 0) {
                    LOG_DEBUG("WebSocket close frame sent to player: " + player_id +
                             " (code: " + std::to_string(close_code) + ", reason: " + reason + ")");
                    return true;
                } else {
                    LOG_DEBUG("Failed to send WebSocket close frame to player: " + player_id);
                    return false;
                }

            } catch (const std::exception& e) {
                LOG_WARNING("Error sending WebSocket close frame to player " + player_id + ": " + std::string(e.what()));
                return false;
            }
        }

        bool WebSocketHandlerBase::validateGameSessionTokenInHandshake(const std::unordered_map<std::string, std::string>& headers, int client_fd) {
            try {
                // 1. 查找Authorization头
                auto auth_it = headers.find("authorization");
                if (auth_it == headers.end()) {
                    LOG_WARNING("Missing Authorization header in WebSocket handshake");
                    return false;
                }

                std::string auth_header = auth_it->second;

                // 2. 解析Bearer令牌
                if (auth_header.substr(0, 7) != "Bearer ") {
                    LOG_WARNING("Invalid Authorization header format, expected 'Bearer <token>'");
                    return false;
                }

                std::string token = auth_header.substr(7);
                if (token.empty()) {
                    LOG_WARNING("Empty game session token in Authorization header");
                    return false;
                }

                // 3. 验证游戏会话令牌（这里需要子类实现具体的验证逻辑）
                if (!validateGameSessionToken(token, client_fd)) {
                    LOG_WARNING("Invalid game session token: " + token.substr(0, 20) + "...");
                    return false;
                }

                LOG_DEBUG("Game session token validated successfully in handshake");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Error validating game session token in handshake: " + std::string(e.what()));
                return false;
            }
        }

        bool WebSocketHandlerBase::validateGameSessionToken(const std::string& token, int client_fd) {
            // 基类提供默认实现（总是返回true），子类应该重写此方法
            // 在实际的游戏服务器中，这里应该调用认证服务验证令牌
            LOG_DEBUG("Base class validateGameSessionToken called, token: " + token.substr(0, 20) + "...");
            return true;
        }

        // ==================== 🔧 优化：基于EventLoop时间轮的心跳机制 ====================

        void WebSocketHandlerBase::startTimerBasedHeartbeat() {
            if (!event_loop_) {
                LOG_ERROR("WebSocketHandlerBase: EventLoop cannot be null for timer-based heartbeat");
                return;
            }

            heartbeat_running_.store(true);
            
            LOG_INFO("Starting timer-based heartbeat (replacing thread-based heartbeat)");

            // 🔧 修复：使用runInLoop确保在EventLoop线程中执行定时器设置
            event_loop_->runInLoop([this]() {
                try {
                    // 1. 心跳定时器（定期发送心跳消息）
                    heartbeat_timer_id_ = event_loop_->runEvery(
                        heartbeat_interval_ms_,
                        [this]() {
                            if (!heartbeat_running_.load()) return;
                            try {
                                sendHeartbeatToAllClients();
                                LOG_DEBUG("WebSocketHandlerBase timer-based heartbeat completed");
                            } catch (const std::exception& e) {
                                LOG_ERROR("WebSocketHandlerBase timer-based heartbeat error: " + std::string(e.what()));
                            } catch (...) {
                                LOG_ERROR("WebSocketHandlerBase timer-based heartbeat unknown error");
                            }
                        }
                    );

                    // 2. 清理定时器（🔧 修复：缩短清理间隔从60秒到20秒，加快僵尸连接清理）
                    cleanup_timer_id_ = event_loop_->runEvery(
                        20000, // 🔧 修复：20秒清理一次离线连接，防止fd堆积
                        [this]() {
                            if (!heartbeat_running_.load()) return;
                            try {
                                cleanupOfflineConnections();
                                LOG_DEBUG("WebSocketHandlerBase timer-based cleanup completed");
                            } catch (const std::exception& e) {
                                LOG_ERROR("WebSocketHandlerBase timer-based cleanup error: " + std::string(e.what()));
                            } catch (...) {
                                LOG_ERROR("WebSocketHandlerBase timer-based cleanup unknown error");
                            }
                        }
                    );

                    // 检查所有定时器是否启动成功
                    if (heartbeat_timer_id_ != 0 && cleanup_timer_id_ != 0) {
                        LOG_INFO("WebSocketHandlerBase timer-based heartbeat started successfully:");
                        LOG_INFO("  - Heartbeat timer ID: " + std::to_string(heartbeat_timer_id_) + " (interval: " + 
                                std::to_string(heartbeat_interval_ms_) + "ms)");
                        LOG_INFO("  - Cleanup timer ID: " + std::to_string(cleanup_timer_id_) + " (interval: 60s)");
                    } else {
                        LOG_ERROR("Failed to start WebSocketHandlerBase timer-based heartbeat");
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in WebSocketHandlerBase timer setup: " + std::string(e.what()));
                } catch (...) {
                    LOG_ERROR("Unknown exception in WebSocketHandlerBase timer setup");
                }
            });
        }

        void WebSocketHandlerBase::stopTimerBasedHeartbeat() {
            if (!event_loop_) {
                return;
            }

            LOG_INFO("Stopping WebSocketHandlerBase timer-based heartbeat");
            heartbeat_running_.store(false);

            if (heartbeat_timer_id_ != 0) {
                event_loop_->cancelTimer(heartbeat_timer_id_);
                LOG_DEBUG("WebSocketHandlerBase heartbeat timer cancelled: " + std::to_string(heartbeat_timer_id_));
                heartbeat_timer_id_ = 0;
            }

            if (cleanup_timer_id_ != 0) {
                event_loop_->cancelTimer(cleanup_timer_id_);
                LOG_DEBUG("WebSocketHandlerBase cleanup timer cancelled: " + std::to_string(cleanup_timer_id_));
                cleanup_timer_id_ = 0;
            }

            LOG_INFO("WebSocketHandlerBase timer-based heartbeat stopped");
        }
        
        void WebSocketHandlerBase::startConnectionMonitor() {
            if (!event_loop_) {
                LOG_ERROR("WebSocketHandlerBase: EventLoop cannot be null for connection monitor");
                return;
            }

            LOG_INFO("Starting connection monitor");

            // 🔧 使用runInLoop确保在EventLoop线程中执行定时器设置
            event_loop_->runInLoop([this]() {
                try {
                    // 连接监控定时器（🔧 修复：缩短监控间隔从10秒到5秒，更积极清理无效连接）
                    connection_monitor_timer_id_ = event_loop_->runEvery(
                        5000, // 🔧 修复：5秒检查一次，更快发现和清理无效连接
                        [this]() {
                            if (!heartbeat_running_.load()) return;
                            try {
                                monitorConnectionHealth();
                                LOG_DEBUG("Connection monitor check completed");
                            } catch (const std::exception& e) {
                                LOG_ERROR("Connection monitor error: " + std::string(e.what()));
                            } catch (...) {
                                LOG_ERROR("Connection monitor unknown error");
                            }
                        }
                    );

                    if (connection_monitor_timer_id_ != 0) {
                        LOG_INFO("Connection monitor started successfully, timer ID: " + 
                                std::to_string(connection_monitor_timer_id_));
                    } else {
                        LOG_ERROR("Failed to start connection monitor");
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in connection monitor setup: " + std::string(e.what()));
                } catch (...) {
                    LOG_ERROR("Unknown exception in connection monitor setup");
                }
            });
        }

        void WebSocketHandlerBase::stopConnectionMonitor() {
            if (!event_loop_) {
                return;
            }

            LOG_INFO("Stopping connection monitor");

            if (connection_monitor_timer_id_ != 0) {
                event_loop_->cancelTimer(connection_monitor_timer_id_);
                LOG_DEBUG("Connection monitor timer cancelled: " + std::to_string(connection_monitor_timer_id_));
                connection_monitor_timer_id_ = 0;
            }

            LOG_INFO("Connection monitor stopped");
        }
        
        void WebSocketHandlerBase::monitorConnectionHealth() {
            std::vector<std::string> invalid_players;
            
            // 🔧 检查所有连接的健康状态
            {
                std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
                for (const auto& [player_id, session] : player_sessions_) {
                    if (!session || !session->isConnectionValid()) {
                        invalid_players.push_back(player_id);
                        LOG_DEBUG("🔧 发现无效连接: " + player_id + 
                                 " (fd=" + std::to_string(session ? session->getConnectionFd() : -1) + ")");
                    }
                }
            }
            
            // 🔧 清理无效连接
            for (const auto& player_id : invalid_players) {
                LOG_INFO("🔧 [MONITOR] 清理无效连接: " + player_id);
                handleWebSocketDisconnection(player_id);
            }
            
            // 🔧 统计信息
            if (!invalid_players.empty()) {
                LOG_INFO("🔧 [MONITOR] 本次清理了 " + std::to_string(invalid_players.size()) + " 个无效连接");
            }
            
            // 🔧 记录当前连接状态
            int total_connections = 0;
            int valid_connections = 0;
            {
                std::shared_lock<std::shared_mutex> lock(sessions_mutex_);
                total_connections = static_cast<int>(player_sessions_.size());
                for (const auto& [player_id, session] : player_sessions_) {
                    if (session && session->isConnectionValid()) {
                        valid_connections++;
                    }
                }
            }
            
            LOG_DEBUG("🔧 [MONITOR] 连接状态: 总连接=" + std::to_string(total_connections) + 
                     ", 有效连接=" + std::to_string(valid_connections));
        }

    } // namespace game_base
} // namespace game_services
