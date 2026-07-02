/**
 * @file websocket_native_server.cpp
 * @brief WebSocket原生服务器实现
 * @date 2025-12-11
 */

#include "common/websocket/native/websocket_native_server.h"
#include "common/websocket/native/websocket_handshake.h"
// #include "common/websocket/merchant_connection_manager.h"  // ⭐ 已废弃
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>  // ⭐ Phase 6: 用于重复登录错误消息
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace common {
namespace websocket {
namespace native {

using namespace common::logger;
using namespace common::network;

// ==================== 构造/析构函数 ====================

WebSocketNativeServer::WebSocketNativeServer(
    std::shared_ptr<EventLoop> event_loop,
    uint16_t port,
    const std::string& bind_addr
)
    : event_loop_(event_loop)
    , port_(port)
    , bind_addr_(bind_addr)
{
    LOG_DEBUG("WebSocket server created: port=" + std::to_string(port_));
}

WebSocketNativeServer::~WebSocketNativeServer() {
    stop();
}

// ==================== 生命周期管理 ====================

bool WebSocketNativeServer::start() {
    if (running_.load()) {
        LOG_WARNING("服务器已在运行中");
        return false;
    }
    
    // ⭐⭐⭐ 验证EventLoop有效性
    if (!event_loop_) {
        LOG_ERROR("❌ EventLoop无效，无法启动WebSocket服务器");
        return false;
    }
    
    LOG_DEBUG("Creating WebSocket listen socket...");
    
    // 创建监听socket
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }
    LOG_DEBUG("Socket created: fd=" + std::to_string(listen_fd));
    
    // 设置socket选项
    int opt = 1;
    if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARNING("设置SO_REUSEADDR失败：" + std::string(strerror(errno)));
    }
    if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        LOG_WARNING("设置SO_REUSEPORT失败：" + std::string(strerror(errno)));
    }
    
    // 设置非阻塞
    int flags = ::fcntl(listen_fd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("Failed to get socket flags: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return false;
    }
    if (::fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("Failed to set non-blocking: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return false;
    }
    
    // 绑定地址
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(bind_addr_.c_str());
    
    if (::bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Bind failed: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return false;
    }
    
    // 监听 - 使用更大的backlog
    if (::listen(listen_fd, 1024) < 0) {
        LOG_ERROR("Listen failed: " + std::string(strerror(errno)));
        ::close(listen_fd);
        return false;
    }
    
    // 创建Socket对象
    listen_socket_ = std::make_unique<Socket>(listen_fd);
    
    // 创建Channel
    accept_channel_ = std::make_shared<Channel>(event_loop_.get(), listen_fd);
    
    // 使用weak_ptr避免循环引用
    std::weak_ptr<WebSocketNativeServer> weak_self = shared_from_this();
    accept_channel_->setReadCallback([weak_self, listen_fd]() {
        auto self = weak_self.lock();
        if (!self) {
            LOG_ERROR("WebSocket server destroyed, ignoring read event");
            return;
        }
        LOG_DEBUG("Accept channel triggered: fd=" + std::to_string(listen_fd));
        self->handleNewConnection();
    });
    
    // 设置错误回调
    accept_channel_->setErrorCallback([listen_fd]() {
        LOG_ERROR("Listen socket error: fd=" + std::to_string(listen_fd) +
                  ", errno=" + std::to_string(errno) + " (" + std::string(strerror(errno)) + ")");
    });
    
    // 启用读事件
    accept_channel_->enableReading();
    
    running_.store(true);
    
    LOG_INFO("WebSocket server started: " + bind_addr_ + ":" + std::to_string(port_) + 
             ", fd=" + std::to_string(listen_fd));
    return true;
}

void WebSocketNativeServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& pair : connections_) {
            pair.second->shutdown();
        }
        connections_.clear();
    }
    
    // 关闭监听
    if (accept_channel_) {
        accept_channel_->disableAll();
        accept_channel_->remove();
        accept_channel_.reset();
    }
    
    if (listen_socket_) {
        listen_socket_.reset();
    }
    
    // 清空IP计数
    {
        std::lock_guard<std::mutex> lock(ip_count_mutex_);
        ip_connection_count_.clear();
    }
    
    LOG_INFO("WebSocket原生服务器已停止");
}

