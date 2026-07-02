/**
 * @file websocket_native_server.h
 * @brief 基于EventLoop的WebSocket原生服务器实现
 * @date 2025-12-11
 */

#ifndef WEBSOCKET_NATIVE_SERVER_H
#define WEBSOCKET_NATIVE_SERVER_H

#include <string>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <vector>
#include <ctime>
#include "common/network/event_loop.h"
#include "common/network/socket.h"
#include "common/network/channel.h"
#include "common/websocket/native/websocket_native_connection.h"
#include "common/http/http_request.h"

// ⭐⭐⭐ Phase 4: 集成商户连接管理 (2025-12-11)
// ⭐⭐⭐ 2025-12-11: 移除对废弃模块的依赖
// 前置声明，避免循环依赖
namespace common {
namespace websocket {
    // class MerchantConnectionManager;  // 已废弃
    // class DeliveryTaskRouter;  // 已废弃
}
}

namespace common {
namespace websocket {
namespace native {

/**
 * @brief WebSocket原生服务器类
 * @details 基于EventLoop实现的WebSocket服务器
 * 
 * 特性：
 * - 完全集成到EventLoop（无独立线程）
 * - 支持HTTP到WebSocket协议升级
 * - 连接池管理
 * - 自动心跳检测
 * - 性能优化（零拷贝、事件驱动）
 */
class WebSocketNativeServer : public std::enable_shared_from_this<WebSocketNativeServer> {
public:
    // 回调函数类型
    using ConnectionCallback = std::function<void(std::shared_ptr<WebSocketNativeConnection>)>;
    using MessageCallback = std::function<void(
        std::shared_ptr<WebSocketNativeConnection>,
        const std::string& message
    )>;
    
    // ⭐⭐⭐ Phase 6 + Phase 8: 商户断开回调 (2025-12-18 增加client_id)
    using MerchantDisconnectCallback = std::function<void(
        const std::string& merchant_code,
        const std::string& client_id,
        const std::string& client_ip,
        const std::string& disconnect_reason
    )>;
    
    /**
     * @brief 构造函数
     * @param event_loop EventLoop实例
     * @param port 监听端口
     * @param bind_addr 绑定地址（默认0.0.0.0）
     */
    WebSocketNativeServer(
        std::shared_ptr<common::network::EventLoop> event_loop,
        uint16_t port,
        const std::string& bind_addr = "0.0.0.0"
    );
    
    ~WebSocketNativeServer();
    
    // ==================== 生命周期管理 ====================
    
    /**
     * @brief 启动服务器
     * @return 成功返回true
     */
    bool start();
    
    /**
     * @brief 停止服务器
     */
    void stop();
    
    /**
     * @brief 检查服务器是否运行
     */
    bool isRunning() const { return running_.load(); }
    
    // ==================== 回调设置 ====================
    
    /**
     * @brief 设置新连接回调
     */
    void setConnectionCallback(ConnectionCallback cb) { connection_callback_ = std::move(cb); }
    
    /**
     * @brief 设置消息回调
     */
    void setMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
    
    /**
     * @brief 设置商户断开回调
     * @details 商户断开连接时触发，用于更新数据库状态、记录日志等
     */
    void setMerchantDisconnectCallback(MerchantDisconnectCallback cb) { 
        merchant_disconnect_callback_ = std::move(cb); 
    }
    
    // ==================== 配置 ====================
    
    /**
     * @brief 设置最大连接数
     */
    void setMaxConnections(size_t max) { max_connections_ = max; }
    
    /**
     * @brief 设置每个IP的最大连接数
     */
    void setMaxConnectionsPerIP(size_t max) { max_per_ip_ = max; }
    
    // ==================== 统计信息 ====================
    
    /**
     * @brief 获取当前连接数
     */
    size_t getConnectionCount() const;
    
    /**
     * @brief 获取指定IP的连接数
     */
    size_t getConnectionCountByIP(const std::string& ip) const;
    
    // ⭐⭐⭐ Phase 4: 商户连接管理API (2025-12-11)
    // ⭐⭐⭐ Phase 8: 支持多客户端同时在线 (2025-12-18)
    
    /**
     * @brief 客户端信息结构（支持多客户端）
     */
    struct ClientInfo {
        std::string client_id;           ///< 客户端唯一标识
        std::string client_name;         ///< 客户端名称(如"游戏1服务器")
        std::string client_ip;           ///< 客户端IP
        std::string client_version;      ///< 客户端版本
        int fd;                          ///< socket fd
        std::time_t connected_at;        ///< 连接时间
        std::time_t last_heartbeat_at;   ///< 最后心跳时间
    };
    
