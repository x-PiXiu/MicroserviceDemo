/**
 * @file test_websocket_native.cpp
 * @brief WebSocket原生实现测试示例
 * @date 2025-12-11
 */

#include "common/websocket/native/websocket_native_server.h"
#include "common/websocket/native/websocket_frame.h"
#include "common/logger/logger.h"
#include "common/network/event_loop.h"
#include <iostream>
#include <memory>
#include <thread>

using namespace common::websocket::native;
using namespace common::logger;
using namespace common::network;

/**
 * @brief 测试WebSocket原生服务器
 */
void testWebSocketNativeServer() {
    std::cout << "========== WebSocket原生服务器测试 ==========" << std::endl;
    
    // 创建EventLoop
    auto event_loop = std::make_shared<EventLoop>();
    
    // 创建WebSocket服务器
    auto ws_server = std::make_shared<WebSocketNativeServer>(
        event_loop,
        9091,  // 端口
        "0.0.0.0"
    );
    
    // 设置新连接回调
    ws_server->setConnectionCallback([](auto conn) {
        LOG_INFO("✅ 新连接：" + conn->getRemoteIP());
        
        // 发送欢迎消息
        conn->sendText("Welcome to WebSocket Native Server!");
    });
    
    // 设置消息回调
    ws_server->setMessageCallback([](auto conn, const std::string& message) {
        LOG_INFO("📨 收到消息：" + message);
        
        // 回显消息
        conn->sendText("Echo: " + message);
        
        // 如果收到"ping"，回复"pong"
        if (message == "ping") {
            conn->sendPing("heartbeat");
        }
    });
    
    // 设置连接限制
    ws_server->setMaxConnections(1000);
    ws_server->setMaxConnectionsPerIP(10);
    
    // 启动服务器
    if (!ws_server->start()) {
        std::cerr << "❌ 服务器启动失败" << std::endl;
        return;
    }
    
    std::cout << "✅ WebSocket服务器已启动：ws://0.0.0.0:9091" << std::endl;
    std::cout << "   使用 websocat 测试：websocat ws://127.0.0.1:9091" << std::endl;
    std::cout << "   按 Ctrl+C 停止服务器..." << std::endl;
    
    // 在后台线程运行EventLoop
    std::thread event_loop_thread([event_loop]() {
        event_loop->loop();
    });
    
    // 定期打印统计信息
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        size_t conn_count = ws_server->getConnectionCount();
        std::cout << "📊 当前连接数：" << conn_count << std::endl;
    }
    
    event_loop_thread.join();
}

/**
 * @brief 测试WebSocket帧协议
 */
void testWebSocketFrame() {
    std::cout << "========== WebSocket帧协议测试 ==========" << std::endl;
    
    // 测试1：文本帧序列化和反序列化
    {
        std::cout << "\n测试1：文本帧" << std::endl;
        
        std::string text = "Hello WebSocket!";
        auto frame = WebSocketFrame::createTextFrame(text, false);
        
        // 序列化
        auto data = frame.serialize();
        std::cout << "  序列化后大小：" << data.size() << " 字节" << std::endl;
        
        // 反序列化
        size_t consumed = 0;
        auto parsed_frame = WebSocketFrame::deserialize(data, consumed);
        
        if (parsed_frame) {
            std::string parsed_text = parsed_frame->getTextPayload();
            std::cout << "  原始文本：" << text << std::endl;
            std::cout << "  解析文本：" << parsed_text << std::endl;
            std::cout << "  ✅ 文本帧测试通过" << std::endl;
        } else {
            std::cout << "  ❌ 文本帧测试失败" << std::endl;
        }
    }
    
    // 测试2：Ping/Pong帧
    {
        std::cout << "\n测试2：Ping帧" << std::endl;
        
        auto ping_frame = WebSocketFrame::createPingFrame("heartbeat");
        auto data = ping_frame.serialize();
        
        size_t consumed = 0;
        auto parsed_frame = WebSocketFrame::deserialize(data, consumed);
        
        if (parsed_frame && parsed_frame->opcode == WebSocketOpcode::PING) {
            std::cout << "  ✅ Ping帧测试通过" << std::endl;
        } else {
            std::cout << "  ❌ Ping帧测试失败" << std::endl;
        }
    }
    
    // 测试3：Close帧
    {
        std::cout << "\n测试3：Close帧" << std::endl;
        
        auto close_frame = WebSocketFrame::createCloseFrame(
            WebSocketCloseCode::NORMAL,
            "Normal closure"
        );
        auto data = close_frame.serialize();
        
        size_t consumed = 0;
        auto parsed_frame = WebSocketFrame::deserialize(data, consumed);
        
        if (parsed_frame && parsed_frame->opcode == WebSocketOpcode::CLOSE) {
            std::cout << "  ✅ Close帧测试通过" << std::endl;
        } else {
            std::cout << "  ❌ Close帧测试失败" << std::endl;
        }
    }
    
    // 测试4：掩码处理
    {
        std::cout << "\n测试4：掩码处理" << std::endl;
        
        std::string text = "Masked message";
        auto frame = WebSocketFrame::createTextFrame(text, true);
        
        // 应用掩码
        frame.applyMask();
        
        // 序列化
        auto data = frame.serialize();
        
        // 反序列化（自动移除掩码）
        size_t consumed = 0;
        auto parsed_frame = WebSocketFrame::deserialize(data, consumed);
        
        if (parsed_frame) {
            std::string parsed_text = parsed_frame->getTextPayload();
            if (parsed_text == text) {
                std::cout << "  ✅ 掩码处理测试通过" << std::endl;
            } else {
                std::cout << "  ❌ 掩码处理测试失败" << std::endl;
            }
        }
    }
    
    std::cout << "\n========== 帧协议测试完成 ==========" << std::endl;
}

/**
 * @brief 主函数
 */
int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::getInstance().setLevel(LogLevel::DEBUG);
    
    if (argc > 1 && std::string(argv[1]) == "frame") {
        // 测试帧协议
        testWebSocketFrame();
    } else {
        // 测试服务器
        testWebSocketNativeServer();
    }
    
    return 0;
}

/**
 * 编译命令：
 * g++ -std=c++17 -o test_websocket_native test_websocket_native.cpp \
 *     -I../../../include \
 *     -L../../../lib \
 *     -lwebsocket_native -lcommon_logger_lib -lnetwork_lib -lssl -lcrypto \
 *     -pthread
 * 
 * 运行测试：
 * ./test_websocket_native          # 测试服务器
 * ./test_websocket_native frame    # 测试帧协议
 * 
 * 使用 websocat 连接测试：
 * websocat ws://127.0.0.1:9091
 */