// ==================== 统计信息 ====================

size_t WebSocketNativeServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

size_t WebSocketNativeServer::getConnectionCountByIP(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(ip_count_mutex_);
    auto it = ip_connection_count_.find(ip);
    return it != ip_connection_count_.end() ? it->second : 0;
}

// ==================== 内部方法 ====================

void WebSocketNativeServer::handleNewConnection() {
    if (!listen_socket_) {
        LOG_ERROR("listen_socket_ is null");
        return;
    }
    
    int listen_fd = listen_socket_->fd();
    if (listen_fd < 0) {
        LOG_ERROR("Invalid listen_fd: " + std::to_string(listen_fd));
        return;
    }
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int accept_count = 0;
    
    // Loop to accept all pending connections (required for edge-triggered mode)
    while (true) {
        addr_len = sizeof(client_addr);
        
        int client_fd = ::accept(listen_fd, 
                                (struct sockaddr*)&client_addr, 
                                &addr_len);
        
        if (client_fd < 0) {
            int err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                // Normal: all pending connections accepted
                break;
            } else if (err == EINTR) {
                continue;
            } else {
                LOG_ERROR("accept failed: errno=" + std::to_string(err) + 
                         " (" + std::string(strerror(err)) + ")");
            }
            break;
        }
        
        accept_count++;
        
        // Get client IP
        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        int client_port = ntohs(client_addr.sin_port);
        
        LOG_DEBUG("New TCP connection: fd=" + std::to_string(client_fd) + 
                 ", ip=" + client_ip + ":" + std::to_string(client_port));
        
        // Check connection limit
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            if (connections_.size() >= max_connections_) {
                LOG_WARNING("Max connections reached: " + std::to_string(max_connections_) +
                           ", rejecting fd=" + std::to_string(client_fd));
                
                // 发送503 Service Unavailable响应
                std::string error_response = 
                    "HTTP/1.1 503 Service Unavailable\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 24\r\n"
                    "\r\n"
                    "Too many connections";
                ::send(client_fd, error_response.c_str(), error_response.size(), 0);
                ::close(client_fd);
                continue;
            }
        }
        
        // Check IP limit
        if (!checkIPLimit(client_ip)) {
            LOG_WARNING("IP connection limit exceeded: " + client_ip + ", rejecting fd=" + std::to_string(client_fd));
            
            // 发送429 Too Many Requests响应
            std::string error_response = 
                "HTTP/1.1 429 Too Many Requests\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 25\r\n"
                "\r\n"
                "Too many connections from IP";
            ::send(client_fd, error_response.c_str(), error_response.size(), 0);
            ::close(client_fd);
            continue;
        }
        
        // Set non-blocking
        int flags = ::fcntl(client_fd, F_GETFL, 0);
        if (flags == -1) {
            LOG_ERROR("Failed to get socket flags: fd=" + std::to_string(client_fd));
            ::close(client_fd);
            continue;
        }
        if (::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            LOG_ERROR("Failed to set non-blocking: fd=" + std::to_string(client_fd));
            ::close(client_fd);
            continue;
        }
        
        // Set TCP_NODELAY
        int opt = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        
        // ⭐⭐⭐ 启用 TCP Keepalive 检测半开连接
        int keepalive = 1;
        int keepidle = 10;     // 10秒无数据后开始探测
        int keepintvl = 5;     // 探测间隔5秒
        int keepcnt = 3;       // 探测3次失败则认为断开
        ::setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
        
        // Create temporary Channel for WebSocket handshake
        auto handshake_channel = std::make_shared<Channel>(event_loop_.get(), client_fd);
        
        // 使用weak_ptr确保安全
        std::weak_ptr<WebSocketNativeServer> weak_self = shared_from_this();
        auto handshake_buffer = std::make_shared<std::string>();
        handshake_buffer->reserve(4096);
        
        // Set read callback for handshake
        handshake_channel->setReadCallback([weak_self, client_fd, client_ip, handshake_channel, handshake_buffer]() {
            auto self = weak_self.lock();
            if (!self) {
                LOG_ERROR("WebSocket server destroyed, closing fd=" + std::to_string(client_fd));
                handshake_channel->disableAll();
                handshake_channel->remove();
                ::close(client_fd);
                return;
            }
            
            char buffer[4096];
            ssize_t n = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            
            if (n < 0) {
                int err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK) {
                    return; // Wait for next readable event
                }
                LOG_ERROR("Handshake read failed: fd=" + std::to_string(client_fd) + 
                         ", errno=" + std::to_string(err));
                handshake_channel->disableAll();
                handshake_channel->remove();
                ::close(client_fd);
                return;
            }
            
            if (n == 0) {
                LOG_DEBUG("Client closed during handshake: fd=" + std::to_string(client_fd));
                handshake_channel->disableAll();
                handshake_channel->remove();
                ::close(client_fd);
                return;
            }
            
            // Accumulate handshake data
            buffer[n] = '\0';
            handshake_buffer->append(buffer, n);
            
            // Check if HTTP headers complete
            if (handshake_buffer->find("\r\n\r\n") == std::string::npos) {
                return; // Wait for more data
            }
            
            // Parse HTTP request
            common::http::HttpRequest req;
            if (!req.parseFromRawData(*handshake_buffer)) {
                LOG_ERROR("Failed to parse HTTP request: fd=" + std::to_string(client_fd));
                
                std::string error_response = 
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 19\r\n"
                    "\r\n"
                    "Invalid HTTP request";
                ::send(client_fd, error_response.c_str(), error_response.size(), 0);
                
                handshake_channel->disableAll();
                handshake_channel->remove();
                ::close(client_fd);
                return;
            }
            
            // Disable temporary Channel
            handshake_channel->disableAll();
            handshake_channel->remove();
            
            // Process WebSocket upgrade
            self->handleWebSocketUpgrade(client_fd, client_ip, req);
        });
        
        // Set error callback
        handshake_channel->setErrorCallback([client_fd, handshake_channel]() {
            LOG_ERROR("Handshake socket error: fd=" + std::to_string(client_fd));
            handshake_channel->disableAll();
            handshake_channel->remove();
            ::close(client_fd);
        });
        
        // Enable reading
        handshake_channel->enableReading();
    }
    
    if (accept_count > 0) {
        LOG_DEBUG("Accepted " + std::to_string(accept_count) + " new connections");
    }
}

