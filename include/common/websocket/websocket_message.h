// include/common/websocket/websocket_message.h
#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace common {
namespace websocket {

using json = nlohmann::json;

/**
 * @brief WebSocket消息类型枚举
 */
enum class MessageType : int {
    // 认证相关
    AUTH_REQUIRED = 0,      // 服务端→客户端: 要求认证
    AUTH_REQUEST = 1,       // 客户端→服务端: 认证请求
    AUTH_SUCCESS = 2,       // 服务端→客户端: 认证成功
    AUTH_FAILED = 3,        // 服务端→客户端: 认证失败
    
    // 心跳保活
    PING = 10,              // 服务端→客户端: 心跳请求
    PONG = 11,              // 客户端→服务端: 心跳响应
    
    // 发货任务
    DELIVERY_TASK = 20,     // 服务端→客户端: 发货任务
    DELIVERY_RESULT = 21,   // 客户端→服务端: 发货结果
    
    // 系统通知
    NOTIFICATION = 30,      // 服务端→客户端: 系统通知
    ERROR = 40,             // 服务端→客户端: 错误消息
    
    UNKNOWN = 99            // 未知类型
};

/**
 * @brief WebSocket消息封装
 * 
 * 消息格式:
 * {
 *   "type": 20,
 *   "timestamp": 1702345678000,
 *   "data": { ... }
 * }
 */
class WebSocketMessage {
public:
    WebSocketMessage() = default;
    explicit WebSocketMessage(MessageType type);

    // 序列化/反序列化
    bool fromJson(const std::string& json_str);
    std::string toJson() const;

    // Getter/Setter
    void setType(MessageType type) { type_ = type; }
    void setData(const json& data) { data_ = data; }
    void setTimestamp(int64_t timestamp) { timestamp_ = timestamp; }

    MessageType getType() const { return type_; }
    json getData() const { return data_; }
    int64_t getTimestamp() const { return timestamp_; }

    // 静态工厂方法
    static WebSocketMessage createAuthRequired();
    static WebSocketMessage createAuthSuccess(const std::string& merchant_code, const std::string& merchant_name);
    static WebSocketMessage createAuthFailed(const std::string& reason);
    static WebSocketMessage createPing();
    static WebSocketMessage createPong();
    static WebSocketMessage createDeliveryTask(
        const std::string& task_id,
        const std::string& order_id,
        int64_t game_id,
        int64_t server_id,
        const std::string& player_account,
        const std::string& reward_items
    );
    static WebSocketMessage createError(const std::string& error_code, const std::string& message);

private:
    MessageType type_ = MessageType::UNKNOWN;
    json data_;
    int64_t timestamp_ = 0;  // Unix时间戳(毫秒)
};

} // namespace websocket
} // namespace common
