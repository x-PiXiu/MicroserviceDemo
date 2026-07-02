/**
 * @file websocket_native_connection.cpp
 * @brief WebSocket原生连接实现
 * @date 2025-12-11
 */

#include "common/websocket/native/websocket_native_connection.h"
#include "common/logger/logger.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <cstring>

namespace common {
namespace websocket {
namespace native {

using namespace common::logger;
using namespace common::network;

// ==================== 构造/析构函数 ====================

WebSocketNativeConnection::WebSocketNativeConnection(
    int socket_fd,
    const std::string& remote_ip,
    std::shared_ptr<EventLoop> event_loop
)
    : socket_fd_(socket_fd)
    , remote_ip_(remote_ip)
    , event_loop_(event_loop)
    , state_(WebSocketConnectionState::CONNECTING)
    , last_activity_time_(std::time(nullptr))
    , last_ping_time_(0)
{
    // Create Channel
    channel_ = std::make_shared<Channel>(event_loop_.get(), socket_fd_);
    
    LOG_DEBUG("WebSocket connection created: fd=" + std::to_string(socket_fd_) + 
             ", remote=" + remote_ip_);
}

WebSocketNativeConnection::~WebSocketNativeConnection() {
    shutdown();
    LOG_DEBUG("WebSocket connection destroyed: fd=" + std::to_string(socket_fd_));
}

// ==================== 生命周期管理 ====================

void WebSocketNativeConnection::start() {
    if (state_ != WebSocketConnectionState::CONNECTING) {
        LOG_WARNING("Invalid state for start: state=" + std::to_string(static_cast<int>(state_)));
        return;
    }
    
    // 设置Channel回调
    auto self = shared_from_this();
    
    channel_->setReadCallback([self]() {
        self->handleRead();
    });
    
    channel_->setWriteCallback([self]() {
        self->handleWrite();
    });
    
    channel_->setErrorCallback([self]() {
        self->handleError();
    });
    
    channel_->setCloseCallback([self]() {
        self->handleClose();
    });
    
    // 启用读事件
    channel_->enableReading();
    
    // Update state
    state_ = WebSocketConnectionState::OPEN;
    last_activity_time_ = std::time(nullptr);
    
    LOG_DEBUG("WebSocket connection started: fd=" + std::to_string(socket_fd_));
}

void WebSocketNativeConnection::close(uint16_t code, const std::string& reason) {
    if (closing_.exchange(true)) {
        return; // Already closing
    }
    
    LOG_DEBUG("Closing WebSocket connection: fd=" + std::to_string(socket_fd_) + 
             ", code=" + std::to_string(code));
    
    // 发送Close帧
    auto close_frame = WebSocketFrame::createCloseFrame(
        static_cast<WebSocketCloseCode>(code),
        reason
    );
    sendFrame(close_frame);
    
    // 更新状态
    state_ = WebSocketConnectionState::CLOSING;
    
    // ⭐⭐⭐ 修复使用后释放错误：使用shared_from_this()而不是[this]
    // 延迟关闭socket（等待对方响应Close帧）
    auto self = shared_from_this();
    event_loop_->runAfter(2000, [self]() {
        self->shutdown();
    });
}

void WebSocketNativeConnection::shutdown() {
    if (state_ == WebSocketConnectionState::CLOSED) {
        return;
    }
    
    // 禁用Channel
    if (channel_) {
        channel_->disableAll();
        channel_->remove();
    }
    
    // 关闭socket
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    
    state_ = WebSocketConnectionState::CLOSED;
    
    // 调用关闭回调
    if (close_callback_) {
        close_callback_(1000, "Connection closed");
    }
}

// ==================== 消息发送 ====================

bool WebSocketNativeConnection::sendText(const std::string& text) {
    // ⭐⭐⭐ 线程安全修复：检查是否在EventLoop线程中
    if (!event_loop_->isInLoopThread()) {
        // 不在EventLoop线程，调度到EventLoop执行
        auto self = shared_from_this();
        std::string text_copy = text;
        event_loop_->runInLoop([self, text_copy]() {
            self->sendTextInternal(text_copy);
        });
        return true;  // 已调度，假设成功
    }
    
    // 在EventLoop线程中，直接发送
    return sendTextInternal(text);
}

bool WebSocketNativeConnection::sendTextInternal(const std::string& text) {
    if (state_ != WebSocketConnectionState::OPEN) {
        LOG_WARNING("sendText failed: connection not open, fd=" + std::to_string(socket_fd_));
        return false;
    }
    
    auto frame = WebSocketFrame::createTextFrame(text, false);
    bool result = sendFrame(frame);
    
    if (!result) {
        LOG_ERROR("❌ sendText FAILED: fd=" + std::to_string(socket_fd_));
    }
    
    return result;
}

void WebSocketNativeConnection::sendTextThreadSafe(const std::string& text) {
    // ⭐⭐⭐ 线程安全版本：始终通过runInLoop调度
    auto self = shared_from_this();
    std::string text_copy = text;
    
    event_loop_->runInLoop([self, text_copy]() {
        if (self->state_ != WebSocketConnectionState::OPEN) {
            LOG_WARNING("sendTextThreadSafe: connection not open, fd=" + std::to_string(self->socket_fd_));
            return;
        }
        self->sendTextInternal(text_copy);
    });
}

bool WebSocketNativeConnection::sendBinary(const std::vector<uint8_t>& data) {
    // ⭐⭐⭐ 线程安全修复：检查是否在EventLoop线程中
    if (!event_loop_->isInLoopThread()) {
        auto self = shared_from_this();
        std::vector<uint8_t> data_copy = data;
        event_loop_->runInLoop([self, data_copy]() {
            if (self->state_ != WebSocketConnectionState::OPEN) {
                LOG_DEBUG("sendBinary: connection not open");
                return;
            }
            auto frame = WebSocketFrame::createBinaryFrame(data_copy, false);
            self->sendFrame(frame);
        });
        return true;
    }
    
    if (state_ != WebSocketConnectionState::OPEN) {
        LOG_DEBUG("Connection not open, cannot send");
        return false;
    }
    
    auto frame = WebSocketFrame::createBinaryFrame(data, false);
    return sendFrame(frame);
}

bool WebSocketNativeConnection::sendPing(const std::string& payload) {
    // ⭐⭐⭐ 线程安全修复
    if (!event_loop_->isInLoopThread()) {
        auto self = shared_from_this();
        std::string payload_copy = payload;
        event_loop_->runInLoop([self, payload_copy]() {
            auto frame = WebSocketFrame::createPingFrame(payload_copy);
            self->last_ping_time_ = std::time(nullptr);
            self->sendFrame(frame);
        });
        return true;
    }
    
    auto frame = WebSocketFrame::createPingFrame(payload);
    last_ping_time_ = std::time(nullptr);
    return sendFrame(frame);
}

bool WebSocketNativeConnection::sendPong(const std::string& payload) {
    // ⭐⭐⭐ 线程安全修复
    if (!event_loop_->isInLoopThread()) {
        auto self = shared_from_this();
        std::string payload_copy = payload;
        event_loop_->runInLoop([self, payload_copy]() {
            auto frame = WebSocketFrame::createPongFrame(payload_copy);
            self->sendFrame(frame);
        });
        return true;
    }
    
    auto frame = WebSocketFrame::createPongFrame(payload);
    return sendFrame(frame);
}

bool WebSocketNativeConnection::sendFrame(const WebSocketFrame& frame) {
    // 序列化帧
    std::vector<uint8_t> data = frame.serialize();
    
    // 检查连接状态
    if (state_ != WebSocketConnectionState::OPEN || socket_fd_ < 0) {
        LOG_WARNING("sendFrame: connection not open, fd=" + std::to_string(socket_fd_));
        return false;
    }
    
    // ⭐⭐⭐ 关键修复：发送前检查socket状态，检测半开连接
    int socket_error = 0;
    socklen_t error_len = sizeof(socket_error);
    if (::getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) < 0 || socket_error != 0) {
        LOG_ERROR("sendFrame: socket has error before send, fd=" + std::to_string(socket_fd_) +
                  ", socket_error=" + std::to_string(socket_error));
        return false;
    }
    