void WebSocketNativeServer::handleWebSocketUpgrade(
    int client_fd,
    const std::string& socket_ip,
    const common::http::HttpRequest& req
) {
    // ⭐⭐⭐ 关键修复：优先从代理头部获取真实客户端IP
    // 当通过Nginx等反向代理时，TCP连接的对端IP是代理服务器IP（如127.0.0.1）
    // 真实客户端IP会被放在 X-Real-IP 或 X-Forwarded-For 头部中
    std::string client_ip = socket_ip;  // 默认使用socket IP
    
    // 优先级：X-Real-IP > X-Forwarded-For > socket IP
    std::string real_ip = req.getHeader("X-Real-IP");
    if (!real_ip.empty()) {
        // 去除首尾空格
        size_t start = real_ip.find_first_not_of(" \t");
        size_t end = real_ip.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            client_ip = real_ip.substr(start, end - start + 1);
        } else if (start != std::string::npos) {
            client_ip = real_ip.substr(start);
        } else {
            client_ip = real_ip;
        }
        LOG_DEBUG("从x-Real-IP头获取真实客户端IP: " + client_ip + " (socket: " + socket_ip + ")");
    } else {
        // 尝试从X-Forwarded-For获取
        std::string forwarded_for = req.getHeader("X-Forwarded-For");
        if (!forwarded_for.empty()) {
            // X-Forwarded-For可能包含多个IP，取第一个（真实客户端IP）
            size_t comma_pos = forwarded_for.find(',');
            if (comma_pos != std::string::npos) {
                forwarded_for = forwarded_for.substr(0, comma_pos);
            }
            // 去除首尾空格
            size_t start = forwarded_for.find_first_not_of(" \t");
            size_t end = forwarded_for.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                client_ip = forwarded_for.substr(start, end - start + 1);
            } else if (start != std::string::npos) {
                client_ip = forwarded_for.substr(start);
            } else {
                client_ip = forwarded_for;
            }
            LOG_DEBUG("从X-Forwarded-For头获取真实客户端IP: " + client_ip + " (socket: " + socket_ip + ")");
        }
    }
    
    LOG_DEBUG("Processing WebSocket upgrade: fd=" + std::to_string(client_fd) + ", ip=" + client_ip);
    
    // Check request path
    std::string request_path = req.getPath();
    
    // Allowed paths: / or /ws/merchant
    if (request_path != "/" && request_path != "/ws/merchant") {
        LOG_WARNING("Unsupported WebSocket path: " + request_path);
        
        std::string error_response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 23\r\n"
            "\r\n"
            "WebSocket path not found";
        
        ::send(client_fd, error_response.c_str(), error_response.size(), 0);
        ::close(client_fd);
        return;
    }
    
    // 1. Validate handshake request
    if (!WebSocketHandshake::isWebSocketRequest(req)) {
        LOG_ERROR("Invalid WebSocket handshake: fd=" + std::to_string(client_fd));
        
        // 发送400 Bad Request响应
        std::string error_response = 
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 25\r\n"
            "\r\n"
            "Invalid WebSocket request";
        
        ::send(client_fd, error_response.c_str(), error_response.size(), 0);
        ::close(client_fd);
        return;
    }
    
    // 2. Generate Sec-WebSocket-Accept
    std::string ws_key = req.getHeader("Sec-WebSocket-Key");
    if (ws_key.empty()) {
        LOG_ERROR("Missing Sec-WebSocket-Key: fd=" + std::to_string(client_fd));
        ::close(client_fd);
        return;
    }
    
    std::string accept_key = WebSocketHandshake::generateAcceptKey(ws_key);
    
    // 3. Build 101 Switching Protocols response
    std::string upgrade_response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
        "\r\n";
    
    // 4. Send 101 response
    ssize_t sent = ::send(client_fd, upgrade_response.c_str(), upgrade_response.size(), 0);
    if (sent <= 0) {
        LOG_ERROR("Failed to send 101 response: fd=" + std::to_string(client_fd));
        ::close(client_fd);
        return;
    }
    
    LOG_DEBUG("WebSocket handshake complete: fd=" + std::to_string(client_fd));
    
    // 5. Create WebSocket connection
    createWebSocketConnection(client_fd, client_ip);
}

