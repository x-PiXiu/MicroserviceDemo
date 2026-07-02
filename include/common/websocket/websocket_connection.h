// include/common/websocket/websocket_connection.h
#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <functional>
#include "common/websocket/websocket_message.h"

namespace common {
namespace websocket {

/**
 * @brief 消息发送回调函数
 * @param message 要发送的消息
 * @return 是否发送成功
 */
using MessageSendCallback = std::function<bool(const WebSocketMessage&)>;

/**
 * @brief WebSocket连接状态
 */
enum class ConnectionState {
    CONNECTING = 0,     // 正在连接
    AUTHENTICATING = 1, // 认证中
    AUTHENTICATED = 2,  // 已认证(可用)
    DISCONNECTED = 3    // 已断开
};

/**
 * @brief WebSocket连接封装
 * 
 * 代表一个商户客户端连接,包含:
 * - 认证信息(merchant_code, merchant_name)
 * - 心跳检测(last_active_time)
 * - 消息发送/接收
 */
class WebSocketConnection {
public:
    /**
     * @brief 构造函数
     * @param send_callback 消息发送回调函数
     * @param remote_ip 客户端IP
     */
    WebSocketConnection(
        MessageSendCallback send_callback,
        const std::string& remote_ip
    );

    ~WebSocketConnection();

    // ==================== 消息发送 ====================
    
    /**
     * @brief 发送消息到客户端
     * @param message 消息对象
     * @return 是否发送成功
     */
    bool sendMessage(const WebSocketMessage& message);

    /**
     * @brief 发送JSON字符串
     * @param json_str JSON字符串
     * @return 是否发送成功
     */
    bool sendJson(const std::string& json_str);

    /**
     * @brief 关闭连接
     * @param reason 关闭原因
     */
    void close(const std::string& reason = "");

    // ==================== 认证管理 ====================
    
    /**
     * @brief 设置为已认证状态
     * @param merchant_code 商户编码
     * @param merchant_name 商户名称
     */
    void setAuthenticated(const std::string& merchant_code, const std::string& merchant_name);

    /**
     * @brief 是否已认证
     */
    bool isAuthenticated() const { return state_ == ConnectionState::AUTHENTICATED; }

    /**
     * @brief 获取商户编码
     */
    std::string getMerchantCode() const { return merchant_code_; }

    /**
     * @brief 获取商户名称
     */
    std::string getMerchantName() const { return merchant_name_; }

    // ==================== 心跳检测 ====================
    
    /**
     * @brief 更新最后活跃时间
     */
    void updateLastActiveTime();

    /**
     * @brief 获取最后活跃时间
     */
    std::chrono::system_clock::time_point getLastActiveTime() const {
        return last_active_time_;
    }

    /**
     * @brief 是否超时(超过60秒未活跃)
     */
    bool isTimeout() const;

    // ==================== 属性访问 ====================
    
    std::string getRemoteIp() const { return remote_ip_; }
    ConnectionState getState() const { return state_; }
    
    /**
     * @brief 标记为已认证（与setAuthenticated同义）
     */
    void markAuthenticated(const std::string& merchant_code, const std::string& merchant_name) {
        setAuthenticated(merchant_code, merchant_name);
    }
    
    /**
     * @brief 获取最后活动时间（秒）
     */
    time_t getLastActivityTime() const {
        return std::chrono::system_clock::to_time_t(last_active_time_);
    }
    
    /**
     * @brief 更新Pong时间
     */
    void updateLastPongTime() {
        updateLastActiveTime();
    }

private:
    MessageSendCallback send_callback_;                     // 消息发送回调
    std::string remote_ip_;                                 // 客户端IP
    
    ConnectionState state_;                                 // 连接状态
    std::string merchant_code_;                             // 商户编码
    std::string merchant_name_;                             // 商户名称
    
    std::chrono::system_clock::time_point last_active_time_; // 最后活跃时间
    std::chrono::system_clock::time_point created_at_;       // 创建时间
};

} // namespace websocket
} // namespace common