    /**
     * @brief 注册商户客户端连接（支持多客户端）
     * @param merchant_code 商户代码
     * @param client_id 客户端唯一标识
     * @param conn WebSocket连接
     * @param client_name 客户端名称(可选)
     * @return 成功返回true
     */
    bool registerMerchantClient(
        const std::string& merchant_code,
        const std::string& client_id,
        std::shared_ptr<WebSocketNativeConnection> conn,
        const std::string& client_name = ""
    );
    
    /**
     * @brief 注册商户连接（兼容旧版本，使用fd作为client_id）
     * @param merchant_code 商户代码
     * @param conn WebSocket连接
     * @return 成功返回true
     * @deprecated 建议使用 registerMerchantClient
     */
    bool registerMerchant(const std::string& merchant_code, 
                         std::shared_ptr<WebSocketNativeConnection> conn);
    
    /**
     * @brief 移除商户的指定客户端连接
     * @param merchant_code 商户代码
     * @param client_id 客户端标识
     */
    void unregisterMerchantClient(const std::string& merchant_code, const std::string& client_id);
    
    /**
     * @brief 移除商户所有客户端连接
     * @param merchant_code 商户代码
     */
    void unregisterMerchant(const std::string& merchant_code);
    
    /**
     * @brief 检查商户是否有任何客户端在线
     * @param merchant_code 商户代码
     * @return 有客户端在线返回true
     */
    bool isMerchantOnline(const std::string& merchant_code) const;
    
    /**
     * @brief 获取商户在线客户端数量
     * @param merchant_code 商户代码
     * @return 在线客户端数量
     */
    size_t getMerchantOnlineClientCount(const std::string& merchant_code) const;
    
    /**
     * @brief 获取商户所有客户端信息
     * @param merchant_code 商户代码
     * @return 客户端信息列表
     */
    std::vector<ClientInfo> getMerchantClients(const std::string& merchant_code) const;
    
    /**
     * @brief 获取商户的一个连接（兼容旧版本，返回第一个可用连接）
     * @param merchant_code 商户代码
     * @return 连接指针，不存在返回nullptr
     */
    std::shared_ptr<WebSocketNativeConnection> getMerchantConnection(
        const std::string& merchant_code
    ) const;
    
    /**
     * @brief 获取商户指定客户端的连接
     * @param merchant_code 商户代码
     * @param client_id 客户端标识
     * @return 连接指针，不存在返回nullptr
     */
    std::shared_ptr<WebSocketNativeConnection> getMerchantClientConnection(
        const std::string& merchant_code,
        const std::string& client_id
    ) const;
    
    /**
     * @brief 获取商户所有客户端连接
     * @param merchant_code 商户代码
     * @return 连接列表
     */
    std::vector<std::shared_ptr<WebSocketNativeConnection>> getMerchantConnections(
        const std::string& merchant_code
    ) const;
    
    /**
     * @brief 向商户的所有客户端发送消息
     * @param merchant_code 商户代码
     * @param message 消息内容
     * @return 成功发送的客户端数量
     */
    size_t sendToMerchantAll(const std::string& merchant_code, const std::string& message);
    
    /**
     * @brief 向商户的第一个可用客户端发送消息（兼容旧版本）
     * @param merchant_code 商户代码
     * @param message 消息内容
     * @return 成功返回true
     */
    bool sendToMerchant(const std::string& merchant_code, const std::string& message);
    
    /**
     * @brief 向商户的指定客户端发送消息
     * @param merchant_code 商户代码
     * @param client_id 客户端标识
     * @param message 消息内容
     * @return 成功返回true
     */
    bool sendToMerchantClient(
        const std::string& merchant_code,
        const std::string& client_id,
        const std::string& message
    );
    
    /**
     * @brief 通过连接获取商户代码
     * @param conn WebSocket连接
     * @return 商户代码，未找到返回空字符串
     */
    std::string getMerchantCodeByConnection(
        std::shared_ptr<WebSocketNativeConnection> conn
    ) const;
    
    /**
     * @brief 通过连接获取客户端标识
     * @param conn WebSocket连接
     * @return 客户端标识，未找到返回空字符串
     */
    std::string getClientIdByConnection(
        std::shared_ptr<WebSocketNativeConnection> conn
    ) const;
    
