#pragma once

#include "game_types.h"
#include <string>
#include <chrono>
#include <atomic>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

// 前向声明
namespace common {
    namespace network {
        class Channel;
    }
}

/**
 * @file player_session_base.h
 * @brief 玩家会话基类定义
 * @details 提供玩家会话的通用功能，包括连接管理、状态跟踪等
 */

namespace game_services {
    namespace game_base {

        /**
         * @brief 玩家会话基类
         * @details 管理单个玩家的连接状态、消息发送等功能
         */
        class PlayerSessionBase {
        public:
            // 回调函数类型定义
            using MessageCallback = std::function<void(const nlohmann::json&)>;
            using DisconnectCallback = std::function<void()>;
            using ErrorCallback = std::function<void(const std::string&)>;
            using SendCallback = std::function<bool(const std::string&, const std::string&)>; // (player_id, message) -> success

            /**
             * @brief 构造函数
             * @param player_id 玩家ID
             * @param connection_fd 连接文件描述符
             * @param client_ip 客户端IP地址
             */
            explicit PlayerSessionBase(const std::string& player_id, 
                                     int connection_fd = -1,
                                     const std::string& client_ip = "");

            /**
             * @brief 虚析构函数
             */
            virtual ~PlayerSessionBase();

            // 禁用拷贝构造和赋值
            PlayerSessionBase(const PlayerSessionBase&) = delete;
            PlayerSessionBase& operator=(const PlayerSessionBase&) = delete;

            // 纯虚函数 - 子类必须实现
            /**
             * @brief 发送消息到客户端
             * @param message 消息内容
             * @return 是否发送成功
             */
            virtual bool sendMessage(const nlohmann::json& message) = 0;

            /**
             * @brief 关闭连接
             */
            virtual void closeConnection() = 0;

            /**
             * @brief 检查连接是否有效
             * @return 连接是否有效
             */
            virtual bool isConnectionValid() const = 0;

            /**
             * @brief 标记连接为无效
             */
            virtual void markConnectionInvalid() = 0;

            // 通用功能 - 基类实现
            /**
             * @brief 获取玩家ID
             * @return 玩家ID
             */
            std::string getPlayerId() const { return player_id_; }
            
            /**
             * @brief 设置玩家ID（用于认证后更新临时ID为真实ID）
             * @param player_id 新的玩家ID
             */
            void setPlayerId(const std::string& player_id) { player_id_ = player_id; }

            /**
             * @brief 获取客户端IP地址
             * @return IP地址
             */
            std::string getClientIp() const { return client_ip_; }

            /**
             * @brief 获取连接文件描述符
             * @return 文件描述符
             */
            int getConnectionFd() const { return connection_fd_; }

            /**
             * @brief 获取玩家状态
             * @return 玩家状态
             */
            PlayerState getPlayerState() const { return player_state_.load(); }

            /**
             * @brief 设置玩家状态
             * @param state 新状态
             */
            void setPlayerState(PlayerState state) { player_state_.store(state); }

            /**
             * @brief 获取所在房间ID
             * @return 房间ID
             */
            std::string getRoomId() const { return room_id_; }

            /**
             * @brief 设置所在房间ID
             * @param room_id 房间ID
             */
            void setRoomId(const std::string& room_id) { room_id_ = room_id; }

            /**
             * @brief 获取连接时间
             * @return 连接时间
             */
            std::chrono::system_clock::time_point getConnectTime() const { return connect_time_; }

            /**
             * @brief 设置连接时间
             * @param time 连接时间
             *
             * 使用场景：
             * - 重新设置连接时间（如重连后）
             * - 测试场景中设置特定时间
             * - 从持久化数据恢复连接信息
             */
            void setConnectTime(const std::chrono::system_clock::time_point& time) {
                connect_time_ = time;
            }
            
            /**
             * @brief 获取最后活动时间
             * @return 最后活动时间
             */
            std::chrono::system_clock::time_point getLastActivity() const { return last_activity_; }

            /**
             * @brief 更新最后活动时间
             */
            void updateLastActivity() { last_activity_ = std::chrono::system_clock::now(); }

