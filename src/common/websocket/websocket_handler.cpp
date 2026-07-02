/**
 * @file websocket_handler.cpp
 * @brief WebSocket握手和消息处理器实现
 * @date 2025-12-11
 */

#include "common/websocket/websocket_handler.h"
#include "common/websocket/merchant_connection_manager.h"
#include "common/websocket/delivery_task_router.h"
#include "common/database/mysql_pool.h"  // ⭐ 新增：包含MySQLConnectionGuard
#include "common/config/config_manager.h"  // ⭐⭐⭐ 新增：Token过期配置检查 (2025-12-25)
#include <jwt-cpp/jwt.h>
#include <chrono>

namespace common {
namespace websocket {

using namespace common::logger;

// ==================== 构造/析构函数 ====================

WebSocketHandler::WebSocketHandler(
    std::shared_ptr<common::database::MySQLPool> db_pool,
    std::shared_ptr<common::network::EventLoop> event_loop,
    const std::string& jwt_secret
)
    : db_pool_(db_pool)
    , event_loop_(event_loop)
    , jwt_secret_(jwt_secret)
    , connection_manager_(MerchantConnectionManager::getInstance())
    , task_router_(std::make_shared<DeliveryTaskRouter>(db_pool, event_loop))
{
    LOG_INFO("WebSocketHandler初始化完成");
    
    // 配置WebSocket服务器
    ws_server_.init_asio();
    ws_server_.set_reuse_addr(true);
    
    // 注册事件回调
    ws_server_.set_open_handler([this](WebSocketConnectionHdl hdl) {
        onOpen(hdl);
    });
    
    ws_server_.set_close_handler([this](WebSocketConnectionHdl hdl) {
        onClose(hdl);
    });
    
    ws_server_.set_message_handler([this](WebSocketConnectionHdl hdl, WebSocketMessagePtr msg) {
        onMessage(hdl, msg);
    });
    
    ws_server_.set_validate_handler([this](WebSocketConnectionHdl hdl) {
        return onValidate(hdl);
    });
    
    // 配置日志（使用我们自己的Logger）
    ws_server_.clear_access_channels(websocketpp::log::alevel::all);
    ws_server_.clear_error_channels(websocketpp::log::elevel::all);
}

WebSocketHandler::~WebSocketHandler() {
    stop();
    LOG_INFO("WebSocketHandler已销毁");
}

// ==================== HTTP路由处理器 ====================

std::function<void(const common::http::HttpRequest&, common::http::HttpResponse&)> 
WebSocketHandler::getUpgradeHandler() {
    return [this](const common::http::HttpRequest& req, common::http::HttpResponse& res) {
        // 检查是否是WebSocket升级请求
        if (req.getHeader("Upgrade") != "websocket" || 
            req.getHeader("Connection").find("Upgrade") == std::string::npos) {
            res.badRequest("不是有效的WebSocket升级请求");
            return;
        }
        
        // 检查WebSocket版本
        std::string ws_version = req.getHeader("Sec-WebSocket-Version");
        if (ws_version != "13") {
            res.badRequest("不支持的WebSocket版本：" + ws_version);
            return;
        }
        
        // 获取JWT Token（从query参数或Header）
        std::string token = req.getParam("token");
        if (token.empty()) {
            token = req.getHeader("Authorization");
            // 移除 "Bearer " 前缀
            if (token.find("Bearer ") == 0) {
                token = token.substr(7);
            }
        }
        
        if (token.empty()) {
            res.unauthorized("缺少认证Token");
            return;
        }
        
        // 验证JWT Token
        auto auth_result = validateJwtToken(token);
        if (!auth_result.success) {
            res.unauthorized(auth_result.error_message);
            return;
        }
        
        // ⭐ 这里HTTP升级到WebSocket的具体实现需要与HttpServer深度集成
        // 由于HttpServer基于自定义的网络框架，我们使用独立端口模式
        nlohmann::json response_data = {
            {"success", true},
            {"message", "请使用WebSocket客户端连接到 ws://host:port/ws/merchant"},
            {"merchant_code", auth_result.merchant_code},
            {"merchant_name", auth_result.merchant_name},
            {"ws_url", "ws://localhost:9091/ws/merchant"}  // 独立WebSocket端口
        };
        
        res.ok(response_data.dump());
    };
}

// ==================== WebSocket服务器控制 ====================

bool WebSocketHandler::start(uint16_t port) {
    if (running_.exchange(true)) {
        LOG_WARNING("WebSocket服务器已在运行中");
        return false;
    }
    
    try {
        // 监听端口
        ws_server_.listen(port);
        ws_server_.start_accept();
        
        // 启动独立线程运行WebSocket服务器
        ws_thread_ = std::make_shared<std::thread>([this, port]() {
            LOG_INFO("WebSocket服务器启动，监听端口：" + std::to_string(port));
            ws_server_.run();
            LOG_INFO("WebSocket服务器已停止");
        });
        
        // 启动心跳检测
        connection_manager_.startHeartbeatChecker(event_loop_);
        
        // 启动超时检测
        task_router_->startTimeoutChecker();
        
        LOG_INFO("✅ WebSocket处理器启动成功");
        return true;
        
    } catch (const std::exception& e) {
        running_.store(false);
        LOG_ERROR("WebSocket服务器启动失败：" + std::string(e.what()));
        return false;
    }
}

void WebSocketHandler::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    try {
        // 停止定时任务
        connection_manager_.stopHeartbeatChecker();
        task_router_->stopTimeoutChecker();
        
        // 停止WebSocket服务器
        ws_server_.stop_listening();
        ws_server_.stop();
        
        // 等待线程结束
        if (ws_thread_ && ws_thread_->joinable()) {
            ws_thread_->join();
        }
        
        LOG_INFO("WebSocket处理器已停止");
        
    } catch (const std::exception& e) {
        LOG_ERROR("停止WebSocket服务器时出错：" + std::string(e.what()));
    }
}

// ==================== JWT认证 ====================

WebSocketHandshakeResult WebSocketHandler::validateJwtToken(const std::string& token) {
    try {
        // 解码并验证JWT Token
        auto decoded = jwt::decode(token);
        
        // 验证签名
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwt_secret_})
            .with_issuer("microservice-payment-platform");  // 固定签发者
        