    // ⭐⭐⭐ 移除了互斥锁，因为所有发送现在都在EventLoop线程中执行
    // 如果需要跨线程发送，请使用sendText()等方法，它们会自动调度到EventLoop
    
    size_t total_sent = 0;
    
    while (total_sent < data.size()) {
        errno = 0;
        ssize_t n = ::send(socket_fd_, 
                          data.data() + total_sent, 
                          data.size() - total_sent, 
                          MSG_NOSIGNAL | MSG_DONTWAIT);
        int saved_errno = errno;
        
        if (n > 0) {
            total_sent += n;
        } else if (n == 0) {
            // send 返回 0，视为连接关闭
            LOG_WARNING("sendFrame: send returned 0, connection may be closed");
            return false;
        } else {
            // n < 0
            if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
                if (total_sent > 0) {
                    LOG_WARNING("sendFrame: partial send, sent=" + std::to_string(total_sent) + 
                               "/" + std::to_string(data.size()));
                    return true;
                } else {
                    LOG_WARNING("sendFrame: would block, fd=" + std::to_string(socket_fd_));
                    return false;
                }
            } else if (saved_errno == EINTR) {
                continue;
            } else if (saved_errno == EPIPE || saved_errno == ECONNRESET) {
                // ⭐ 连接已断开（半开连接检测）
                LOG_WARNING("sendFrame: connection broken, fd=" + std::to_string(socket_fd_) +
                           ", errno=" + std::to_string(saved_errno) + " (" + strerror(saved_errno) + ")");
                // 触发关闭处理
                state_ = WebSocketConnectionState::CLOSED;
                return false;
            } else {
                LOG_ERROR("sendFrame: send error, fd=" + std::to_string(socket_fd_) + 
                         ", errno=" + std::to_string(saved_errno) + " (" + strerror(saved_errno) + ")");
                return false;
            }
        }
    }
    
