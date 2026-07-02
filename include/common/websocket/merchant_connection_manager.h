// include/common/websocket/merchant_connection_manager.h
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include "common/websocket/websocket_connection.h"
#include "common/websocket/websocket_message.h"

namespace common {

// 前向声明
namespace network {
    class EventLoop;
}

namespace websocket {

/**
 * @brief 商户连接管理器(单例模式)
 * 
 * 功能:
 * 1. 管理所有商户WebSocket连接(merchant_code -> Connection映射)
 * 2. 路由消息到指定商户
 * 3. 心跳检测与超时清理
 * 4. 统计信息查询
 */
class MerchantConnectionManager {
public:
    /**
     * @brief 获取单例实例
     */
    static MerchantConnectionManager& getInstance();

    MerchantConnectionManager(const MerchantConnectionManager&) = delete;
    MerchantConnectionManager& operator=(const MerchantConnectionManager&) = delete;

    // ==================== 连接管理 ====================
    
    /**
     * @brief 注册新连接(认证成功后调用)
     * @param merchant_code 商户编码
     * @param connection 连接对象
     * @return 是否注册成功(如果该商户已有连接,会踢掉旧连接)
     */
    bool registerConnection(
        const std::string& merchant_code,
        std::shared_ptr<WebSocketConnection> connection
    );

    /**
     * @brief 移除连接
     * @param merchant_code 商户编码
     */
    void removeConnection(const std::string& merchant_code);

    /**
     * @brief 获取商户连接
     * @param merchant_code 商户编码
     * @return 连接对象(不存在返回nullptr)
     */
    std::shared_ptr<WebSocketConnection> getConnection(const std::string& merchant_code);

    /**
     * @brief 检查商户是否在线
     */
    bool isOnline(const std::string& merchant_code);

    /**
     * @brief 获取在线商户数量
     */
    size_t getOnlineCount();

    /**
     * @brief 获取所有在线商户列表
     */
    std::vector<std::string> getOnlineMerchants();

    // ==================== 消息发送 ====================
    
    /**
     * @brief 发送消息到指定商户
     * @param merchant_code 商户编码
     * @param message 消息对象
     * @return 是否发送成功(商户离线返回false)
     */
    bool sendToMerchant(const std::string& merchant_code, const WebSocketMessage& message);

    /**
     * @brief 广播消息到所有在线商户
     * @param message 消息对象
     */
    void broadcast(const WebSocketMessage& message);

    // ==================== 心跳检测 ====================
    
    /**
     * @brief 启动心跳检测定时器(每30秒检测一次)
     * @param event_loop EventLoop实例，用于注册定时器
     */
    void startHeartbeatChecker(std::shared_ptr<common::network::EventLoop> event_loop);

    /**
     * @brief 停止心跳检测
     */
    void stopHeartbeatChecker();

    // ==================== 统计信息 ====================
    
    /**
     * @brief 获取统计信息(JSON格式)
     */
    std::string getStatistics();

private:
    MerchantConnectionManager();
    ~MerchantConnectionManager();

    /**
     * @brief 心跳检测逻辑(清理超时连接)
     */
    void checkHeartbeat();

    std::unordered_map<std::string, std::shared_ptr<WebSocketConnection>> connections_; // merchant_code -> Connection
    std::mutex mutex_;                                  // 线程锁
    
    std::shared_ptr<common::network::EventLoop> event_loop_;  // EventLoop实例(用于定时器)
    uint64_t heartbeat_timer_id_{0};                    // 心跳检测定时器ID
    std::atomic<bool> running_{false};                  // 是否运行中
};

} // namespace websocket
} // namespace common
