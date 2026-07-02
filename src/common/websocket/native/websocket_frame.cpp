/**
 * @file websocket_frame.cpp
 * @brief WebSocket帧协议实现
 * @date 2025-12-11
 */

#include "common/websocket/native/websocket_frame.h"
#include "common/logger/logger.h"
#include <cstring>
#include <random>
#include <arpa/inet.h>

namespace common {
namespace websocket {
namespace native {

using namespace common::logger;

// ==================== 静态工厂方法 ====================

WebSocketFrame WebSocketFrame::createTextFrame(const std::string& text, bool is_masked) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = WebSocketOpcode::TEXT;
    frame.mask = is_masked;
    frame.payload.assign(text.begin(), text.end());
    frame.payload_length = frame.payload.size();
    
    if (is_masked) {
        frame.masking_key = generateMaskingKey();
        frame.applyMask();
    }
    
    return frame;
}

WebSocketFrame WebSocketFrame::createBinaryFrame(const std::vector<uint8_t>& data, bool is_masked) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = WebSocketOpcode::BINARY;
    frame.mask = is_masked;
    frame.payload = data;
    frame.payload_length = frame.payload.size();
    
    if (is_masked) {
        frame.masking_key = generateMaskingKey();
        frame.applyMask();
    }
    
    return frame;
}

WebSocketFrame WebSocketFrame::createPingFrame(const std::string& payload_data) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = WebSocketOpcode::PING;
    frame.mask = false;
    frame.payload.assign(payload_data.begin(), payload_data.end());
    frame.payload_length = frame.payload.size();
    return frame;
}

WebSocketFrame WebSocketFrame::createPongFrame(const std::string& payload_data) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = WebSocketOpcode::PONG;
    frame.mask = false;
    frame.payload.assign(payload_data.begin(), payload_data.end());
    frame.payload_length = frame.payload.size();
    return frame;
}

WebSocketFrame WebSocketFrame::createCloseFrame(WebSocketCloseCode code, const std::string& reason) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = WebSocketOpcode::CLOSE;
    frame.mask = false;
    
    // 关闭帧格式：2字节状态码 + 原因字符串
    uint16_t code_network = htons(static_cast<uint16_t>(code));
    frame.payload.resize(2 + reason.size());
    std::memcpy(frame.payload.data(), &code_network, 2);
    if (!reason.empty()) {
        std::memcpy(frame.payload.data() + 2, reason.data(), reason.size());
    }
    
    frame.payload_length = frame.payload.size();
    return frame;
}

// ==================== 序列化/反序列化 ====================

std::vector<uint8_t> WebSocketFrame::serialize() const {
    std::vector<uint8_t> result;
    
    // 第1字节：FIN + RSV + Opcode
    uint8_t byte1 = 0;
    if (fin) byte1 |= 0x80;
    if (rsv1) byte1 |= 0x40;
    if (rsv2) byte1 |= 0x20;
    if (rsv3) byte1 |= 0x10;
    byte1 |= static_cast<uint8_t>(opcode) & 0x0F;
    result.push_back(byte1);
    
    // 第2字节：MASK + Payload Length (7位)
    uint8_t byte2 = 0;
    if (mask) byte2 |= 0x80;
    
    if (payload_length < 126) {
        byte2 |= static_cast<uint8_t>(payload_length);
        result.push_back(byte2);
    } else if (payload_length < 65536) {
        byte2 |= 126;
        result.push_back(byte2);
        // ⭐⭐⭐ 修复：扩展长度使用网络字节序（大端），直接手动拆分
        // 注意：不要使用 htons 后再拆分，会导致双重转换！
        uint16_t len16 = static_cast<uint16_t>(payload_length);
        result.push_back((len16 >> 8) & 0xFF);  // 高字节在前
        result.push_back(len16 & 0xFF);          // 低字节在后
    } else {
        byte2 |= 127;
        result.push_back(byte2);
        // 扩展长度：8字节（网络字节序）
        uint64_t len64 = payload_length;
        for (int i = 7; i >= 0; --i) {
            result.push_back((len64 >> (i * 8)) & 0xFF);
        }
    }
    
    // Masking Key（如果MASK=1）
    if (mask) {
        result.push_back((masking_key >> 24) & 0xFF);
        result.push_back((masking_key >> 16) & 0xFF);
        result.push_back((masking_key >> 8) & 0xFF);
        result.push_back(masking_key & 0xFF);
    }
    
    // Payload Data
    result.insert(result.end(), payload.begin(), payload.end());
    
    return result;
}

