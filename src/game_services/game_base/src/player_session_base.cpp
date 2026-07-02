#include "player_session_base.h"
#include "common/logger/logger.h"
#include "common/network/channel.h"
#include <chrono>

namespace game_services {
    namespace game_base {

        PlayerSessionBase::PlayerSessionBase(const std::string& player_id, 
                                           int connection_fd,
                                           const std::string& client_ip)
            : player_id_(player_id)
            , connection_fd_(connection_fd)
            , client_ip_(client_ip)
            , player_state_(PlayerState::CONNECTED)
            , connect_time_(std::chrono::system_clock::now())
            , last_activity_(std::chrono::system_clock::now())
            , last_heartbeat_(std::chrono::system_clock::now()) {
            
            LOG_INFO("PlayerSession created for " + player_id_ + " from " + client_ip_);
        }

        PlayerSessionBase::~PlayerSessionBase() {
            LOG_INFO("PlayerSession destroyed for " + player_id_);
        }

        int64_t PlayerSessionBase::getConnectionDurationMs() const {
            auto now = std::chrono::system_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - connect_time_).count();
        }

        bool PlayerSessionBase::isTimeout(int timeout_ms) const {
            auto now = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity_);
            return duration.count() > timeout_ms;
        }

        nlohmann::json PlayerSessionBase::getSessionStats() const {
            auto now = std::chrono::system_clock::now();
            auto connection_duration = std::chrono::duration_cast<std::chrono::seconds>(now - connect_time_);
            auto last_activity_duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_activity_);
            auto last_heartbeat_duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat_);

            return nlohmann::json{
                {"connection_duration_seconds", connection_duration.count()},
                {"last_activity_seconds_ago", last_activity_duration.count()},
                {"last_heartbeat_seconds_ago", last_heartbeat_duration.count()},
                {"sent_messages", sent_messages_count_.load()},
                {"received_messages", received_messages_count_.load()},
                {"sent_bytes", sent_bytes_count_.load()},
                {"received_bytes", received_bytes_count_.load()},
                {"heartbeat_count", heartbeat_count_.load()},
                {"is_timeout", isTimeout()},
                {"connection_valid", isConnectionValid()}
            };
        }

        nlohmann::json PlayerSessionBase::getSessionInfo() const {
            return nlohmann::json{
                {"player_id", player_id_},
                {"client_ip", client_ip_},
                {"connection_fd", connection_fd_},
                {"player_state", static_cast<int>(player_state_.load())},
                {"room_id", room_id_},
                {"connect_time", std::chrono::duration_cast<std::chrono::milliseconds>(
                    connect_time_.time_since_epoch()).count()},
                {"last_activity", std::chrono::duration_cast<std::chrono::milliseconds>(
                    last_activity_.time_since_epoch()).count()},
                {"is_qt6_client", is_qt6_client_},
                {"qt6_version", qt6_version_},
                {"client_version", client_version_},
                {"supports_compression", supports_compression_},
                {"preferred_fps", preferred_fps_},
                {"stats", getSessionStats()}
            };
        }

        bool PlayerSessionBase::sendQt6Message(const std::string& type, const nlohmann::json& data) {
            auto qt6_message = createQt6Message(type, data);
            bool result = sendMessage(qt6_message);
            
            if (result) {
                incrementSentMessages();
            }
            
            return result;
        }

        bool PlayerSessionBase::sendQt6SystemMessage(const std::string& message_type, const std::string& content) {
            auto system_data = nlohmann::json{
                {"message_type", message_type},
                {"content", content},
                {"level", "info"}
            };
            
            return sendQt6Message("system_message", system_data);
        }

        bool PlayerSessionBase::sendQt6ErrorMessage(int error_code, const std::string& error_message) {
            auto error_data = nlohmann::json{
                {"error_code", error_code},
                {"error_message", error_message},
                {"level", "error"}
            };
            
            return sendQt6Message("error", error_data);
        }

        nlohmann::json PlayerSessionBase::createQt6Message(const std::string& type, const nlohmann::json& data) {
            auto message = nlohmann::json{
                {"type", type},
                {"data", data},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };

            // 如果是Qt6客户端，添加Qt6特定字段
            if (is_qt6_client_) {
                message["qt6_optimized"] = true;
                message["client_info"] = {
                    {"qt_version", qt6_version_},
                    {"client_version", client_version_},
                    {"preferred_fps", preferred_fps_}
                };
                
                // 如果支持压缩，添加压缩标记
                if (supports_compression_) {
                    message["compression_available"] = true;
                }
            }

            return message;
        }

        void PlayerSessionBase::notifyMessage(const nlohmann::json& message) {
            if (message_callback_) {
                try {
                    message_callback_(message);
                } catch (const std::exception& e) {
                    LOG_ERROR("Message callback error for player " + player_id_ + ": " + std::string(e.what()));
                }
            }
        }

        void PlayerSessionBase::notifyDisconnect() {
            if (disconnect_callback_) {
                try {
                    disconnect_callback_();
                } catch (const std::exception& e) {
                    LOG_ERROR("Disconnect callback error for player " + player_id_ + ": " + std::string(e.what()));
                }
            }
        }

        void PlayerSessionBase::notifyError(const std::string& error) {
            if (error_callback_) {
                try {
                    error_callback_(error);
                } catch (const std::exception& e) {
                    LOG_ERROR("Error callback error for player " + player_id_ + ": " + std::string(e.what()));
                }
            }
        }

        bool PlayerSessionBase::sendSystemMessage(const std::string& message_type, const nlohmann::json& data) {
            // 如果是Qt6客户端，使用Qt6优化的消息格式
            if (is_qt6_client_) {
                return sendQt6Message(message_type, data);
            } else {
                // 普通客户端使用标准格式
                auto message = nlohmann::json{
                    {"type", message_type},
                    {"data", data},
                    {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()}
                };

                bool result = sendMessage(message);
                if (result) {
                    incrementSentMessages();
                }
                return result;
            }
        }

        void PlayerSessionBase::setChannel(std::shared_ptr<common::network::Channel> channel) {
            channel_ = channel;
        }

        std::shared_ptr<common::network::Channel> PlayerSessionBase::getChannel() const {
            return channel_;
        }

    } // namespace game_base
} // namespace game_services