void WebSocketNativeServer::removeConnection(int fd) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(fd);
    LOG_DEBUG("Connection removed: fd=" + std::to_string(fd) + 
             ", remaining=" + std::to_string(connections_.size()));
}

bool WebSocketNativeServer::checkIPLimit(const std::string& ip) {
    std::lock_guard<std::mutex> lock(ip_count_mutex_);
    auto it = ip_connection_count_.find(ip);
    if (it != ip_connection_count_.end() && it->second >= max_per_ip_) {
        return false;
    }
    return true;
}

bool WebSocketNativeServer::parseHttpRequest(
    const std::string& raw_request,
    common::http::HttpRequest& req
) {
    // ⭐⭐⭐ 使用HTTP服务的完整解析器（而非简化实现）
    return req.parseFromRawData(raw_request);
}

void WebSocketNativeServer::createWebSocketConnection(
    int client_fd,
    const std::string& client_ip
) {
    // 创建WebSocket连接
    auto conn = std::make_shared<WebSocketNativeConnection>(
        client_fd,
        client_ip,
        event_loop_
    );
    
    // 设置消息回调
    // ⭐⭐⭐ 修复循环引用：使用weak_ptr避免内存泄漏
    if (message_callback_) {
        std::weak_ptr<WebSocketNativeConnection> weak_conn = conn;
        conn->setMessageCallback([this, weak_conn](const std::string& msg) {
            auto conn_ptr = weak_conn.lock();
            if (conn_ptr) {
                message_callback_(conn_ptr, msg);
            }
        });
    }
    
    // 设置关闭回调 - ⭐⭐⭐ Phase 8: 支持多客户端结构 (2025-12-18)
    conn->setCloseCallback([this, client_fd, client_ip](uint16_t code, const std::string& reason) {
        (void)code;
        
        // ⭐ 查找该连接是否有关联的商户和客户端
        std::string merchant_code;
        std::string client_id;
        {
            std::lock_guard<std::mutex> lock(merchant_map_mutex_);
            auto it = fd_to_merchant_client_.find(client_fd);
            if (it != fd_to_merchant_client_.end()) {
                merchant_code = it->second.first;
                client_id = it->second.second;
                
                // 移除fd到商户客户端的映射
                fd_to_merchant_client_.erase(it);
                
                // 移除商户客户端记录
                auto merchant_it = merchant_clients_.find(merchant_code);
                if (merchant_it != merchant_clients_.end()) {
                    merchant_it->second.erase(client_id);
                    if (merchant_it->second.empty()) {
                        merchant_clients_.erase(merchant_it);
                    }
                }
                
                // 统计在线客户端数
                size_t total_clients = 0;
                for (const auto& [code, client_map] : merchant_clients_) {
                    total_clients += client_map.size();
                }
                
                LOG_INFO("Merchant client disconnected: " + merchant_code + 
                         ", client_id=" + client_id +
                         ", fd=" + std::to_string(client_fd) + 
                         ", total_clients=" + std::to_string(total_clients));
            }
        }
        
        // 移除连接
        removeConnection(client_fd);
        
        // 减少IP计数
        {
            std::lock_guard<std::mutex> lock(ip_count_mutex_);
            auto it = ip_connection_count_.find(client_ip);
            if (it != ip_connection_count_.end() && it->second > 0) {
                it->second--;
            }
        }
        
        // ⭐⭐⭐ Phase 6 + Phase 8: 调用商户断开回调（用于更新数据库状态、记录日志）
        if (!merchant_code.empty() && merchant_disconnect_callback_) {
            try {
                merchant_disconnect_callback_(merchant_code, client_id, client_ip, reason);
            } catch (const std::exception& e) {
                LOG_ERROR("Merchant disconnect callback exception: " + std::string(e.what()));
            }
        }
        
        // ⭐⭐⭐ Phase 5: 已废弃外部MerchantConnectionManager
        // 商户离线状态通过merchant_disconnect_callback_处理
        // if (!merchant_code.empty() && merchant_manager_) {
        //     merchant_manager_->removeConnection(merchant_code);
        // }
    });
    
    // 启动连接
    conn->start();
    
    // 添加到连接池
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[client_fd] = conn;
    }
    
    // 增加IP计数
    {
        std::lock_guard<std::mutex> lock(ip_count_mutex_);
        ip_connection_count_[client_ip]++;
    }
    
    // 调用连接回调
    if (connection_callback_) {
        connection_callback_(conn);
    }
    
    LOG_DEBUG("WebSocket connection created: fd=" + std::to_string(client_fd) + ", ip=" + client_ip);
}