            /**
             * @brief 获取连接持续时间
             * @return 持续时间(毫秒)
             */
            int64_t getConnectionDurationMs() const;

            /**
             * @brief 检查连接是否超时
             * @param timeout_ms 超时时间(毫秒)
             * @return 是否超时
             */
            bool isTimeout(int timeout_ms = 60000) const; // 默认1分钟

            /**
             * @brief 设置Channel
             * @param channel 网络Channel
             */
            void setChannel(std::shared_ptr<common::network::Channel> channel);

            /**
             * @brief 获取Channel
             * @return 网络Channel
             */
            std::shared_ptr<common::network::Channel> getChannel() const;

            /**
             * @brief 检查是否为Qt6客户端
             * @return 是否为Qt6客户端
             */
            bool isQt6Client() const { return is_qt6_client_; }

            /**
             * @brief 设置Qt6客户端标识
             * @param is_qt6 是否为Qt6客户端
             */
            void setQt6Client(bool is_qt6) { is_qt6_client_ = is_qt6; }

            /**
             * @brief 获取Qt6版本信息
             * @return Qt6版本
             */
            std::string getQt6Version() const { return qt6_version_; }

            /**
             * @brief 设置Qt6版本信息
             * @param version Qt6版本
             */
            void setQt6Version(const std::string& version) { qt6_version_ = version; }

            /**
             * @brief 获取客户端版本
             * @return 客户端版本
             */
            std::string getClientVersion() const { return client_version_; }

            /**
             * @brief 设置客户端版本
             * @param version 客户端版本
             */
            void setClientVersion(const std::string& version) { client_version_ = version; }

            /**
             * @brief 检查是否支持压缩
             * @return 是否支持压缩
             */
            bool supportsCompression() const { return supports_compression_; }

            /**
             * @brief 设置压缩支持
             * @param supports 是否支持压缩
             */
            void setSupportsCompression(bool supports) { supports_compression_ = supports; }

            /**
             * @brief 获取首选帧率
             * @return 首选帧率
             */
            int getPreferredFps() const { return preferred_fps_; }

            /**
             * @brief 设置首选帧率
             * @param fps 首选帧率
             */
            void setPreferredFps(int fps) { preferred_fps_ = fps; }

            /**
             * @brief 获取会话统计信息
             * @return 统计信息JSON
             */
            nlohmann::json getSessionStats() const;

            /**
             * @brief 获取完整会话信息
             * @return 会话信息JSON
             */
            nlohmann::json getSessionInfo() const;

            // Qt6专用消息发送方法
            /**
             * @brief 发送Qt6优化的消息
             * @param type 消息类型
             * @param data 消息数据
             * @return 是否发送成功
             */
            bool sendQt6Message(const std::string& type, const nlohmann::json& data);

            /**
             * @brief 发送Qt6格式的系统消息
             * @param message_type 消息类型
             * @param content 消息内容
             * @return 是否发送成功
             */
            bool sendQt6SystemMessage(const std::string& message_type, const std::string& content);

            /**
             * @brief 发送Qt6格式的错误消息
             * @param error_code 错误代码
             * @param error_message 错误消息
             * @return 是否发送成功
             */
            bool sendQt6ErrorMessage(int error_code, const std::string& error_message);

            /**
             * @brief 发送系统消息
             * @param message_type 消息类型
             * @param data 消息数据
             * @return 是否发送成功
             */
            bool sendSystemMessage(const std::string& message_type, const nlohmann::json& data);

            // 回调函数设置
            void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
            void setDisconnectCallback(DisconnectCallback callback) { disconnect_callback_ = callback; }
            void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
            void setSendCallback(SendCallback callback) { send_callback_ = callback; }

            // 统计信息
            /**
             * @brief 增加发送消息计数
             */
            void incrementSentMessages() { sent_messages_count_++; }

            /**
             * @brief 增加接收消息计数
             */
            void incrementReceivedMessages() { received_messages_count_++; }

            /**
             * @brief 获取发送消息数量
             * @return 发送消息数量
             */
            int64_t getSentMessagesCount() const { return sent_messages_count_.load(); }

