// src/common/websocket/merchant_connection_manager.cpp
#include "common/websocket/merchant_connection_manager.h"
#include "common/logger/logger.h"
#include "common/network/event_loop.h"
#include <algorithm>

namespace common {
namespace websocket {

// 单例实例
MerchantConnectionManager& MerchantConnectionManager::getInstance() {
    static MerchantConnectionManager instance;
    return instance;
}

MerchantConnectionManager::MerchantConnectionManager() {
    LOG_INFO("商户连接管理器已创建");
}

MerchantConnectionManager::~MerchantConnectionManager() {
    stopHeartbeatChecker();
    
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear();
    
    LOG_INFO("商户连接管理器已销毁");
}

// ==================== 连接管理 ====================

bool MerchantConnectionManager::registerConnection(
    const std::string& merchant_code,
    std::shared_ptr<WebSocketConnection> connection
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(merchant_code);
    if (it != connections_.end()) {
        // 已有连接,踢掉旧连接
        LOG_WARNING("商户 " + merchant_code + " 重复登录,踢掉旧连接");
        
        // 发送被踢下线通知
        WebSocketMessage kick_msg = WebSocketMessage::createError(
            "DUPLICATE_LOGIN",
            "检测到新的客户端登录,当前连接已断开"
        );
        it->second->sendMessage(kick_msg);
        it->second->close("duplicate_login");
        
        LOG_INFO("旧连接已踢下线: " + merchant_code);
    }
    
    // 注册新连接
    connections_[merchant_code] = connection;
    
    LOG_INFO("商户连接已注册: " + merchant_code + 
             ", 当前在线数: " + std::to_string(connections_.size()));
    
    return true;
}

void MerchantConnectionManager::removeConnection(const std::string& merchant_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(merchant_code);
    if (it != connections_.end()) {
        connections_.erase(it);
        LOG_INFO("商户连接已移除: " + merchant_code + 
                 ", 当前在线数: " + std::to_string(connections_.size()));
    }
}

std::shared_ptr<WebSocketConnection> MerchantConnectionManager::getConnection(
    const std::string& merchant_code
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(merchant_code);
    if (it != connections_.end()) {
        return it->second;
    }
    
    return nullptr;
}

bool MerchantConnectionManager::isOnline(const std::string& merchant_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.find(merchant_code) != connections_.end();
}

size_t MerchantConnectionManager::getOnlineCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

std::vector<std::string> MerchantConnectionManager::getOnlineMerchants() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> merchants;
    merchants.reserve(connections_.size());
    
    for (const auto& [merchant_code, conn] : connections_) {
        merchants.push_back(merchant_code);
    }
    
    return merchants;
}

// ==================== 消息发送 ====================

bool MerchantConnectionManager::sendToMerchant(
    const std::string& merchant_code,
    const WebSocketMessage& message
) {
    auto conn = getConnection(merchant_code);
    if (!conn) {
        LOG_WARNING("商户离线,无法发送消息: " + merchant_code);
        return false;
    }
    
    if (!conn->isAuthenticated()) {
        LOG_WARNING("商户未认证,无法发送消息: " + merchant_code);
        return false;
    }
    
    bool success = conn->sendMessage(message);
    if (success) {
        conn->updateLastActiveTime();
    }
    
    return success;
}

void MerchantConnectionManager::broadcast(const WebSocketMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int success_count = 0;
    int fail_count = 0;
    
    for (auto& [merchant_code, conn] : connections_) {
        if (conn && conn->isAuthenticated()) {
            if (conn->sendMessage(message)) {
                conn->updateLastActiveTime();
                success_count++;
            } else {
                fail_count++;
            }
        }
    }
    
    LOG_INFO("广播消息完成: 成功=" + std::to_string(success_count) + 
             ", 失败=" + std::to_string(fail_count));
}

// ==================== 心跳检测 ====================

void MerchantConnectionManager::startHeartbeatChecker(
    std::shared_ptr<common::network::EventLoop> event_loop
) {
    if (running_.exchange(true)) {
        LOG_WARNING("心跳检测已在运行中");
        return;
    }
    
    if (!event_loop) {
        LOG_ERROR("EventLoop为空,无法启动心跳检测");
        running_.store(false);
        return;
    }
    
    event_loop_ = event_loop;
    
    // ⭐⭐⭐ 使用EventLoop的时间轮,每30秒检测一次
    event_loop_->queueInLoop([this]() {
        heartbeat_timer_id_ = event_loop_->runEvery(
            30 * 1000,  // 30秒
            [this]() {
                try {
                    checkHeartbeat();
                } catch (const std::exception& e) {
                    LOG_ERROR("心跳检测异常: " + std::string(e.what()));
                }
            }
        );
        
        LOG_INFO("✅ 心跳检测定时任务已启动(每30秒), timer_id=" + 
                 std::to_string(heartbeat_timer_id_));
    });
}

void MerchantConnectionManager::stopHeartbeatChecker() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (event_loop_ && heartbeat_timer_id_ > 0) {
        event_loop_->cancelTimer(heartbeat_timer_id_);
        heartbeat_timer_id_ = 0;
        LOG_INFO("心跳检测定时任务已停止");
    }
    
    event_loop_.reset();
}

void MerchantConnectionManager::checkHeartbeat() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> timeout_merchants;
    
    for (auto& [merchant_code, conn] : connections_) {
        if (conn->isTimeout()) {
            LOG_WARNING("商户心跳超时: " + merchant_code);
            
            conn->close("heartbeat_timeout");
            timeout_merchants.push_back(merchant_code);
        } else if (conn->isAuthenticated()) {
            // 发送心跳PING
            WebSocketMessage ping_msg = WebSocketMessage::createPing();
            conn->sendMessage(ping_msg);
        }
    }
    
    // 移除超时连接
    for (const auto& merchant_code : timeout_merchants) {
        connections_.erase(merchant_code);
    }
    
    if (!timeout_merchants.empty()) {
        LOG_INFO("清理超时连接: " + std::to_string(timeout_merchants.size()) + "个, " +
                 "当前在线: " + std::to_string(connections_.size()));
    }
}

// ==================== 统计信息 ====================

std::string MerchantConnectionManager::getStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    json stats;
    stats["online_count"] = connections_.size();
    stats["running"] = running_.load();
    
    json merchants = json::array();
    for (const auto& [merchant_code, conn] : connections_) {
        json merchant_info;
        merchant_info["merchant_code"] = merchant_code;
        merchant_info["merchant_name"] = conn->getMerchantName();
        merchant_info["remote_ip"] = conn->getRemoteIp();
        merchant_info["is_authenticated"] = conn->isAuthenticated();
        merchant_info["is_timeout"] = conn->isTimeout();
        merchants.push_back(merchant_info);
    }
    stats["merchants"] = merchants;
    
    return stats.dump(2);
}

} // namespace websocket
} // namespace common