// Phase 4 + Phase 8: Merchant multi-client connection management (2025-12-18)

// ⭐⭐⭐ 新版本：支持多客户端同时在线
bool WebSocketNativeServer::registerMerchantClient(
    const std::string& merchant_code,
    const std::string& client_id,
    std::shared_ptr<WebSocketNativeConnection> conn,
    const std::string& client_name
) {
    if (!conn) {
        LOG_ERROR("Register merchant client failed: connection is null");
        return false;
    }
    
    int fd = conn->getFd();
    std::string actual_client_id = client_id.empty() ? std::to_string(fd) : client_id;
    
    {
        std::lock_guard<std::mutex> lock(merchant_map_mutex_);
        
        // 创建客户端信息
        ClientInfo info;
        info.client_id = actual_client_id;
        info.client_name = client_name.empty() ? ("Client_" + actual_client_id) : client_name;
        info.client_ip = conn->getRemoteIp();
        info.client_version = conn->getClientVersion();
        info.fd = fd;
        info.connected_at = std::time(nullptr);
        info.last_heartbeat_at = info.connected_at;
        
        // 检查是否已存在相同client_id的连接
        auto& clients = merchant_clients_[merchant_code];
        auto it = clients.find(actual_client_id);
        if (it != clients.end()) {
            // 同client_id已存在，更新信息（可能是重连）
            int old_fd = it->second.fd;
            if (old_fd != fd) {
                // 移除旧的fd映射
                fd_to_merchant_client_.erase(old_fd);
                LOG_INFO("Merchant " + merchant_code + " client " + actual_client_id + 
                         " reconnected, old_fd=" + std::to_string(old_fd) + ", new_fd=" + std::to_string(fd));
            }
        }
        
        // 注册新映射
        clients[actual_client_id] = info;
        fd_to_merchant_client_[fd] = std::make_pair(merchant_code, actual_client_id);
        
        // 统计在线客户端数
        size_t total_clients = 0;
        for (const auto& [code, client_map] : merchant_clients_) {
            total_clients += client_map.size();
        }
        
        LOG_INFO("Merchant client registered: " + merchant_code + 
                 ", client_id=" + actual_client_id + 
                 ", client_name=" + info.client_name +
                 ", fd=" + std::to_string(fd) + 
                 ", merchant_clients=" + std::to_string(clients.size()) +
                 ", total_clients=" + std::to_string(total_clients));
    }
    
    return true;
}

