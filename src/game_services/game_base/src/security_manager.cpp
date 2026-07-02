/**
 * @file security_manager.cpp
 * @brief 游戏安全管理器实现
 */

#include "security_manager.h"
#include "common/logger/logger.h"
#include "common/monitoring/performance_monitor.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <thread>

namespace game_services {
    namespace game_base {

        SecurityManager::SecurityManager(const SecurityConfig& config)
            : config_(config) {
            LOG_INFO("🛡️ SecurityManager initialized with configuration:");
            LOG_INFO("  - Max messages per minute: " + std::to_string(config_.max_messages_per_minute));
            LOG_INFO("  - Max messages per second: " + std::to_string(config_.max_messages_per_second));
            LOG_INFO("  - Max connections per IP: " + std::to_string(config_.max_connections_per_ip));
            LOG_INFO("  - Message size limit: " + std::to_string(config_.max_message_size) + " bytes");
            LOG_INFO("  - Authentication required: " + std::string(config_.require_authentication ? "Yes" : "No"));
        }

        bool SecurityManager::validateConnection(const std::string& client_ip, const nlohmann::json& additional_info) {
            auto start_time = std::chrono::high_resolution_clock::now();
            total_connections_++;

            try {
                // 1. 检查IP白名单（如果启用）
                if (config_.enable_ip_whitelist) {
                    std::shared_lock<std::shared_mutex> lock(security_mutex_);
                    if (whitelist_.find(client_ip) == whitelist_.end()) {
                        recordSecurityEvent(client_ip, SecurityEventType::UNAUTHORIZED_ACCESS, "IP not in whitelist");
                        rejected_connections_++;
                        return false;
                    }
                }

                // 2. 检查IP黑名单
                if (isBlacklisted(client_ip)) {
                    recordSecurityEvent(client_ip, SecurityEventType::BLACKLISTED_IP, "Connection from blacklisted IP");
                    rejected_connections_++;
                    return false;
                }

                // 3. 检查连接数限制
                ClientSecurityState* state = getOrCreateClientState(client_ip);
                if (state->active_connections.load() >= config_.max_connections_per_ip) {
                    recordSecurityEvent(client_ip, SecurityEventType::CONNECTION_LIMIT_EXCEEDED, 
                                      "Too many connections: " + std::to_string(state->active_connections.load()));
                    rejected_connections_++;
                    return false;
                }

                // 4. 更新客户端状态
                state->active_connections++;
                state->last_activity = std::chrono::system_clock::now();

                // 5. 记录性能指标
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                PERF_HISTOGRAM("security_connection_validation_duration_us", static_cast<double>(duration.count()));
                PERF_GAUGE("security_active_connections", static_cast<double>(total_connections_.load() - rejected_connections_.load()));

                LOG_DEBUG("✅ Connection validated for IP: " + client_ip);
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Security validation error for " + client_ip + ": " + std::string(e.what()));
                PERF_ERROR("security_validation", "validation_error", "security_manager");
                rejected_connections_++;
                return false;
            }
        }

        bool SecurityManager::validateMessage(const std::string& client_ip, const std::string& message) {
            total_messages_++;

            try {
                // 1. 检查消息大小
                if (message.size() > config_.max_message_size) {
                    recordSecurityEvent(client_ip, SecurityEventType::MESSAGE_SIZE_EXCEEDED, 
                                      "Message too large: " + std::to_string(message.size()) + " bytes");
                    rejected_messages_++;
                    return false;
                }

                // 2. 检查速率限制
                if (!checkRateLimit(client_ip)) {
                    rejected_messages_++;
                    return false;
                }

                // 3. 基本输入验证
                if (!isValidMessage(message)) {
                    recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, "Invalid message format");
                    rejected_messages_++;
                    return false;
                }

                // 4. 尝试解析为JSON进行进一步验证
                try {
                    nlohmann::json json_message = nlohmann::json::parse(message);
                    return validateJsonMessage(client_ip, json_message);
                } catch (const nlohmann::json::parse_error&) {
                    // 非JSON消息，基本验证通过即可
                    return true;
                }

            } catch (const std::exception& e) {
                LOG_ERROR("Message validation error for " + client_ip + ": " + std::string(e.what()));
                PERF_ERROR("security_validation", "message_validation_error", "security_manager");
                rejected_messages_++;
                return false;
            }
        }