            /**
             * @brief 获取接收消息数量
             * @return 接收消息数量
             */
            int64_t getReceivedMessagesCount() const { return received_messages_count_.load(); }

            /**
             * @brief 记录心跳（收到心跳响应时调用）
             */
            void recordHeartbeat() {
                last_heartbeat_ = std::chrono::system_clock::now();
                heartbeat_count_++;
                missed_heartbeats_ = 0;  // 收到心跳响应，重置丢失计数
            }

            /**
             * @brief 获取最后心跳时间
             * @return 最后心跳时间
             */
            std::chrono::system_clock::time_point getLastHeartbeat() const { return last_heartbeat_; }

            /**
             * @brief 获取心跳次数
             * @return 心跳次数
             */
            int64_t getHeartbeatCount() const { return heartbeat_count_.load(); }

            /**
             * @brief 增加丢失心跳计数
             */
            void incrementMissedHeartbeats() { missed_heartbeats_++; }

            /**
             * @brief 重置丢失心跳计数
             */
            void resetMissedHeartbeats() { missed_heartbeats_ = 0; }

            /**
             * @brief 获取丢失心跳计数
             * @return 丢失心跳次数
             */
            int getMissedHeartbeats() const { return missed_heartbeats_.load(); }

            /**
             * @brief 获取发送字节数
             * @return 发送字节数
             */
            int64_t getSentBytesCount() const { return sent_bytes_count_.load(); }

            /**
             * @brief 获取接收字节数
             * @return 接收字节数
             */
            int64_t getReceivedBytesCount() const { return received_bytes_count_.load(); }

            /**
             * @brief 添加发送消息大小统计
             * @param size 消息大小（字节）
             */
            void addSentMessageSize(size_t size) {
                sent_bytes_count_.fetch_add(size);
            }

            /**
             * @brief 添加接收消息大小统计
             * @param size 消息大小（字节）
             */
            void addReceivedMessageSize(size_t size) {
                received_bytes_count_.fetch_add(size);
            }

        protected:
            std::string player_id_;                         // 玩家ID
            int connection_fd_;                             // 连接文件描述符
            std::string client_ip_;                         // 客户端IP地址
            std::atomic<PlayerState> player_state_;         // 玩家状态
            std::string room_id_;                           // 所在房间ID

            // 时间戳
            std::chrono::system_clock::time_point connect_time_;
            std::chrono::system_clock::time_point last_activity_;
            std::chrono::system_clock::time_point last_heartbeat_;

            // Qt6客户端信息
            bool is_qt6_client_ = false;                    // 是否为Qt6客户端
            std::string qt6_version_;                       // Qt6版本
            std::string client_version_;                    // 客户端版本
            bool supports_compression_ = false;             // 是否支持压缩
            int preferred_fps_ = 60;                        // 首选帧率

            // 统计信息
            std::atomic<int64_t> sent_messages_count_{0};   // 发送消息数量
            std::atomic<int64_t> received_messages_count_{0}; // 接收消息数量
            std::atomic<int64_t> heartbeat_count_{0};       // 心跳次数
            std::atomic<int> missed_heartbeats_{0};         // 丢失心跳次数
            std::atomic<int64_t> sent_bytes_count_{0};      // 发送字节数
            std::atomic<int64_t> received_bytes_count_{0};  // 接收字节数

            // 网络连接
            std::shared_ptr<common::network::Channel> channel_;  // 网络Channel

            // 回调函数
            MessageCallback message_callback_;
            DisconnectCallback disconnect_callback_;
            ErrorCallback error_callback_;
            SendCallback send_callback_;  // 实际发送消息的回调

            /**
             * @brief 创建Qt6格式的消息
             * @param type 消息类型
             * @param data 消息数据
             * @return 格式化的消息
             */
            nlohmann::json createQt6Message(const std::string& type, const nlohmann::json& data);

            /**
             * @brief 通知消息接收
             * @param message 消息内容
             */
            void notifyMessage(const nlohmann::json& message);

            /**
             * @brief 通知连接断开
             */
            void notifyDisconnect();

            /**
             * @brief 通知错误
             * @param error 错误消息
             */
            void notifyError(const std::string& error);
        };

    } // namespace game_base
} // namespace game_services