    return true;
}

void WebSocketNativeConnection::flushWriteQueue() {
    // ⭐ 直接发送模式：不再使用队列，此方法保留以保持接口兼容
    // 实际发送已在 sendFrame() 中完成
}

// ==================== 事件处理 ====================

void WebSocketNativeConnection::handleRead() {
    // ⭐⭐⭐ ET模式修复：必须循环读取直到EAGAIN
    char buffer[8192];
    bool should_close = false;
    bool has_error = false;
    
    while (true) {
        ssize_t n = ::recv(socket_fd_, buffer, sizeof(buffer), 0);
        
        if (n > 0) {
            // 追加到读缓冲区
            read_buffer_.insert(read_buffer_.end(), buffer, buffer + n);
            
            // 更新活跃时间
            last_activity_time_ = std::time(nullptr);
            
            // ⭐ ET模式：继续读取，直到EAGAIN
            continue;
            
        } else if (n == 0) {
            // Connection closed
            LOG_DEBUG("Connection closed: fd=" + std::to_string(socket_fd_));
            should_close = true;
            break;
            
        } else {
            // Error or EAGAIN
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // ⭐ ET模式：数据读完了，退出循环
                break;
            } else {
                LOG_ERROR("Read error: fd=" + std::to_string(socket_fd_) + 
                         ", errno=" + std::to_string(errno));
                has_error = true;
                break;
            }
        }
    }
    
    // ⭐ 数据读取完毕后，解析所有帧
    while (!read_buffer_.empty()) {
        size_t bytes_consumed = 0;
        auto frame = WebSocketFrame::deserialize(read_buffer_, bytes_consumed);
        
        if (!frame) {
            // 数据不完整，等待更多数据
            break;
        }
        
        // 移除已消费的字节
        read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + bytes_consumed);
        
        // 处理帧
        handleFrame(frame);
    }
    
    // 处理关闭或错误（在帧处理完成后）
    if (should_close) {
        handleClose();
    } else if (has_error) {
        handleError();
    }
}

void WebSocketNativeConnection::handleWrite() {
    // ⭐ 直接发送模式：不再使用EPOLLOUT
    // 所有发送已在 sendFrame() 中同步完成
    // 禁用写事件，避免不必要的触发
    if (channel_ && channel_->isWriting()) {
        channel_->disableWriting();
    }
}

void WebSocketNativeConnection::handleError() {
    LOG_ERROR("Connection error: fd=" + std::to_string(socket_fd_));
    
    if (error_callback_) {
        error_callback_("Connection error");
    }
    
    shutdown();
}

void WebSocketNativeConnection::handleClose() {
    LOG_DEBUG("Handling connection close: fd=" + std::to_string(socket_fd_));
    shutdown();
}

// ==================== 帧处理 ====================

void WebSocketNativeConnection::handleFrame(std::shared_ptr<WebSocketFrame> frame) {
    switch (frame->opcode) {
        case WebSocketOpcode::TEXT:
            // 文本消息
            if (message_callback_) {
                message_callback_(frame->getTextPayload());
            }
            break;
            
        case WebSocketOpcode::BINARY:
            // 二进制消息
            if (binary_callback_) {
                binary_callback_(frame->payload);
            }
            break;
            
        case WebSocketOpcode::PING:
            // 收到Ping，自动回复Pong
            sendPong(frame->getTextPayload());
            LOG_DEBUG("收到Ping，已回复Pong");
            break;
            
        case WebSocketOpcode::PONG:
            // 收到Pong
            LOG_DEBUG("收到Pong");
            break;
            
        case WebSocketOpcode::CLOSE:
            // 收到关闭帧
            if (state_ == WebSocketConnectionState::OPEN) {
                // 如果我们还未关闭，回复Close帧
                auto close_frame = WebSocketFrame::createCloseFrame(
                    WebSocketCloseCode::NORMAL,
                    "Acknowledged"
                );
                sendFrame(close_frame);
            }
            
            if (close_callback_) {
                // 解析关闭码和原因
                uint16_t code = 1000;
                std::string reason;
                if (frame->payload.size() >= 2) {
                    code = (static_cast<uint16_t>(frame->payload[0]) << 8) | 
                           frame->payload[1];
                    if (frame->payload.size() > 2) {
                        reason = std::string(frame->payload.begin() + 2, 
                                           frame->payload.end());
                    }
                }
                close_callback_(code, reason);
            }
            
            shutdown();
            break;
            
        default:
            LOG_WARNING("Unknown opcode: " + 
                       std::to_string(static_cast<int>(frame->opcode)));
            break;
    }
}

} // namespace native
} // namespace websocket
} // namespace common
