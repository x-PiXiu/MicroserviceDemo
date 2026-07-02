/**
 * @file friend_room_manager.h
 * @brief 好友房间管理器 - 支持好友对战房间创建和管理
 * @author AI Assistant
 * @date 2026-02-23
 * @version 1.0.0
 *
 * 职责：
 * - 好友房间创建和管理
 * - 好友邀请发送和处理
 * - 房间权限控制
 * - 与好友系统集成
 */

#pragma once

#include "gomoku_types.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <nlohmann/json.hpp>

namespace game_services {
namespace gomoku {

// 前向声明
class GomokuRoom;
class GomokuRoomManager;

/**
 * 好友关系状态
 */
enum class FriendStatus {
    NONE = 0,           // 无关系
    PENDING_SENT = 1,   // 已发送请求
    PENDING_RECEIVED = 2, // 收到请求
    FRIENDS = 3,        // 已是好友
    BLOCKED = 4         // 已拉黑
};

/**
 * 好友信息
 */
struct FriendInfo {
    std::string user_id;
    std::string username;
    std::string avatar_url;
    FriendStatus status = FriendStatus::NONE;
    int rating = 1200;
    std::string tier_name;
    bool online = false;
    std::chrono::system_clock::time_point last_online;

    nlohmann::json toJson() const {
        return {
            {"user_id", user_id},
            {"username", username},
            {"avatar_url", avatar_url},
            {"status", static_cast<int>(status)},
            {"rating", rating},
            {"tier_name", tier_name},
            {"online", online}
        };
    }
};

/**
 * 好友房间类型
 */
enum class FriendRoomType {
    FRIENDS_ONLY,       // 仅好友可加入
    INVITE_ONLY,        // 仅被邀请者可加入
    FRIENDS_OF_FRIENDS  // 好友的好友也可加入
};

/**
 * 好友房间配置
 */
struct FriendRoomConfig {
    std::string room_id;
    std::string host_user_id;
    std::string room_name;
    FriendRoomType room_type = FriendRoomType::FRIENDS_ONLY;

    // 游戏配置
    GameMode game_mode = GameMode::FREESTYLE;
    int time_control_seconds = 0;       // 0表示无限制

    // 邀请配置
    bool allow_spectators = true;
    bool allow_chat = true;
    int max_spectators = 10;

    // 邀请列表
    std::vector<std::string> invited_user_ids;
    std::chrono::system_clock::time_point invite_expiry;
    int invite_expiry_minutes = 30;

    nlohmann::json toJson() const {
        return {
            {"room_id", room_id},
            {"host_user_id", host_user_id},
            {"room_name", room_name},
            {"room_type", static_cast<int>(room_type)},
            {"game_mode", gameModeToString(game_mode)},
            {"time_control_seconds", time_control_seconds},
            {"allow_spectators", allow_spectators},
            {"allow_chat", allow_chat},
            {"max_spectators", max_spectators}
        };
    }
};

/**
 * 好友房间邀请
 */
struct FriendRoomInvite {
    std::string invite_id;
    std::string room_id;
    std::string host_user_id;
    std::string host_username;
    std::string target_user_id;
    std::string room_name;
    GameMode game_mode = GameMode::FREESTYLE;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    bool accepted = false;
    bool declined = false;

    nlohmann::json toJson() const {
        return {
            {"invite_id", invite_id},
            {"room_id", room_id},
            {"host_user_id", host_user_id},
            {"host_username", host_username},
            {"target_user_id", target_user_id},
            {"room_name", room_name},
            {"game_mode", gameModeToString(game_mode)},
            {"accepted", accepted},
            {"declined", declined}
        };
    }
};

/**
 * 好友房间状态
 */
struct FriendRoomState {
    std::string room_id;
    FriendRoomConfig config;
    std::vector<std::string> player_ids;
    std::vector<std::string> spectator_ids;
    std::unordered_map<std::string, bool> player_ready_status;
    bool game_started = false;
    std::chrono::system_clock::time_point created_at;