// 兼容旧版本：使用fd作为client_id
bool WebSocketNativeServer::registerMerchant(
    const std::string& merchant_code,
    std::shared_ptr<WebSocketNativeConnection> conn
) {
    if (!conn) {
        LOG_ERROR("Register merchant failed: connection is null");
        return false;
    }
    
    // 使用fd作为默认client_id
    std::string client_id = std::to_string(conn->getFd());
    return registerMerchantClient(merchant_code, client_id, conn, "");
}

void WebSocketNativeServer::unregisterMerchantClient(
    const std::string& merchant_code, 
    const std::string& client_id
) {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    
    auto merchant_it = merchant_clients_.find(merchant_code);
    if (merchant_it == merchant_clients_.end()) {
        return;
    }
    
    auto& clients = merchant_it->second;
    auto client_it = clients.find(client_id);
    if (client_it != clients.end()) {
        int fd = client_it->second.fd;
        
        // 移除映射
        fd_to_merchant_client_.erase(fd);
        clients.erase(client_it);
        
        // 如果商户没有客户端了，移除商户记录
        if (clients.empty()) {
            merchant_clients_.erase(merchant_it);
        }
        
        LOG_INFO("Merchant client unregistered: " + merchant_code + 
                 ", client_id=" + client_id + 
                 ", fd=" + std::to_string(fd));
    }
}

void WebSocketNativeServer::unregisterMerchant(const std::string& merchant_code) {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    
    auto it = merchant_clients_.find(merchant_code);
    if (it != merchant_clients_.end()) {
        // 移除所有客户端的fd映射
        for (const auto& [client_id, info] : it->second) {
            fd_to_merchant_client_.erase(info.fd);
        }
        
        size_t client_count = it->second.size();
        merchant_clients_.erase(it);
        
        LOG_INFO("Merchant unregistered (all clients): " + merchant_code + 
                 ", removed_clients=" + std::to_string(client_count));
    }
}

