// src/common/websocket/websocket_message.cpp
#include "common/websocket/websocket_message.h"
#include <chrono>

namespace common {
namespace websocket {

WebSocketMessage::WebSocketMessage(MessageType type) : type_(type) {
    auto now = std::chrono::system_clock::now();
    timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
}

bool WebSocketMessage::fromJson(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        
        if (j.contains("type")) {
            type_ = static_cast<MessageType>(j["type"].get<int>());
        }
        
        if (j.contains("timestamp")) {
            timestamp_ = j["timestamp"].get<int64_t>();
        }
        
        if (j.contains("data")) {
            data_ = j["data"];
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string WebSocketMessage::toJson() const {
    json j;
    j["type"] = static_cast<int>(type_);
    j["timestamp"] = timestamp_;
    j["data"] = data_;
    return j.dump();
}

WebSocketMessage WebSocketMessage::createAuthRequired() {
    WebSocketMessage msg(MessageType::AUTH_REQUIRED);
    msg.setData({{"message", "请发送认证信息"}});
    return msg;
}

WebSocketMessage WebSocketMessage::createAuthSuccess(
    const std::string& merchant_code,
    const std::string& merchant_name
) {
    WebSocketMessage msg(MessageType::AUTH_SUCCESS);
    msg.setData({
        {"merchant_code", merchant_code},
        {"merchant_name", merchant_name},
        {"message", "认证成功"}
    });
    return msg;
}

WebSocketMessage WebSocketMessage::createAuthFailed(const std::string& reason) {
    WebSocketMessage msg(MessageType::AUTH_FAILED);
    msg.setData({
        {"reason", reason},
        {"message", "认证失败"}
    });
    return msg;
}

WebSocketMessage WebSocketMessage::createPing() {
    return WebSocketMessage(MessageType::PING);
}

WebSocketMessage WebSocketMessage::createPong() {
    return WebSocketMessage(MessageType::PONG);
}

WebSocketMessage WebSocketMessage::createDeliveryTask(
    const std::string& task_id,
    const std::string& order_id,
    int64_t game_id,
    int64_t server_id,
    const std::string& player_account,
    const std::string& reward_items
) {
    WebSocketMessage msg(MessageType::DELIVERY_TASK);
    msg.setData({
        {"task_id", task_id},
        {"order_id", order_id},
        {"game_id", game_id},
        {"server_id", server_id},
        {"player_account", player_account},
        {"reward_items", json::parse(reward_items)}
    });
    return msg;
}

WebSocketMessage WebSocketMessage::createError(
    const std::string& error_code,
    const std::string& message
) {
    WebSocketMessage msg(MessageType::ERROR);
    msg.setData({
        {"error_code", error_code},
        {"message", message}
    });
    return msg;
}

} // namespace websocket
} // namespace common
