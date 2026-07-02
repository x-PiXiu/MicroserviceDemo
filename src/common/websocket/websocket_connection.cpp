// src/common/websocket/websocket_connection.cpp
#include "common/websocket/websocket_connection.h"
#include "common/logger/logger.h"
#include <chrono>

using namespace common::logger;

namespace common {
namespace websocket {

WebSocketConnection::WebSocketConnection(
    MessageSendCallback send_callback,
    const std::string& remote_ip
) : send_callback_(send_callback),
    remote_ip_(remote_ip),
    state_(ConnectionState::CONNECTING)
{
    auto now = std::chrono::system_clock::now();
    last_active_time_ = now;
    created_at_ = now;
    
    LOG_INFO("WebSocket连接已创建: IP=" + remote_ip_);
}

WebSocketConnection::~WebSocketConnection() {
    // 析构时确保连接已关闭
    if (state_ != ConnectionState::DISCONNECTED) {
        close("connection_destroyed");
    }
}

bool WebSocketConnection::sendMessage(const WebSocketMessage& message) {
    return sendJson(message.toJson());
}

bool WebSocketConnection::sendJson(const std::string& json_str) {
    if (!send_callback_) {
        LOG_ERROR("发送回调未设置");
        return false;
    }
    
    try {
        WebSocketMessage msg;
        if (msg.fromJson(json_str)) {
            return send_callback_(msg);
        } else {
            LOG_ERROR("解析JSON消息失败");
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("发送消息异常: " + std::string(e.what()));
        return false;
    }
}

void WebSocketConnection::close(const std::string& reason) {
    state_ = ConnectionState::DISCONNECTED;
    LOG_INFO("关闭WebSocket连接: 商户=" + merchant_code_ + ", 原因=" + reason);
}

void WebSocketConnection::setAuthenticated(
    const std::string& merchant_code,
    const std::string& merchant_name
) {
    merchant_code_ = merchant_code;
    merchant_name_ = merchant_name;
    state_ = ConnectionState::AUTHENTICATED;
    updateLastActiveTime();
    
    LOG_INFO("商户认证成功: " + merchant_code + " (" + merchant_name + "), IP=" + remote_ip_);
}

void WebSocketConnection::updateLastActiveTime() {
    last_active_time_ = std::chrono::system_clock::now();
}

bool WebSocketConnection::isTimeout() const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_active_time_
    ).count();
    
    return elapsed > 60; // 60秒超时
}

} // namespace websocket
} // namespace common