bool WebSocketNativeServer::isMerchantOnline(const std::string& merchant_code) const {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    auto it = merchant_clients_.find(merchant_code);
    return it != merchant_clients_.end() && !it->second.empty();
}

size_t WebSocketNativeServer::getMerchantOnlineClientCount(const std::string& merchant_code) const {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    auto it = merchant_clients_.find(merchant_code);
    if (it != merchant_clients_.end()) {
        return it->second.size();
    }
    return 0;
}

std::vector<WebSocketNativeServer::ClientInfo> WebSocketNativeServer::getMerchantClients(
    const std::string& merchant_code
) const {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    std::vector<ClientInfo> result;
    
    auto it = merchant_clients_.find(merchant_code);
    if (it != merchant_clients_.end()) {
        for (const auto& [client_id, info] : it->second) {
            result.push_back(info);
        }
    }
    
    return result;
}

std::shared_ptr<WebSocketNativeConnection> WebSocketNativeServer::getMerchantConnection(
    const std::string& merchant_code
) const {
    std::lock_guard<std::mutex> map_lock(merchant_map_mutex_);
    
    auto it = merchant_clients_.find(merchant_code);
    if (it == merchant_clients_.end() || it->second.empty()) {
        return nullptr;
    }
    
    // 返回第一个可用的连接
    int fd = it->second.begin()->second.fd;
    
    std::lock_guard<std::mutex> conn_lock(connections_mutex_);
    auto conn_it = connections_.find(fd);
    if (conn_it != connections_.end()) {
        return conn_it->second;
    }
    
    return nullptr;
}