        verifier.verify(decoded);

        // ⭐⭐⭐ 从配置文件读取是否启用Token过期检查 (2025-12-31)
        auto& config_mgr = common::config::ConfigManager::getInstance();
        bool token_expiry_enabled = config_mgr.get<bool>("server.websocket.token_expiry_enabled", false);
        int64_t token_expiry_seconds = config_mgr.get<int64_t>("server.websocket.token_expiry_seconds", 86400);
        int64_t token_clock_skew_seconds = config_mgr.get<int64_t>("server.websocket.token_clock_skew_seconds", 300);

        // 检查过期时间（只有在启用过期检查时才验证）
        if (token_expiry_enabled && decoded.has_payload_claim("timestamp")) {
            try {
                // timestamp 是字符串类型，需要转换
                std::string timestamp_str = decoded.get_payload_claim("timestamp").as_string();
                int64_t timestamp = std::stoll(timestamp_str);
                int64_t now = std::time(nullptr);
                int64_t time_diff = std::abs(now - timestamp);

                // 检查是否过期
                if (time_diff > token_expiry_seconds) {
                    return WebSocketHandshakeResult::makeError("Token时间戳已过期");
                }

                // 检查时钟偏差（防止来自未来的时间戳）
                if (timestamp > now + token_clock_skew_seconds) {
                    return WebSocketHandshakeResult::makeError("Token时间戳异常（来自未来）");
                }
            } catch (const std::exception& e) {
                return WebSocketHandshakeResult::makeError("Token时间戳解析失败");
            }
        } else if (token_expiry_enabled && decoded.has_expires_at()) {
            // 兼容标准的 JWT exp 字段
            auto exp = decoded.get_expires_at();
            auto now = std::chrono::system_clock::now();
            if (exp < now) {
                return WebSocketHandshakeResult::makeError("Token已过期");
            }
        }

        // 提取商户信息
        if (!decoded.has_payload_claim("merchant_code")) {
            return WebSocketHandshakeResult::makeError("Token缺少merchant_code字段");
        }
        
        std::string merchant_code = decoded.get_payload_claim("merchant_code").as_string();
        std::string merchant_name = decoded.has_payload_claim("merchant_name") 
            ? decoded.get_payload_claim("merchant_name").as_string() 
            : "";
        