    nlohmann::json toJson() const {
        return {
            {"room_id", room_id},
            {"config", config.toJson()},
            {"player_ids", player_ids},
            {"spectator_ids", spectator_ids},
            {"player_ready_status", player_ready_status},
            {"game_started", game_started}
        };
    }
};

/**
 * 好友服务接口（用于解耦）
 */
class IFriendService {
public:
    virtual ~IFriendService() = default;

    /**
     * 检查是否为好友
     */
    virtual bool areFriends(const std::string& user_id1, const std::string& user_id2) = 0;

    /**
     * 获取好友列表
     */
    virtual std::vector<FriendInfo> getFriendList(const std::string& user_id) = 0;

    /**
     * 获取好友状态
     */
    virtual FriendStatus getFriendStatus(
        const std::string& user_id1,
        const std::string& user_id2) = 0;

    /**
     * 检查用户是否在线
     */
    virtual bool isUserOnline(const std::string& user_id) = 0;

    /**
     * 发送通知给用户
     */
    virtual void notifyUser(
        const std::string& user_id,
        const std::string& type,
        const nlohmann::json& data) = 0;
};

/**
 * 好友房间事件类型
 */
enum class FriendRoomEventType {
    ROOM_CREATED,
    ROOM_CLOSED,
    PLAYER_JOINED,
    PLAYER_LEFT,
    PLAYER_READY,
    INVITE_SENT,
    INVITE_ACCEPTED,
    INVITE_DECLINED,
    INVITE_EXPIRED,
    GAME_STARTED,
    SPECTATOR_JOINED,
    SPECTATOR_LEFT
};

/**
 * 好友房间事件
 */
struct FriendRoomEvent {
    FriendRoomEventType type;
    std::string room_id;
    std::string user_id;
    nlohmann::json data;
    std::chrono::system_clock::time_point timestamp;

    FriendRoomEvent(FriendRoomEventType t, const std::string& rid, const std::string& uid)
        : type(t), room_id(rid), user_id(uid), timestamp(std::chrono::system_clock::now()) {}
};

/**
 * 好友房间事件回调
 */
using FriendRoomEventCallback = std::function<void(const FriendRoomEvent&)>;

/**
 * 好友房间管理器配置
 */
struct FriendRoomManagerConfig {
    bool enabled = true;

    // 房间限制
    int max_rooms_per_user = 3;         // 每用户最大房间数
    int max_invites_per_room = 10;      // 每房间最大邀请数
    int invite_expiry_minutes = 30;     // 邀请过期时间（分钟）
    int room_expiry_minutes = 60;       // 空房间过期时间（分钟）

    // 权限设置
    bool require_mutual_friends = false; // 是否要求相互关注
    bool allow_cross_region = true;     // 允许跨区域

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(FriendRoomManagerConfig,
        enabled, max_rooms_per_user, max_invites_per_room,
        invite_expiry_minutes, room_expiry_minutes,
        require_mutual_friends, allow_cross_region)
};

/**
 * 好友房间管理器
 */
class FriendRoomManager {
public:
    /**
     * 构造函数
     */
    FriendRoomManager(
        std::shared_ptr<IFriendService> friend_service,
        const FriendRoomManagerConfig& config = FriendRoomManagerConfig());

    ~FriendRoomManager();

    // ========== 生命周期 ==========

    /**
     * 初始化
     */
    bool initialize();

    /**
     * 关闭
     */
    void shutdown();

    // ========== 房间管理 ==========

    /**
     * 创建好友房间
     * @param host_user_id 房主用户ID
     * @param config 房间配置
     * @return 房间ID（失败返回空）
     */
    std::optional<std::string> createFriendRoom(
        const std::string& host_user_id,
        const FriendRoomConfig& config);

    /**
     * 关闭好友房间
     */
    bool closeFriendRoom(const std::string& room_id, const std::string& by_user_id);

    /**
     * 获取房间状态
     */
    std::optional<FriendRoomState> getRoomState(const std::string& room_id) const;

    /**
     * 获取用户创建的房间
     */
    std::vector<FriendRoomState> getUserRooms(const std::string& user_id) const;