std::shared_ptr<WebSocketNativeConnection> WebSocketNativeServer::getMerchantClientConnection(
    const std::string& merchant_code,
    const std::string& client_id
) const {
    std::lock_guard<std::mutex> map_lock(merchant_map_mutex_);
    
    auto merchant_it = merchant_clients_.find(merchant_code);
    if (merchant_it == merchant_clients_.end()) {
        return nullptr;
    }
    
    auto client_it = merchant_it->second.find(client_id);
    if (client_it == merchant_it->second.end()) {
        return nullptr;
    }
    
    int fd = client_it->second.fd;
    
    std::lock_guard<std::mutex> conn_lock(connections_mutex_);
    auto conn_it = connections_.find(fd);
    if (conn_it != connections_.end()) {
        return conn_it->second;
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<WebSocketNativeConnection>> WebSocketNativeServer::getMerchantConnections(
    const std::string& merchant_code
) const {
    std::vector<std::shared_ptr<WebSocketNativeConnection>> result;
    
    std::lock_guard<std::mutex> map_lock(merchant_map_mutex_);
    
    auto it = merchant_clients_.find(merchant_code);
    if (it == merchant_clients_.end()) {
        return result;
    }
    
    std::lock_guard<std::mutex> conn_lock(connections_mutex_);
    for (const auto& [client_id, info] : it->second) {
        auto conn_it = connections_.find(info.fd);
        if (conn_it != connections_.end()) {
            result.push_back(conn_it->second);
        }
    }
    
    return result;
}

size_t WebSocketNativeServer::sendToMerchantAll(
    const std::string& merchant_code,
    const std::string& message
) {
    auto connections = getMerchantConnections(merchant_code);
    size_t success_count = 0;
    
    for (auto& conn : connections) {
        if (conn && conn->isConnected()) {
            if (conn->sendText(message)) {
                success_count++;
            }
        }
    }
    
    if (success_count > 0) {
        LOG_DEBUG("Sent message to merchant " + merchant_code + 
                  ", success=" + std::to_string(success_count) + 
                  "/" + std::to_string(connections.size()));
    }
    
    return success_count;
}

bool WebSocketNativeServer::sendToMerchant(
    const std::string& merchant_code,
    const std::string& message
) {
    auto conn = getMerchantConnection(merchant_code);
    if (!conn) {
        LOG_DEBUG("Merchant offline, cannot send: " + merchant_code);
        return false;
    }
    
    if (!conn->isConnected()) {
        LOG_DEBUG("Merchant connection closed: " + merchant_code);
        return false;
    }
    
    return conn->sendText(message);
}

bool WebSocketNativeServer::sendToMerchantClient(
    const std::string& merchant_code,
    const std::string& client_id,
    const std::string& message
) {
    auto conn = getMerchantClientConnection(merchant_code, client_id);
    if (!conn) {
        LOG_DEBUG("Merchant client offline: " + merchant_code + "/" + client_id);
        return false;
    }
    
    if (!conn->isConnected()) {
        LOG_DEBUG("Merchant client connection closed: " + merchant_code + "/" + client_id);
        return false;
    }
    
    return conn->sendText(message);
}

std::string WebSocketNativeServer::getMerchantCodeByConnection(
    std::shared_ptr<WebSocketNativeConnection> conn
) const {
    if (!conn) {
        return "";
    }
    
    int fd = conn->getFd();
    
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    auto it = fd_to_merchant_client_.find(fd);
    if (it != fd_to_merchant_client_.end()) {
        return it->second.first;  // merchant_code
    }
    
    return "";
}

std::string WebSocketNativeServer::getClientIdByConnection(
    std::shared_ptr<WebSocketNativeConnection> conn
) const {
    if (!conn) {
        return "";
    }
    
    int fd = conn->getFd();
    
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    auto it = fd_to_merchant_client_.find(fd);
    if (it != fd_to_merchant_client_.end()) {
        return it->second.second;  // client_id
    }
    
    return "";
}

// ⭐⭐⭐ Phase 8: 更新客户端心跳时间 (2025-12-19)
bool WebSocketNativeServer::updateClientHeartbeat(
    const std::string& merchant_code,
    const std::string& client_id
) {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    
    auto merchant_it = merchant_clients_.find(merchant_code);
    if (merchant_it == merchant_clients_.end()) {
        return false;
    }
    
    auto client_it = merchant_it->second.find(client_id);
    if (client_it == merchant_it->second.end()) {
        return false;
    }
    
    // 更新内存中的心跳时间
    client_it->second.last_heartbeat_at = std::time(nullptr);
    
    LOG_DEBUG("Client heartbeat updated: " + merchant_code + "/" + client_id);
    return true;
}

// ⭐⭐⭐ Phase 8 优化: 获取所有在线商户编码 (2025-12-19)
// 用于批量同步心跳到数据库，避免连接池耗尽
std::vector<std::string> WebSocketNativeServer::getOnlineMerchantCodes() const {
    std::lock_guard<std::mutex> lock(merchant_map_mutex_);
    
    std::vector<std::string> result;
    result.reserve(merchant_clients_.size());
    
    for (const auto& pair : merchant_clients_) {
        // 只添加有客户端在线的商户
        if (!pair.second.empty()) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

// ⭐⭐⭐ 2025-12-11: 已废弃setMerchantConnectionManager
// void WebSocketNativeServer::setMerchantConnectionManager(
//     std::shared_ptr<common::websocket::MerchantConnectionManager> manager
// ) {
//     merchant_manager_ = manager;
//     LOG_INFO("✅ 已设置外部MerchantConnectionManager（与旧版本兼容）");
// }

// ⭐⭐⭐ 2025-12-11: 已废弃setDeliveryTaskRouter
// void WebSocketNativeServer::setDeliveryTaskRouter(
//     std::shared_ptr<common::websocket::DeliveryTaskRouter> router
// ) {
//     task_router_ = router;
//     LOG_INFO("✅ 已设置外部DeliveryTaskRouter（与旧版本兼容）");
// }

} // namespace native
} // namespace websocket
} // namespace common