        // 从数据库验证商户
        return validateMerchantFromDB(merchant_code, token);
        
    } catch (const jwt::error::token_verification_exception& e) {
        return WebSocketHandshakeResult::makeError("Token验证失败：" + std::string(e.what()));
    } catch (const std::exception& e) {
        return WebSocketHandshakeResult::makeError("Token解析错误：" + std::string(e.what()));
    }
}

WebSocketHandshakeResult WebSocketHandler::validateMerchantFromDB(
    const std::string& merchant_code,
    const std::string& token
) {
    (void)token;  // 未使用参数，保留以便未来扩展
    
    // ⭐⭐⭐ 修复连接泄漏：使用MySQLConnectionGuard自动归还连接
    common::database::MySQLConnectionGuard conn_guard(*db_pool_);
    if (!conn_guard.isValid()) {
        return WebSocketHandshakeResult::makeError("数据库连接失败");
    }
    
    try {
        // 查询商户信息
        std::string sql = 
            "SELECT merchant_name, status, websocket_enabled "
            "FROM merchants "
            "WHERE merchant_code = ? AND deleted_at IS NULL";
        
        auto stmt = conn_guard->prepareStatement(sql);
        stmt->setString(1, merchant_code);
        auto rs = stmt->executeQuery();
        
        if (!rs->next()) {
            return WebSocketHandshakeResult::makeError("商户不存在");
        }
        
        std::string merchant_name = rs->getString("merchant_name");
        int status = rs->getInt("status");
        bool websocket_enabled = rs->getBoolean("websocket_enabled");
        
        if (status != 1) {
            return WebSocketHandshakeResult::makeError("商户已禁用");
        }
        
        if (!websocket_enabled) {
            return WebSocketHandshakeResult::makeError("商户未启用WebSocket功能");
        }
        
        return WebSocketHandshakeResult::makeSuccess(merchant_code, merchant_name);
        
    } catch (const std::exception& e) {
        LOG_ERROR("验证商户失败：" + std::string(e.what()));
        return WebSocketHandshakeResult::makeError("验证商户失败");
    }
}

// ==================== WebSocketpp事件回调 ====================

void WebSocketHandler::onOpen(WebSocketConnectionHdl hdl) {
    try {
        auto conn = ws_server_.get_con_from_hdl(hdl);
        std::string uri = conn->get_uri()->str();
        
        LOG_INFO("WebSocket连接打开：" + uri);
        
        // 发送认证请求
        nlohmann::json auth_required = {
            {"type", "auth_required"},
            {"timestamp", std::time(nullptr)},
            {"message", "请发送认证消息"}
        };
        
        sendJsonMessage(hdl, auth_required);
        
    } catch (const std::exception& e) {
        LOG_ERROR("onOpen处理失败：" + std::string(e.what()));
    }
}