    // ========== 邀请管理 ==========

    /**
     * 发送房间邀请
     * @param room_id 房间ID
     * @param from_user_id 邀请者
     * @param to_user_id 被邀请者
     * @return 邀请ID（失败返回空）
     */
    std::optional<std::string> sendInvite(
        const std::string& room_id,
        const std::string& from_user_id,
        const std::string& to_user_id);

    /**
     * 接受邀请
     */
    bool acceptInvite(const std::string& invite_id, const std::string& by_user_id);

    /**
     * 拒绝邀请
     */
    bool declineInvite(const std::string& invite_id, const std::string& by_user_id);

    /**
     * 取消邀请
     */
    bool cancelInvite(const std::string& invite_id, const std::string& by_user_id);

    /**
     * 获取用户的待处理邀请
     */
    std::vector<FriendRoomInvite> getPendingInvites(const std::string& user_id) const;

    /**
     * 获取房间的邀请列表
     */
    std::vector<FriendRoomInvite> getRoomInvites(const std::string& room_id) const;

    // ========== 玩家管理 ==========

    /**
     * 加入房间
     */
    bool joinRoom(const std::string& room_id, const std::string& user_id);

    /**
     * 离开房间
     */
    bool leaveRoom(const std::string& room_id, const std::string& user_id);

    /**
     * 设置准备状态
     */
    bool setReady(const std::string& room_id, const std::string& user_id, bool ready);

    /**
     * 检查是否可以加入
     */
    bool canJoinRoom(const std::string& room_id, const std::string& user_id) const;

    // ========== 观战管理 ==========

    /**
     * 作为观战者加入
     */
    bool joinAsSpectator(const std::string& room_id, const std::string& user_id);

    /**
     * 观战者离开
     */
    bool leaveAsSpectator(const std::string& room_id, const std::string& user_id);

    // ========== 回调设置 ==========

    /**
     * 设置事件回调
     */
    void setEventCallback(FriendRoomEventCallback callback) {
        event_callback_ = std::move(callback);
    }

    // ========== 统计 ==========

    /**
     * 获取统计信息
     */
    nlohmann::json getStatistics() const;

private:
    std::shared_ptr<IFriendService> friend_service_;
    FriendRoomManagerConfig config_;

    // 房间管理
    mutable std::mutex rooms_mutex_;
    std::unordered_map<std::string, FriendRoomState> rooms_;
    std::unordered_map<std::string, std::string> user_to_room_; // user_id -> room_id

    // 邀请管理
    mutable std::mutex invites_mutex_;
    std::unordered_map<std::string, FriendRoomInvite> invites_;
    std::unordered_map<std::string, std::vector<std::string>> user_invites_; // user_id -> invite_ids
    std::unordered_map<std::string, std::vector<std::string>> room_invites_; // room_id -> invite_ids

    // 事件回调
    FriendRoomEventCallback event_callback_;

    // 统计
    mutable std::mutex stats_mutex_;
    int64_t total_rooms_created_{0};
    int64_t total_invites_sent_{0};
    int64_t total_games_played_{0};

    // ========== 内部方法 ==========

    /**
     * 生成房间ID
     */
    std::string generateRoomId() const;

    /**
     * 生成邀请ID
     */
    std::string generateInviteId() const;

    /**
     * 触发事件
     */
    void triggerEvent(FriendRoomEventType type, const std::string& room_id, const std::string& user_id, const nlohmann::json& data = {});

    /**
     * 清理过期邀请
     */
    void cleanupExpiredInvites();

    /**
     * 清理空房间
     */
    void cleanupEmptyRooms();

    /**
     * 检查邀请是否有效
     */
    bool isInviteValid(const FriendRoomInvite& invite) const;

    /**
     * 检查房间是否有效
     */
    bool isRoomValid(const FriendRoomState& room) const;

    /**
     * 通知用户
     */
    void notifyUser(const std::string& user_id, const std::string& type, const nlohmann::json& data);
};

} // namespace gomoku
} // namespace game_services
