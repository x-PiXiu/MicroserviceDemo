/**
 * @file websocket_native_connection.h
 * @brief 基于EventLoop的WebSocket原生连接实现
 * @date 2025-12-11
 */

#ifndef WEBSOCKET_NATIVE_CONNECTION_H
#define WEBSOCKET_NATIVE_CONNECTION_H

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <ctime>
#include "common/network/event_loop.h"
#include "common/network/channel.h"
#include "common/websocket/native/websocket_frame.h"

namespace common {
namespace websocket {
namespace native {

/**
 * @brief WebSocket连接状态
 */
enum class WebSocketConnectionState {
    CONNECTING,  ///< 连接中（握手进行中）
    OPEN,        ///< 已打开（可以收发消息）
    CLOSING,     ///< 关闭中（已发送关闭帧）
    CLOSED       ///< 已关闭
};

/**
 * @brief WebSocket原生连接类
 * @details 基于EventLoop实现的WebSocket连接
 * 
 * 特性：
 * - 基于EventLoop的异步IO
 * - 完整的WebSocket协议支持
 * - 自动心跳检测
 * - 优雅关闭
 */
class WebSocketNativeConnection : public std::enable_shared_from_this<WebSocketNativeConnection> {
public:
    // 回调函数类型
    using MessageCallback = std::function<void(const std::string& message)>;
    using BinaryCallback = std::function<void(const std::vector<uint8_t>& data)>;
    using CloseCallback = std::function<void(uint16_t code, const std::string& reason)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    /**
     * @brief 构造函数
     * @param socket_fd 已连接的socket文件描述符
     * @param remote_ip 远程IP地址
     * @param event_loop EventLoop实例
     */
    WebSocketNativeConnection(
        int socket_fd,
        const std::string& remote_ip,
        std::shared_ptr<common::network::EventLoop> event_loop
    );
    
    ~WebSocketNativeConnection();
    
    // ==================== 生命周期管理 ====================
    
    /**
     * @brief 启动连接（添加到EventLoop）
     */
    void start();
    
    /**
     * @brief 关闭连接
     * @param code 关闭码
     * @param reason 关闭原因
     */
    void close(uint16_t code = 1000, const std::string& reason = "");
    
    /**
     * @brief 强制关闭连接
     */
    void shutdown();
    
    // ==================== 消息发送 ====================
    
    /**
     * @brief 发送文本消息
     * @param text 文本内容
     * @return 成功返回true
     */
    bool sendText(const std::string& text);
    
    /**
     * @brief 发送二进制消息
     * @param data 二进制数据
     * @return 成功返回true
     */
    bool sendBinary(const std::vector<uint8_t>& data);
    
    /**
     * @brief 发送Ping帧
     * @param payload Ping数据（可选）
     * @return 成功返回true
     */
    bool sendPing(const std::string& payload = "");
    
    /**
     * @brief 发送Pong帧
     * @param payload Pong数据（可选）
     * @return 成功返回true
     */
    bool sendPong(const std::string& payload = "");
    
    // ==================== 回调设置 ====================
    
    void setMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void setBinaryCallback(BinaryCallback cb) { binary_callback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { close_callback_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { error_callback_ = std::move(cb); }
    
    // ==================== 状态查询 ====================
    
    WebSocketConnectionState getState() const { return state_; }
    bool isConnected() const { return state_ == WebSocketConnectionState::OPEN; }
    std::string getRemoteIP() const { return remote_ip_; }
    std::string getRemoteIp() const { return remote_ip_; }  // 别名，兼容大小写
    time_t getLastActivityTime() const { return last_activity_time_; }
    
    // ⭐⭐⭐ Phase 8: 客户端版本信息 (2025-12-18)
    std::string getClientVersion() const { return client_version_; }
    void setClientVersion(const std::string& version) { client_version_ = version; }
    
    /**
     * @brief 获取socket文件描述符
     * @return socket fd
     */
    int getFd() const { return socket_fd_; }
    
    /**
     * @brief 获取EventLoop实例
     * @return EventLoop shared_ptr
     */
    std::shared_ptr<common::network::EventLoop> getEventLoop() const { return event_loop_; }
    
    /**
     * @brief 线程安全的发送文本消息
     * @details 可以从任意线程调用，会自动调度到EventLoop线程执行
     * @param text 文本内容
     */
    void sendTextThreadSafe(const std::string& text);
    
private:
    // ==================== 事件处理 ====================
    
    /**
     * @brief 处理可读事件
     */
    void handleRead();
    
    /**
     * @brief 处理可写事件
     */
    void handleWrite();
    
    /**
     * @brief 处理错误事件
     */
    void handleError();
    
    /**
     * @brief 处理关闭事件
     */
    void handleClose();
    
    // ==================== 帧处理 ====================
    
    /**
     * @brief 处理接收到的帧
     * @param frame 帧对象
     */
    void handleFrame(std::shared_ptr<WebSocketFrame> frame);
    
    /**
     * @brief 发送帧
     * @param frame 帧对象
     * @return 成功返回true
     */
    bool sendFrame(const WebSocketFrame& frame);
    
    /**
     * @brief 内部发送文本实现（必须在EventLoop线程调用）
     * @param text 文本内容
     * @return 成功返回true
     */
    bool sendTextInternal(const std::string& text);
    
    /**
     * @brief 刷新写队列（尝试发送队列中的所有数据）
     * @note 在ET模式下，不能完全依赖EPOLLOUT，需要主动刷新
     */
    void flushWriteQueue();
    
    // ==================== 成员变量 ====================
    
    int socket_fd_;                              ///< Socket文件描述符
    std::string remote_ip_;                      ///< 远程IP
    std::shared_ptr<common::network::EventLoop> event_loop_;  ///< EventLoop
    std::shared_ptr<common::network::Channel> channel_;       ///< Channel
    
    WebSocketConnectionState state_;             ///< 连接状态
    std::atomic<bool> closing_{false};           ///< 是否正在关闭
    
    // 接收缓冲区
    std::vector<uint8_t> read_buffer_;           ///< 读缓冲区
    
    // 发送缓冲区（支持异步发送）
    std::deque<std::vector<uint8_t>> write_queue_;  ///< 写队列
    std::mutex write_mutex_;                     ///< 写锁
    
    // 回调函数
    MessageCallback message_callback_;
    BinaryCallback binary_callback_;
    CloseCallback close_callback_;
    ErrorCallback error_callback_;
    
    // 活跃时间
    time_t last_activity_time_;                  ///< 最后活跃时间
    time_t last_ping_time_;                      ///< 最后Ping时间
    
    // ⭐⭐⭐ Phase 8: 客户端信息 (2025-12-18)
    std::string client_version_;                 ///< 客户端版本号
};

} // namespace native
} // namespace websocket
} // namespace common

#endif // WEBSOCKET_NATIVE_CONNECTION_H
