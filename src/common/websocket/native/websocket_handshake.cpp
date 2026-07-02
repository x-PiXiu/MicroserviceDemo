/**
 * @file websocket_handshake.cpp
 * @brief WebSocket握手协议实现
 * @date 2025-12-11
 */

#include "common/websocket/native/websocket_handshake.h"
#include "common/logger/logger.h"
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <sstream>
#include <algorithm>

namespace common {
namespace websocket {
namespace native {

using namespace common::logger;

// ==================== 公共方法 ====================

bool WebSocketHandshake::isWebSocketRequest(const common::http::HttpRequest& req) {
    // 1. 检查HTTP方法（必须是GET）
    if (req.getMethod() != common::http::HttpMethod::GET) {
        return false;
    }
    
    // 2. 检查Upgrade头（必须是"websocket"）
    std::string upgrade = req.getHeader("Upgrade");
    std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(), ::tolower);
    if (upgrade != "websocket") {
        return false;
    }
    
    // 3. 检查Connection头（必须包含"Upgrade"）
    std::string connection = req.getHeader("Connection");
    std::transform(connection.begin(), connection.end(), connection.begin(), ::tolower);
    if (connection.find("upgrade") == std::string::npos) {
        return false;
    }
    
    // 4. 检查Sec-WebSocket-Key（必须存在）
    std::string ws_key = req.getHeader("Sec-WebSocket-Key");
    if (ws_key.empty()) {
        return false;
    }
    
    // 5. 检查Sec-WebSocket-Version（必须是13）
    std::string ws_version = req.getHeader("Sec-WebSocket-Version");
    if (ws_version != "13") {
        return false;
    }
    
    return true;
}

std::string WebSocketHandshake::generateAcceptKey(const std::string& ws_key) {
    // 1. 拼接魔术字符串
    std::string concat = ws_key + MAGIC_STRING;
    
    // 2. 计算SHA-1哈希
    std::string sha1_hash = sha1(concat);
    
    // 3. Base64编码
    return base64Encode(sha1_hash);
}

common::http::HttpResponse WebSocketHandshake::buildUpgradeResponse(
    const common::http::HttpRequest& req,
    const std::string& accept_key
) {
    (void)req;  // 暂时未使用
    
    common::http::HttpResponse res;
    
    // 设置状态码：101 Switching Protocols
    res.setStatus(101, "Switching Protocols");
    
    // 设置必需的响应头
    res.setHeader("Upgrade", "websocket");
    res.setHeader("Connection", "Upgrade");
    res.setHeader("Sec-WebSocket-Accept", accept_key);
    
    return res;
}

bool WebSocketHandshake::handleHandshake(
    const common::http::HttpRequest& req,
    common::http::HttpResponse& res
) {
    // 1. 验证WebSocket请求
    if (!isWebSocketRequest(req)) {
        res.badRequest("Invalid WebSocket request");
        LOG_ERROR("WebSocket握手失败：请求验证失败");
        return false;
    }
    
    // 2. 获取Sec-WebSocket-Key
    std::string ws_key = req.getHeader("Sec-WebSocket-Key");
    
    // 3. 生成Sec-WebSocket-Accept
    std::string accept_key = generateAcceptKey(ws_key);
    
    // 4. 构建升级响应
    res = buildUpgradeResponse(req, accept_key);
    
    LOG_DEBUG("WebSocket握手成功");
    return true;
}

// ==================== 私有辅助方法 ====================

std::string WebSocketHandshake::sha1(const std::string& input) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);
    return std::string(reinterpret_cast<char*>(hash), SHA_DIGEST_LENGTH);
}

std::string WebSocketHandshake::base64Encode(const std::string& input) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    
    // 不添加换行符
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    
    bio = BIO_push(b64, bio);
    BIO_write(bio, input.c_str(), static_cast<int>(input.length()));
    BIO_flush(bio);
    
    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    std::string result(buffer_ptr->data, buffer_ptr->length);
    
    BIO_free_all(bio);
    
    return result;
}

} // namespace native
} // namespace websocket
} // namespace common