std::shared_ptr<WebSocketFrame> WebSocketFrame::deserialize(
    const std::vector<uint8_t>& data,
    size_t& bytes_consumed
) {
    bytes_consumed = 0;
    
    // 至少需要2字节
    if (data.size() < 2) {
        return nullptr;
    }
    
    auto frame = std::make_shared<WebSocketFrame>();
    size_t offset = 0;
    
    // 解析第1字节
    uint8_t byte1 = data[offset++];
    frame->fin = (byte1 & 0x80) != 0;
    frame->rsv1 = (byte1 & 0x40) != 0;
    frame->rsv2 = (byte1 & 0x20) != 0;
    frame->rsv3 = (byte1 & 0x10) != 0;
    frame->opcode = static_cast<WebSocketOpcode>(byte1 & 0x0F);
    
    // 解析第2字节
    uint8_t byte2 = data[offset++];
    frame->mask = (byte2 & 0x80) != 0;
    uint8_t payload_len = byte2 & 0x7F;
    
    // 解析扩展长度
    if (payload_len < 126) {
        frame->payload_length = payload_len;
    } else if (payload_len == 126) {
        if (data.size() < offset + 2) return nullptr;
        frame->payload_length = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;
    } else { // 127
        if (data.size() < offset + 8) return nullptr;
        frame->payload_length = 0;
        for (int i = 0; i < 8; ++i) {
            frame->payload_length = (frame->payload_length << 8) | data[offset + i];
        }
        offset += 8;
    }
    
    // 解析Masking Key
    if (frame->mask) {
        if (data.size() < offset + 4) return nullptr;
        frame->masking_key = (static_cast<uint32_t>(data[offset]) << 24) |
                             (static_cast<uint32_t>(data[offset + 1]) << 16) |
                             (static_cast<uint32_t>(data[offset + 2]) << 8) |
                             static_cast<uint32_t>(data[offset + 3]);
        offset += 4;
    }
    
    // 解析Payload
    if (data.size() < offset + frame->payload_length) {
        return nullptr; // 数据不完整
    }
    
    frame->payload.assign(data.begin() + offset, data.begin() + offset + frame->payload_length);
    offset += frame->payload_length;
    
    // 如果有掩码，移除掩码
    if (frame->mask) {
        frame->removeMask();
    }
    
    bytes_consumed = offset;
    return frame;
}

// ==================== 辅助方法 ====================

std::string WebSocketFrame::getTextPayload() const {
    return std::string(payload.begin(), payload.end());
}

void WebSocketFrame::applyMask() {
    if (mask && masking_key != 0) {
        maskData(payload, masking_key);
    }
}

void WebSocketFrame::removeMask() {
    if (mask && masking_key != 0) {
        maskData(payload, masking_key); // XOR操作，两次相同即解密
        mask = false; // 标记已解密
    }
}

uint32_t WebSocketFrame::generateMaskingKey() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
    return dis(gen);
}

void WebSocketFrame::maskData(std::vector<uint8_t>& data, uint32_t mask_key) {
    uint8_t mask_bytes[4];
    mask_bytes[0] = (mask_key >> 24) & 0xFF;
    mask_bytes[1] = (mask_key >> 16) & 0xFF;
    mask_bytes[2] = (mask_key >> 8) & 0xFF;
    mask_bytes[3] = mask_key & 0xFF;
    
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= mask_bytes[i % 4];
    }
}

} // namespace native
} // namespace websocket
} // namespace common
