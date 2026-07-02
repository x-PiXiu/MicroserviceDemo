/**
 * @file websocket_handshake.h
 * @brief WebSocket握手协议实现（RFC 6455）
 * @date 2025-12-11
 */

#ifndef WEBSOCKET_HANDSHAKE_H
#define WEBSOCKET_HANDSHAKE_H

#include <string>
#include "common/http/http_request.h"
#include "common/http/http_response.h"

namespace common {
namespace websocket {
namespace native {

/**
 * @brief WebSocket握手处理类
 * @details 实现RFC 6455规定的握手协议
 * 
 * 客户端握手请求示例：
 * GET /chat HTTP/1.1
 * Host: server.example.com
 * Upgrade: websocket
 * Connection: Upgrade
 * Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
 * Sec-WebSocket-Version: 13
 * 
 * 服务器握手响应示例：
 * HTTP/1.1 101 Switching Protocols
 * Upgrade: websocket
 * Connection: Upgrade
 * Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
 */
class WebSocketHandshake {
public:
    /**
     * @brief 验证是否为有效的WebSocket升级请求
     * @param req HTTP请求
     * @return 是否为有效的WebSocket请求
     */
    static bool isWebSocketRequest(const common::http::HttpRequest& req);
    
    /**
     * @brief 计算Sec-WebSocket-Accept值
     * @param ws_key 客户端发送的Sec-WebSocket-Key
     * @return 服务器返回的Sec-WebSocket-Accept
     * 
     * 算法（RFC 6455）：
     * 1. 将Sec-WebSocket-Key与魔术字符串"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"拼接
     * 2. 对拼接后的字符串进行SHA-1哈希
     * 3. 将哈希结果进行Base64编码
     */
    static std::string generateAcceptKey(const std::string& ws_key);
    
    /**
     * @brief 构建WebSocket升级响应（101 Switching Protocols）
     * @param req HTTP请求
     * @param accept_key Sec-WebSocket-Accept值
     * @return HTTP响应对象
     */
    static common::http::HttpResponse buildUpgradeResponse(
        const common::http::HttpRequest& req,
        const std::string& accept_key
    );
    
    /**
     * @brief 验证并处理完整的握手流程
     * @param req HTTP请求
     * @param res HTTP响应（输出参数）
     * @return 验证成功返回true
     */
    static bool handleHandshake(
        const common::http::HttpRequest& req,
        common::http::HttpResponse& res
    );
    
private:
    /**
     * @brief WebSocket魔术字符串（RFC 6455规定）
     */
    static constexpr const char* MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    /**
     * @brief 计算SHA-1哈希
     * @param input 输入字符串
     * @return SHA-1哈希结果（20字节）
     */
    static std::string sha1(const std::string& input);
    
    /**
     * @brief Base64编码
     * @param input 输入数据
     * @return Base64编码字符串
     */
    static std::string base64Encode(const std::string& input);
};

} // namespace native
} // namespace websocket
} // namespace common

#endif // WEBSOCKET_HANDSHAKE_H