void WebSocketHandler::onClose(WebSocketConnectionHdl hdl) {
    try {
        std::lock_guard<std::mutex> lock(hdl_map_mutex_);
        
        // 查找商户代码
        auto it = hdl_to_merchant_.find(hdl.lock().get());
        if (it != hdl_to_merchant_.end()) {
            std::string merchant_code = it->second;
            
            LOG_INFO("商户连接关闭：" + merchant_code);
            
            // 从连接管理器中移除
            connection_manager_.removeConnection(merchant_code);
            
            // 从映射中移除
            hdl_to_merchant_.erase(it);
        } else {
            LOG_INFO("未认证的WebSocket连接关闭");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("onClose处理失败：" + std::string(e.what()));
    }
}

void WebSocketHandler::onMessage(WebSocketConnectionHdl hdl, WebSocketMessagePtr msg) {
    try {
        std::string payload = msg->get_payload();
        auto json_data = nlohmann::json::parse(payload);
        
        std::string msg_type = json_data.value("type", "");
        
        if (msg_type == "auth") {
            handleAuthMessage(hdl, json_data);
        } else if (msg_type == "pong") {
            handlePongMessage(hdl, json_data);
        } else if (msg_type == "delivery_result") {
            handleDeliveryResultMessage(hdl, json_data);
        } else {
            LOG_WARNING("未知消息类型：" + msg_type);
        }
        
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("解析WebSocket消息失败：" + std::string(e.what()));
        closeWithError(hdl, "消息格式错误");
    } catch (const std::exception& e) {
        LOG_ERROR("处理WebSocket消息失败：" + std::string(e.what()));
    }
}

bool WebSocketHandler::onValidate(WebSocketConnectionHdl hdl) {
    (void)hdl;  // 未使用参数
    // 始终返回true，认证在消息层处理
    return true;
}

// ==================== 消息处理 ====================

void WebSocketHandler::handleAuthMessage(
    WebSocketConnectionHdl hdl,
    const nlohmann::json& msg_data
) {
    std::string token = msg_data.value("token", "");
    if (token.empty()) {
        closeWithError(hdl, "缺少认证Token");
        return;
    }
    
    // 验证Token
    auto auth_result = validateJwtToken(token);
    if (!auth_result.success) {
        closeWithError(hdl, auth_result.error_message);
        return;
    }
    
    std::string merchant_code = auth_result.merchant_code;
    std::string merchant_name = auth_result.merchant_name;
    
    // 创建WebSocketConnection包装（使用回调函数）
    auto ws_conn = std::make_shared<WebSocketConnection>(
        [this, hdl](const WebSocketMessage& msg) -> bool {
            try {
                sendJsonMessage(hdl, nlohmann::json::parse(msg.toJson()));
                return true;
            } catch (const std::exception& e) {
                LOG_ERROR("发送消息失败: " + std::string(e.what()));
                return false;
            }
        },
        merchant_code
    );
    
    // 认证成功
    ws_conn->markAuthenticated(merchant_code, merchant_name);
    
    // 添加到连接管理器
    connection_manager_.registerConnection(merchant_code, ws_conn);
    
    // 记录映射关系
    {
        std::lock_guard<std::mutex> lock(hdl_map_mutex_);
        hdl_to_merchant_[hdl.lock().get()] = merchant_code;
    }
    
    // 发送认证成功消息
    nlohmann::json auth_success = {
        {"type", "auth_success"},
        {"timestamp", std::time(nullptr)},
        {"merchant_code", merchant_code},
        {"merchant_name", merchant_name},
        {"message", "认证成功"}
    };
    
    sendJsonMessage(hdl, auth_success);
    
    LOG_INFO("商户认证成功：" + merchant_code + " (" + merchant_name + ")");
}

void WebSocketHandler::handlePongMessage(
    WebSocketConnectionHdl hdl,
    const nlohmann::json& msg_data
) {
    (void)msg_data;  // 未使用参数
    
    std::lock_guard<std::mutex> lock(hdl_map_mutex_);
    
    auto it = hdl_to_merchant_.find(hdl.lock().get());
    if (it != hdl_to_merchant_.end()) {
        auto conn = connection_manager_.getConnection(it->second);
        if (conn) {
            conn->updateLastPongTime();
            LOG_DEBUG("收到商户Pong：" + it->second);
        }
    }
}

void WebSocketHandler::handleDeliveryResultMessage(
    WebSocketConnectionHdl hdl,
    const nlohmann::json& msg_data
) {
    (void)hdl;  // 未使用参数
    
    std::string task_id = msg_data.value("task_id", "");
    if (task_id.empty()) {
        LOG_ERROR("发货结果缺少task_id");
        return;
    }
    
    // 委托给DeliveryTaskRouter处理
    task_router_->handleDeliveryResult(task_id, msg_data.dump());
}

// ==================== 发货任务API ====================

bool WebSocketHandler::sendDeliveryTask(
    const std::string& merchant_code,
    const nlohmann::json& task_data,
    DeliveryTaskRouter::DeliveryCallback callback
) {
    // 从 task_data 中提取参数
    std::string order_id = task_data.value("order_id", "");
    int64_t game_id = task_data.value("game_id", 0);
    int64_t server_id = task_data.value("server_id", 0);
    std::string player_account = task_data.value("player_account", "");
    std::string reward_items = task_data.value("reward_items", "");
    
    // 调用 dispatchDeliveryTask
    std::string task_id = task_router_->dispatchDeliveryTask(
        order_id,
        merchant_code,
        game_id,
        server_id,
        player_account,
        reward_items,
        callback
    );
    
    return !task_id.empty();
}

bool WebSocketHandler::isMerchantOnline(const std::string& merchant_code) {
    auto conn = connection_manager_.getConnection(merchant_code);
    return conn && conn->isAuthenticated();
}

// ==================== 辅助方法 ====================

void WebSocketHandler::sendJsonMessage(
    WebSocketConnectionHdl hdl,
    const nlohmann::json& json_data
) {
    try {
        std::string payload = json_data.dump();
        ws_server_.send(hdl, payload, websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        LOG_ERROR("发送WebSocket消息失败：" + std::string(e.what()));
    }
}

void WebSocketHandler::closeWithError(
    WebSocketConnectionHdl hdl,
    const std::string& reason
) {
    try {
        nlohmann::json error_msg = {
            {"type", "auth_failed"},
            {"timestamp", std::time(nullptr)},
            {"reason", reason}
        };
        
        sendJsonMessage(hdl, error_msg);
        
        ws_server_.close(hdl, websocketpp::close::status::policy_violation, reason);
        
        LOG_WARNING("关闭WebSocket连接：" + reason);
        
    } catch (const std::exception& e) {
        LOG_ERROR("关闭连接失败：" + std::string(e.what()));
    }
}

// ⭐⭐⭐ Phase 3: 心跳检测和超时管理 (2025-12-11)

int WebSocketHandler::sendHeartbeatPing() {
    if (!running_.load()) {
        LOG_WARNING("⚠️ WebSocket服务器未运行，跳过心跳检测");
        return 0;
    }
    
    try {
        // 获取所有在线商户
        auto online_merchants = connection_manager_.getOnlineMerchants();
        
        if (online_merchants.empty()) {
            LOG_DEBUG("🔍 当前无在线商户，跳过心跳检测");
            return 0;
        }
        
        int success_count = 0;
        
        // 向所有在线商户发送Ping消息
        for (const auto& merchant_code : online_merchants) {
            auto connection = connection_manager_.getConnection(merchant_code);
            if (connection && connection->isAuthenticated()) {
                if (connection->sendMessage(WebSocketMessage::createPing())) {
                    success_count++;
                }
            }
        }
        
        LOG_DEBUG("💓 WebSocket心跳检测完成: 在线商户" + std::to_string(online_merchants.size()) + 
                  ", 成功发送" + std::to_string(success_count));
        
        return success_count;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ WebSocket心跳检测失败: " + std::string(e.what()));
        return 0;
    }
}

int WebSocketHandler::cleanupTimeoutConnections(int timeout_seconds) {
    if (!running_.load()) {
        LOG_WARNING("⚠️ WebSocket服务器未运行，跳过超时检测");
        return 0;
    }
    
    try {
        // 获取所有在线商户
        auto online_merchants = connection_manager_.getOnlineMerchants();
        
        if (online_merchants.empty()) {
            LOG_DEBUG("🔍 当前无在线商户，跳过超时检测");
            return 0;
        }
        
        int cleaned_count = 0;
        auto current_time = std::time(nullptr);
        
        // 检查每个连接的活跃时间
        for (const auto& merchant_code : online_merchants) {
            auto connection = connection_manager_.getConnection(merchant_code);
            if (connection) {
                auto last_activity = connection->getLastActivityTime();
                int idle_seconds = current_time - last_activity;
                
                // 超过超时时间，关闭连接
                if (idle_seconds > timeout_seconds) {
                    LOG_WARNING("⚠️ 商户 " + merchant_code + " 连接超时 (" + 
                                std::to_string(idle_seconds) + "秒)，关闭连接");
                    
                    // 关闭连接
                    connection->close("连接超时");
                    
                    // 从连接管理器移除
                    connection_manager_.removeConnection(merchant_code);
                    
                    cleaned_count++;
                }
            }
        }
        
        if (cleaned_count > 0) {
            LOG_INFO("✨ WebSocket超时检测完成: 清理了 " + std::to_string(cleaned_count) + " 个超时连接");
        } else {
            LOG_DEBUG("🔍 WebSocket超时检测完成: 无超时连接");
        }
        
        return cleaned_count;
        
    } catch (const std::exception& e) {
        LOG_ERROR("❌ WebSocket超时检测失败: " + std::string(e.what()));
        return 0;
    }
}

} // namespace websocket
} // namespace common