        bool SecurityManager::validateJsonMessage(const std::string& client_ip, const nlohmann::json& json_message) {
            try {
                // 1. 检查JSON深度
                if (!checkJsonDepth(json_message)) {
                    recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, "JSON depth exceeded");
                    return false;
                }

                // 2. 验证消息结构
                if (!json_message.is_object()) {
                    recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, "JSON message must be an object");
                    return false;
                }

                // 3. 检查必需字段
                if (!json_message.contains("type")) {
                    recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, "Missing 'type' field");
                    return false;
                }

                // 4. 验证特定消息类型的格式
                std::string message_type = json_message["type"].get<std::string>();
                
                if (message_type == "join_room" || message_type == "create_room") {
                    if (!json_message.contains("room_name") || !json_message.contains("player_name")) {
                        recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, 
                                          "Missing required fields for " + message_type);
                        return false;
                    }

                    // 验证房间名和玩家名
                    std::string room_name = json_message["room_name"].get<std::string>();
                    std::string player_name = json_message["player_name"].get<std::string>();
                    
                    if (!isValidRoomName(room_name) || !isValidPlayerName(player_name)) {
                        recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, "Invalid room or player name");
                        return false;
                    }
                }

                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("JSON message validation error for " + client_ip + ": " + std::string(e.what()));
                recordSecurityEvent(client_ip, SecurityEventType::INVALID_INPUT, "JSON validation error");
                return false;
            }
        }

        bool SecurityManager::checkRateLimit(const std::string& client_ip) {
            ClientSecurityState* state = getOrCreateClientState(client_ip);
            updateRateLimitWindows(state);

            bool within_limits = true;

            // 检查每秒消息限制
            if (state->messages_this_second.load() >= config_.max_messages_per_second) {
                recordSecurityEvent(client_ip, SecurityEventType::RATE_LIMIT_EXCEEDED, 
                                  "Per-second rate limit exceeded");
                within_limits = false;
            }

            // 检查每分钟消息限制
            if (state->messages_this_minute.load() >= config_.max_messages_per_minute) {
                recordSecurityEvent(client_ip, SecurityEventType::RATE_LIMIT_EXCEEDED, 
                                  "Per-minute rate limit exceeded");
                within_limits = false;
            }

            if (within_limits) {
                state->messages_this_second++;
                state->messages_this_minute++;
                state->last_activity = std::chrono::system_clock::now();
            }

            return within_limits;
        }

        void SecurityManager::recordSecurityEvent(const std::string& client_ip, 
                                                SecurityEventType event_type,
                                                const std::string& details) {
            security_events_++;

            try {
                ClientSecurityState* state = getOrCreateClientState(client_ip);
                state->violation_count++;
                
                // 限制最近违规记录数量
                if (state->recent_violations.size() >= 10) {
                    state->recent_violations.erase(state->recent_violations.begin());
                }
                state->recent_violations.push_back(event_type);

                // 检查是否需要加入黑名单
                if (shouldBlacklist(state)) {
                    addToBlacklist(client_ip, config_.blacklist_duration_minutes, "Automatic");
                }

                // 记录日志
                std::string event_name;
                switch (event_type) {
                    case SecurityEventType::RATE_LIMIT_EXCEEDED: event_name = "RATE_LIMIT_EXCEEDED"; break;
                    case SecurityEventType::INVALID_INPUT: event_name = "INVALID_INPUT"; break;
                    case SecurityEventType::SUSPICIOUS_BEHAVIOR: event_name = "SUSPICIOUS_BEHAVIOR"; break;
                    case SecurityEventType::UNAUTHORIZED_ACCESS: event_name = "UNAUTHORIZED_ACCESS"; break;
                    case SecurityEventType::BLACKLISTED_IP: event_name = "BLACKLISTED_IP"; break;
                    case SecurityEventType::TOKEN_VALIDATION_FAILED: event_name = "TOKEN_VALIDATION_FAILED"; break;
                    case SecurityEventType::MESSAGE_SIZE_EXCEEDED: event_name = "MESSAGE_SIZE_EXCEEDED"; break;
                    case SecurityEventType::CONNECTION_LIMIT_EXCEEDED: event_name = "CONNECTION_LIMIT_EXCEEDED"; break;
                    default: event_name = "UNKNOWN"; break;
                }

                LOG_WARNING("🚨 Security Event [" + event_name + "] from " + client_ip + 
                           " (violations: " + std::to_string(state->violation_count.load()) + "): " + details);

                // 记录性能指标
                PERF_COUNTER("security_events_total", 1.0, {{"event_type", event_name}, {"client_ip", client_ip}});

            } catch (const std::exception& e) {
                LOG_ERROR("Error recording security event: " + std::string(e.what()));
            }
        }

        bool SecurityManager::isBlacklisted(const std::string& client_ip) {
            std::shared_lock<std::shared_mutex> lock(security_mutex_);

            auto it = client_states_.find(client_ip);
            if (it != client_states_.end() && it->second->is_blacklisted) {
                // 检查黑名单是否已过期
                if (std::chrono::system_clock::now() > it->second->blacklist_until) {
                    lock.unlock();
                    removeFromBlacklist(client_ip); // 自动移除过期的黑名单项
                    return false;
                }
                return true;
            }

            return blacklist_.find(client_ip) != blacklist_.end();
        }

        void SecurityManager::addToBlacklist(const std::string& client_ip, int duration_minutes, const std::string& reason) {
            std::unique_lock<std::shared_mutex> lock(security_mutex_);

            blacklist_.insert(client_ip);
            
            ClientSecurityState* state = getOrCreateClientState(client_ip);
            state->is_blacklisted = true;
            state->blacklist_until = std::chrono::system_clock::now() + std::chrono::minutes(duration_minutes);

            LOG_WARNING("🚫 IP " + client_ip + " added to blacklist for " + std::to_string(duration_minutes) + 
                       " minutes. Reason: " + reason);

            PERF_COUNTER("security_blacklist_additions_total", 1.0, {{"reason", reason}});
        }

        void SecurityManager::removeFromBlacklist(const std::string& client_ip) {
            std::unique_lock<std::shared_mutex> lock(security_mutex_);

            blacklist_.erase(client_ip);
            
            auto it = client_states_.find(client_ip);
            if (it != client_states_.end()) {
                it->second->is_blacklisted = false;
                it->second->blacklist_until = std::chrono::system_clock::time_point{};
            }

            LOG_INFO("✅ IP " + client_ip + " removed from blacklist");
            PERF_COUNTER("security_blacklist_removals_total", 1.0);
        }

        void SecurityManager::addToWhitelist(const std::string& client_ip) {
            std::unique_lock<std::shared_mutex> lock(security_mutex_);
            whitelist_.insert(client_ip);
            LOG_INFO("✅ IP " + client_ip + " added to whitelist");
            PERF_COUNTER("security_whitelist_additions_total", 1.0);
        }

        void SecurityManager::removeFromWhitelist(const std::string& client_ip) {
            std::unique_lock<std::shared_mutex> lock(security_mutex_);
            whitelist_.erase(client_ip);
            LOG_INFO("❌ IP " + client_ip + " removed from whitelist");
            PERF_COUNTER("security_whitelist_removals_total", 1.0);
        }

        bool SecurityManager::checkPermission(const std::string& player_id,
                                             const std::string& operation,
                                             const std::string& resource) {
            // 基础权限检查实现
            // 在实际应用中，这里应该与认证系统集成
            
            if (!config_.require_authentication) {
                return true; // 如果不需要认证，允许所有操作
            }

            // 简单的基于操作类型的权限检查
            if (operation == "create_room" || operation == "join_room" || operation == "leave_room") {
                // 基本游戏操作，已认证用户都可以执行
                return !player_id.empty();
            } else if (operation == "admin_kick" || operation == "admin_ban") {
                // 管理员操作，需要特殊权限
                return false; // 暂不实现管理员权限系统
            }

            return true; // 默认允许
        }

        nlohmann::json SecurityManager::getSecurityStats() const {
            std::shared_lock<std::shared_mutex> lock(security_mutex_);

            nlohmann::json stats;
            stats["total_connections"] = total_connections_.load();
            stats["rejected_connections"] = rejected_connections_.load();
            stats["total_messages"] = total_messages_.load();
            stats["rejected_messages"] = rejected_messages_.load();
            stats["security_events"] = security_events_.load();
            stats["blacklisted_ips"] = blacklist_.size();
            stats["whitelisted_ips"] = whitelist_.size();
            stats["tracked_clients"] = client_states_.size();
            
            // 计算统计比率
            uint64_t total_conn = total_connections_.load();
            uint64_t total_msg = total_messages_.load();
            
            stats["connection_rejection_rate"] = (total_conn > 0) ? 
                static_cast<double>(rejected_connections_.load()) / total_conn : 0.0;
            stats["message_rejection_rate"] = (total_msg > 0) ? 
                static_cast<double>(rejected_messages_.load()) / total_msg : 0.0;

            // 配置信息
            stats["config"] = config_.toJson();
            
            stats["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            return stats;
        }

        nlohmann::json SecurityManager::getClientSecurityState(const std::string& client_ip) const {
            std::shared_lock<std::shared_mutex> lock(security_mutex_);

            auto it = client_states_.find(client_ip);
            if (it == client_states_.end()) {
                return nlohmann::json{{"error", "Client not found"}};
            }

            const ClientSecurityState* state = it->second.get();
            
            nlohmann::json client_info;
            client_info["client_ip"] = state->client_ip;
            client_info["first_seen"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                state->first_seen.time_since_epoch()).count();
            client_info["last_activity"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                state->last_activity.time_since_epoch()).count();
            client_info["messages_this_minute"] = state->messages_this_minute.load();
            client_info["messages_this_second"] = state->messages_this_second.load();
            client_info["violation_count"] = state->violation_count.load();
            client_info["active_connections"] = state->active_connections.load();
            client_info["is_blacklisted"] = state->is_blacklisted;
            
            if (state->is_blacklisted) {
                client_info["blacklist_until"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    state->blacklist_until.time_since_epoch()).count();
            }

            // 最近违规记录
            nlohmann::json violations = nlohmann::json::array();
            for (auto violation : state->recent_violations) {
                violations.push_back(static_cast<int>(violation));
            }
            client_info["recent_violations"] = violations;

            return client_info;
        }

        void SecurityManager::updateConfig(const SecurityConfig& new_config) {
            config_ = new_config;
            LOG_INFO("🔧 Security configuration updated");
            PERF_COUNTER("security_config_updates_total", 1.0);
        }

        void SecurityManager::cleanup() {
            std::unique_lock<std::shared_mutex> lock(security_mutex_);

            auto now = std::chrono::system_clock::now();
            auto cutoff_time = now - std::chrono::hours(24); // 清理24小时前的数据

            // 清理过期的客户端状态
            for (auto it = client_states_.begin(); it != client_states_.end();) {
                if (it->second->last_activity < cutoff_time && 
                    it->second->active_connections.load() == 0 && 
                    !it->second->is_blacklisted) {
                    it = client_states_.erase(it);
                } else {
                    ++it;
                }
            }

            // 清理过期的黑名单项
            cleanupExpiredBlacklist();

            LOG_INFO("🧹 Security manager cleanup completed, tracking " + 
                     std::to_string(client_states_.size()) + " clients");
        }

        void SecurityManager::registerConnection(const std::string& client_ip, const std::string& player_id) {
            ClientSecurityState* state = getOrCreateClientState(client_ip);
            state->last_activity = std::chrono::system_clock::now();
            
            LOG_DEBUG("📡 Connection registered: " + client_ip + " → " + player_id);
        }

        void SecurityManager::unregisterConnection(const std::string& client_ip, const std::string& player_id) {
            std::shared_lock<std::shared_mutex> lock(security_mutex_);
            
            auto it = client_states_.find(client_ip);
            if (it != client_states_.end()) {
                if (it->second->active_connections.load() > 0) {
                    it->second->active_connections--;
                }
                it->second->last_activity = std::chrono::system_clock::now();
            }
            
            LOG_DEBUG("📡 Connection unregistered: " + client_ip + " ← " + player_id);
        }

        ClientSecurityState* SecurityManager::getOrCreateClientState(const std::string& client_ip) {
            std::unique_lock<std::shared_mutex> lock(security_mutex_);

            auto it = client_states_.find(client_ip);
            if (it != client_states_.end()) {
                return it->second.get();
            }

            // 创建新的客户端状态
            auto state = std::make_unique<ClientSecurityState>();
            state->client_ip = client_ip;
            
            ClientSecurityState* state_ptr = state.get();
            client_states_[client_ip] = std::move(state);
            
            return state_ptr;
        }

        bool SecurityManager::checkJsonDepth(const nlohmann::json& json, int current_depth) const {
            if (current_depth > static_cast<int>(config_.max_json_depth)) {
                return false;
            }

            if (json.is_object()) {
                for (const auto& [key, value] : json.items()) {
                    if (!checkJsonDepth(value, current_depth + 1)) {
                        return false;
                    }
                }
            } else if (json.is_array()) {
                for (const auto& item : json) {
                    if (!checkJsonDepth(item, current_depth + 1)) {
                        return false;
                    }
                }
            }

            return true;
        }

        void SecurityManager::updateRateLimitWindows(ClientSecurityState* state) {
            auto now = std::chrono::system_clock::now();

            // 更新每秒窗口
            if (now - state->second_window_start >= std::chrono::seconds(1)) {
                state->messages_this_second.store(0);
                state->second_window_start = now;
            }

            // 更新每分钟窗口
            if (now - state->minute_window_start >= std::chrono::minutes(1)) {
                state->messages_this_minute.store(0);
                state->minute_window_start = now;
            }
        }

        bool SecurityManager::shouldBlacklist(const ClientSecurityState* state) {
            return state->violation_count.load() >= config_.blacklist_threshold;
        }

        void SecurityManager::cleanupExpiredBlacklist() {
            auto now = std::chrono::system_clock::now();

            for (auto it = client_states_.begin(); it != client_states_.end(); ++it) {
                if (it->second->is_blacklisted && now > it->second->blacklist_until) {
                    it->second->is_blacklisted = false;
                    it->second->blacklist_until = std::chrono::system_clock::time_point{};
                    blacklist_.erase(it->first);
                }
            }
        }

        bool SecurityManager::isValidPlayerName(const std::string& name) const {
            if (name.empty() || name.length() > 50) {
                return false;
            }

            // 只允许字母、数字、下划线、中文字符
            std::regex pattern(R"(^[\w\u4e00-\u9fa5]+$)");
            return std::regex_match(name, pattern);
        }

        bool SecurityManager::isValidRoomName(const std::string& name) const {
            if (name.empty() || name.length() > 100) {
                return false;
            }

            // 只允许字母、数字、下划线、短横线、中文字符和空格
            std::regex pattern(R"(^[\w\u4e00-\u9fa5\s-]+$)");
            return std::regex_match(name, pattern);
        }

        bool SecurityManager::isValidMessage(const std::string& message) const {
            if (message.empty()) {
                return false;
            }

            // 检查是否包含恶意模式
            static const std::vector<std::string> malicious_patterns = {
                "<script", "</script>", "javascript:", "onload=", "onerror=",
                "eval(", "setTimeout(", "setInterval(", "Function(",
                "window.", "document.", "location.", "alert("
            };

            std::string lower_message = message;
            std::transform(lower_message.begin(), lower_message.end(), lower_message.begin(), ::tolower);

            for (const auto& pattern : malicious_patterns) {
                if (lower_message.find(pattern) != std::string::npos) {
                    return false;
                }
            }

            return true;
        }

    } // namespace game_base
} // namespace game_services