    /**
     * @brief 更新客户端心跳时间 ⭐⭐⭐ Phase 8 (2025-12-19)
     * @param merchant_code 商户代码
     * @param client_id 客户端标识
     * @return 更新成功返回true
     */
    bool updateClientHeartbeat(
        const std::string& merchant_code,
        const std::string& client_id
    );
    
    /**
     * @brief 获取所有在线商户编码列表 ⭐⭐⭐ Phase 8 优化 (2025-12-19)
     * @details 用于批量同步心跳到数据库，避免连接池耗尽
     * @return 在线商户编码列表（去重）
     */
    std::vector<std::string> getOnlineMerchantCodes() const;
    
    /**
     * @brief 设置商户连接管理器（已废弃）
     * @deprecated MerchantConnectionManager已废弃，使用原生实现的registerMerchant/unregisterMerchant
     */
    // void setMerchantConnectionManager(...);  // 已废弃
    
    /**
     * @brief 设置发货任务路由器（已废弃）
     * @deprecated DeliveryTaskRouter已废弃
     */
    // void setDeliveryTaskRouter(...);  // 已废弃
    
private:
    // ==================== 内部方法 ====================
    
    /**
     * @brief 处理新连接
     */
    void handleNewConnection();
    
    /**
     * @brief 处理WebSocket升级
     * @param client_fd 客户端socket
     * @param client_ip 客户端IP地址
     * @param req HTTP请求
     */
    void handleWebSocketUpgrade(int client_fd, const std::string& client_ip, const common::http::HttpRequest& req);
    
    /**
     * @brief 解析HTTP请求
     * @param raw_request 原始HTTP请求数据
     * @param req 解析结果
     * @return 解析成功返回true
     */
    bool parseHttpRequest(const std::string& raw_request, common::http::HttpRequest& req);
    
    /**
     * @brief 创建WebSocket连接
     * @param client_fd 客户端socket
     * @param client_ip 客户端IP地址
     */
    void createWebSocketConnection(int client_fd, const std::string& client_ip);
    
    /**
     * @brief 移除连接
     */
    void removeConnection(int fd);
    
    /**
     * @brief 检查IP连接限制
     */
    bool checkIPLimit(const std::string& ip);
    
    // ==================== 成员变量 ====================
    
    std::shared_ptr<common::network::EventLoop> event_loop_;  ///< EventLoop
    uint16_t port_;                              ///< 监听端口
    std::string bind_addr_;                      ///< 绑定地址
    
    std::unique_ptr<common::network::Socket> listen_socket_;   ///< 监听socket
    std::shared_ptr<common::network::Channel> accept_channel_; ///< 接受连接的Channel
    
    std::atomic<bool> running_{false};           ///< 运行状态
    
    // 连接管理
    std::map<int, std::shared_ptr<WebSocketNativeConnection>> connections_;  ///< 连接池
    mutable std::mutex connections_mutex_;       ///< 连接池锁
    
    std::map<std::string, size_t> ip_connection_count_;  ///< IP连接计数
    mutable std::mutex ip_count_mutex_;          ///< IP计数锁
    
    // 回调
    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    MerchantDisconnectCallback merchant_disconnect_callback_;  // ⭐ 商户断开回调
    
    // 配置
    size_t max_connections_{10000};              ///< 最大连接数
    size_t max_per_ip_{100};                     ///< 每IP最大连接数
    
    // ⭐⭐⭐ Phase 4 + Phase 8: 商户多客户端连接映射 (2025-12-18)
    // 新结构：一个商户可以有多个客户端
    std::map<std::string, std::map<std::string, ClientInfo>> merchant_clients_;  ///< merchant_code -> (client_id -> ClientInfo)
    std::map<int, std::pair<std::string, std::string>> fd_to_merchant_client_;   ///< fd -> (merchant_code, client_id)
    mutable std::mutex merchant_map_mutex_;      ///< 商户映射锁
    
    // ⭐⭐⭐ Phase 4: 可选的外部管理器（与旧版本兼容 - 已废弃）
    // std::shared_ptr<common::websocket::MerchantConnectionManager> merchant_manager_;  // 已废弃
    // std::shared_ptr<common::websocket::DeliveryTaskRouter> task_router_;  // 已废弃
};

} // namespace native
} // namespace websocket
} // namespace common

#endif // WEBSOCKET_NATIVE_SERVER_H
