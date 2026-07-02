/**
 * @file websocket_frame.h
 * @brief WebSocket帧结构定义（RFC 6455）
 * @details 基于EventLoop的原生WebSocket协议实现
 * @date 2025-12-11
 */

#ifndef WEBSOCKET_FRAME_H
#define WEBSOCKET_FRAME_H

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace common {
namespace websocket {
namespace native {

/**
 * @brief WebSocket操作码（RFC 6455）
 */
enum class WebSocketOpcode : uint8_t {
    CONTINUATION = 0x0,  ///< 后续帧
    TEXT = 0x1,          ///< 文本帧
    BINARY = 0x2,        ///< 二进制帧
    CLOSE = 0x8,         ///< 关闭帧
    PING = 0x9,          ///< Ping帧
    PONG = 0xA           ///< Pong帧
};

/**
 * @brief WebSocket关闭状态码（RFC 6455）
 */
enum class WebSocketCloseCode : uint16_t {
    NORMAL = 1000,              ///< 正常关闭
    GOING_AWAY = 1001,          ///< 端点离开
    PROTOCOL_ERROR = 1002,      ///< 协议错误
    UNSUPPORTED_DATA = 1003,    ///< 不支持的数据类型
    INVALID_PAYLOAD = 1007,     ///< 无效的payload数据
    POLICY_VIOLATION = 1008,    ///< 违反策略
    MESSAGE_TOO_BIG = 1009,     ///< 消息过大
    INTERNAL_ERROR = 1011       ///< 内部错误
};

/**
 * @brief WebSocket帧结构
 * 
 * 帧格式（RFC 6455）：
 * 
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 * |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 * |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 * | |1|2|3|       |K|             |                               |
 * +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 * |     Extended payload length continued, if payload len == 127  |
 * + - - - - - - - - - - - - - - - +-------------------------------+
 * |                               |Masking-key, if MASK set to 1  |
 * +-------------------------------+-------------------------------+
 * | Masking-key (continued)       |          Payload Data         |
 * +-------------------------------- - - - - - - - - - - - - - - - +
 * :                     Payload Data continued ...                :
 * + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 * |                     Payload Data continued ...                |
 * +---------------------------------------------------------------+
 */
struct WebSocketFrame {
    // 头部字段
    bool fin{true};                    ///< FIN位：是否最后一帧
    bool rsv1{false};                  ///< RSV1位：保留（扩展使用）
    bool rsv2{false};                  ///< RSV2位：保留（扩展使用）
    bool rsv3{false};                  ///< RSV3位：保留（扩展使用）
    WebSocketOpcode opcode{WebSocketOpcode::TEXT};  ///< 操作码
    bool mask{false};                  ///< MASK位：是否有掩码
    uint64_t payload_length{0};        ///< Payload长度
    uint32_t masking_key{0};           ///< 掩码密钥（4字节）
    
    // Payload数据
    std::vector<uint8_t> payload;      ///< 实际数据
    
    /**
     * @brief 默认构造函数
     */
    WebSocketFrame() = default;
    
    /**
     * @brief 构造文本帧
     * @param text 文本内容
     * @param is_masked 是否需要掩码
     */
    static WebSocketFrame createTextFrame(const std::string& text, bool is_masked = false);
    
    /**
     * @brief 构造二进制帧
     * @param data 二进制数据
     * @param is_masked 是否需要掩码
     */
    static WebSocketFrame createBinaryFrame(const std::vector<uint8_t>& data, bool is_masked = false);
    
    /**
     * @brief 构造Ping帧
     * @param payload_data Ping数据（可选）
     */
    static WebSocketFrame createPingFrame(const std::string& payload_data = "");
    
    /**
     * @brief 构造Pong帧
     * @param payload_data Pong数据（可选）
     */
    static WebSocketFrame createPongFrame(const std::string& payload_data = "");
    
    /**
     * @brief 构造关闭帧
     * @param code 关闭码
     * @param reason 关闭原因
     */
    static WebSocketFrame createCloseFrame(
        WebSocketCloseCode code = WebSocketCloseCode::NORMAL,
        const std::string& reason = ""
    );
    
    /**
     * @brief 序列化为字节流
     * @return 字节流
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * @brief 从字节流反序列化
     * @param data 字节流
     * @param bytes_consumed 消耗的字节数（输出参数）
     * @return 解析成功返回帧对象，失败返回nullptr
     */
    static std::shared_ptr<WebSocketFrame> deserialize(
        const std::vector<uint8_t>& data,
        size_t& bytes_consumed
    );
    
    /**
     * @brief 获取文本内容（仅TEXT帧有效）
     * @return 文本内容
     */
    std::string getTextPayload() const;
    
    /**
     * @brief 应用掩码（客户端发送时使用）
     */
    void applyMask();
    
    /**
     * @brief 移除掩码（服务器接收时使用）
     */
    void removeMask();
    
private:
    /**
     * @brief 生成随机掩码密钥
     */
    static uint32_t generateMaskingKey();
    
    /**
     * @brief 对数据进行掩码/解掩码操作
     * @param data 数据
     * @param mask_key 掩码密钥
     */
    static void maskData(std::vector<uint8_t>& data, uint32_t mask_key);
};

} // namespace native
} // namespace websocket
} // namespace common

#endif // WEBSOCKET_FRAME_H
