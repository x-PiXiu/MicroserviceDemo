#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <atomic>
#include <shared_mutex>
#include <nlohmann/json.hpp>

/**
 * @file security_manager.h
 * @brief 游戏安全管理器 - 增强游戏基类的安全防护能力
 * @details 提供输入验证、速率限制、IP黑名单、权限控制等安全功能
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 安全事件类型
         */
        enum class SecurityEventType {
            RATE_LIMIT_EXCEEDED,        // 超过速率限制
            INVALID_INPUT,              // 无效输入
            SUSPICIOUS_BEHAVIOR,        // 可疑行为
            UNAUTHORIZED_ACCESS,        // 未授权访问
            BLACKLISTED_IP,            // IP黑名单
            TOKEN_VALIDATION_FAILED,    // 令牌验证失败
            MESSAGE_SIZE_EXCEEDED,      // 消息大小超限
            CONNECTION_LIMIT_EXCEEDED   // 连接数超限
        };

        /**
         * @brief 安全配置
         */
        struct SecurityConfig {
            // 速率限制配置
            int max_messages_per_minute = 60;          // 每分钟最大消息数
            int max_messages_per_second = 10;          // 每秒最大消息数
            int max_connections_per_ip = 10;           // 每IP最大连接数
            
            // 消息大小限制
            size_t max_message_size = 65536;           // 64KB
            size_t max_json_depth = 10;                // JSON最大嵌套深度
            
            // 安全策略配置
            int blacklist_threshold = 5;               // 触发黑名单的违规次数
            int blacklist_duration_minutes = 60;       // 黑名单持续时间（分钟）
            bool enable_ip_whitelist = false;          // 是否启用IP白名单
            
            // 权限验证配置
            bool require_authentication = true;        // 是否要求认证
            int token_expiry_hours = 24;              // 令牌有效期（小时）
            
            nlohmann::json toJson() const {
                return nlohmann::json{
                    {"max_messages_per_minute", max_messages_per_minute},
                    {"max_messages_per_second", max_messages_per_second},
                    {"max_connections_per_ip", max_connections_per_ip},
                    {"max_message_size", max_message_size},
                    {"max_json_depth", max_json_depth},
                    {"blacklist_threshold", blacklist_threshold},
                    {"blacklist_duration_minutes", blacklist_duration_minutes},
                    {"enable_ip_whitelist", enable_ip_whitelist},
                    {"require_authentication", require_authentication},
                    {"token_expiry_hours", token_expiry_hours}
                };
            }
        };

        /**
         * @brief 客户端安全状态
         */
        struct ClientSecurityState {
            std::string client_ip;
            std::chrono::system_clock::time_point first_seen;
            std::chrono::system_clock::time_point last_activity;
            
            // 速率限制计数器
            std::atomic<int> messages_this_minute{0};
            std::atomic<int> messages_this_second{0};
            std::chrono::system_clock::time_point minute_window_start;
            std::chrono::system_clock::time_point second_window_start;
            
            // 违规记录
            std::atomic<int> violation_count{0};
            std::vector<SecurityEventType> recent_violations;
            
            // 连接计数
            std::atomic<int> active_connections{0};
            
            bool is_blacklisted = false;
            std::chrono::system_clock::time_point blacklist_until;
            
            ClientSecurityState() : 
                first_seen(std::chrono::system_clock::now()),
                last_activity(std::chrono::system_clock::now()),
                minute_window_start(std::chrono::system_clock::now()),
                second_window_start(std::chrono::system_clock::now()) {}
        };

        /**
         * @brief 游戏安全管理器
         * @details 提供全面的安全防护功能，集成到游戏基类中
         */
        class SecurityManager {
        public:
            /**
             * @brief 构造函数
             * @param config 安全配置
             */
            explicit SecurityManager(const SecurityConfig& config = SecurityConfig{});

            /**
             * @brief 析构函数
             */
            ~SecurityManager() = default;

            // 禁用拷贝构造和赋值
            SecurityManager(const SecurityManager&) = delete;
            SecurityManager& operator=(const SecurityManager&) = delete;

            /**
             * @brief 验证客户端连接
             * @param client_ip 客户端IP地址
             * @param additional_info 附加信息
             * @return 是否允许连接
             */
            bool validateConnection(const std::string& client_ip, 
                                  const nlohmann::json& additional_info = {});

            /**
             * @brief 验证消息
             * @param client_ip 客户端IP
             * @param message 消息内容
             * @return 是否允许消息
             */
            bool validateMessage(const std::string& client_ip, const std::string& message);

            /**
             * @brief 验证JSON消息
             * @param client_ip 客户端IP
             * @param json_message JSON消息
             * @return 是否允许消息
             */
            bool validateJsonMessage(const std::string& client_ip, const nlohmann::json& json_message);

            /**
             * @brief 检查速率限制
             * @param client_ip 客户端IP
             * @return 是否在速率限制内
             */
            bool checkRateLimit(const std::string& client_ip);

            /**
             * @brief 记录安全事件
             * @param client_ip 客户端IP
             * @param event_type 事件类型
             * @param details 事件详情
             */
            void recordSecurityEvent(const std::string& client_ip, 
                                   SecurityEventType event_type,
                                   const std::string& details = "");

            /**
             * @brief 检查IP是否在黑名单中
             * @param client_ip 客户端IP
             * @return 是否在黑名单中
             */
            bool isBlacklisted(const std::string& client_ip);

            /**
             * @brief 手动添加到黑名单
             * @param client_ip 客户端IP
             * @param duration_minutes 持续时间（分钟）
             * @param reason 原因
             */
            void addToBlacklist(const std::string& client_ip, 
                               int duration_minutes = 60,
                               const std::string& reason = "Manual");

            /**
             * @brief 从黑名单中移除
             * @param client_ip 客户端IP
             */
            void removeFromBlacklist(const std::string& client_ip);

            /**
             * @brief 添加到白名单
             * @param client_ip 客户端IP
             */
            void addToWhitelist(const std::string& client_ip);

            /**
             * @brief 从白名单中移除
             * @param client_ip 客户端IP
             */
            void removeFromWhitelist(const std::string& client_ip);

            /**
             * @brief 检查权限
             * @param player_id 玩家ID
             * @param operation 操作名称
             * @param resource 资源名称
             * @return 是否有权限
             */
            bool checkPermission(const std::string& player_id,
                               const std::string& operation,
                               const std::string& resource = "");

            /**
             * @brief 获取安全统计信息
             * @return 安全统计JSON
             */
            nlohmann::json getSecurityStats() const;

            /**
             * @brief 获取客户端安全状态
             * @param client_ip 客户端IP
             * @return 客户端安全状态
             */
            nlohmann::json getClientSecurityState(const std::string& client_ip) const;

            /**
             * @brief 更新安全配置
             * @param new_config 新配置
             */
            void updateConfig(const SecurityConfig& new_config);

            /**
             * @brief 获取当前配置
             * @return 当前安全配置
             */
            SecurityConfig getConfig() const { return config_; }

            /**
             * @brief 清理过期数据
             */
            void cleanup();

            /**
             * @brief 注册连接
             * @param client_ip 客户端IP
             * @param player_id 玩家ID
             */
            void registerConnection(const std::string& client_ip, const std::string& player_id);

            /**
             * @brief 注销连接
             * @param client_ip 客户端IP
             * @param player_id 玩家ID
             */
            void unregisterConnection(const std::string& client_ip, const std::string& player_id);

        private:
            SecurityConfig config_;
            mutable std::shared_mutex security_mutex_;
            
            // 客户端安全状态映射
            std::unordered_map<std::string, std::unique_ptr<ClientSecurityState>> client_states_;
            
            // IP黑白名单
            std::unordered_set<std::string> blacklist_;
            std::unordered_set<std::string> whitelist_;
            
            // 统计信息
            std::atomic<uint64_t> total_connections_{0};
            std::atomic<uint64_t> rejected_connections_{0};
            std::atomic<uint64_t> total_messages_{0};
            std::atomic<uint64_t> rejected_messages_{0};
            std::atomic<uint64_t> security_events_{0};

            /**
             * @brief 获取或创建客户端状态
             * @param client_ip 客户端IP
             * @return 客户端状态指针
             */
            ClientSecurityState* getOrCreateClientState(const std::string& client_ip);

            /**
             * @brief 检查JSON深度
             * @param json JSON对象
             * @param current_depth 当前深度
             * @return 是否在允许深度内
             */
            bool checkJsonDepth(const nlohmann::json& json, int current_depth = 0) const;

            /**
             * @brief 更新速率限制窗口
             * @param state 客户端状态
             */
            void updateRateLimitWindows(ClientSecurityState* state);

            /**
             * @brief 检查是否应该加入黑名单
             * @param state 客户端状态
             * @return 是否应该加入黑名单
             */
            bool shouldBlacklist(const ClientSecurityState* state);

            /**
             * @brief 清理过期的黑名单项
             */
            void cleanupExpiredBlacklist();

            /**
             * @brief 输入验证辅助函数
             */
            bool isValidPlayerName(const std::string& name) const;
            bool isValidRoomName(const std::string& name) const;
            bool isValidMessage(const std::string& message) const;
        };

    } // namespace game_base
} // namespace game_services


