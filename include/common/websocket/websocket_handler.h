/**
 * @file websocket_handler.h
 * @brief WebSocket握手和消息处理器
 * @details 实现HTTP到WebSocket的协议升级、JWT认证、消息路由
 * @date 2025-12-11
 */

#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>

#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/logger/logger.h"
#include "common/database/mysql_pool.h"
#include "common/network/event_loop.h"
#include "common/websocket/websocket_connection.h"
#include "common/websocket/merchant_connection_manager.h"
#include "common/websocket/delivery_task_router.h"

namespace common {
namespace websocket {

// WebSocketpp类型定义
using WebSocketServer = websocketpp::server<websocketpp::config::asio>;
using WebSocketConnectionHdl = websocketpp::connection_hdl;
using WebSocketMessagePtr = WebSocketServer::message_ptr;

/**
 * @brief WebSocket握手验证结果
 */
struct WebSocketHandshakeResult {
    bool success{false};                  ///< 是否成功
    std::string merchant_code;            ///< 商户代码
    std::string merchant_name;            ///< 商户名称
    std::string error_message;            ///< 错误信息
    
    static WebSocketHandshakeResult makeSuccess(
        const std::string& code,
        const std::string& name
    ) {
        WebSocketHandshakeResult result;
        result.success = true;
        result.merchant_code = code;
        result.merchant_name = name;
        return result;
    }
    
    static WebSocketHandshakeResult makeError(const std::string& error) {
        WebSocketHandshakeResult result;
        result.success = false;
        result.error_message = error;
        return result;
    }
};

/**
 * @brief WebSocket处理器类
 * @details 管理WebSocket连接、认证、消息路由
 * 
 * 核心功能：
 * 1. HTTP升级到WebSocket（握手处理）
 * 2. JWT Token认证
 * 3. 商户连接管理
 * 4. 消息路由和分发
 * 5. 心跳和超时检测
 */
class WebSocketHandler {
public:
    /**
     * @brief 构造函数
     * @param db_pool 数据库连接池
     * @param event_loop 事件循环
     * @param jwt_secret JWT密钥
     */
    explicit WebSocketHandler(
        std::shared_ptr<common::database::MySQLPool> db_pool,
        std::shared_ptr<common::network::EventLoop> event_loop,
        const std::string& jwt_secret = ""
    );
    
    ~WebSocketHandler();
    
    // ==================== HTTP路由处理器 ====================
    
    /**
     * @brief HTTP升级到WebSocket的路由处理器
     * @details 用于注册到HttpServer：server.get("/ws/merchant", handler.getUpgradeHandler())
     * @return HTTP请求处理函数
     */
    std::function<void(const common::http::HttpRequest&, common::http::HttpResponse&)> 
    getUpgradeHandler();
    
    // ==================== WebSocketpp事件处理 ====================
    
    /**
     * @brief 启动WebSocket服务器
     * @param port 监听端口（独立端口）
     * @return 成功返回true
     */
    bool start(uint16_t port);
    
    /**
     * @brief 停止WebSocket服务器
     */
    void stop();
    
    /**
     * @brief 检查服务器是否运行
     */
    bool isRunning() const { return running_.load(); }
    
    // ==================== JWT认证 ====================
    
    /**
     * @brief 设置JWT密钥
     * @param secret JWT密钥
     */
    void setJwtSecret(const std::string& secret) { jwt_secret_ = secret; }
    
    /**
     * @brief 验证JWT Token
     * @param token JWT Token
     * @return 验证结果
     */
    WebSocketHandshakeResult validateJwtToken(const std::string& token);
    
    // ==================== 发货任务API ====================
    
    /**
     * @brief 发送发货任务到指定商户
     * @param merchant_code 商户代码
     * @param task_data 任务数据（JSON）
     * @param callback 结果回调
     * @return 成功返回true
     */
    bool sendDeliveryTask(
        const std::string& merchant_code,
        const nlohmann::json& task_data,
        DeliveryTaskRouter::DeliveryCallback callback
    );
    
    /**
     * @brief 检查商户是否在线
     * @param merchant_code 商户代码
     * @return 在线返回true
     */
    bool isMerchantOnline(const std::string& merchant_code);
    
    /**
     * @brief 发送心跳Ping消息到所有在线商户 ⭐⭐⭐ Phase 3
     * @return 成功发送的数量
     */
    int sendHeartbeatPing();
    
    /**
     * @brief 清理超时的WebSocket连接 ⭐⭐⭐ Phase 3
     * @param timeout_seconds 超时时间（秒）
     * @return 清理的连接数量
     */
    int cleanupTimeoutConnections(int timeout_seconds = 120);
    
private:
    // ==================== 内部方法 ====================
    
    /**
     * @brief WebSocket连接打开回调
     */
    void onOpen(WebSocketConnectionHdl hdl);
    
    /**
     * @brief WebSocket连接关闭回调
     */
    void onClose(WebSocketConnectionHdl hdl);
    
    /**
     * @brief WebSocket消息接收回调
     */
    void onMessage(WebSocketConnectionHdl hdl, WebSocketMessagePtr msg);
    
    /**
     * @brief WebSocket验证回调
     */
    bool onValidate(WebSocketConnectionHdl hdl);
    
    /**
     * @brief 处理认证消息
     */
    void handleAuthMessage(
        WebSocketConnectionHdl hdl,
        const nlohmann::json& msg_data
    );
    
    /**
     * @brief 处理Pong消息
     */
    void handlePongMessage(
        WebSocketConnectionHdl hdl,
        const nlohmann::json& msg_data
    );
    
    /**
     * @brief 处理发货结果消息
     */
    void handleDeliveryResultMessage(
        WebSocketConnectionHdl hdl,
        const nlohmann::json& msg_data
    );
    
    /**
     * @brief 发送JSON消息到连接
     */
    void sendJsonMessage(
        WebSocketConnectionHdl hdl,
        const nlohmann::json& json_data
    );
    
    /**
     * @brief 关闭连接并发送错误
     */
    void closeWithError(
        WebSocketConnectionHdl hdl,
        const std::string& reason
    );
    
    /**
     * @brief 从数据库验证商户凭证
     */
    WebSocketHandshakeResult validateMerchantFromDB(
        const std::string& merchant_code,
        const std::string& token
    );
    
private:
    // ==================== 成员变量 ====================
    
    // 核心组件
    std::shared_ptr<common::database::MySQLPool> db_pool_;
    std::shared_ptr<common::network::EventLoop> event_loop_;
    std::string jwt_secret_;
    
    // WebSocket服务器
    WebSocketServer ws_server_;
    std::shared_ptr<std::thread> ws_thread_;
    std::atomic<bool> running_{false};
    
    // 连接管理
    MerchantConnectionManager& connection_manager_;
    std::shared_ptr<DeliveryTaskRouter> task_router_;
    
    // 连接映射（WebSocketpp连接 -> 商户代码）
    std::map<void*, std::string> hdl_to_merchant_;
    std::mutex hdl_map_mutex_;
};

} // namespace websocket
} // namespace common

#endif // WEBSOCKET_HANDLER_H
